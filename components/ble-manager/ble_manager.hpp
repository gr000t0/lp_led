#pragma once
#include <cstdint>
#include "esp_err.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

// Deine externe Anwendungsschicht. Definitionen vorerst unten in ble_manager.cpp.
// Diese Funktionen laufen im BLE-Task: nicht warten, sondern Befehle kopieren/einreihen.
namespace service_layer {
    esp_err_t set_led_color(uint8_t red, uint8_t green, uint8_t blue);
    esp_err_t connect_wifi(const char* ssid, const char* password);
}

// Ein Manager, ein BLE-Client. Objekt muss während der gesamten Laufzeit existieren.
// Öffentliche Steuerung nur aus EINEM Anwendungstask, nie aus ServiceLayer-Hooks.
class BleManager {
public:
    BleManager() = default;
    BleManager(const BleManager&) = delete;
    BleManager& operator=(const BleManager&) = delete;

    esp_err_t start(const char* name = "LPSD-ESP32-C6");
    esp_err_t start_advertising();
    esp_err_t stop_advertising(); // asynchron; stop() wartet bei Bedarf
    esp_err_t stop();

private:
    enum Attribute {
        SERVICE,
        LED_DECL, LED_VALUE,
        SSID_DECL, SSID_VALUE,
        PASSWORD_DECL, PASSWORD_VALUE,
        APPLY_DECL, APPLY_VALUE,
    };
    static constexpr EventBits_t RUNNING = BIT0;
    static constexpr EventBits_t STOPPING = BIT1;
    static constexpr EventBits_t CONNECTED = BIT2;
    static constexpr EventBits_t DISCONNECTED = BIT3;
    static constexpr EventBits_t ADV_READY = BIT4;
    static constexpr EventBits_t SCAN_READY = BIT5;
    static constexpr EventBits_t SERVICE_READY = BIT6;
    static constexpr EventBits_t ADVERTISING = BIT7;
    static constexpr EventBits_t ADV_STOPPED = BIT8;
    static constexpr EventBits_t READY = ADV_READY | SCAN_READY | SERVICE_READY;
    static constexpr EventBits_t ALL_BITS = (BIT8 << 1) - 1;

    // Direkt die beiden registrierten Callbacks; keine weiteren Callback-Klassen.
    static void gap_callback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* p);
    static void gatts_callback(esp_gatts_cb_event_t event, esp_gatt_if_t i,
                               esp_ble_gatts_cb_param_t* p);
    void try_advertise();
    esp_gatt_status_t handle_write(uint16_t handle, const uint8_t* data, uint16_t len);

    static BleManager* instance_;
    EventGroupHandle_t events_ = nullptr;
    esp_bd_addr_t peer_{};
    esp_gatts_attr_db_t table_[9]{};
    uint16_t handles_[9]{};
    esp_ble_adv_params_t advertising_{};
    // Dauerhaft gespeichert, bis die asynchronen Konfigurations-Callbacks eintreffen.
    esp_ble_adv_data_t adv_data_{};
    esp_ble_adv_data_t scan_data_{};
    char ssid_[33]{};
    char password_[65]{};
    bool ssid_received_ = false;
    bool password_received_ = false; // leeres Passwort ist ein gültiger empfangener Wert
};