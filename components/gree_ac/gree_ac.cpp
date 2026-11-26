// based on: https://github.com/DomiStyle/esphome-panasonic-ac
#include "gree_ac.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace gree_ac {

static const char *const TAG = "gree_ac";

const char *const GreeAC::VERSION = "0.0.1";
const uint16_t GreeAC::READ_TIMEOUT = 100;
const uint8_t GreeAC::MIN_TEMPERATURE = 16;
const uint8_t GreeAC::MAX_TEMPERATURE = 30;
const float GreeAC::TEMPERATURE_STEP = 1.0;
const float GreeAC::TEMPERATURE_TOLERANCE = 2;
const uint8_t GreeAC::TEMPERATURE_THRESHOLD = 100;
const uint8_t GreeAC::DATA_MAX = sizeof(SerialProcess_t::data);

climate::ClimateTraits GreeAC::traits()
{
    auto traits = climate::ClimateTraits();

    traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
    traits.set_visual_min_temperature(MIN_TEMPERATURE);
    traits.set_visual_max_temperature(MAX_TEMPERATURE);
    traits.set_visual_temperature_step(TEMPERATURE_STEP);

    traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_AUTO, climate::CLIMATE_MODE_COOL,
                                climate::CLIMATE_MODE_HEAT, climate::CLIMATE_MODE_FAN_ONLY, climate::CLIMATE_MODE_DRY});

    traits.set_supported_fan_modes({climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW,
                                    climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_HIGH});

    return traits;
}

void GreeAC::setup()
{
    this->set_supported_custom_fan_modes({fan_modes::FAN_MIN, fan_modes::FAN_MAX});

  // Initialize times
    this->init_time_ = millis();
    this->last_packet_sent_ = millis();
    this->light_mode_ = LightMode::AUTO;
    this->light_state_ = false;

    if (this->light_select_ != nullptr) {
        this->light_select_->publish_state(static_cast<size_t>(this->light_mode_));
    }

    if (this->enable_tx_switch_ != nullptr) {
        this->enable_tx_switch_->publish_state(true);
    }

    if (this->dump_packets_switch_ != nullptr) {
        this->dump_packets_switch_->publish_state(false);
    }

    this->serialProcess_.state = STATE_WAIT_SYNC;
    this->serialProcess_.last_byte_time = millis();
    this->serialProcess_.size = 0;

    ESP_LOGI(TAG, "Gree AC component v%s starting...", VERSION);
}

void GreeAC::dump_config() {
    LOG_CLIMATE("", "Gree AC", this);
    ESP_LOGCONFIG(TAG, "  Component Version: %s", VERSION);
}

void GreeAC::loop()
{
  uint8_t loop_count = 0;
  while (available() && loop_count < 32) {
    if (this->serialProcess_.state == STATE_COMPLETE) {
      break;
    }
    loop_count++;

    uint8_t c;
    if (!this->read_byte(&c)) {
      break;
    }
    uint32_t now = millis();
    this->serialProcess_.last_byte_time = now;

    this->serialProcess_.data[this->serialProcess_.size++] = c;
    size_t s = this->serialProcess_.size;

    // Check for sync marker within packet (resync)
    if (s >= 2 && this->serialProcess_.data[s-2] == 0x7E && this->serialProcess_.data[s-1] == 0x7E) {
        if (s > 2) {
            this->serialProcess_.data[0] = 0x7E;
            this->serialProcess_.data[1] = 0x7E;
            this->serialProcess_.size = 2;
            s = 2;
        }
    } else if (s == 1 && this->serialProcess_.data[0] != 0x7E) {
        this->serialProcess_.size = 0;
        continue;
    } else if (s == 2 && this->serialProcess_.data[0] == 0x7E && this->serialProcess_.data[1] != 0x7E) {
        this->serialProcess_.size = 0;
        continue;
    }

    if (s == 3) {
      if (c > DATA_MAX - 3) {
        this->serialProcess_.size = 0;
        continue;
      }
      this->serialProcess_.frame_size = c;
    }

    if (s >= 3 && s == (size_t)(this->serialProcess_.frame_size + 3)) {
      this->serialProcess_.state = STATE_COMPLETE;
    } else if (s >= DATA_MAX) {
      this->serialProcess_.size = 0;
    }
  }
}

