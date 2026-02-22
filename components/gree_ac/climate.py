#based on: https://github.com/DomiStyle/esphome-panasonic-ac
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    CONF_ICON,
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
CONF_HEALTH_SWITCH              = "health_switch"
CONF_BEEPER_SWITCH              = "beeper_switch"
CONF_SLEEP_SWITCH               = "sleep_switch"
CONF_XFAN_SWITCH                = "xfan_switch"
CONF_POWERSAVE_SWITCH           = "powersave_switch"
CONF_TURBO_SWITCH               = "turbo_switch"

CONF_QUIET_SELECT               = "quiet_select"

CONF_CURRENT_TEMPERATURE_SENSOR = "current_temperature_sensor"

QUIET_OPTIONS = [
    "Off",
    "On",
    "Auto",
]

HORIZONTAL_SWING_OPTIONS = [
    "Off",
    "Swing - Full",
    "Constant - Left",
    "Constant - Mid-Left",
    "Constant - Middle",
    "Constant - Mid-Right",
    "Constant - Right",
]


VERTICAL_SWING_OPTIONS = [
    "Off",
    "Swing - Full",
    "Swing - Down",
    "Swing - Mid-Down",
    "Swing - Middle",
    "Swing - Mid-Up",
    "Swing - Up",
    "Constant - Down",
    "Constant - Mid-Down",
    "Constant - Middle",
    "Constant - Mid-Up",
    "Constant - Up",
]

DISPLAY_OPTIONS = [
    "Set temperature",
    "Actual temperature",
    "Outside temperature",
]

DISPLAY_UNIT_OPTIONS = [
    "C",
    "F",
]

SCHEMA = climate.climate_schema(climate.Climate).extend(
    {
        cv.Optional(CONF_NAME, default="Thermostat"): cv.string_strict,
        cv.GenerateID(CONF_HORIZONTAL_SWING_SELECT): cv.declare_id(GreeACSelect),
        cv.GenerateID(CONF_VERTICAL_SWING_SELECT): cv.declare_id(GreeACSelect),
        cv.GenerateID(CONF_DISPLAY_SELECT): cv.declare_id(GreeACSelect),
        cv.GenerateID(CONF_DISPLAY_UNIT_SELECT): cv.declare_id(GreeACSelect),
        cv.GenerateID(CONF_LIGHT_SWITCH): cv.declare_id(GreeACSwitch),
        cv.GenerateID(CONF_HEALTH_SWITCH): cv.declare_id(GreeACSwitch),
        cv.GenerateID(CONF_BEEPER_SWITCH): cv.declare_id(GreeACSwitch),
        cv.GenerateID(CONF_SLEEP_SWITCH): cv.declare_id(GreeACSwitch),
        cv.GenerateID(CONF_XFAN_SWITCH): cv.declare_id(GreeACSwitch),
        cv.GenerateID(CONF_POWERSAVE_SWITCH): cv.declare_id(GreeACSwitch),
        cv.GenerateID(CONF_TURBO_SWITCH): cv.declare_id(GreeACSwitch),
        cv.GenerateID(CONF_QUIET_SELECT): cv.declare_id(GreeACSelect),
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
            "Horizontal swing",
            HORIZONTAL_SWING_OPTIONS,
            "set_horizontal_swing_select",
            "mdi:arrow-expand-horizontal",
        ),
        (
            CONF_VERTICAL_SWING_SELECT,
            "Vertical swing",
            VERTICAL_SWING_OPTIONS,
            "set_vertical_swing_select",
            "mdi:arrow-expand-vertical",
        ),
        (
            CONF_DISPLAY_SELECT,
            "Display mode",
            DISPLAY_OPTIONS,
            "set_display_select",
            "mdi:wrench-cog",
        ),
        (
            CONF_DISPLAY_UNIT_SELECT,
            "Display unit",
            DISPLAY_UNIT_OPTIONS,
            "set_display_unit_select",
            "mdi:wrench-cog",
        ),
        (
            CONF_QUIET_SELECT,
            "Quiet",
            QUIET_OPTIONS,
            "set_quiet_select",
            "mdi:headphones",
        ),
    ]
    for conf_key, name, options, setter, icon in selects:
        sel_id = config[conf_key]
        sel_conf = select.select_schema(GreeACSelect)(
            {CONF_ID: sel_id, CONF_NAME: name, CONF_ICON: icon}
        )
        sel_var = await select.new_select(sel_conf, options=options)
        await cg.register_component(sel_var, sel_conf)
        cg.add(getattr(var, setter)(sel_var))

    switches = [
        (CONF_LIGHT_SWITCH, "Light", "set_light_switch", "mdi:lightbulb-on-outline"),
        (CONF_HEALTH_SWITCH, "Health", "set_health_switch", "mdi:pine-tree"),
        (CONF_BEEPER_SWITCH, "Beeper", "set_beeper_switch", "mdi:bell-ring"),
        (CONF_SLEEP_SWITCH, "Sleep", "set_sleep_switch", "mdi:power-sleep"),
        (CONF_XFAN_SWITCH, "X-Fan", "set_xfan_switch", "mdi:fan"),
        (CONF_POWERSAVE_SWITCH, "Powersave", "set_powersave_switch", "mdi:leaf"),
        (CONF_TURBO_SWITCH, "Turbo", "set_turbo_switch", "mdi:car-turbocharger"),
    ]
    for conf_key, name, setter, icon in switches:
        sw_id = config[conf_key]
        sw_conf = switch.switch_schema(GreeACSwitch)(
            {CONF_ID: sw_id, CONF_NAME: name, CONF_ICON: icon}
        )
        sw_var = await switch.new_switch(sw_conf)
        await cg.register_component(sw_var, sw_conf)
        cg.add(getattr(var, setter)(sw_var))

    if CONF_CURRENT_TEMPERATURE_SENSOR in config:
        sens = await cg.get_variable(config[CONF_CURRENT_TEMPERATURE_SENSOR])
        cg.add(var.set_current_temperature_sensor(sens))
