#include "settings_manager.hpp"
#include "nvs.h"
#include "nvs_flash.h"
esp_err_t CredentialsManager::init() {
   
    return nvs_flash_init();
}
esp_err_t CredentialsManager::load_wifi(WifiCredentials& out) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("wifi_creds", NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    WifiCredentials value{};
    size_t n = sizeof(value);
    err = nvs_get_blob(handle, "wifi_v1", &value, &n);
    nvs_close(handle);
    if (err != ESP_OK) return err;
    out = value;
    return ESP_OK;
}
esp_err_t CredentialsManager::save_wifi(const WifiCredentials& credentials) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("wifi_creds", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, "wifi_v1", &credentials, sizeof(credentials));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t CredentialsManager::forget_wifi() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("wifi_creds", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_erase_key(handle, "wifi_v1");
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}


