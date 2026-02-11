import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.components import ble_client, light
from esphome.const import CONF_OUTPUT_ID

CONF_BLE_CLIENT_ID = "ble_client_id"

DEPENDENCIES = ["ble_client"]

snl660_ns = cg.esphome_ns.namespace("neewer_snl660")
SNL660Light = snl660_ns.class_("SNL660Light", cg.Component, light.LightOutput)

CONFIG_SCHEMA = light.LIGHT_SCHEMA.extend(
    {
        cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(SNL660Light),
        cv.Required(CONF_BLE_CLIENT_ID): cv.use_id(ble_client.BLEClient),
    }
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])

    await cg.register_component(var, config)
    await light.register_light(var, config)

    client = await cg.get_variable(config[CONF_BLE_CLIENT_ID])
    cg.add(var.set_ble_client(client))
