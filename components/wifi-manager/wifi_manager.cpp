#include "wifi_manager.hpp"
#include <cstring>
namespace { constexpr EventBits_t STOPPED = BIT0; }
esp_err_t WifiManager::init() {
    if (initialized_) return ESP_ERR_INVALID_STATE;
    events_ = xEventGroupCreate();
    if (!events_) return ESP_ERR_NO_MEM;
    netif_ = esp_netif_create_default_wifi_sta();
    if (!netif_) return ESP_ERR_NO_MEM;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t e = esp_wifi_init(&cfg);
    if (e != ESP_OK) return e;
    e = esp_wifi_set_storage(WIFI_STORAGE_RAM); // persist explicitly only after successful IP
    if (e != ESP_OK) return e;
    e = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, this, &wifi_handler_);
    if (e != ESP_OK) return e;
    e = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, this, &ip_handler_);
    if (e != ESP_OK) return e;
    e = esp_wifi_set_mode(WIFI_MODE_STA);
    initialized_ = (e == ESP_OK);
    return e;
}
esp_err_t WifiManager::connect(const WifiCredentials& c) {
    if (!initialized_) return ESP_ERR_INVALID_STATE;
    if (!valid_credentials(c)) return ESP_ERR_INVALID_ARG;
    esp_err_t e = stop();
    if (e != ESP_OK) return e;
    wifi_config_t cfg{};
    memcpy(cfg.sta.ssid, c.ssid, strlen(c.ssid));
    memcpy(cfg.sta.password, c.password, strlen(c.password));
    cfg.sta.threshold.authmode = strlen(c.password) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;
    e = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (e != ESP_OK) return e;
    retries_ = 0;
    reason_ = 0;
    reconnect_ = true;
    state_ = State::Connecting;
    e = esp_wifi_start();
    started_ = (e == ESP_OK);
    if (e != ESP_OK) { reconnect_ = false; state_ = State::Failed; }
    return e;
}
esp_err_t WifiManager::stop() {
    if (!started_) return ESP_OK;
    reconnect_ = false;
    xEventGroupClearBits(events_, STOPPED);
    esp_err_t e = esp_wifi_stop();
    if (e != ESP_OK) return e;
    if (!(xEventGroupWaitBits(events_, STOPPED, pdTRUE, pdFALSE, pdMS_TO_TICKS(3000)) & STOPPED))
        return ESP_ERR_TIMEOUT;
    started_ = false;
    state_ = State::Stopped;
    return ESP_OK;
}
esp_err_t WifiManager::set_power_save(wifi_ps_type_t mode) { return esp_wifi_set_ps(mode); }
void WifiManager::event_handler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    auto& self = *static_cast<WifiManager*>(arg);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_STOP) {
        self.state_ = State::Stopped;
        xEventGroupSetBits(self.events_, STOPPED);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (self.reconnect_ && esp_wifi_connect() != ESP_OK) self.state_ = State::Failed;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        self.reason_ = static_cast<wifi_event_sta_disconnected_t*>(data)->reason;
        if (!self.reconnect_) return;
        self.state_ = State::Connecting;
        if (self.retries_++ < self.retry_limit_) {
            if (esp_wifi_connect() != ESP_OK) self.state_ = State::Failed;
        } else self.state_ = State::Failed;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP && self.reconnect_) {
        self.retries_ = 0;
        self.state_ = State::Connected;
    }
}
