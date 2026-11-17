#pragma once
#include <atomic>
#include <mutex>
#include "credentials.hpp"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "components/credentials_manager/credentials_manager.hpp"

class WifiManager {
public:
    enum state_t {
        Stopped,
        Connecting,
        Connected,
        Failed
    };

    WifiManager() = default;
    ~WifiManager();
    WifiManager(const WifiManager&) = delete;
    WifiManager& operator=(const WifiManager&) = delete;

    esp_err_t init();
    esp_err_t connect(const WifiCredentials& credentials);
    esp_err_t connect();
    esp_err_t stop();

    void set_retry_limit(unsigned retries) { retry_limit_ = retries; }
    state_t get_state() const { return state_; }
    bool is_connected() const { return state_ == state_t::Connected; }

private:
    esp_err_t connect_(const WifiCredentials& credentials, bool save_credentials);
    void cleanup();

    CredentialsManager credentials_manager_;

    static void event_handler(void*, esp_event_base_t, int32_t, void*);

    esp_netif_t* netif_ = nullptr;
    esp_event_handler_instance_t wifi_handler_ = nullptr;
    esp_event_handler_instance_t ip_handler_ = nullptr;

    wifi_config_t cfg_{};

    std::mutex mutex_;
    bool initialized_ = false;
    bool wifi_initialized_ = false;
    bool started_ = false;
    bool owns_event_loop_ = false;

    std::atomic<state_t> state_{state_t::Stopped};
    std::atomic<bool> reconnect_{false};
    std::atomic<unsigned> retries_{0};
    std::atomic<unsigned> retry_limit_{5};
};