from esphome import pins
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, esp32, sensor, text_sensor, uart
from esphome.const import (
    CONF_ID,
    CONF_NUMBER,
    CONF_TX_PIN,
    CONF_UART_ID,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_RUNNING,
    DEVICE_CLASS_TEMPERATURE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_FAN,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_CELSIUS,
    UNIT_HOUR,
    UNIT_KELVIN,
    UNIT_MINUTE,
    UNIT_PERCENT,
)

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["binary_sensor", "sensor", "text_sensor"]

climos_ns = cg.esphome_ns.namespace("climos")
Climos = climos_ns.class_("Climos", cg.Component, uart.UARTDevice)

# Messwert-IDs 0x81..0x84 auf dem Bus; Zuordnung (Außen/Zu/Ab/Fortluft) noch unbekannt
TEMPERATURE_KEYS = ["temperature_81", "temperature_82", "temperature_83", "temperature_84"]

TEMPERATURE_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_CELSIUS,
    accuracy_decimals=1,
    device_class=DEVICE_CLASS_TEMPERATURE,
    state_class=STATE_CLASS_MEASUREMENT,
)

# Diagnose: misst das 9. Bit und die Abstände auf dem Bus, Pin wie uart rx_pin
CONF_BIT_CAPTURE_PIN = "bit_capture_pin"
CONF_FAN_STAGE = "fan_stage"
CONF_FILTER_REMAINING = "filter_remaining"
UNIT_DAYS = "d"
CONF_OPERATING_HOURS = "operating_hours"
CONF_FAN_OPERATING_HOURS = "fan_operating_hours"
# Status-Byte (Objekt 0x1A): Bit 3 Fortluftventilator, Bit 5 Sommerlüftung läuft
CONF_STATUS_1A = "status_1a"
CONF_FILTER_INTERVAL = "filter_interval"
CONF_OPERATING_MODE = "operating_mode"
# Folgende Werte kommen nur im Komplett-Abzug, wenn sich das Bedienteil am Bus anmeldet
CONF_LAST_MESSAGE = "last_message"
CONF_BUS_VERSION = "bus_version"
CONF_SW_MASTER = "sw_master"
CONF_SW_SLAVE = "sw_slave"
CONF_SW_TFT = "sw_tft"
# Zustand der letzten Umschaltung (vorgemerkt / gesendet / bestätigt / keine Bestätigung / gesperrt)
CONF_SEND_STATUS = "send_status"
# Frostschutz-Art (0x7B) und Uhr der Anlage (0x01/0x02 aus dem Zeit-Frame)
CONF_FROST_MODE = "frost_mode"
CONF_PANEL_CLOCK = "panel_clock"

FILTER_DAYS_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_DAYS,
    accuracy_decimals=0,
    device_class=DEVICE_CLASS_DURATION,
    state_class=STATE_CLASS_MEASUREMENT,
)
DIAGNOSTIC_TEXT_SCHEMA = text_sensor.text_sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC)
CONF_SYSTEM_ON = "system_on"
CONF_SUMMER_VENTILATION = "summer_ventilation"
CONF_EXHAUST_FAN = "exhaust_fan"
# Einstellwerte der Sommerlüftung, nur im Komplett-Abzug
CONF_SUMMER_THRESHOLD = "summer_threshold"
CONF_SUMMER_MIN_OUTSIDE = "summer_min_outside"
# Rohwert Objekt 0x44, Bedeutung unbekannt (keine Drehzahl, reagiert nicht auf die Stufe)
CONF_FAN_LEVEL_RAW = "fan_level_raw"
CONF_SUMMER_HYSTERESIS = "summer_hysteresis"
CONF_ERROR_CODE = "error_code"
# Freigabe der Sommerlüftung (Objekt 0x1B Bit 7), nur im Komplett-Abzug
CONF_SUMMER_ENABLED = "summer_enabled"
# Lüftereinstellungen, nur im Komplett-Abzug
STAGE_PERCENT_KEYS = ["stage_1_percent", "stage_2_percent", "stage_3_percent"]
CONF_BOOST_DURATION = "boost_duration"
CONF_AWAY_INTERVAL = "away_interval"
# Wochenprogramm je Tag (Objekte 0x64..0x78), nur im Komplett-Abzug
SCHEDULE_KEYS = [
    "schedule_monday",
    "schedule_tuesday",
    "schedule_wednesday",
    "schedule_thursday",
    "schedule_friday",
    "schedule_saturday",
    "schedule_sunday",
]
DEBALANCE_KEYS = ["debalance_stage_1", "debalance_stage_2", "debalance_stage_3"]
# Reihenfolge der Schwellen vermutlich wie in der Anleitung: eco, sicher, Feuchte-WT
FROST_THRESHOLD_KEYS = ["frost_threshold_1", "frost_threshold_2", "frost_threshold_3"]
SETTING_MINUTES_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_MINUTE,
    accuracy_decimals=0,
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
)

