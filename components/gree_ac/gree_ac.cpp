// based on: https://github.com/DomiStyle/esphome-panasonic-ac
#include "gree_ac.h"

#include "esphome/core/log.h"

namespace esphome {
namespace gree_ac {

static const char *const TAG = "gree_ac";

climate::ClimateTraits GreeAC::traits()
{
    auto traits = climate::ClimateTraits();

    traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
    traits.set_visual_min_temperature(MIN_TEMPERATURE);
    traits.set_visual_max_temperature(MAX_TEMPERATURE);
    traits.set_visual_temperature_step(TEMPERATURE_STEP);

    traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_AUTO, climate::CLIMATE_MODE_COOL,
                                climate::CLIMATE_MODE_HEAT, climate::CLIMATE_MODE_FAN_ONLY, climate::CLIMATE_MODE_DRY});

    traits.set_supported_custom_fan_modes({fan_modes::FAN_AUTO, fan_modes::FAN_LOW,
                                           fan_modes::FAN_MEDL, fan_modes::FAN_MED,
                                           fan_modes::FAN_MEDH, fan_modes::FAN_HIGH,
                                           fan_modes::FAN_TURBO, fan_modes::FAN_QUIET});

    return traits;
}

void GreeAC::setup()
{
  // Initialize times
    this->init_time_ = millis();
    this->last_packet_sent_ = millis();
    this->serialProcess_.state = STATE_WAIT_SYNC;
    this->serialProcess_.last_byte_time = millis();

    ESP_LOGI(TAG, "Gree AC component v%s starting...", VERSION);
}

void GreeAC::dump_config() {
    LOG_CLIMATE("", "Gree AC", this);
    ESP_LOGCONFIG(TAG, "  Component Version: %s", VERSION);
}

void GreeAC::loop()
{
    read_data();  // Read data from UART (if there is any)
}

void GreeAC::read_data() {
  // Check for timeout of partially received packet
  if (this->serialProcess_.state != STATE_WAIT_SYNC &&
      this->serialProcess_.state != STATE_COMPLETE &&
      this->serialProcess_.state != STATE_RESTART &&
      millis() - this->serialProcess_.last_byte_time > READ_TIMEOUT) {
    ESP_LOGV(TAG, "Packet reception timeout (state=%d, bytes=%zu), resetting state machine",
             (int)this->serialProcess_.state, this->serialProcess_.data.size());
    this->serialProcess_.state = STATE_RESTART;
  }

  while (available()) {
    /* If we had a packet or a packet had not been decoded yet - do not receive more data */
    if (this->serialProcess_.state == STATE_COMPLETE) {
      break;
    }

    uint8_t c;
    if (!this->read_byte(&c)) {
      break;
    }
    this->serialProcess_.last_byte_time = millis();

    if (this->serialProcess_.state == STATE_RESTART) {
      this->serialProcess_.data.clear();
      this->serialProcess_.state = STATE_WAIT_SYNC;
    }

    switch (this->serialProcess_.state) {
      case STATE_WAIT_SYNC:
        if (c == 0x7E) {
          if (this->serialProcess_.data.size() < 2) {
            this->serialProcess_.data.push_back(c);
          }
        } else {
          if (this->serialProcess_.data.size() == 2) {
            // Found non-0x7E after at least two 0x7E's -> it's the length
            this->serialProcess_.data.push_back(c);
            this->serialProcess_.frame_size = c;
            this->serialProcess_.state = STATE_RECIEVE;
          } else {
            // Noise or partial sync, reset
            this->serialProcess_.data.clear();
          }
        }
        break;

      case STATE_RECIEVE:
        this->serialProcess_.data.push_back(c);
        if (this->serialProcess_.data.size() >= (size_t)(this->serialProcess_.frame_size + 3)) {
          /* WE HAVE A FULL FRAME FROM AC */
          this->serialProcess_.state = STATE_COMPLETE;
        }
        break;

      default:
        break;
    }

    if (this->serialProcess_.data.size() >= DATA_MAX) {
      ESP_LOGW(TAG, "Buffer overflow, resetting state machine");
      this->serialProcess_.data.clear();
      this->serialProcess_.state = STATE_WAIT_SYNC;
    }
  }
}