bool GreeAC::update_current_temperature(float temperature)
{
    if (temperature > TEMPERATURE_THRESHOLD) {
        ESP_LOGW(TAG, "Received out of range inside temperature: %f", temperature);
        return false;
    }

    if (this->current_temperature == temperature)
        return false;

    this->current_temperature = temperature;
    return true;
}

bool GreeAC::update_target_temperature(float temperature)
{
    if (temperature > TEMPERATURE_THRESHOLD) {
        ESP_LOGW(TAG, "Received out of range target temperature %.2f", temperature);
        return false;
    }

    if (this->target_temperature == temperature)
        return false;

    this->target_temperature = temperature;
    return true;
}

bool GreeAC::update_fan_mode(FanMode fan_mode)
{
    if (fan_mode == FanMode::MIN || fan_mode == FanMode::MAX) {
        const char *custom = (fan_mode == FanMode::MIN) ? fan_modes::FAN_MIN : fan_modes::FAN_MAX;
        if (this->get_custom_fan_mode() == custom)
            return false;
        this->fan_mode.reset();
        this->set_custom_fan_mode_(custom);
        return true;
    }

    climate::ClimateFanMode new_fan_mode;
    switch (fan_mode) {
        case FanMode::AUTO:
            new_fan_mode = climate::CLIMATE_FAN_AUTO;
            break;
        case FanMode::LOW:
            new_fan_mode = climate::CLIMATE_FAN_LOW;
            break;
        case FanMode::MED:
            new_fan_mode = climate::CLIMATE_FAN_MEDIUM;
            break;
        case FanMode::HIGH:
            new_fan_mode = climate::CLIMATE_FAN_HIGH;
            break;
        default:
            ESP_LOGW(TAG, "Unknown fan mode: %u", static_cast<uint8_t>(fan_mode));
            return false;
    }

    if (this->fan_mode == new_fan_mode && this->get_custom_fan_mode().empty())
        return false;

    this->clear_custom_fan_mode_();
    this->fan_mode = new_fan_mode;
    return true;
}

bool GreeAC::update_swing_horizontal(HorizontalSwing swing)
{
    if (swing >= HorizontalSwing::COUNT || this->horizontal_swing_state_ == swing)
        return false;

    this->horizontal_swing_state_ = swing;

    if (this->horizontal_swing_select_ != nullptr)
    {
        const auto idx = static_cast<size_t>(swing);
        auto active = this->horizontal_swing_select_->active_index();
        if (!active.has_value() || *active != idx)
        {
            this->horizontal_swing_select_->publish_state(idx);
        }
    }
    return true;
}

bool GreeAC::update_swing_vertical(VerticalSwing swing)
{
    if (swing >= VerticalSwing::COUNT || this->vertical_swing_state_ == swing)
        return false;

    this->vertical_swing_state_ = swing;

    if (this->vertical_swing_select_ != nullptr)
    {
        const auto idx = static_cast<size_t>(swing);
        auto active = this->vertical_swing_select_->active_index();
        if (!active.has_value() || *active != idx)
        {
            this->vertical_swing_select_->publish_state(idx);
        }
    }
    return true;
}

bool GreeAC::update_display(DisplayMode display)
{
    if (display >= DisplayMode::COUNT || this->display_state_ == display)
        return false;

    this->display_state_ = display;

    if (this->display_select_ != nullptr)
    {
        const auto idx = static_cast<size_t>(display);
        auto active = this->display_select_->active_index();
        if (!active.has_value() || *active != idx)
        {
            this->display_select_->publish_state(idx);
        }
    }
    return true;
}

bool GreeAC::update_display_unit(DisplayUnit display_unit)
{
    if (display_unit >= DisplayUnit::COUNT || this->display_unit_state_ == display_unit)
        return false;

    this->display_unit_state_ = display_unit;

    if (this->display_unit_select_ != nullptr)
    {
        const auto idx = static_cast<size_t>(display_unit);
        auto active = this->display_unit_select_->active_index();
        if (!active.has_value() || *active != idx)
        {
            this->display_unit_select_->publish_state(idx);
        }
    }
    return true;
}

