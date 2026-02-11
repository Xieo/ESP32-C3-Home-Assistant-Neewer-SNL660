#pragma once

#include "esphome/core/component.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/ble_client/ble_client.h"

namespace esphome {
namespace neewer_snl660 {

class SNL660Light : public Component, public light::LightOutput, public ble_client::BLEClientNode {
 public:
  void set_ble_client(ble_client::BLEClient *client);

  light::LightTraits get_traits() override;
  void write_state(light::LightState *state) override;

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

 protected:
  ble_client::BLEClient *client_{nullptr};

  uint16_t write_handle_{0};
  bool handles_ready_{false};

  bool last_on_{false};
  int last_bri_{-1};   // 0..100
  int last_temp_{-1};  // 32..56

  bool write_packet_(uint8_t prefix, uint8_t value);
};

}  // namespace neewer_snl660
}  // namespace esphome
