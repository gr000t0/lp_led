#pragma once
#include <atomic>
#include "credentials.hpp"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
class WifiManager {
public:
    enum class State { Stopped, Connecting, Connected, Failed };
    WifiManager() = default;
    WifiManager(const WifiManager&) = delete;
    WifiManager& operator=(const WifiManager&) = delete;
    esp_err_t init();
    esp_err_t connect(const WifiCredentials& credentials);
    esp_err_t stop();
    esp_err_t set_power_save(wifi_ps_type_t mode);
    void set_retry_limit(unsigned retries) { retry_limit_ = retries; }
    State state() const { return state_.load(); }
    uint8_t disconnect_reason() const { return reason_.load(); }
    bool connected() const { return state() == State::Connected; }
    // All public mutations from one application task. Event handler only updates state/retries.
private:
    static void event_handler(void*, esp_event_base_t, int32_t, void*);
    esp_netif_t* netif_ = nullptr;
    esp_event_handler_instance_t wifi_handler_ = nullptr, ip_handler_ = nullptr;
    EventGroupHandle_t events_ = nullptr;
    std::atomic<State> state_{State::Stopped};
    std::atomic<bool> reconnect_{false};
    std::atomic<unsigned> retries_{0}, retry_limit_{5};
    std::atomic<uint8_t> reason_{0};
    bool initialized_ = false, started_ = false;
};