bool GreeAC::update_light(bool light)
{
    bool changed = (this->light_state_ != light);
    this->light_state_ = light;

    if (this->light_select_ != nullptr && this->light_mode_ < LightMode::COUNT)
    {
        const auto idx = static_cast<size_t>(this->light_mode_);
        auto active = this->light_select_->active_index();
        if (!active.has_value() || *active != idx)
        {
            this->light_select_->publish_state(idx);
            changed = true;
        }
    }
    return changed;
}

bool GreeAC::update_ionizer(bool ionizer)
{
    if (this->ionizer_state_ == ionizer)
        return false;

    this->ionizer_state_ = ionizer;

    if (this->ionizer_switch_ != nullptr)
    {
        this->ionizer_switch_->publish_state(this->ionizer_state_);
    }
    return true;
}

bool GreeAC::update_beeper(bool beeper)
{
    if (this->beeper_state_ == beeper)
        return false;

    this->beeper_state_ = beeper;

    if (this->beeper_switch_ != nullptr)
    {
        this->beeper_switch_->publish_state(this->beeper_state_);
    }
    return true;
}

bool GreeAC::update_sleep(bool sleep)
{
    if (this->sleep_state_ == sleep)
        return false;

    this->sleep_state_ = sleep;

    if (this->sleep_switch_ != nullptr)
    {
        this->sleep_switch_->publish_state(this->sleep_state_);
    }
    return true;
}

bool GreeAC::update_xfan(bool xfan)
{
    if (this->xfan_state_ == xfan)
        return false;

    this->xfan_state_ = xfan;

    if (this->xfan_switch_ != nullptr)
    {
        this->xfan_switch_->publish_state(this->xfan_state_);
    }
    return true;
}

bool GreeAC::update_powersave(bool powersave)
{
    if (this->powersave_state_ == powersave)
        return false;

    this->powersave_state_ = powersave;

    if (this->powersave_switch_ != nullptr)
    {
        this->powersave_switch_->publish_state(this->powersave_state_);
    }
    return true;
}

bool GreeAC::update_turbo(bool turbo)
{
    if (this->turbo_state_ == turbo)
        return false;

    this->turbo_state_ = turbo;

    if (this->turbo_switch_ != nullptr)
    {
        this->turbo_switch_->publish_state(this->turbo_state_);
    }
    return true;
}

bool GreeAC::update_ifeel(bool ifeel)
{
    if (this->ifeel_state_ == ifeel)
        return false;

    this->ifeel_state_ = ifeel;

    if (this->ifeel_switch_ != nullptr)
    {
        this->ifeel_switch_->publish_state(this->ifeel_state_);
    }
    return true;
}

