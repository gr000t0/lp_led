#include "wifi_manager.hpp"
#include <cstring>
#include "esp_log.h"

static constexpr const char* TAG = "WifiManager";

namespace {

// safe credential mem copy

template <typename T, size_t N>
esp_err_t copy_credential(uint8_t* destination, size_t capacity, const T (&source)[N]) {
    size_t length = 0;
    while (length < N && source[length] != 0) {
        ++length;
    }
    if (length > capacity) return ESP_ERR_INVALID_ARG;
    std::memcpy(destination, source, length);
    return ESP_OK;
}


template <typename T>
esp_err_t copy_credential_pointer(uint8_t* destination, size_t capacity, const T* source) {
    if (!source) return ESP_ERR_INVALID_ARG;
    size_t length = 0;
    while (length <= capacity && source[length] != 0) {
        ++length;
    }
    if (length > capacity) return ESP_ERR_INVALID_ARG;
    std::memcpy(destination, source, length);
    return ESP_OK;
}

esp_err_t copy_credential(uint8_t* destination, size_t capacity, const char* source) {
    return copy_credential_pointer(destination, capacity, source);
}
esp_err_t copy_credential(uint8_t* destination, size_t capacity, const uint8_t* source) {
    return copy_credential_pointer(destination, capacity, source);
}
} // namespace

WifiManager::~WifiManager() {
    (void)stop();
    cleanup();
}

void WifiManager::cleanup() {
    reconnect_ = false;

    if (started_) {
        (void)esp_wifi_stop();
        started_ = false;
    }
    if (wifi_handler_) {
        (void)esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler_);
        wifi_handler_ = nullptr;
    }
    if (ip_handler_) {
        (void)esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler_);
        ip_handler_ = nullptr;
    }
    if (netif_) {
        esp_netif_destroy_default_wifi(netif_);
        netif_ = nullptr;
    }
    if (wifi_initialized_) {
        (void)esp_wifi_deinit();
        wifi_initialized_ = false;
    }
    if (owns_event_loop_) {
        (void)esp_event_loop_delete_default();
        owns_event_loop_ = false;
    }
    initialized_ = false;
    state_ = state_t::Stopped;
}

esp_err_t WifiManager::init() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) return ESP_OK;

    esp_err_t err = credentials_manager_.init();
    if (err != ESP_OK) return err;

    err = esp_netif_init();
    if (err != ESP_OK) return err;

    err = esp_event_loop_create_default();
    if (err == ESP_OK) {
        owns_event_loop_ = true;
    } else if (err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    netif_ = esp_netif_create_default_wifi_sta();
    if (!netif_) {
        cleanup();
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        cleanup();
        return err;
    }
    wifi_initialized_ = true;


    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        cleanup();
        return err;
    }

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              &WifiManager::event_handler, this,
                                              &wifi_handler_);
    if (err != ESP_OK) {
        cleanup();
        return err;
    }

    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              &WifiManager::event_handler, this,
                                              &ip_handler_);
    if (err != ESP_OK) {
        cleanup();
        return err;
    }

    initialized_ = true;
    return ESP_OK;
}


esp_err_t WifiManager::connect_(const WifiCredentials& credentials, bool save_credentials) {
    if (!initialized_) return ESP_ERR_INVALID_STATE;
    if (state_ == state_t::Connecting || state_ == state_t::Connected) return ESP_ERR_INVALID_STATE;

    cfg_ = {};
    esp_err_t err = copy_credential(cfg_.sta.ssid, sizeof(cfg_.sta.ssid), credentials.ssid);
    if (err != ESP_OK) return err;
    err = copy_credential(cfg_.sta.password, sizeof(cfg_.sta.password), credentials.password);
    if (err != ESP_OK) return err;

    if (save_credentials) {
        err = credentials_manager_.save_wifi(credentials);
        if (err != ESP_OK) return err;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        reconnect_ = false;
        state_ = state_t::Failed;
        return err;
    };
    err = esp_wifi_set_config(WIFI_IF_STA, &cfg_);
    if (err != ESP_OK) {
        reconnect_ = false;
        state_ = state_t::Failed;
        return err;
    }

    reconnect_ = true;
    retries_ = 0;
    state_ = state_t::Connecting;

    if (!started_) {
        err = esp_wifi_start();
        if (err != ESP_OK){
            reconnect_ = false;
            state_ = state_t::Failed;
            return err;
        };
        started_ = true;
    }

    err = esp_wifi_connect();
    if (err != ESP_OK){
        reconnect_ = false;
        state_ = state_t::Failed;
        return err;
    };
    return ESP_OK;


    
}

esp_err_t WifiManager::connect(const WifiCredentials& credentials) {
    std::lock_guard<std::mutex> lock(mutex_);
    return connect_(credentials, true);
}

esp_err_t WifiManager::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) return ESP_ERR_INVALID_STATE;
    if (state_ == state_t::Connecting || state_ == state_t::Connected) return ESP_ERR_INVALID_STATE;

    WifiCredentials credentials{};
    esp_err_t err = credentials_manager_.load_wifi(credentials);
    if (err != ESP_OK) return err;

    return connect_(credentials, false);
}

esp_err_t WifiManager::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    reconnect_ = false;
    state_ = state_t::Stopped;

    if (!started_) return ESP_OK;


    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) {
        state_ = state_t::Failed;
        return err;
    }
    started_ = false;
    return ESP_OK;
}


void WifiManager::event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    auto& self = *static_cast<WifiManager*>(arg);
    std::lock_guard<std::mutex> lock(self.mutex_);
    (void)event_data;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WiFi started");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            if (!self.reconnect_) break; 

            self.state_ = state_t::Connecting;
            ESP_LOGI(TAG, "WiFi disconnected");
            if (self.retries_ < self.retry_limit_) {
                const unsigned retry = ++self.retries_;
                ESP_LOGI(TAG, "Retrying to connect to WiFi (%u/%u)",
                         retry, self.retry_limit_.load());
                esp_err_t err = esp_wifi_connect();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to reconnect to WiFi: %s", esp_err_to_name(err));
                    self.reconnect_ = false;
                    self.state_ = state_t::Failed;
                }
            } else {
                self.reconnect_ = false;
                self.state_ = state_t::Failed;
            }
            break;
        default:
            break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
        case IP_EVENT_STA_GOT_IP:
            if (!self.reconnect_ || !self.started_) break;
            ESP_LOGI(TAG, "Got IP");
            self.retries_ = 0;
            self.state_ = state_t::Connected;
            break;
        default:
            break;
        }
    }
}