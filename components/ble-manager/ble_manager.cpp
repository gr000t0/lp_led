#include "ble_manager.hpp"
#include <cstring>
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"

namespace {
constexpr char TAG[] = "BLE";
// Menschenlesbar: 7c2a0000-6e3b-4a2e-9f71-8b6d20c14590
// Service ...0000, LED ...0004, SSID ...0001, Passwort ...0002, Apply ...0003.
// Bluedroid verwendet die umgekehrte Byte-Reihenfolge.
uint8_t uuids[5][16] = {
    {0x90,0x45,0xc1,0x20,0x6d,0x8b,0x71,0x9f,0x2e,0x4a,0x3b,0x6e,0x00,0x00,0x2a,0x7c},
    {0x90,0x45,0xc1,0x20,0x6d,0x8b,0x71,0x9f,0x2e,0x4a,0x3b,0x6e,0x04,0x00,0x2a,0x7c},
    {0x90,0x45,0xc1,0x20,0x6d,0x8b,0x71,0x9f,0x2e,0x4a,0x3b,0x6e,0x01,0x00,0x2a,0x7c},
    {0x90,0x45,0xc1,0x20,0x6d,0x8b,0x71,0x9f,0x2e,0x4a,0x3b,0x6e,0x02,0x00,0x2a,0x7c},
    {0x90,0x45,0xc1,0x20,0x6d,0x8b,0x71,0x9f,0x2e,0x4a,0x3b,0x6e,0x03,0x00,0x2a,0x7c}
};
uint16_t primary_uuid = ESP_GATT_UUID_PRI_SERVICE;
uint16_t declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
uint8_t write_property = ESP_GATT_CHAR_PROP_BIT_WRITE;
}
BleManager* BleManager::instance_ = nullptr;

esp_err_t BleManager::cold_start() {
    if (instance_) return ESP_ERR_INVALID_STATE;
    if (!events_) events_ = xEventGroupCreate();
    if (!events_) return ESP_ERR_NO_MEM;
    xEventGroupClearBits(events_, READY | ADV_STOPPED | DISCONNECTED | UNREGISTERED);
    advertise_after_disconnect_ = true;
    connected_ = false;
    gatts_if_ = ESP_GATT_IF_NONE;
    instance_ = this;

    memset(handles_, 0, sizeof(handles_));
    memset(ssid_, 0, sizeof(ssid_));
    memset(password_, 0, sizeof(password_));
    memset(peer_, 0, sizeof(peer_));

    table_[SERVICE].attr_control.auto_rsp = ESP_GATT_AUTO_RSP;
    table_[SERVICE].att_desc = {ESP_UUID_LEN_16,
        reinterpret_cast<uint8_t*>(&primary_uuid), ESP_GATT_PERM_READ,
        16, 16, uuids[0]};
    const uint16_t max_lengths[] = {3, 32, 63, 1};
    // Service initialization:
    for (int n = 0; n < 4; ++n) {
        // Declaration characteristics
        auto& declaration = table_[1 + 2*n];
        declaration.attr_control.auto_rsp = ESP_GATT_AUTO_RSP;
        declaration.att_desc = {ESP_UUID_LEN_16,
            reinterpret_cast<uint8_t*>(&declaration_uuid), ESP_GATT_PERM_READ,
            1, 1, &write_property};
        // Value characteristics
        auto& value = table_[2 + 2*n];
        value.attr_control.auto_rsp = ESP_GATT_RSP_BY_APP;
        value.att_desc = {ESP_UUID_LEN_128, uuids[n+1], ESP_GATT_PERM_WRITE_ENCRYPTED,
                         max_lengths[n], 0, nullptr};
    }

    advertising_ = {};
    advertising_.adv_int_min = 0x100; // 160 ms
    advertising_.adv_int_max = 0x200; // 320 ms
    advertising_.adv_type = ADV_TYPE_IND;
    advertising_.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    advertising_.channel_map = ADV_CHNL_ALL;
    advertising_.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
    adv_data_ = {};
    adv_data_.flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT;
    adv_data_.service_uuid_len = 16;
    adv_data_.p_service_uuid = uuids[0];
    scan_data_ = {};
    scan_data_.set_scan_rsp = true;
    scan_data_.include_name = true;

    //  IDF-6.0 warnings
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    esp_bt_controller_config_t bt = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
#pragma GCC diagnostic pop
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_callback));
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_callback));
    ESP_ERROR_CHECK(esp_ble_gatt_set_local_mtu(185));
    ESP_ERROR_CHECK(esp_ble_gap_set_device_name(name_));

    esp_ble_auth_req_t auth = ESP_LE_AUTH_REQ_SC_ONLY;
    esp_ble_io_cap_t io = ESP_IO_CAP_NONE;
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth, sizeof(auth)));
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &io, sizeof(io)));

    // Service -> Advertising-Daten -> Scan-Response -> Advertising, per Callback.
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(0));
    return ESP_OK;
}

esp_err_t BleManager::start_advertising() {
    esp_err_t err = esp_ble_gap_start_advertising(&advertising_);
    return err;
}