SETTING_TEMPERATURE_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_CELSIUS,
    accuracy_decimals=1,
    device_class=DEVICE_CLASS_TEMPERATURE,
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
)
BINARY_SENSOR_KEYS = (CONF_SYSTEM_ON, CONF_SUMMER_VENTILATION, CONF_EXHAUST_FAN, CONF_SUMMER_ENABLED)

TEXT_SENSOR_KEYS = (
    CONF_FAN_STAGE,
    CONF_OPERATING_MODE,
    CONF_LAST_MESSAGE,
    CONF_BUS_VERSION,
    CONF_SW_MASTER,
    CONF_SW_SLAVE,
    CONF_SW_TFT,
    CONF_SEND_STATUS,
    CONF_FROST_MODE,
    CONF_PANEL_CLOCK,
)

OPERATING_HOURS_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_HOUR,
    accuracy_decimals=0,
    device_class=DEVICE_CLASS_DURATION,
    state_class=STATE_CLASS_TOTAL_INCREASING,
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Climos),
        cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
        cv.Optional(CONF_BIT_CAPTURE_PIN): pins.internal_gpio_input_pin_schema,
        # Sendepin über RMT (11 Bit je Zeichen mit 9. Bit). Nicht zusätzlich als uart tx_pin eintragen.
        cv.Optional(CONF_TX_PIN): pins.internal_gpio_output_pin_schema,
        **{cv.Optional(key): TEMPERATURE_SCHEMA for key in TEMPERATURE_KEYS},
        cv.Optional(CONF_FAN_STAGE): text_sensor.text_sensor_schema(icon=ICON_FAN),
        cv.Optional(CONF_FILTER_REMAINING): FILTER_DAYS_SCHEMA,
        cv.Optional(CONF_FILTER_INTERVAL): FILTER_DAYS_SCHEMA,
        cv.Optional(CONF_OPERATING_MODE): text_sensor.text_sensor_schema(icon=ICON_FAN),
        cv.Optional(CONF_SYSTEM_ON): binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_POWER),
        cv.Optional(CONF_SUMMER_VENTILATION): binary_sensor.binary_sensor_schema(),
        cv.Optional(CONF_EXHAUST_FAN): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_RUNNING, icon=ICON_FAN
        ),
        cv.Optional(CONF_SUMMER_THRESHOLD): SETTING_TEMPERATURE_SCHEMA,
        cv.Optional(CONF_SUMMER_MIN_OUTSIDE): SETTING_TEMPERATURE_SCHEMA,
        cv.Optional(CONF_SUMMER_ENABLED): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
        cv.Optional(CONF_SUMMER_HYSTERESIS): sensor.sensor_schema(
            unit_of_measurement=UNIT_KELVIN,
            accuracy_decimals=1,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        **{
            cv.Optional(key): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                icon=ICON_FAN,
                accuracy_decimals=0,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            )
            for key in STAGE_PERCENT_KEYS
        },
        **{
            cv.Optional(key): text_sensor.text_sensor_schema(
                icon="mdi:calendar-clock", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            )
            for key in SCHEDULE_KEYS
        },
        **{
            cv.Optional(key): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                accuracy_decimals=0,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            )
            for key in DEBALANCE_KEYS
        },
        **{cv.Optional(key): SETTING_TEMPERATURE_SCHEMA for key in FROST_THRESHOLD_KEYS},
        cv.Optional(CONF_BOOST_DURATION): SETTING_MINUTES_SCHEMA,
        cv.Optional(CONF_AWAY_INTERVAL): SETTING_MINUTES_SCHEMA,
        cv.Optional(CONF_ERROR_CODE): sensor.sensor_schema(
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_FAN_LEVEL_RAW): sensor.sensor_schema(
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        **{
            cv.Optional(key): DIAGNOSTIC_TEXT_SCHEMA
            for key in TEXT_SENSOR_KEYS
            if key not in (CONF_OPERATING_MODE, CONF_FAN_STAGE, CONF_SEND_STATUS, CONF_FROST_MODE, CONF_PANEL_CLOCK)
        },
        cv.Optional(CONF_SEND_STATUS): text_sensor.text_sensor_schema(icon="mdi:send-check"),
        cv.Optional(CONF_FROST_MODE): text_sensor.text_sensor_schema(icon="mdi:snowflake-thermometer"),
        cv.Optional(CONF_PANEL_CLOCK): text_sensor.text_sensor_schema(icon="mdi:clock-outline"),
        cv.Optional(CONF_OPERATING_HOURS): OPERATING_HOURS_SCHEMA,
        cv.Optional(CONF_FAN_OPERATING_HOURS): OPERATING_HOURS_SCHEMA,
        cv.Optional(CONF_STATUS_1A): sensor.sensor_schema(
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_BIT_CAPTURE_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_BIT_CAPTURE_PIN])
        cg.add(var.set_bit_capture_pin(pin))

    if CONF_TX_PIN in config:
        # RMT-Treiber ist im ESP-IDF-Build standardmäßig ausgeschlossen
        esp32.include_builtin_idf_component("esp_driver_rmt")
        cg.add(var.set_tx_pin(config[CONF_TX_PIN][CONF_NUMBER]))

    for index, key in enumerate(TEMPERATURE_KEYS):
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(var.set_temperature_sensor(index, sens))

    for key, setter in (
        (CONF_FILTER_REMAINING, var.set_filter_remaining_sensor),
        (CONF_OPERATING_HOURS, var.set_operating_hours_sensor),
        (CONF_FAN_OPERATING_HOURS, var.set_fan_operating_hours_sensor),
        (CONF_STATUS_1A, var.set_status_1a_sensor),
        (CONF_FILTER_INTERVAL, var.set_filter_interval_sensor),
        (CONF_SUMMER_THRESHOLD, var.set_summer_threshold_sensor),
        (CONF_SUMMER_MIN_OUTSIDE, var.set_summer_min_outside_sensor),
        (CONF_FAN_LEVEL_RAW, var.set_fan_level_raw_sensor),
        (CONF_SUMMER_HYSTERESIS, var.set_summer_hysteresis_sensor),
        (CONF_ERROR_CODE, var.set_error_code_sensor),
        (CONF_BOOST_DURATION, var.set_boost_duration_sensor),
        (CONF_AWAY_INTERVAL, var.set_away_interval_sensor),
    ):
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(setter(sens))

    for index, key in enumerate(DEBALANCE_KEYS):
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(var.set_debalance_sensor(index, sens))

    for index, key in enumerate(FROST_THRESHOLD_KEYS):
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(var.set_frost_threshold_sensor(index, sens))

    for index, key in enumerate(STAGE_PERCENT_KEYS):
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(var.set_stage_percent_sensor(index, sens))

    for day, key in enumerate(SCHEDULE_KEYS):
        if key in config:
            sens = await text_sensor.new_text_sensor(config[key])
            cg.add(var.set_schedule_text_sensor(day, sens))

    for key in BINARY_SENSOR_KEYS:
        if key in config:
            sens = await binary_sensor.new_binary_sensor(config[key])
            cg.add(getattr(var, f"set_{key}_binary_sensor")(sens))

    for key in TEXT_SENSOR_KEYS:
        if key in config:
            sens = await text_sensor.new_text_sensor(config[key])
            cg.add(getattr(var, f"set_{key}_text_sensor")(sens))
