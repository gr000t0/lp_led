#pragma once
#include "ble_manager.hpp"
#include "protocol.hpp"
#include "freertos/queue.h"
#include "freertos/semphr.h"
struct DeviceCommand {
    enum class Type { SetColor, ConnectWifi, ForgetWifi } type{};
    uint32_t color = 0;
    WifiCredentials credentials{};
};
// GATT protocol only. Does not call Wi-Fi, NVS, LED, or sleep APIs from BLE callbacks.
class DeviceService : public GattService {
public:
    esp_err_t init(QueueHandle_t commands, bool require_encryption = true);
    const uint8_t* uuid128() const override;
    void on_gatts_event(esp_gatts_cb_event_t, esp_gatt_if_t, esp_ble_gatts_cb_param_t*) override;
    void set_status(const char* text);
    void set_displayed_color(uint32_t color);
private:
    enum Index { Service, SsidDeclaration, SsidValue, PasswordDeclaration, PasswordValue,
                 ApplyDeclaration, ApplyValue, RgbDeclaration, RgbValue,
                 StatusDeclaration, StatusValue, Count };
    esp_gatt_status_t write(uint16_t handle, const uint8_t* data, size_t length);
    void read(esp_gatt_if_t, esp_ble_gatts_cb_param_t*);
    void clear_pending();
    QueueHandle_t queue_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    esp_gatts_attr_db_t table_[Count]{};
    uint16_t handles_[Count]{};
    WifiCredentials pending_{};
    bool have_ssid_ = false, have_password_ = false;
    protocol::PreparedWrite prepared_{};
    uint16_t mtu_ = 23;
    char read_snapshot_[192]{};
    char status_[192] = "starting";
    uint32_t color_ = 0;
};
