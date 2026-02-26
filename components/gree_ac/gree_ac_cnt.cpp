// based on: https://github.com/DomiStyle/esphome-panasonic-ac
#include "gree_ac_cnt.h"
#include "esphome/core/log.h"
#include "esphome/core/util.h"
#include <cstring>

namespace esphome {
namespace gree_ac {
namespace CNT {

static const char *const TAG = "gree_ac.serial";

static const uint8_t ALLOWED_PACKETS[] = {protocol::CMD_IN_UNIT_REPORT};
static const uint8_t BYTES_TO_CHECK[] = {4, 5, 6, 8, 9, 11, 16, 18, 40};

void GreeACCNT::setup()
{
    GreeAC::setup();
    ESP_LOGD(TAG, "Using serial protocol for Gree AC");
    memset(this->lastpacket, 0, sizeof(this->lastpacket));

    this->startup_special_sent_ = false;
    this->mac_packets_pending_ = 6;
    this->last_mac_sequence_millis_ = 0;
    this->last_sync_time_sent_ = millis() - 10000;
    this->last_packet_duration_ms_ = 0;
    /* allow immediate transmission of the first packet */
    this->last_packet_sent_ = millis() - protocol::TIME_REFRESH_PERIOD_MS - 1000;
}

void GreeACCNT::loop()
{
    /* this reads data from UART */
    GreeAC::loop();

    uint32_t now = millis();

    /* we have a frame from AC */
    if (this->serialProcess_.state == STATE_COMPLETE)
    {
        /* log for ESPHome debug */
        log_packet(this->serialProcess_.data);

        /* mark that we have received a response (even if it might be invalid) */
        this->wait_response_ = false;

        if (verify_packet())  /* Verify length, header, counter and checksum */
        {
            this->last_packet_received_ = now;  /* Set the time at which we received our last packet */

            /* A valid recieved packet of accepted type marks module as being ready */
            if (this->state_ != ACState::Ready)
            {
                this->state_ = ACState::Ready;
                Component::status_clear_error();
            }

            if (this->update_ == ACUpdate::NoUpdate)
            {
                handle_packet(); /* this will update state of components in HA as well as internal settings */
            }
        }

        /* restart for next packet */
        this->serialProcess_.data.clear();
        this->serialProcess_.state = STATE_WAIT_SYNC;
    }

    /* we will send a packet to the AC as a response to indicate changes */
    /* Check for 330ms gap since last packet finished transmission */
    if (now - this->last_packet_sent_ >= (protocol::TIME_REFRESH_PERIOD_MS + this->last_packet_duration_ms_))
    {
        if (!this->startup_special_sent_)
        {
            send_special_startup_packet();
        }
        else if (this->mac_packets_pending_ > 0)
        {
            send_mac_report_packet();
            this->mac_packets_pending_--;
            if (this->mac_packets_pending_ == 0)
            {
                this->last_mac_sequence_millis_ = now;
            }
        }
        else if (now - this->last_sync_time_sent_ >= 10000)
        {
            send_sync_time_packet();
        }
        else if (now - this->last_mac_sequence_millis_ >= protocol::TIME_MAC_CYCLE_PERIOD_MS)
        {
            this->mac_packets_pending_ = 6;
            /* next loop will start sending them */
        }
        else
        {
            send_params_set_packet();
        }
    }

    /* if there are no packets for some time - mark module as not ready */
    if (millis() - this->last_packet_received_ >= protocol::TIME_TIMEOUT_INACTIVE_MS)
    {
        if (this->state_ != ACState::Initializing)
        {
            this->state_ = ACState::Initializing;
            Component::status_set_error();
        }
    }
}

/*
 * ESPHome control request
 */

void GreeACCNT::control(const climate::ClimateCall &call)
{
    if (this->state_ != ACState::Ready)
        return;

    if (call.get_mode().has_value())
    {
        ESP_LOGV(TAG, "Requested mode change");
        reqmodechange = true;
        this->update_ = ACUpdate::UpdateStart;
        this->mode = *call.get_mode();
    }

    if (call.get_target_temperature().has_value())
    {
        ESP_LOGV(TAG, "Requested target teperature change");
        this->update_ = ACUpdate::UpdateStart;
        this->target_temperature = *call.get_target_temperature();
        if (this->target_temperature < MIN_TEMPERATURE)
        {
            this->target_temperature = MIN_TEMPERATURE;
        }
        else if (this->target_temperature > MAX_TEMPERATURE)
        {
            this->target_temperature = MAX_TEMPERATURE;
        }
    }

    if (call.has_custom_fan_mode())
    {
        ESP_LOGV(TAG, "Requested fan mode change");
        reqmodechange = true;
        this->update_ = ACUpdate::UpdateStart;
        this->set_custom_fan_mode_(call.get_custom_fan_mode());

        /* Requirement 3: When the fan mode gets changed while turbo is on, the turbo mode must be deactivated.
           Also for quiet mode. */
        this->update_turbo(false);
        this->update_quiet(quiet_options::OFF);
    }

    if (call.get_swing_mode().has_value())
    {
        ESP_LOGV(TAG, "Requested swing mode change");
        reqmodechange = true;
        this->update_ = ACUpdate::UpdateStart;
        switch (*call.get_swing_mode()) {
            case climate::CLIMATE_SWING_BOTH:
                this->vertical_swing_state_   =   vertical_swing_options::FULL;
                this->horizontal_swing_state_ = horizontal_swing_options::FULL;
                break;
            case climate::CLIMATE_SWING_OFF:
                /* both center */
                this->vertical_swing_state_   =   vertical_swing_options::CMID;
                this->horizontal_swing_state_ = horizontal_swing_options::CMID;
                break;
            case climate::CLIMATE_SWING_VERTICAL:
                /* vertical full, horizontal center */
                this->vertical_swing_state_   =   vertical_swing_options::FULL;
                this->horizontal_swing_state_ = horizontal_swing_options::CMID;
                break;
            case climate::CLIMATE_SWING_HORIZONTAL:
                /* horizontal full, vertical center */
                this->vertical_swing_state_   =   vertical_swing_options::CMID;
                this->horizontal_swing_state_ = horizontal_swing_options::FULL;
                break;
            default:
                ESP_LOGV(TAG, "Unsupported swing mode requested");
                /* both center */
                this->vertical_swing_state_   =   vertical_swing_options::CMID;
                this->horizontal_swing_state_ = horizontal_swing_options::CMID;
                break;
        }
    }
}

void GreeACCNT::transmit_packet(const uint8_t *packet, size_t length)
{
    this->last_packet_sent_ = millis();
    this->last_packet_duration_ms_ = (length * 11000) / 4800;
    write_array(packet, length);
    log_packet(packet, length, true);
}

void GreeACCNT::send_special_startup_packet()
{
    uint8_t packet[] = {
        0x7E, 0x7E, 0x10, 0x02,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00,
        0x28, 0x1E, 0x19, 0x23, 0x23, 0x00, 0xBA
    };
    transmit_packet(packet, sizeof(packet));
    this->startup_special_sent_ = true;
    ESP_LOGD(TAG, "Sent special startup packet");
}

/*
 * Send a raw packet, as is
 */
void GreeACCNT::send_params_set_packet()
{
    if (this->wait_response_)
    {
        if (millis() - this->last_packet_sent_ < protocol::TIME_WAIT_RESPONSE_TIMEOUT_MS)
        {
            /* waiting for report to come */
            return;
        }
        else
        {
            ESP_LOGW(TAG, "Timed out waiting for response from AC unit");
            this->wait_response_ = false;
        }
    }

    uint8_t payload[protocol::SET_PACKET_LEN];
    memset(payload, 0, sizeof(payload));
    
    payload[protocol::SET_CONST_02_BYTE] = protocol::SET_CONST_02_VAL; /* Some always 0x02 byte... */
    payload[protocol::SET_CONST_BIT_BYTE] = protocol::SET_CONST_BIT_MASK; /* Some always true bit */

    /* Prepare the rest of the frame */
    /* this handles tricky part of 0xAF value and flag marking that WiFi does not apply any changes */
    switch(this->update_)
    {
        default:
        case ACUpdate::NoUpdate:
            // payload[protocol::SET_NOCHANGE_BYTE] |= protocol::SET_NOCHANGE_MASK; // Bit 0x08 at byte 11 indicates no change
            break;
        case ACUpdate::UpdateStart:
            payload[protocol::SET_AF_BYTE] = protocol::SET_AF_VAL;
            break;
        case ACUpdate::UpdateClear:
            break;
    }

    /* MODE and POWER --------------------------------------------------------------------------- */
    uint8_t mode = protocol::REPORT_MODE_AUTO;
    bool power = false;
    switch (this->mode)
    {
        case climate::CLIMATE_MODE_AUTO:
            mode = protocol::REPORT_MODE_AUTO;
            power = true;
            break;
        case climate::CLIMATE_MODE_COOL:
            mode = protocol::REPORT_MODE_COOL;
            power = true;
            break;
        case climate::CLIMATE_MODE_DRY:
            mode = protocol::REPORT_MODE_DRY;
            power = true;
            break;
        case climate::CLIMATE_MODE_FAN_ONLY:
            mode = protocol::REPORT_MODE_FAN;
            power = true;
            break;
        case climate::CLIMATE_MODE_HEAT:
            mode = protocol::REPORT_MODE_HEAT;
            power = true;
            break;
        default:
        case climate::CLIMATE_MODE_OFF:
            /* In case of MODE_OFF we will not alter the last mode setting recieved from AC, see determine_mode() */
            switch (this->mode_internal_)
            {
                case climate::CLIMATE_MODE_AUTO:
                    mode = protocol::REPORT_MODE_AUTO;
                    break;
                case climate::CLIMATE_MODE_COOL:
                    mode = protocol::REPORT_MODE_COOL;
                    break;
                case climate::CLIMATE_MODE_DRY:
                    mode = protocol::REPORT_MODE_DRY;
                    break;
                case climate::CLIMATE_MODE_FAN_ONLY:
                    mode = protocol::REPORT_MODE_FAN;
                    break;
                case climate::CLIMATE_MODE_HEAT:
                    mode = protocol::REPORT_MODE_HEAT;
                    break;
            }
            power = false;
            break;
    }

    payload[protocol::REPORT_MODE_BYTE] |= (mode << protocol::REPORT_MODE_POS);
    if (power)
    {
        payload[protocol::REPORT_PWR_BYTE] |= protocol::REPORT_PWR_MASK;
    }

    /* TARGET TEMPERATURE --------------------------------------------------------------------------- */
    uint8_t target_temperature = static_cast<uint8_t>(round(this->target_temperature));
    payload[protocol::REPORT_TEMP_SET_BYTE] |= ((target_temperature - protocol::REPORT_TEMP_SET_OFF) << protocol::REPORT_TEMP_SET_POS) & protocol::REPORT_TEMP_SET_MASK;

    /* FAN SPEED --------------------------------------------------------------------------- */
    /* below will default to AUTO */
    if (this->has_custom_fan_mode())
    {
        const auto custom_fan_mode = this->get_custom_fan_mode();
        uint8_t fan_mode_byte4 = 0;
        uint8_t fan_mode_byte18 = 0x08; // Auto

        if (custom_fan_mode == fan_modes::FAN_AUTO)
        {
            // Already set to defaults
        }
        else if (custom_fan_mode == fan_modes::FAN_MIN)
        {
            fan_mode_byte4 = 1;
            fan_mode_byte18 = 0x09;
        }
        else if (custom_fan_mode == fan_modes::FAN_LOW)
        {
            fan_mode_byte4 = 2;
            fan_mode_byte18 = 0x0A;
        }
        else if (custom_fan_mode == fan_modes::FAN_MED)
        {
            fan_mode_byte4 = 2;
            fan_mode_byte18 = 0x0B;
        }
        else if (custom_fan_mode == fan_modes::FAN_HIGH)
        {
            fan_mode_byte4 = 3;
            fan_mode_byte18 = 0x0C;
        }
        else if (custom_fan_mode == fan_modes::FAN_MAX)
        {
            fan_mode_byte4 = 3;
            fan_mode_byte18 = 0x0D;
        }

        payload[protocol::REPORT_FAN_SPD2_BYTE] |= (fan_mode_byte4 << protocol::REPORT_FAN_SPD2_POS);
        payload[protocol::REPORT_FAN_SPD1_BYTE] |= (fan_mode_byte18 << protocol::REPORT_FAN_SPD1_POS);
    }

    if (this->turbo_state_)
    {
        payload[protocol::REPORT_FAN_TURBO_BYTE] |= protocol::REPORT_FAN_TURBO_MASK;
    }

    if (this->quiet_state_ == quiet_options::ON)
    {
        payload[protocol::REPORT_FAN_QUIET_BYTE] |= protocol::REPORT_FAN_QUIET_MASK;
    }
    else if (this->quiet_state_ == quiet_options::AUTO)
    {
        payload[protocol::REPORT_FAN_QUIET_BYTE] |= protocol::REPORT_FAN_QUIET_AUTO_MASK;
    }

    /* VERTICAL SWING --------------------------------------------------------------------------- */
    uint8_t mode_vertical_swing = protocol::REPORT_VSWING_OFF;
    if (this->vertical_swing_state_ == vertical_swing_options::OFF)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_OFF;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::FULL)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_FULL;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::DOWN)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_DOWN;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::MIDD)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_MIDD;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::MID)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_MID;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::MIDU)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_MIDU;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::UP)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_UP;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::CDOWN)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_CDOWN;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::CMIDD)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_CMIDD;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::CMID)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_CMID;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::CMIDU)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_CMIDU;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::CUP)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_CUP;
    }
    else
    {
        mode_vertical_swing = protocol::REPORT_VSWING_OFF;
    }
    payload[protocol::REPORT_VSWING_BYTE] |= (mode_vertical_swing << protocol::REPORT_VSWING_POS);

    /* HORIZONTAL SWING --------------------------------------------------------------------------- */
    uint8_t mode_horizontal_swing = protocol::REPORT_HSWING_OFF;
    if (this->horizontal_swing_state_ == horizontal_swing_options::OFF)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_OFF;
    }
    else if (this->horizontal_swing_state_ == horizontal_swing_options::FULL)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_FULL;
    }
    else if (this->horizontal_swing_state_ == horizontal_swing_options::CLEFT)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_CLEFT;
    }
    else if (this->horizontal_swing_state_ == horizontal_swing_options::CMIDL)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_CMIDL;
    }
    else if (this->horizontal_swing_state_ == horizontal_swing_options::CMID)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_CMID;
    }
    else if (this->horizontal_swing_state_ == horizontal_swing_options::CMIDR)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_CMIDR;
    }
    else if (this->horizontal_swing_state_ == horizontal_swing_options::CRIGHT)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_CRIGHT;
    }
    else
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_OFF;
    }
    payload[protocol::REPORT_HSWING_BYTE] |= (mode_horizontal_swing << protocol::REPORT_HSWING_POS);

    /* DISPLAY --------------------------------------------------------------------------- */
    uint8_t display_mode = protocol::REPORT_DISP_MODE_SET;
    if (this->display_state_ == display_options::SET)
    {
        display_mode = protocol::REPORT_DISP_MODE_SET;
    }
    else if (this->display_state_ == display_options::ACT)
    {
        display_mode = protocol::REPORT_DISP_MODE_ACT;
    }

    payload[protocol::REPORT_DISP_MODE_BYTE] |= (display_mode << protocol::REPORT_DISP_MODE_POS);

    if (this->light_state_)
    {
        payload[protocol::REPORT_DISP_ON_BYTE] |= protocol::REPORT_DISP_ON_MASK;
    }

    /* DISPLAY UNIT --------------------------------------------------------------------------- */
    if (this->display_unit_state_ == display_unit_options::DEGF)
    {
        payload[protocol::REPORT_DISP_F_BYTE] |= protocol::REPORT_DISP_F_MASK;
    }

    /* IONIZER -------------------------------------------------------------------------- */
    if (this->ionizer_state_)
    {
        payload[protocol::REPORT_IONIZER1_BYTE] |= protocol::REPORT_IONIZER1_MASK;
        payload[protocol::REPORT_IONIZER2_BYTE] |= protocol::REPORT_IONIZER2_MASK;
    }

    /* BEEPER --------------------------------------------------------------------------- */
    if (!this->beeper_state_)
    {
        payload[protocol::REPORT_BEEPER_BYTE] |= protocol::REPORT_BEEPER_MASK;
    }

    /* SLEEP --------------------------------------------------------------------------- */
    if (this->sleep_state_)
    {
        payload[protocol::REPORT_SLEEP_BYTE] |= protocol::REPORT_SLEEP_MASK;
    }

    /* XFAN --------------------------------------------------------------------------- */
    if (this->xfan_state_)
    {
        payload[protocol::REPORT_XFAN_BYTE] |= protocol::REPORT_XFAN_MASK;
    }

    /* POWERSAVE --------------------------------------------------------------------------- */
    if (this->powersave_state_)
    {
        payload[protocol::REPORT_POWERSAVE_BYTE] |= protocol::REPORT_POWERSAVE_MASK;
    }

    /* IFEEL --------------------------------------------------------------------------- */
    if (this->ifeel_state_)
    {
        payload[protocol::REPORT_IFEEL_BYTE] |= protocol::REPORT_IFEEL_MASK;
    }

    /* Do the command, length */

    memcpy(this->lastpacket, payload, protocol::SET_PACKET_LEN);
    
    uint8_t full_packet[protocol::SET_PACKET_LEN + 5];
    full_packet[0] = protocol::SYNC;
    full_packet[1] = protocol::SYNC;
    full_packet[2] = protocol::SET_PACKET_LEN + 2;
    full_packet[3] = protocol::CMD_OUT_PARAMS_SET;
    memcpy(&full_packet[4], payload, protocol::SET_PACKET_LEN);

    /* Do checksum - sum of all bytes except sync and checksum itself% 0x100 
       the module would be realized by the fact that we are using uint8_t*/
    uint8_t checksum = 0;
    for (uint8_t i = 2 ; i < protocol::SET_PACKET_LEN + 4 ; i++)
    {
        checksum += full_packet[i];
    }
    full_packet[protocol::SET_PACKET_LEN + 4] = checksum;

    this->wait_response_ = true;
    transmit_packet(full_packet, sizeof(full_packet));

    /* update setting state-machine */
    switch(this->update_)
    {
        case ACUpdate::NoUpdate:
            break;
        case ACUpdate::UpdateStart:
            this->update_ = ACUpdate::NoUpdate; // Transition directly to NoUpdate to send AF only once
            break;
        case ACUpdate::UpdateClear:
            this->update_ = ACUpdate::NoUpdate;
            break;
        default:
            this->update_ = ACUpdate::NoUpdate;
            break;
    }
}