esp_err_t BleManager::stop_advertising() {
    return esp_ble_gap_stop_advertising();
}

esp_err_t BleManager::stop() {
    if (instance_ != this) return ESP_ERR_INVALID_STATE;
    ESP_ERROR_CHECK(pause());

    xEventGroupClearBits(events_, UNREGISTERED);
    ESP_ERROR_CHECK(esp_ble_gatts_app_unregister(gatts_if_));
    if (!(xEventGroupWaitBits(events_, UNREGISTERED, pdFALSE, pdTRUE,
                             pdMS_TO_TICKS(3000)) & UNREGISTERED))
        return ESP_ERR_TIMEOUT;

    ESP_ERROR_CHECK(esp_bluedroid_disable());
    ESP_ERROR_CHECK(esp_bluedroid_deinit());
    ESP_ERROR_CHECK(esp_bt_controller_disable());
    ESP_ERROR_CHECK(esp_bt_controller_deinit());
    instance_ = nullptr;
    gatts_if_ = ESP_GATT_IF_NONE;
    memset(ssid_, 0, sizeof(ssid_));
    memset(password_, 0, sizeof(password_));
    memset(handles_, 0, sizeof(handles_));
    memset(table_, 0, sizeof(table_));
    memset(peer_, 0, sizeof(peer_));
    xEventGroupClearBits(events_, READY | ADV_STOPPED | DISCONNECTED | UNREGISTERED);
    ESP_LOGI(TAG, "BLE stopped");
    return ESP_OK;
}

esp_err_t BleManager::pause() {
    if (instance_ != this) return ESP_ERR_INVALID_STATE;
    advertise_after_disconnect_ = false; // VOR dem Disconnect

    // Auch bei pause() unmittelbar nach cold_start() die Initialisierung abwarten.
    if (!(xEventGroupWaitBits(events_, READY, pdFALSE, pdTRUE,
                             pdMS_TO_TICKS(3000)) & READY)) return ESP_ERR_TIMEOUT;

    xEventGroupClearBits(events_, ADV_STOPPED);
    esp_err_t err = stop_advertising();
    if (err != ESP_OK) return err;
    if (!(xEventGroupWaitBits(events_, ADV_STOPPED, pdFALSE, pdTRUE,
                             pdMS_TO_TICKS(3000)) & ADV_STOPPED)) return ESP_ERR_TIMEOUT;

    xEventGroupClearBits(events_, DISCONNECTED);
    esp_bd_addr_t peer;
    portENTER_CRITICAL(&peer_mux_);
    const bool connected = connected_.load();
    memcpy(peer, peer_, sizeof(peer));
    portEXIT_CRITICAL(&peer_mux_);
    if (connected) {
        err = esp_ble_gap_disconnect(peer);
        if (err != ESP_OK && connected_) return err;
        if (connected_ && !(xEventGroupWaitBits(events_, DISCONNECTED, pdFALSE, pdTRUE,
                                              pdMS_TO_TICKS(3000)) & DISCONNECTED))
            return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t BleManager::resume() {
    if (instance_ != this) return ESP_ERR_INVALID_STATE;
    advertise_after_disconnect_ = true;
    esp_err_t err = start_advertising();
    return err;
}

void BleManager::gap_callback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* p) {
    if (!instance_) return;
    auto& self = *instance_;
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        ESP_ERROR_CHECK(p->adv_data_cmpl.status == ESP_BT_STATUS_SUCCESS ? ESP_OK : ESP_FAIL);
        ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&self.scan_data_));
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        ESP_ERROR_CHECK(p->scan_rsp_data_cmpl.status == ESP_BT_STATUS_SUCCESS ? ESP_OK : ESP_FAIL);
        if (self.advertise_after_disconnect_)
            ESP_ERROR_CHECK(self.start_advertising());
        xEventGroupSetBits(self.events_, READY);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        ESP_ERROR_CHECK(p->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS ? ESP_OK : ESP_FAIL);
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        ESP_ERROR_CHECK(p->adv_stop_cmpl.status == ESP_BT_STATUS_SUCCESS ? ESP_OK : ESP_FAIL);
        xEventGroupSetBits(self.events_, ADV_STOPPED);
        break;
    case ESP_GAP_BLE_SEC_REQ_EVT:
        esp_ble_gap_security_rsp(p->ble_security.ble_req.bd_addr, true);
        break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        ESP_LOGI(TAG, "Encryption %s", p->ble_security.auth_cmpl.success ? "ready" : "failed");
        break;
    default: break;
    }
}