void GreeAC::update_current_temperature(float temperature)
{
    if (temperature > TEMPERATURE_THRESHOLD) {
        ESP_LOGW(TAG, "Received out of range inside temperature: %f", temperature);
        return;
    }

    this->current_temperature = temperature;
}

void GreeAC::update_target_temperature(float temperature)
{
    if (temperature > TEMPERATURE_THRESHOLD) {
        ESP_LOGW(TAG, "Received out of range target temperature %.2f", temperature);
        return;
    }

    this->target_temperature = temperature;
}

void GreeAC::update_swing_horizontal(const std::string &swing)
{
    this->horizontal_swing_state_ = swing;

    if (this->horizontal_swing_select_ != nullptr &&
        this->horizontal_swing_select_->current_option() != this->horizontal_swing_state_)
    {
        this->horizontal_swing_select_->publish_state(this->horizontal_swing_state_);
    }
}

void GreeAC::update_swing_vertical(const std::string &swing)
{
    this->vertical_swing_state_ = swing;

    if (this->vertical_swing_select_ != nullptr && 
        this->vertical_swing_select_->current_option() != this->vertical_swing_state_)
    {
        this->vertical_swing_select_->publish_state(this->vertical_swing_state_);
    }
}

void GreeAC::update_display(const std::string &display)
{
    this->display_state_ = display;

    if (this->display_select_ != nullptr && 
        this->display_select_->current_option() != this->display_state_)
    {
        this->display_select_->publish_state(this->display_state_);
    }
}

void GreeAC::update_display_unit(const std::string &display_unit)
{
    this->display_unit_state_ = display_unit;

    if (this->display_unit_select_ != nullptr && 
        this->display_unit_select_->current_option() != this->display_unit_state_)
    {
        this->display_unit_select_->publish_state(this->display_unit_state_);
    }
}

void GreeAC::update_light(bool light)
{
    this->light_state_ = light;

    if (this->light_switch_ != nullptr)
    {
        this->light_switch_->publish_state(this->light_state_);
    }
}

void GreeAC::update_plasma(bool plasma)
{
    this->plasma_state_ = plasma;

    if (this->plasma_switch_ != nullptr)
    {
        this->plasma_switch_->publish_state(this->plasma_state_);
    }
}

void GreeAC::update_beeper(bool beeper)
{
    this->beeper_state_ = beeper;

    if (this->beeper_switch_ != nullptr)
    {
        this->beeper_switch_->publish_state(this->beeper_state_);
    }
}

void GreeAC::update_sleep(bool sleep)
{
    this->sleep_state_ = sleep;

    if (this->sleep_switch_ != nullptr)
    {
        this->sleep_switch_->publish_state(this->sleep_state_);
    }
}

void GreeAC::update_xfan(bool xfan)
{
    this->xfan_state_ = xfan;

    if (this->xfan_switch_ != nullptr)
    {
        this->xfan_switch_->publish_state(this->xfan_state_);
    }
}

void GreeAC::update_save(bool save)
{
    this->save_state_ = save;

    if (this->save_switch_ != nullptr)
    {
        this->save_switch_->publish_state(this->save_state_);
    }
}

climate::ClimateAction GreeAC::determine_action()
{
    if (this->mode == climate::CLIMATE_MODE_OFF) {
        return climate::CLIMATE_ACTION_OFF;
    } else if (this->mode == climate::CLIMATE_MODE_FAN_ONLY) {
        return climate::CLIMATE_ACTION_FAN;
    } else if (this->mode == climate::CLIMATE_MODE_DRY) {
        return climate::CLIMATE_ACTION_DRYING;
    } else if ((this->mode == climate::CLIMATE_MODE_COOL || this->mode == climate::CLIMATE_MODE_HEAT_COOL) &&
                this->current_temperature + TEMPERATURE_TOLERANCE >= this->target_temperature) {
        return climate::CLIMATE_ACTION_COOLING;
    } else if ((this->mode == climate::CLIMATE_MODE_HEAT || this->mode == climate::CLIMATE_MODE_HEAT_COOL) &&
                this->current_temperature - TEMPERATURE_TOLERANCE <= this->target_temperature) {
        return climate::CLIMATE_ACTION_HEATING;
    } else {
        return climate::CLIMATE_ACTION_IDLE;
    }
}

