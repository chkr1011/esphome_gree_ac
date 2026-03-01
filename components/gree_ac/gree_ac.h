// based on: https://github.com/DomiStyle/esphome-panasonic-ac
#pragma once

#include "esphome/components/climate/climate.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"

namespace esphome {

namespace gree_ac {


namespace fan_modes{
    const char* const FAN_AUTO  = "Auto";
    const char* const FAN_MIN   = "Minimum";
    const char* const FAN_LOW   = "Low";
    const char* const FAN_MED   = "Medium";
    const char* const FAN_HIGH  = "High";
    const char* const FAN_MAX   = "Maximum";
}

/* this must be same as QUIET_OPTIONS in climate.py */
namespace quiet_options{
    const char* const OFF   = "Off";
    const char* const ON    = "On";
    const char* const AUTO  = "Auto";
}

/* this must be same as HORIZONTAL_SWING_OPTIONS in climate.py */
namespace horizontal_swing_options{
    const char* const OFF    = "Off";
    const char* const FULL   = "Swing - Full";
    const char* const CLEFT  = "Constant - Left";
    const char* const CMIDL  = "Constant - Mid-Left";
    const char* const CMID   = "Constant - Middle";
    const char* const CMIDR  = "Constant - Mid-Right";
    const char* const CRIGHT = "Constant - Right";
}

/* this must be same as VERTICAL_SWING_OPTIONS in climate.py */
namespace vertical_swing_options{
    const char* const OFF   = "Off";
    const char* const FULL  = "Swing - Full";
    const char* const DOWN  = "Swing - Down";
    const char* const MIDD  = "Swing - Mid-Down";
    const char* const MID   = "Swing - Middle";
    const char* const MIDU  = "Swing - Mid-Up";
    const char* const UP    = "Swing - Up";
    const char* const CDOWN = "Constant - Down";
    const char* const CMIDD = "Constant - Mid-Down";
    const char* const CMID  = "Constant - Middle";
    const char* const CMIDU = "Constant - Mid-Up";
    const char* const CUP   = "Constant - Up";
}

/* this must be same as DISPLAY_OPTIONS in climate.py */
namespace display_options{
    const char* const SET  = "Set temperature";
    const char* const ACT  = "Actual temperature";
}

/* this must be same as DISPLAY_UNIT_OPTIONS in climate.py */
namespace display_unit_options{
    const char* const DEGC = "C";
    const char* const DEGF = "F";
}

/* this must be same as LIGHT_OPTIONS in climate.py */
namespace light_options{
    const char* const OFF   = "Off";
    const char* const ON    = "On";
    const char* const AUTO  = "Auto";
}

typedef enum {
        STATE_WAIT_SYNC,
        STATE_RECIEVE,
        STATE_COMPLETE,
        STATE_RESTART
} SerialProcessState_t;

typedef struct {
  uint8_t data[200];
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

        void set_quiet_select(select::Select *quiet_select);

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

        select::Select *quiet_select_            = nullptr; /* Select for quiet mode */

        std::string vertical_swing_state_;
        std::string horizontal_swing_state_;

        std::string display_state_;
        std::string display_unit_state_;
        std::string quiet_state_;
        std::string light_mode_;

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

        bool update_swing_horizontal(const std::string &swing);
        bool update_swing_vertical(const std::string &swing);

        bool update_display(const std::string &display);
        bool update_display_unit(const std::string &display_unit);

        bool update_light(bool light);
        bool update_ionizer(bool ionizer);
        bool update_beeper(bool beeper);
        bool update_sleep(bool sleep);
        bool update_xfan(bool xfan);
        bool update_powersave(bool powersave);
        bool update_turbo(bool turbo);
        bool update_ifeel(bool ifeel);
        bool update_quiet(const std::string &quiet);

        virtual void on_horizontal_swing_change(const std::string &swing) = 0;
        virtual void on_vertical_swing_change(const std::string &swing) = 0;

        virtual void on_display_change(const std::string &display) = 0;
        virtual void on_display_unit_change(const std::string &display_unit) = 0;

        virtual void on_light_mode_change(const std::string &mode) = 0;
        virtual void on_ionizer_change(bool ionizer) = 0;
        virtual void on_beeper_change(bool beeper) = 0;
        virtual void on_sleep_change(bool sleep) = 0;
        virtual void on_xfan_change(bool xfan) = 0;
        virtual void on_powersave_change(bool powersave) = 0;
        virtual void on_turbo_change(bool turbo) = 0;
        virtual void on_ifeel_change(bool ifeel) = 0;
        virtual void on_quiet_change(const std::string &quiet) = 0;

        climate::ClimateAction determine_action();

        void log_packet(const uint8_t *data, size_t len, bool outgoing = false);

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