bool GreeAC::update_quiet(QuietMode quiet)
{
    if (quiet >= QuietMode::COUNT || this->quiet_state_ == quiet)
        return false;

    this->quiet_state_ = quiet;

    if (this->quiet_select_ != nullptr)
    {
        const auto idx = static_cast<size_t>(quiet);
        auto active = this->quiet_select_->active_index();
        if (!active.has_value() || *active != idx)
        {
            this->quiet_select_->publish_state(idx);
        }
    }
    return true;
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

void GreeAC::set_vertical_swing_select(select::Select *vertical_swing_select)
{
    this->vertical_swing_select_ = vertical_swing_select;
    this->vertical_swing_select_->add_on_state_callback([this](size_t index) {
        if (index >= static_cast<size_t>(VerticalSwing::COUNT))
            return;
        auto swing = static_cast<VerticalSwing>(index);
        if (swing == this->vertical_swing_state_)
            return;
        this->on_vertical_swing_change(swing);
    });
}

void GreeAC::set_horizontal_swing_select(select::Select *horizontal_swing_select)
{
    this->horizontal_swing_select_ = horizontal_swing_select;
    this->horizontal_swing_select_->add_on_state_callback([this](size_t index) {
        if (index >= static_cast<size_t>(HorizontalSwing::COUNT))
            return;
        auto swing = static_cast<HorizontalSwing>(index);
        if (swing == this->horizontal_swing_state_)
            return;
        this->on_horizontal_swing_change(swing);
    });
}

void GreeAC::set_display_select(select::Select *display_select)
{
    this->display_select_ = display_select;
    this->display_select_->add_on_state_callback([this](size_t index) {
        if (index >= static_cast<size_t>(DisplayMode::COUNT))
            return;
        auto display = static_cast<DisplayMode>(index);
        if (display == this->display_state_)
            return;
        this->on_display_change(display);
    });
}

void GreeAC::set_display_unit_select(select::Select *display_unit_select)
{
    this->display_unit_select_ = display_unit_select;
    this->display_unit_select_->add_on_state_callback([this](size_t index) {
        if (index >= static_cast<size_t>(DisplayUnit::COUNT))
            return;
        auto unit = static_cast<DisplayUnit>(index);
        if (unit == this->display_unit_state_)
            return;
        this->on_display_unit_change(unit);
    });
}

void GreeAC::set_light_select(select::Select *light_select)
{
    this->light_select_ = light_select;
    this->light_select_->add_on_state_callback([this](size_t index) {
        if (index >= static_cast<size_t>(LightMode::COUNT))
            return;
        auto mode = static_cast<LightMode>(index);
        if (mode == this->light_mode_)
            return;
        this->on_light_mode_change(mode);
    });
}

void GreeAC::set_ionizer_switch(switch_::Switch *ionizer_switch)
{
    this->ionizer_switch_ = ionizer_switch;
    this->ionizer_switch_->add_on_state_callback([this](bool state) {
        if (state == this->ionizer_state_)
            return;
        this->on_ionizer_change(state);
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

void GreeAC::set_powersave_switch(switch_::Switch *powersave_switch)
{
    this->powersave_switch_ = powersave_switch;
    this->powersave_switch_->add_on_state_callback([this](bool state) {
        if (state == this->powersave_state_)
            return;
        this->on_powersave_change(state);
    });
}

void GreeAC::set_turbo_switch(switch_::Switch *turbo_switch)
{
    this->turbo_switch_ = turbo_switch;
    this->turbo_switch_->add_on_state_callback([this](bool state) {
        if (state == this->turbo_state_)
            return;
        this->on_turbo_change(state);
    });
}

void GreeAC::set_ifeel_switch(switch_::Switch *ifeel_switch)
{
    this->ifeel_switch_ = ifeel_switch;
    this->ifeel_switch_->add_on_state_callback([this](bool state) {
        if (state == this->ifeel_state_)
            return;
        this->on_ifeel_change(state);
    });
}

void GreeAC::set_enable_tx_switch(switch_::Switch *enable_tx_switch)
{
    this->enable_tx_switch_ = enable_tx_switch;
}

void GreeAC::set_dump_packets_switch(switch_::Switch *dump_packets_switch)
{
    this->dump_packets_switch_ = dump_packets_switch;
}

void GreeAC::set_quiet_select(select::Select *quiet_select)
{
    this->quiet_select_ = quiet_select;
    this->quiet_select_->add_on_state_callback([this](size_t index) {
        if (index >= static_cast<size_t>(QuietMode::COUNT))
            return;
        auto quiet = static_cast<QuietMode>(index);
        if (quiet == this->quiet_state_)
            return;
        this->on_quiet_change(quiet);
    });
}

void GreeAC::set_model_id_text_sensor(text_sensor::TextSensor *model_id_text_sensor)
{
    this->model_id_text_sensor_ = model_id_text_sensor;
}

/*
 * Debugging
 */

void GreeAC::log_packet(const uint8_t *data, size_t len, bool outgoing)
{
    if (this->dump_packets_switch_ != nullptr && !this->dump_packets_switch_->state) {
        return;
    }

#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
    const char *prefix = "";
    if (outgoing) {
        bool tx_enabled = (this->enable_tx_switch_ == nullptr || this->enable_tx_switch_->state);
        prefix = tx_enabled ? " TX :" : "(TX):";
    } else {
        prefix = " RX :";
    }
    char hex_buf[format_hex_pretty_size(sizeof(SerialProcess_t::data))];
    ESP_LOGV(TAG, "%s %s", prefix, format_hex_pretty_to(hex_buf, sizeof(hex_buf), data, len, '.'));
#else
    (void) data;
    (void) len;
    (void) outgoing;
#endif
}


}  // namespace gree_ac
}  // namespace esphome
