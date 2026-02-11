#include "snl660_light.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "esphome/components/esp32_ble/ble_uuid.h"
#include "esphome/core/log.h"

#include <esp_gattc_api.h>

namespace esphome {
namespace neewer_snl660 {

static const char *const TAG = "neewer_snl660";

static esp_bt_uuid_t make_uuid128(const uint8_t raw[16]) {
  esp_bt_uuid_t uuid{};
  uuid.len = ESP_UUID_LEN_128;
  std::memcpy(uuid.uuid.uuid128, raw, 16);
  return uuid;
}

static const uint8_t SVC_BE[16] = {0x69,0x40,0x00,0x01,0xB5,0xA3,0xF3,0x93,0xE0,0xA9,0xE5,0x0E,0x24,0xDC,0xCA,0x99};
static const uint8_t SVC_LE[16] = {0x99,0xCA,0xDC,0x24,0x0E,0xE5,0xA9,0xE0,0x93,0xF3,0xA3,0xB5,0x01,0x00,0x40,0x69};

static const uint8_t CHR_BE[16] = {0x69,0x40,0x00,0x02,0xB5,0xA3,0xF3,0x93,0xE0,0xA9,0xE5,0x0E,0x24,0xDC,0xCA,0x99};
static const uint8_t CHR_LE[16] = {0x99,0xCA,0xDC,0x24,0x0E,0xE5,0xA9,0xE0,0x93,0xF3,0xA3,0xB5,0x02,0x00,0x40,0x69};

static const auto SVC_UUID_BE = esp32_ble::ESPBTUUID::from_uuid(make_uuid128(SVC_BE));
static const auto SVC_UUID_LE = esp32_ble::ESPBTUUID::from_uuid(make_uuid128(SVC_LE));
static const auto CHR_UUID_BE = esp32_ble::ESPBTUUID::from_uuid(make_uuid128(CHR_BE));
static const auto CHR_UUID_LE = esp32_ble::ESPBTUUID::from_uuid(make_uuid128(CHR_LE));

static inline int clamp_i(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void SNL660Light::set_ble_client(ble_client::BLEClient *client) {
  client_ = client;
  if (client_ != nullptr) {
    client_->register_ble_node(this);
    this->node_state = esp32_ble_tracker::ClientState::IDLE;
  }
}

light::LightTraits SNL660Light::get_traits() {
  auto traits = light::LightTraits();
  traits.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE});
  traits.set_min_mireds(179);  // ~5600K
  traits.set_max_mireds(313);  // ~3200K
  return traits;
}

void SNL660Light::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t,
                                      esp_ble_gattc_cb_param_t *) {
  if (client_ == nullptr) return;

  if (event == ESP_GATTC_DISCONNECT_EVT || event == ESP_GATTC_CLOSE_EVT) {
    handles_ready_ = false;
    write_handle_ = 0;
    last_on_ = false;
    last_bri_ = -1;
    last_temp_ = -1;
    this->node_state = esp32_ble_tracker::ClientState::IDLE;
    ESP_LOGW(TAG, "BLE disconnected (handles cleared)");
    return;
  }

  if (event == ESP_GATTC_SEARCH_CMPL_EVT) {
    auto *svc = client_->get_service(SVC_UUID_BE);
    bool use_be = true;
    if (svc == nullptr) {
      svc = client_->get_service(SVC_UUID_LE);
      use_be = false;
    }
    if (svc == nullptr) {
      ESP_LOGW(TAG, "Service not found during discovery");
      this->node_state = esp32_ble_tracker::ClientState::ESTABLISHED;
      return;
    }

    auto *chr = svc->get_characteristic(use_be ? CHR_UUID_BE : CHR_UUID_LE);
    if (chr == nullptr) chr = svc->get_characteristic(use_be ? CHR_UUID_LE : CHR_UUID_BE);

    if (chr == nullptr) {
      ESP_LOGW(TAG, "Write characteristic not found during discovery");
      this->node_state = esp32_ble_tracker::ClientState::ESTABLISHED;
      return;
    }

    write_handle_ = chr->handle;
    handles_ready_ = (write_handle_ != 0);
    ESP_LOGI(TAG, "✅ Cached write handle: 0x%04X", write_handle_);

    this->node_state = esp32_ble_tracker::ClientState::ESTABLISHED;
  }
}

bool SNL660Light::write_packet_(uint8_t prefix, uint8_t value) {
  if (client_ == nullptr || !client_->connected()) return false;
  if (!handles_ready_ || write_handle_ == 0) return false;

  uint8_t pkt[5];
  pkt[0] = 0x78;
  pkt[1] = prefix;
  pkt[2] = 0x01;
  pkt[3] = value;
  pkt[4] = (uint8_t) ((pkt[0] + pkt[1] + pkt[2] + pkt[3]) & 0xFF);

  esp_err_t err = esp_ble_gattc_write_char(
      client_->get_gattc_if(), client_->get_conn_id(), write_handle_,
      sizeof(pkt), pkt, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);

  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Write failed err=%d", (int) err);
    return false;
  }
  return true;
}

void SNL660Light::write_state(light::LightState *state) {
  if (client_ == nullptr || !client_->connected()) {
    ESP_LOGW(TAG, "BLE not connected; skip");
    return;
  }
  if (!handles_ready_) {
    ESP_LOGW(TAG, "Write handle not ready yet; skip");
    return;
  }

  const bool on = state->current_values.is_on();

  float bri_f = state->current_values.get_brightness();
  int bri = (int) lroundf(bri_f * 100.0f);
  bri = clamp_i(bri, 0, 100);

  float mireds = state->current_values.get_color_temperature();
  float kelvin = (mireds > 1.0f) ? (1000000.0f / mireds) : 4500.0f;
  kelvin = fmaxf(3200.0f, fminf(5600.0f, kelvin));

  int temp_val = (int) lroundf(kelvin / 100.0f);  // 32..56
  temp_val = clamp_i(temp_val, 32, 56);

  // Only send power when it changes (matches your Python usage)
  if (on != last_on_) {
    write_packet_(0x81, on ? 0x01 : 0x02);
    last_on_ = on;
  }

  if (!on) return;

  // Brightness packet only when it changes
  if (bri != last_bri_) {
    write_packet_(0x82, (uint8_t) bri);
    last_bri_ = bri;
  }

  // CCT packet only when it changes
  if (temp_val != last_temp_) {
    write_packet_(0x83, (uint8_t) temp_val);
    last_temp_ = temp_val;
  }
}

}  // namespace neewer_snl660
}  // namespace esphome
