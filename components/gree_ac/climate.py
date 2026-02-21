#based on: https://github.com/DomiStyle/esphome-panasonic-ac
from esphome.const import (
    CONF_ID,
    CONF_NAME,
)
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, climate, sensor, select, switch

AUTO_LOAD = ["switch", "sensor", "select"]
DEPENDENCIES = ["uart"]

gree_ac_ns = cg.esphome_ns.namespace("gree_ac")
GreeAC = gree_ac_ns.class_(
    "GreeAC", cg.Component, uart.UARTDevice, climate.Climate
)
gree_ac_cnt_ns = gree_ac_ns.namespace("CNT")
GreeACCNT = gree_ac_cnt_ns.class_("GreeACCNT", GreeAC, cg.Component)

GreeACSwitch = gree_ac_ns.class_(
    "GreeACSwitch", switch.Switch, cg.Component
)
GreeACSelect = gree_ac_ns.class_(
    "GreeACSelect", select.Select, cg.Component
)


CONF_HORIZONTAL_SWING_SELECT    = "horizontal_swing_select"
CONF_VERTICAL_SWING_SELECT      = "vertical_swing_select"
CONF_DISPLAY_SELECT             = "display_select"
CONF_DISPLAY_UNIT_SELECT        = "display_unit_select"

CONF_LIGHT_SWITCH               = "light_switch"
CONF_PLASMA_SWITCH              = "plasma_switch"
CONF_BEEPER_SWITCH              = "beeper_switch"
CONF_SLEEP_SWITCH               = "sleep_switch"
CONF_XFAN_SWITCH                = "xfan_switch"
CONF_SAVE_SWITCH                = "save_switch"

CONF_CURRENT_TEMPERATURE_SENSOR = "current_temperature_sensor"

HORIZONTAL_SWING_OPTIONS = [
    "0 - OFF",
    "1 - Swing - Full",
    "2 - Constant - Left",
    "3 - Constant - Mid-Left",
    "4 - Constant - Middle",
    "5 - Constant - Mid-Right",
    "6 - Constant - Right",
]


VERTICAL_SWING_OPTIONS = [
    "00 - OFF",
    "01 - Swing - Full",
    "02 - Swing - Down",
    "03 - Swing - Mid-Down",
    "04 - Swing - Middle",
    "05 - Swing - Mid-Up",
    "06 - Swing - Up",
    "07 - Constant - Down",
    "08 - Constant - Mid-Down",
    "09 - Constant - Middle",
    "10 - Constant - Mid-Up",
    "11 - Constant - Up",
]

DISPLAY_OPTIONS = [
    "2 - Set temperature",
    "3 - Actual temperature",
    "4 - Outside temperature",
]

DISPLAY_UNIT_OPTIONS = [
    "C",
    "F",
]

SCHEMA = climate.climate_schema(climate.Climate).extend(
    {
        cv.Optional(CONF_NAME, default="climate"): cv.string_strict,
        cv.GenerateID(CONF_HORIZONTAL_SWING_SELECT): cv.declare_id(GreeACSelect),
        cv.GenerateID(CONF_VERTICAL_SWING_SELECT): cv.declare_id(GreeACSelect),
        cv.GenerateID(CONF_DISPLAY_SELECT): cv.declare_id(GreeACSelect),
        cv.GenerateID(CONF_DISPLAY_UNIT_SELECT): cv.declare_id(GreeACSelect),
        cv.GenerateID(CONF_LIGHT_SWITCH): cv.declare_id(GreeACSwitch),
        cv.GenerateID(CONF_PLASMA_SWITCH): cv.declare_id(GreeACSwitch),
        cv.GenerateID(CONF_BEEPER_SWITCH): cv.declare_id(GreeACSwitch),
        cv.GenerateID(CONF_SLEEP_SWITCH): cv.declare_id(GreeACSwitch),
        cv.GenerateID(CONF_XFAN_SWITCH): cv.declare_id(GreeACSwitch),
        cv.GenerateID(CONF_SAVE_SWITCH): cv.declare_id(GreeACSwitch),
        cv.Optional(CONF_CURRENT_TEMPERATURE_SENSOR): cv.use_id(sensor.Sensor),
    }
).extend(uart.UART_DEVICE_SCHEMA)

CONFIG_SCHEMA = cv.All(
    SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(GreeACCNT),
        }
    ),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await climate.register_climate(var, config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    selects = [
        (
            CONF_HORIZONTAL_SWING_SELECT,
            "hswing",
            HORIZONTAL_SWING_OPTIONS,
            "set_horizontal_swing_select",
        ),
        (
            CONF_VERTICAL_SWING_SELECT,
            "vswing",
            VERTICAL_SWING_OPTIONS,
            "set_vertical_swing_select",
        ),
        (CONF_DISPLAY_SELECT, "display_mode", DISPLAY_OPTIONS, "set_display_select"),
        (
            CONF_DISPLAY_UNIT_SELECT,
            "display_unit",
            DISPLAY_UNIT_OPTIONS,
            "set_display_unit_select",
        ),
    ]
    for conf_key, name, options, setter in selects:
        sel_id = config[conf_key]
        sel_conf = select.select_schema(GreeACSelect)(
            {CONF_ID: sel_id, CONF_NAME: name}
        )
        sel_var = await select.new_select(sel_conf, options=options)
        await cg.register_component(sel_var, sel_conf)
        cg.add(getattr(var, setter)(sel_var))

    switches = [
        (CONF_LIGHT_SWITCH, "light", "set_light_switch"),
        (CONF_PLASMA_SWITCH, "health", "set_plasma_switch"),
        (CONF_BEEPER_SWITCH, "beeper", "set_beeper_switch"),
        (CONF_SLEEP_SWITCH, "sleep", "set_sleep_switch"),
        (CONF_XFAN_SWITCH, "xfan", "set_xfan_switch"),
        (CONF_SAVE_SWITCH, "powersave", "set_save_switch"),
    ]
    for conf_key, name, setter in switches:
        sw_id = config[conf_key]
        sw_conf = switch.switch_schema(GreeACSwitch)(
            {CONF_ID: sw_id, CONF_NAME: name}
        )
        sw_var = await switch.new_switch(sw_conf)
        await cg.register_component(sw_var, sw_conf)
        cg.add(getattr(var, setter)(sw_var))

    if CONF_CURRENT_TEMPERATURE_SENSOR in config:
        sens = await cg.get_variable(config[CONF_CURRENT_TEMPERATURE_SENSOR])
        cg.add(var.set_current_temperature_sensor(sens))
