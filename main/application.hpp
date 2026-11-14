#pragma once
#include "led.hpp"
#include "wifi_manager.hpp"
#include "ble_manager.hpp"
#include "device_service.hpp"
#include "power_manager.hpp"
#include "time_manager.hpp"
#include "settings_manager.hpp"
class Application {
public:
    esp_err_t init();
    void run();
    // Workflow policy lives here, not inside generic managers.
    esp_err_t connect_wifi(const WifiCredentials&, bool save_after_success = false);
    esp_err_t sleep_light_for(uint64_t us);
    esp_err_t sleep_deep_for(uint64_t us);
    esp_err_t sleep_light_until_button(gpio_num_t gpio, int level = 0);
    esp_err_t sleep_deep_until_button(gpio_num_t gpio, bool high = false);
private:
    esp_err_t prepare_for_sleep();
    esp_err_t resume_after_sleep();
    void handle(const DeviceCommand&);
    void update_network();
    void publish_status();
    BleConfig ble_config() const;
    static WifiCredentials default_credentials();
    led led_;
    WifiManager wifi_;
    BleManager ble_;
    DeviceService service_;
    PowerManager power_;
    TimeManager time_;
    SettingsManager settings_;
    QueueHandle_t commands_ = nullptr;
    WifiCredentials current_{};
    bool persist_pending_ = false, fallback_used_ = false;
    WifiManager::State previous_state_ = WifiManager::State::Stopped;
    int64_t connection_started_ = 0;
    bool attempt_pending_ = false, logged_sync_ = false;
    const char* result_ = "boot";
};