void GreeACCNT::send_mac_report_packet()
{
    uint8_t full_packet[16];
    uint8_t mac[6];
    get_mac_address_raw(mac);

    full_packet[0] = protocol::SYNC;
    full_packet[1] = protocol::SYNC;
    full_packet[2] = 0x0D; // Length
    full_packet[3] = protocol::CMD_OUT_MAC_REPORT;
    full_packet[4] = 0x07;
    full_packet[5] = 0x00;
    full_packet[6] = 0x00;
    full_packet[7] = 0x00;
    memcpy(&full_packet[8], mac, 6);
    full_packet[14] = 0x00;

    uint8_t checksum = 0;
    for (uint8_t i = 2; i < 15; i++)
    {
        checksum += full_packet[i];
    }
    full_packet[15] = checksum;

    ESP_LOGD(TAG, "Sending MAC report: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    transmit_packet(full_packet, 16);
}

void GreeACCNT::send_sync_time_packet()
{
    uint8_t full_packet[17];
    full_packet[0] = protocol::SYNC;
    full_packet[1] = protocol::SYNC;
    full_packet[2] = 0x0E; // Length
    full_packet[3] = protocol::CMD_OUT_SYNC_TIME;
    full_packet[4] = 0x04;
    memset(&full_packet[5], 0, 10);
    full_packet[15] = 0x7E;

    uint8_t checksum = 0;
    for (uint8_t i = 2; i < 16; i++)
    {
        checksum += full_packet[i];
    }
    full_packet[16] = checksum;

    ESP_LOGD(TAG, "Sending sync time packet");
    transmit_packet(full_packet, 17);
    this->last_sync_time_sent_ = millis();
}

/*
 * Packet handling
 */

bool GreeACCNT::verify_packet()
{
    /* At least 2 sync bytes + length + type + checksum */
    if (this->serialProcess_.data.size() < 5)
    {
        ESP_LOGW(TAG, "Dropping invalid packet (length)");
        return false;
    }

    /* The header (aka sync bytes) was checked by GreeAC::read_data() */

    /* The frame len was assumed by GreeAC::read_data() */

    /* Check if this packet type sould be processed */
    bool commandAllowed = false;
    for (uint8_t packet : ALLOWED_PACKETS)
    {
        if (this->serialProcess_.data[3] == packet)
        {
            commandAllowed = true;
            break;
        }
    }
    if (!commandAllowed)
    {
        ESP_LOGW(TAG, "Dropping invalid packet (command [%02X] not allowed)", this->serialProcess_.data[3]);
        return false;
    }

    /* Check checksum - sum of all bytes except sync and checksum itself% 0x100 
       the module would be realized by the fact that we are using uint8_t*/
    uint8_t checksum = 0;
    for (uint8_t i = 2 ; i < this->serialProcess_.data.size() - 1 ; i++)
    {
        checksum += this->serialProcess_.data[i];
    }
    if (checksum != this->serialProcess_.data[this->serialProcess_.data.size()-1])
    {
        ESP_LOGD(TAG, "Dropping invalid packet (checksum)");
        return false;
    }

    return true;
}

void GreeACCNT::handle_packet()
{
    if (this->serialProcess_.data[3] == protocol::CMD_IN_UNIT_REPORT)
    {
        /* here we will remove unnecessary elements - header and checksum */
        this->serialProcess_.data.erase(this->serialProcess_.data.begin(), this->serialProcess_.data.begin() + 4); /* remove header */
        this->serialProcess_.data.pop_back();  /* remove checksum */

        /* now process the data */
        bool hasChanged = this->processUnitReport();

        // Detect if AC state differs from what we last sent (indicates remote change)
        bool remoteChanged = false;
        for (uint8_t i : BYTES_TO_CHECK)
        {
            if (i < 45) {
                uint8_t last = lastpacket[i];
                uint8_t current = this->serialProcess_.data[i];
                if (i == protocol::SET_NOCHANGE_BYTE) {
                    last &= ~protocol::SET_NOCHANGE_MASK;
                    current &= ~protocol::SET_NOCHANGE_MASK;
                }
                if (last != current) {
                    remoteChanged = true;
                    break;
                }
            }
        }

        if (hasChanged || remoteChanged || reqmodechange)
        {
            ESP_LOGD(TAG, "State update: hasChanged=%d, remoteChanged=%d, reqmodechange=%d", hasChanged, remoteChanged, reqmodechange);
            this->publish_state();
            reqmodechange = false;
        }

    }
    else 
    {
        ESP_LOGD(TAG, "Received unknown packet type: 0x%02X", this->serialProcess_.data[3]);
    }
}

/*
 * This decodes frame recieved from AC Unit
 */
bool GreeACCNT::processUnitReport()
{
    bool hasChanged = false;

    climate::ClimateMode newMode = determine_mode();
    if (this->mode != newMode) {
        this->mode = newMode;
        hasChanged = true;
    }

    const char* newFanMode = determine_fan_mode();
    if (!this->has_custom_fan_mode() || this->get_custom_fan_mode() != newFanMode) {
        this->set_custom_fan_mode_(newFanMode);
        hasChanged = true;
    }
    
    uint8_t temset = (this->serialProcess_.data[protocol::REPORT_TEMP_SET_BYTE] & protocol::REPORT_TEMP_SET_MASK) >> protocol::REPORT_TEMP_SET_POS;
    float newTargetTemperature = (float)(temset + protocol::REPORT_TEMP_SET_OFF);

    if (this->target_temperature != newTargetTemperature)
    {
        this->target_temperature = newTargetTemperature;
        hasChanged = true;
    }
    
    /* if there is no external sensor mapped to represent current temperature we will get data from AC unit */
    if (this->current_temperature_sensor_ == nullptr)
    {
        float newCurrentTemperature = (float)(this->serialProcess_.data[protocol::REPORT_TEMP_ACT_BYTE] - protocol::REPORT_TEMP_ACT_OFF);
        if (this->current_temperature != newCurrentTemperature) {
            this->current_temperature = newCurrentTemperature;
            hasChanged = true;
        }
    }

    const char* verticalSwing = determine_vertical_swing();
    if (this->vertical_swing_state_ != verticalSwing) {
        this->update_swing_vertical(verticalSwing);
        hasChanged = true;
    }

    const char* horizontalSwing = determine_horizontal_swing();
    if (this->horizontal_swing_state_ != horizontalSwing) {
        this->update_swing_horizontal(horizontalSwing);
        hasChanged = true;
    }

    climate::ClimateSwingMode newSwingMode;
    if (strcmp(verticalSwing, vertical_swing_options::FULL) == 0 && strcmp(horizontalSwing, horizontal_swing_options::FULL) == 0)
        newSwingMode = climate::CLIMATE_SWING_BOTH;
    else if (strcmp(verticalSwing, vertical_swing_options::FULL) == 0)
        newSwingMode = climate::CLIMATE_SWING_VERTICAL;
    else if (strcmp(horizontalSwing, horizontal_swing_options::FULL) == 0)
        newSwingMode = climate::CLIMATE_SWING_HORIZONTAL;
    else
        newSwingMode = climate::CLIMATE_SWING_OFF;
    
    if (this->swing_mode != newSwingMode) {
        this->swing_mode = newSwingMode;
        hasChanged = true;
    }

    const char* display = determine_display();
    if (this->display_state_ != display) {
        this->update_display(display);
        hasChanged = true;
    }

    bool light = determine_light();
    if (this->light_state_ != light) {
        this->update_light(light);
        hasChanged = true;
    }

    const char* display_unit = determine_display_unit();
    if (this->display_unit_state_ != display_unit) {
        this->update_display_unit(display_unit);
        hasChanged = true;
    }

    bool ionizer = determine_ionizer();
    if (this->ionizer_state_ != ionizer) {
        this->update_ionizer(ionizer);
        hasChanged = true;
    }

    bool beeper = determine_beeper();
    if (this->beeper_state_ != beeper) {
        this->update_beeper(beeper);
        hasChanged = true;
    }

    bool sleep = determine_sleep();
    if (this->sleep_state_ != sleep) {
        this->update_sleep(sleep);
        hasChanged = true;
    }

    bool xfan = determine_xfan();
    if (this->xfan_state_ != xfan) {
        this->update_xfan(xfan);
        hasChanged = true;
    }

    bool powersave = determine_powersave();
    if (this->powersave_state_ != powersave) {
        this->update_powersave(powersave);
        hasChanged = true;
    }

    bool turbo = determine_turbo();
    if (this->turbo_state_ != turbo) {
        this->update_turbo(turbo);
        hasChanged = true;
    }

    bool ifeel = determine_ifeel();
    if (this->ifeel_state_ != ifeel) {
        this->update_ifeel(ifeel);
        hasChanged = true;
    }

    const char* quiet = determine_quiet();
    if (this->quiet_state_ != quiet) {
        this->update_quiet(quiet);
        hasChanged = true;
    }

    return hasChanged;
}

climate::ClimateMode GreeACCNT::determine_mode()
{
    uint8_t mode = (this->serialProcess_.data[protocol::REPORT_MODE_BYTE] & protocol::REPORT_MODE_MASK) >> protocol::REPORT_MODE_POS;

    /* as mode presented by climate component incorporates both power and mode we will store this separately for Gree
       in _internal_ fields */
    /* check unit power flag */
    this->power_internal_ = (this->serialProcess_.data[protocol::REPORT_PWR_BYTE] & protocol::REPORT_PWR_MASK) != 0;

    /* check unit mode */
    switch (mode)
    {
        case protocol::REPORT_MODE_AUTO:
            this->mode_internal_ = climate::CLIMATE_MODE_AUTO;
            break;
        case protocol::REPORT_MODE_COOL:
            this->mode_internal_ = climate::CLIMATE_MODE_COOL;
            break;
        case protocol::REPORT_MODE_DRY:
            this->mode_internal_ = climate::CLIMATE_MODE_DRY;
            break;
        case protocol::REPORT_MODE_FAN:
            this->mode_internal_ = climate::CLIMATE_MODE_FAN_ONLY;
            break;
        case protocol::REPORT_MODE_HEAT:
            this->mode_internal_ = climate::CLIMATE_MODE_HEAT;
            break;
        default:
            ESP_LOGW(TAG, "Received unknown climate mode");
            this->mode_internal_ = climate::CLIMATE_MODE_OFF;
            break;
    }

    /* if unit is powered on - return the mode, otherwise return CLIMATE_MODE_OFF */
    if (this->power_internal_)
    {
        return this->mode_internal_;
    }
    else
    {
        return climate::CLIMATE_MODE_OFF;
    }
}

const char* GreeACCNT::determine_fan_mode()
{
    /* fan setting has quite complex representation in the packet, brace for it */
    uint8_t fan_mode = (this->serialProcess_.data[protocol::REPORT_FAN_SPD1_BYTE] & protocol::REPORT_FAN_SPD1_MASK);

    if (fan_mode == 0x08)
        return fan_modes::FAN_AUTO;
    else if (fan_mode == 0x09)
        return fan_modes::FAN_MIN;
    else if (fan_mode == 0x0A)
        return fan_modes::FAN_LOW;
    else if (fan_mode == 0x0B)
        return fan_modes::FAN_MED;
    else if (fan_mode == 0x0C)
        return fan_modes::FAN_HIGH;
    else if (fan_mode == 0x0D)
        return fan_modes::FAN_MAX;
    else
    {
        ESP_LOGW(TAG, "Received unknown fan mode: %d", fan_mode);
        return fan_modes::FAN_AUTO;
    }
}

const char* GreeACCNT::determine_vertical_swing()
{
    uint8_t mode = (this->serialProcess_.data[protocol::REPORT_VSWING_BYTE]  & protocol::REPORT_VSWING_MASK) >> protocol::REPORT_VSWING_POS;

    switch (mode) {
        case protocol::REPORT_VSWING_OFF:
            return vertical_swing_options::OFF;
        case protocol::REPORT_VSWING_FULL:
            return vertical_swing_options::FULL;
        case protocol::REPORT_VSWING_CUP:
            return vertical_swing_options::CUP;
        case protocol::REPORT_VSWING_CMIDU:
            return vertical_swing_options::CMIDU;
        case protocol::REPORT_VSWING_CMID:
            return vertical_swing_options::CMID;
        case protocol::REPORT_VSWING_CMIDD:
            return vertical_swing_options::CMIDD;
        case protocol::REPORT_VSWING_CDOWN:
            return vertical_swing_options::CDOWN;
        case protocol::REPORT_VSWING_DOWN:
            return vertical_swing_options::DOWN;
        case protocol::REPORT_VSWING_MIDD:
            return vertical_swing_options::MIDD;
        case protocol::REPORT_VSWING_MID:
            return vertical_swing_options::MID;
        case protocol::REPORT_VSWING_MIDU:
            return vertical_swing_options::MIDU;
        case protocol::REPORT_VSWING_UP:
            return vertical_swing_options::UP;
        default:
            ESP_LOGW(TAG, "Received unknown vertical swing mode");
            return vertical_swing_options::OFF;
    }
}

const char* GreeACCNT::determine_horizontal_swing()
{
    uint8_t mode = (this->serialProcess_.data[protocol::REPORT_HSWING_BYTE]  & protocol::REPORT_HSWING_MASK) >> protocol::REPORT_HSWING_POS;

    switch (mode) {
        case protocol::REPORT_HSWING_OFF:
            return horizontal_swing_options::OFF;
        case protocol::REPORT_HSWING_FULL:
            return horizontal_swing_options::FULL;
        case protocol::REPORT_HSWING_CLEFT:
            return horizontal_swing_options::CLEFT;
        case protocol::REPORT_HSWING_CMIDL:
            return horizontal_swing_options::CMIDL;
        case protocol::REPORT_HSWING_CMID:
            return horizontal_swing_options::CMID;
        case protocol::REPORT_HSWING_CMIDR:
            return horizontal_swing_options::CMIDR;
        case protocol::REPORT_HSWING_CRIGHT:
            return horizontal_swing_options::CRIGHT;
        default:
            ESP_LOGW(TAG, "Received unknown horizontal swing mode");
            return horizontal_swing_options::OFF;
    }
}

const char* GreeACCNT::determine_display()
{
    uint8_t mode = (this->serialProcess_.data[protocol::REPORT_DISP_MODE_BYTE] & protocol::REPORT_DISP_MODE_MASK) >> protocol::REPORT_DISP_MODE_POS;

    switch (mode) {
        case protocol::REPORT_DISP_MODE_SET:
            return display_options::SET;
        case protocol::REPORT_DISP_MODE_ACT:
            return display_options::ACT;
        case protocol::REPORT_DISP_MODE_OUT:
            ESP_LOGW(TAG, "Outside temperature display mode is not supported and was requested by the unit. Falling back to Set temperature.");
            return display_options::SET;
        default:
            return display_options::SET;
    }
}

bool GreeACCNT::determine_light()
{
    return (this->serialProcess_.data[protocol::REPORT_DISP_ON_BYTE] & protocol::REPORT_DISP_ON_MASK) != 0;
}

const char* GreeACCNT::determine_display_unit()
{
    if (this->serialProcess_.data[protocol::REPORT_DISP_F_BYTE] & protocol::REPORT_DISP_F_MASK)
    {
        return display_unit_options::DEGF;
    }
    else
    {
        return display_unit_options::DEGC;
    }
}

bool GreeACCNT::determine_ionizer(){
    bool ionizer1 = (this->serialProcess_.data[protocol::REPORT_IONIZER1_BYTE] & protocol::REPORT_IONIZER1_MASK) != 0;
    bool ionizer2 = (this->serialProcess_.data[protocol::REPORT_IONIZER2_BYTE] & protocol::REPORT_IONIZER2_MASK) != 0;
    return ionizer1 || ionizer2;
}

bool GreeACCNT::determine_beeper(){
    return (this->serialProcess_.data[protocol::REPORT_BEEPER_BYTE] & protocol::REPORT_BEEPER_MASK) == 0;
}

bool GreeACCNT::determine_sleep(){
    return (this->serialProcess_.data[protocol::REPORT_SLEEP_BYTE] & protocol::REPORT_SLEEP_MASK) != 0;
}

bool GreeACCNT::determine_xfan(){
    return (this->serialProcess_.data[protocol::REPORT_XFAN_BYTE] & protocol::REPORT_XFAN_MASK) != 0;
}

bool GreeACCNT::determine_powersave(){
    return (this->serialProcess_.data[protocol::REPORT_POWERSAVE_BYTE] & protocol::REPORT_POWERSAVE_MASK) != 0;
}

bool GreeACCNT::determine_turbo(){
    return (this->serialProcess_.data[protocol::REPORT_FAN_TURBO_BYTE] & protocol::REPORT_FAN_TURBO_MASK) != 0;
}

bool GreeACCNT::determine_ifeel(){
    return (this->serialProcess_.data[protocol::REPORT_IFEEL_BYTE] & protocol::REPORT_IFEEL_MASK) != 0;
}

const char* GreeACCNT::determine_quiet(){
    if (this->serialProcess_.data[protocol::REPORT_FAN_QUIET_BYTE] & protocol::REPORT_FAN_QUIET_MASK)
        return quiet_options::ON;
    if (this->serialProcess_.data[protocol::REPORT_FAN_QUIET_BYTE] & protocol::REPORT_FAN_QUIET_AUTO_MASK)
        return quiet_options::AUTO;
    return quiet_options::OFF;
}


/*
 * Sensor handling
 */

void GreeACCNT::on_vertical_swing_change(const std::string &swing)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting vertical swing position");

    this->update_ = ACUpdate::UpdateStart;
    this->vertical_swing_state_ = swing;
}

