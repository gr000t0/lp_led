#pragma once
#include <atomic>
#include <string>
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

class GattService {
public:
    virtual ~GattService() = default;
    virtual const uint8_t* uuid128() const = 0;
    virtual void on_gatts_event(esp_gatts_cb_event_t, esp_gatt_if_t, esp_ble_gatts_cb_param_t*) = 0;
};
struct BleConfig {
    const char* name = "LPSD-ESP32-C6";
    uint16_t advertising_min = 0x100;
    uint16_t advertising_max = 0x200;
    bool restart_advertising_on_disconnect = true;
};

class BleManager {
public:
    BleManager() = default;
    BleManager(const BleManager&) = delete;
    BleManager& operator=(const BleManager&) = delete;
    bool running() const { return running_; } // application task only
    esp_err_t start(GattService& service, const BleConfig& config = {});
    esp_err_t stop(); // synchronous, call from app task, NEVER from a GAP/GATTS callback
    esp_err_t start_advertising();
    esp_err_t stop_advertising();
    bool connected() const { return connected_.load(); }
    bool ready() const { return ready_.load(); }

    void on_gap_event(esp_gap_ble_cb_event_t, esp_ble_gap_cb_param_t*);
    void on_gatts_event(esp_gatts_cb_event_t, esp_gatt_if_t, esp_ble_gatts_cb_param_t*);
private:
    static void gap_callback(esp_gap_ble_cb_event_t, esp_ble_gap_cb_param_t*);
    static void gatts_callback(esp_gatts_cb_event_t, esp_gatt_if_t, esp_ble_gatts_cb_param_t*);
    void try_advertise();
    static BleManager* instance_;
    GattService* service_ = nullptr;
    BleConfig config_{};
    std::string name_;
    esp_ble_adv_params_t advertising_{};
    esp_bd_addr_t peer_{};
    EventGroupHandle_t events_ = nullptr;
    esp_pm_lock_handle_t awake_lock_ = nullptr;
    std::atomic<bool> connected_{false}, ready_{false}, stopping_{false};
    bool adv_data_ready_ = false
    bool scan_data_ready_ = false
    bool service_ready_ = false;
    bool running_ = false;
};
