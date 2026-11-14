#include "settings_manager.hpp"
#include "nvs.h"
#include "nvs_flash.h"
esp_err_t SettingsManager::init() {
    // Do not silently erase all NVS on an initialization error.
    return nvs_flash_init();
}
esp_err_t SettingsManager::load_wifi(WifiCredentials& out) {
    nvs_handle_t h;
    esp_err_t e = nvs_open("app_settings", NVS_READONLY, &h);
    if (e != ESP_OK) return e;
    WifiCredentials value{};
    size_t n = sizeof(value);
    e = nvs_get_blob(h, "wifi_v1", &value, &n);
    nvs_close(h);
    if (e != ESP_OK) return e;
    if (n != sizeof(value) || !valid_credentials(value)) return ESP_ERR_INVALID_ARG;
    out = value;
    return ESP_OK;
}
esp_err_t SettingsManager::save_wifi(const WifiCredentials& c) {
    if (!valid_credentials(c)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t e = nvs_open("app_settings", NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_blob(h, "wifi_v1", &c, sizeof(c));
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}
esp_err_t SettingsManager::forget_wifi() {
    nvs_handle_t h;
    esp_err_t e = nvs_open("app_settings", NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_erase_key(h, "wifi_v1");
    if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}