void GreeACCNT::on_horizontal_swing_change(const std::string &swing)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting horizontal swing position");

    this->update_ = ACUpdate::UpdateStart;
    this->horizontal_swing_state_ = swing;
}

void GreeACCNT::on_display_change(const std::string &display)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting display mode");

    this->update_ = ACUpdate::UpdateStart;
    this->display_state_ = display;
}

void GreeACCNT::on_display_unit_change(const std::string &display_unit)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting display unit");

    this->update_ = ACUpdate::UpdateStart;
    this->display_unit_state_ = display_unit;
}

void GreeACCNT::on_light_change(bool light)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting light");

    this->update_ = ACUpdate::UpdateStart;
    this->light_state_ = light;
}

void GreeACCNT::on_ionizer_change(bool ionizer)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting ionizer");

    this->update_ = ACUpdate::UpdateStart;
    this->ionizer_state_ = ionizer;
}

void GreeACCNT::on_beeper_change(bool beeper)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting beeper");

    this->update_ = ACUpdate::UpdateStart;
    this->beeper_state_ = beeper;
}

void GreeACCNT::on_sleep_change(bool sleep)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting sleep");

    this->update_ = ACUpdate::UpdateStart;
    this->sleep_state_ = sleep;
}

void GreeACCNT::on_xfan_change(bool xfan)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting xfan");

    this->update_ = ACUpdate::UpdateStart;
    this->xfan_state_ = xfan;
}

void GreeACCNT::on_powersave_change(bool powersave)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting powersave");

    this->update_ = ACUpdate::UpdateStart;
    this->powersave_state_ = powersave;
}

void GreeACCNT::on_turbo_change(bool turbo)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting turbo");

    this->update_ = ACUpdate::UpdateStart;
    this->turbo_state_ = turbo;

    /* Requirement 1: when turbo gets on, quite must get off. */
    if (turbo) {
        this->update_quiet(quiet_options::OFF);
    }
}

void GreeACCNT::on_ifeel_change(bool ifeel)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting ifeel");

    this->update_ = ACUpdate::UpdateStart;
    this->ifeel_state_ = ifeel;
}

void GreeACCNT::on_quiet_change(const std::string &quiet)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting quiet mode");

    this->update_ = ACUpdate::UpdateStart;
    this->quiet_state_ = quiet;

    /* Requirement 1: when gets on/auto then turbo must go off. */
    if (quiet != quiet_options::OFF) {
        this->update_turbo(false);
    }
}

}  // namespace CNT
}  // namespace gree_ac
}  // namespace esphome