void BleManager::gatts_callback(esp_gatts_cb_event_t event, esp_gatt_if_t i,
                               esp_ble_gatts_cb_param_t* p) {
    if (!instance_) return;
    auto& self = *instance_;
    switch (event) {
    case ESP_GATTS_REG_EVT:
        ESP_ERROR_CHECK(p->reg.status == ESP_GATT_OK ? ESP_OK : ESP_FAIL);
        self.gatts_if_ = i;
        ESP_ERROR_CHECK(esp_ble_gatts_create_attr_tab(self.table_, i, 9, 0));
        break;
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        ESP_ERROR_CHECK(p->add_attr_tab.status == ESP_GATT_OK &&
            p->add_attr_tab.num_handle == 9 ? ESP_OK : ESP_FAIL);
        memcpy(self.handles_, p->add_attr_tab.handles, sizeof(self.handles_));
        ESP_ERROR_CHECK(esp_ble_gatts_start_service(self.handles_[SERVICE]));
        break;
    case ESP_GATTS_START_EVT:
        ESP_ERROR_CHECK(p->start.status == ESP_GATT_OK ? ESP_OK : ESP_FAIL);
        ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&self.adv_data_));
        break;
    case ESP_GATTS_UNREG_EVT:
        xEventGroupSetBits(self.events_, UNREGISTERED);
        break;
    case ESP_GATTS_CONNECT_EVT:
        portENTER_CRITICAL(&self.peer_mux_);
        memcpy(self.peer_, p->connect.remote_bda, sizeof(self.peer_));
        self.connected_ = true;
        portEXIT_CRITICAL(&self.peer_mux_);
        if (self.advertise_after_disconnect_)
            ESP_ERROR_CHECK(esp_ble_set_encryption(p->connect.remote_bda, ESP_BLE_SEC_ENCRYPT));
        ESP_LOGI(TAG, "Connected");
        break;
    case ESP_GATTS_DISCONNECT_EVT:
        memset(self.ssid_, 0, sizeof(self.ssid_));
        memset(self.password_, 0, sizeof(self.password_));
        ESP_LOGI(TAG, "Disconnected");
        if (self.advertise_after_disconnect_)
            ESP_ERROR_CHECK(self.start_advertising());
        portENTER_CRITICAL(&self.peer_mux_);
        memset(self.peer_, 0, sizeof(self.peer_));
        self.connected_ = false;
        portEXIT_CRITICAL(&self.peer_mux_);
        xEventGroupSetBits(self.events_, DISCONNECTED);
        break;
    case ESP_GATTS_WRITE_EVT: {
        esp_gatt_status_t status;
        if (p->write.is_prep) status = ESP_GATT_REQ_NOT_SUPPORTED;
        else if (p->write.offset != 0) status = ESP_GATT_INVALID_OFFSET;
        else status = self.handle_write(p->write.handle, p->write.value, p->write.len);
        if (p->write.need_rsp)
            esp_ble_gatts_send_response(i, p->write.conn_id, p->write.trans_id, status, nullptr);
        break;
    }
    case ESP_GATTS_EXEC_WRITE_EVT:
        esp_ble_gatts_send_response(i, p->exec_write.conn_id, p->exec_write.trans_id,
                                   ESP_GATT_REQ_NOT_SUPPORTED, nullptr);
        break;
    default: break;
    }
}

esp_gatt_status_t BleManager::handle_write(uint16_t handle, const uint8_t* data, uint16_t len) {
    if (handle == handles_[LED_VALUE]) {
        if (len != 3) return ESP_GATT_INVALID_ATTR_LEN;
        return service_layer::set_led_color(data[0], data[1], data[2]) == ESP_OK
            ? ESP_GATT_OK : static_cast<esp_gatt_status_t>(0x80);
    }
    if (handle == handles_[SSID_VALUE] || handle == handles_[PASSWORD_VALUE]) {
        const bool is_ssid = handle == handles_[SSID_VALUE];
        if ((is_ssid && (len == 0 || len > 32)) || (!is_ssid && len > 63))
            return ESP_GATT_INVALID_ATTR_LEN;
        if (len && memchr(data, 0, len)) return static_cast<esp_gatt_status_t>(0x80);
        char* target = is_ssid ? ssid_ : password_;
        memset(target, 0, is_ssid ? sizeof(ssid_) : sizeof(password_));
        if (len) memcpy(target, data, len);
        return ESP_GATT_OK;
    }
    if (handle == handles_[APPLY_VALUE]) {
        if (len != 1) return ESP_GATT_INVALID_ATTR_LEN;
        if (data[0] != 1 || ssid_[0] == '\0') return static_cast<esp_gatt_status_t>(0x80);
        if (service_layer::connect_wifi(ssid_, password_) != ESP_OK)
            return static_cast<esp_gatt_status_t>(0x80);
        memset(ssid_, 0, sizeof(ssid_));
        memset(password_, 0, sizeof(password_));
        return ESP_GATT_OK;
    }
    return ESP_GATT_WRITE_NOT_PERMIT;
}

namespace service_layer {
esp_err_t set_led_color(uint8_t red, uint8_t green, uint8_t blue) {
    ESP_LOGI(TAG, "TODO LED: R=%u G=%u B=%u", unsigned(red), unsigned(green), unsigned(blue));
    return ESP_ERR_NOT_SUPPORTED; // nRF Connect sieht Fehler 0x80, solange nicht implementiert.
}
esp_err_t connect_wifi(const char* ssid, const char* password) {
    (void)ssid;
    (void)password;
    ESP_LOGI(TAG, "TODO: WLAN-Befehl an ServiceLayer uebergeben");
    return ESP_ERR_NOT_SUPPORTED;
}
}