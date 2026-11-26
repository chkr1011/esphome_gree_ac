// based on: https://github.com/DomiStyle/esphome-panasonic-ac
#pragma once

#include "esphome/components/climate/climate.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"

namespace esphome {

namespace gree_ac {


namespace fan_modes {
    const char *const FAN_MIN = "Minimum";
    const char *const FAN_MAX = "Maximum";
}

enum class FanMode : uint8_t {
    Auto = 0,
    Min = 1,
    Low = 2,
    Med = 3,
    High = 4,
    Max = 5,
};

/* Option indices must match QUIET_OPTIONS in climate.py */
enum class QuietMode : uint8_t {
    Off = 0,
    On = 1,
    Auto = 2,
    Count = 3,
    Unset = 0xFF,
};

/* Option indices must match HORIZONTAL_SWING_OPTIONS in climate.py */
enum class HorizontalSwing : uint8_t {
    Off = 0,
    Full = 1,
    CLeft = 2,
    CMidL = 3,
    CMid = 4,
    CMidR = 5,
    CRight = 6,
    Count = 7,
    Unset = 0xFF,
};

/* Option indices must match VERTICAL_SWING_OPTIONS in climate.py */
enum class VerticalSwing : uint8_t {
    Off = 0,
    Full = 1,
    Down = 2,
    MidD = 3,
    Mid = 4,
    MidU = 5,
    Up = 6,
    CDown = 7,
    CMidD = 8,
    CMid = 9,
    CMidU = 10,
    CUp = 11,
    Count = 12,
    Unset = 0xFF,
};

/* Option indices must match DISPLAY_OPTIONS in climate.py */
enum class DisplayMode : uint8_t {
    Set = 0,
    Act = 1,
    Count = 2,
    Unset = 0xFF,
};

/* Option indices must match DISPLAY_UNIT_OPTIONS in climate.py */
enum class DisplayUnit : uint8_t {
    DegC = 0,
    DegF = 1,
    Count = 2,
    Unset = 0xFF,
};

/* Option indices must match LIGHT_OPTIONS in climate.py */
enum class LightMode : uint8_t {
    Off = 0,
    On = 1,
    Auto = 2,
    Count = 3,
    Unset = 0xFF,
};

typedef enum {
        STATE_WAIT_SYNC,
        STATE_RECIEVE,
        STATE_COMPLETE,
        STATE_RESTART
} SerialProcessState_t;

typedef struct {
  uint8_t data[64];
  size_t size;
  uint8_t frame_size;
  SerialProcessState_t state;
  uint32_t last_byte_time;
} SerialProcess_t;

class GreeAC : public Component, public uart::UARTDevice, public climate::Climate {
    public:
        void set_vertical_swing_select(select::Select *vertical_swing_select);
        void set_horizontal_swing_select(select::Select *horizontal_swing_select);

        void set_display_select(select::Select *display_select);
        void set_display_unit_select(select::Select *display_unit_select);

        void set_light_select(select::Select *light_select);
        void set_ionizer_switch(switch_::Switch *ionizer_switch);
        void set_beeper_switch(switch_::Switch *beeper_switch);
        void set_sleep_switch(switch_::Switch *sleep_switch);
        void set_xfan_switch(switch_::Switch *xfan_switch);
        void set_powersave_switch(switch_::Switch *powersave_switch);
        void set_turbo_switch(switch_::Switch *turbo_switch);
        void set_ifeel_switch(switch_::Switch *ifeel_switch);
        void set_enable_tx_switch(switch_::Switch *enable_tx_switch);
        void set_dump_packets_switch(switch_::Switch *dump_packets_switch);

        void set_quiet_select(select::Select *quiet_select);

        void set_model_id_text_sensor(text_sensor::TextSensor *model_id_text_sensor);

        void setup() override;
        void loop() override;
        void dump_config() override;

    protected:
        select::Select *vertical_swing_select_   = nullptr; /* Advanced vertical swing select */
        select::Select *horizontal_swing_select_ = nullptr; /* Advanced horizontal swing select */

        select::Select *display_select_          = nullptr; /* Select for setting display mode */
        select::Select *display_unit_select_     = nullptr; /* Select for setting display temperature unit */

