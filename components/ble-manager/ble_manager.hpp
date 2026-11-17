#pragma once
#include <atomic>
#include <cstdint>
#include "esp_err.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

namespace service_layer {
    esp_err_t set_led_color(uint8_t red, uint8_t green, uint8_t blue);
    esp_err_t connect_wifi(const char* ssid, const char* password);
}

class BleManager {
public:
    BleManager() = default;
    BleManager(const BleManager&) = delete;
    BleManager& operator=(const BleManager&) = delete;

    esp_err_t cold_start();
    esp_err_t start_advertising();
    esp_err_t stop_advertising();
    esp_err_t pause();
    esp_err_t resume();
    esp_err_t stop();

private:
    enum Attribute {
        SERVICE,
        LED_DECL, LED_VALUE,
        SSID_DECL, SSID_VALUE,
        PASSWORD_DECL, PASSWORD_VALUE,
        APPLY_DECL, APPLY_VALUE,
    };

    static void gap_callback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* p);
    static void gatts_callback(esp_gatts_cb_event_t event, esp_gatt_if_t i,
                              esp_ble_gatts_cb_param_t* p);
    esp_gatt_status_t handle_write(uint16_t handle, const uint8_t* data, uint16_t len);


    static constexpr EventBits_t READY = BIT0;
    static constexpr EventBits_t ADV_STOPPED = BIT1;
    static constexpr EventBits_t DISCONNECTED = BIT2;
    static constexpr EventBits_t UNREGISTERED = BIT3;
    EventGroupHandle_t events_ = nullptr;
    std::atomic<bool> advertise_after_disconnect_{true};
    std::atomic<bool> connected_{false};
    esp_gatt_if_t gatts_if_ = ESP_GATT_IF_NONE;
    // Schützt nur das Kopieren der Adresse, niemals Bluetooth-Aufrufe oder Warten.
    portMUX_TYPE peer_mux_ = portMUX_INITIALIZER_UNLOCKED;

    static BleManager* instance_;
    esp_bd_addr_t peer_{};
    const char* name_ = "LPSD_WP";
    esp_gatts_attr_db_t table_[9]{};
    uint16_t handles_[9]{};
    esp_ble_adv_params_t advertising_{};
    esp_ble_adv_data_t adv_data_{};
    esp_ble_adv_data_t scan_data_{};
    char ssid_[33]{};
    char password_[65]{};
};