/*
 * Sensor handling
 */

void GreeAC::set_current_temperature_sensor(sensor::Sensor *current_temperature_sensor)
{
    this->current_temperature_sensor_ = current_temperature_sensor;
    this->current_temperature_sensor_->add_on_state_callback([this](float state)
        {
            this->current_temperature = state;
            this->publish_state();
        });
}

void GreeAC::set_vertical_swing_select(select::Select *vertical_swing_select)
{
    this->vertical_swing_select_ = vertical_swing_select;
    this->vertical_swing_select_->add_on_state_callback([this](size_t index) {
        auto value = this->vertical_swing_select_->at(index);
        if (!value.has_value() || *value == this->vertical_swing_state_)
            return;
        this->on_vertical_swing_change(*value);
    });
}

void GreeAC::set_horizontal_swing_select(select::Select *horizontal_swing_select)
{
    this->horizontal_swing_select_ = horizontal_swing_select;
    this->horizontal_swing_select_->add_on_state_callback([this](size_t index) {
        auto value = this->horizontal_swing_select_->at(index);
        if (!value.has_value() || *value == this->horizontal_swing_state_)
            return;
        this->on_horizontal_swing_change(*value);
    });
}

void GreeAC::set_display_select(select::Select *display_select)
{
    this->display_select_ = display_select;
    this->display_select_->add_on_state_callback([this](size_t index) {
        auto value = this->display_select_->at(index);
        if (!value.has_value() || *value == this->display_state_)
            return;
        this->on_display_change(*value);
    });
}

void GreeAC::set_display_unit_select(select::Select *display_unit_select)
{
    this->display_unit_select_ = display_unit_select;
    this->display_unit_select_->add_on_state_callback([this](size_t index) {
        auto value = this->display_unit_select_->at(index);
        if (!value.has_value() || *value == this->display_unit_state_)
            return;
        this->on_display_unit_change(*value);
    });
}

void GreeAC::set_light_switch(switch_::Switch *light_switch)
{
    this->light_switch_ = light_switch;
    this->light_switch_->add_on_state_callback([this](bool state) {
        if (state == this->light_state_)
            return;
        this->on_light_change(state);
    });
}

void GreeAC::set_plasma_switch(switch_::Switch *plasma_switch)
{
    this->plasma_switch_ = plasma_switch;
    this->plasma_switch_->add_on_state_callback([this](bool state) {
        if (state == this->plasma_state_)
            return;
        this->on_plasma_change(state);
    });
}

void GreeAC::set_beeper_switch(switch_::Switch *beeper_switch)
{
    this->beeper_switch_ = beeper_switch;
    this->beeper_switch_->add_on_state_callback([this](bool state) {
        if (state == this->beeper_state_)
            return;
        this->on_beeper_change(state);
    });
}

void GreeAC::set_sleep_switch(switch_::Switch *sleep_switch)
{
    this->sleep_switch_ = sleep_switch;
    this->sleep_switch_->add_on_state_callback([this](bool state) {
        if (state == this->sleep_state_)
            return;
        this->on_sleep_change(state);
    });
}

void GreeAC::set_xfan_switch(switch_::Switch *xfan_switch)
{
    this->xfan_switch_ = xfan_switch;
    this->xfan_switch_->add_on_state_callback([this](bool state) {
        if (state == this->xfan_state_)
            return;
        this->on_xfan_change(state);
    });
}

void GreeAC::set_save_switch(switch_::Switch *save_switch)
{
    this->save_switch_ = save_switch;
    this->save_switch_->add_on_state_callback([this](bool state) {
        if (state == this->save_state_)
            return;
        this->on_save_change(state);
    });
}

/*
 * Debugging
 */

void GreeAC::log_packet(std::vector<uint8_t> data, bool outgoing)
{
    if (outgoing) {
        ESP_LOGV(TAG, "TX: %s", format_hex_pretty(data).c_str());
    } else {
        ESP_LOGV(TAG, "RX: %s", format_hex_pretty(data).c_str());
    }
}

}  // namespace gree_ac
}  // namespace esphome