        select::Select *light_select_            = nullptr; /* Select for light */
        switch_::Switch *ionizer_switch_         = nullptr; /* Switch for ionizer */
        switch_::Switch *beeper_switch_          = nullptr; /* Switch for beeper */
        switch_::Switch *sleep_switch_           = nullptr; /* Switch for sleep */
        switch_::Switch *xfan_switch_            = nullptr; /* Switch for X-fan */
        switch_::Switch *powersave_switch_       = nullptr; /* Switch for powersave */
        switch_::Switch *turbo_switch_           = nullptr; /* Switch for turbo */
        switch_::Switch *ifeel_switch_           = nullptr; /* Switch for I-Feel */
        switch_::Switch *enable_tx_switch_      = nullptr; /* Switch for enabling TX */
        switch_::Switch *dump_packets_switch_   = nullptr; /* Switch for dumping packets */

        select::Select *quiet_select_            = nullptr; /* Select for quiet mode */

        text_sensor::TextSensor *model_id_text_sensor_ = nullptr; /* Text sensor for Model ID */

        VerticalSwing vertical_swing_state_{VerticalSwing::Unset};
        HorizontalSwing horizontal_swing_state_{HorizontalSwing::Unset};

        DisplayMode display_state_{DisplayMode::Unset};
        DisplayUnit display_unit_state_{DisplayUnit::Unset};
        QuietMode quiet_state_{QuietMode::Unset};
        LightMode light_mode_{LightMode::Auto};

        bool light_state_;
        bool ionizer_state_;
        bool beeper_state_;
        bool sleep_state_;
        bool xfan_state_;
        bool powersave_state_;
        bool turbo_state_;
        bool ifeel_state_;

        SerialProcess_t serialProcess_;

        uint32_t init_time_;   // Stores the current time
        // uint32_t last_read_;   // Stores the time at which the last read was done
        uint32_t last_packet_sent_;  // Stores the time at which the last packet was sent
        uint32_t last_03packet_sent_;  // Stores the time at which the last packet was sent
        uint32_t last_packet_received_;  // Stores the time at which the last packet was received
        bool wait_response_;

        climate::ClimateTraits traits() override;

        bool update_current_temperature(float temperature);
        bool update_target_temperature(float temperature);

        bool update_swing_horizontal(HorizontalSwing swing);
        bool update_swing_vertical(VerticalSwing swing);

        bool update_display(DisplayMode display);
        bool update_display_unit(DisplayUnit display_unit);

        bool update_fan_mode(FanMode fan_mode);
        bool update_light(bool light);
        bool update_ionizer(bool ionizer);
        bool update_beeper(bool beeper);
        bool update_sleep(bool sleep);
        bool update_xfan(bool xfan);
        bool update_powersave(bool powersave);
        bool update_turbo(bool turbo);
        bool update_ifeel(bool ifeel);
        bool update_quiet(QuietMode quiet);

        virtual void on_horizontal_swing_change(HorizontalSwing swing) = 0;
        virtual void on_vertical_swing_change(VerticalSwing swing) = 0;

        virtual void on_display_change(DisplayMode display) = 0;
        virtual void on_display_unit_change(DisplayUnit display_unit) = 0;

        virtual void on_light_mode_change(LightMode mode) = 0;
        virtual void on_ionizer_change(bool ionizer) = 0;
        virtual void on_beeper_change(bool beeper) = 0;
        virtual void on_sleep_change(bool sleep) = 0;
        virtual void on_xfan_change(bool xfan) = 0;
        virtual void on_powersave_change(bool powersave) = 0;
        virtual void on_turbo_change(bool turbo) = 0;
        virtual void on_ifeel_change(bool ifeel) = 0;
        virtual void on_quiet_change(QuietMode quiet) = 0;

        climate::ClimateAction determine_action();

        void log_packet(const uint8_t *data, size_t len, bool outgoing = false) __attribute__((noinline));

    protected:
        static const char *const VERSION;
        static const uint16_t READ_TIMEOUT;
        static const uint8_t MIN_TEMPERATURE;
        static const uint8_t MAX_TEMPERATURE;
        static const float TEMPERATURE_STEP;
        static const float TEMPERATURE_TOLERANCE;
        static const uint8_t TEMPERATURE_THRESHOLD;
        static const uint8_t DATA_MAX;
};

}  // namespace gree_ac
}  // namespace esphome
