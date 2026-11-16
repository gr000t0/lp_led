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

esp_err_t BleManager::start(const char* name) {
    if (!name || !*name || strlen(name) > 29) return ESP_ERR_INVALID_ARG;
    if (instance_ && instance_ != this) return ESP_ERR_INVALID_STATE;
    if (!events_) events_ = xEventGroupCreate();
    if (!events_) return ESP_ERR_NO_MEM;
    if (xEventGroupGetBits(events_) & RUNNING) return ESP_ERR_INVALID_STATE;

    instance_ = this;
    xEventGroupClearBits(events_, ALL_BITS);
    memset(handles_, 0, sizeof(handles_));
    memset(ssid_, 0, sizeof(ssid_));
    memset(password_, 0, sizeof(password_));
    ssid_received_ = password_received_ = false;

    // Feste GATT-Tabelle: ein Service mit vier schreibbaren Characteristics.
    table_[SERVICE].attr_control.auto_rsp = ESP_GATT_AUTO_RSP;
    table_[SERVICE].att_desc = {ESP_UUID_LEN_16,
        reinterpret_cast<uint8_t*>(&primary_uuid), ESP_GATT_PERM_READ, 16, 16, uuids[0]};
    const uint16_t max_lengths[] = {3, 32, 63, 1};
    for (int n = 0; n < 4; ++n) {
        auto& declaration = table_[1 + 2*n];
        declaration.attr_control.auto_rsp = ESP_GATT_AUTO_RSP;
        declaration.att_desc = {ESP_UUID_LEN_16,
            reinterpret_cast<uint8_t*>(&declaration_uuid), ESP_GATT_PERM_READ,
            1, 1, &write_property};
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

    esp_bt_controller_config_t bt = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_callback));
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_callback));
    ESP_ERROR_CHECK(esp_ble_gatt_set_local_mtu(185));
    ESP_ERROR_CHECK(esp_ble_gap_set_device_name(name));

    // Verschlüsselung für WLAN-Passwort: Just Works, kein dauerhaftes Bonding.
    esp_ble_auth_req_t auth = ESP_LE_AUTH_REQ_SC_ONLY;
    esp_ble_io_cap_t io = ESP_IO_CAP_NONE;
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth, sizeof(auth)));
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &io, sizeof(io)));

    xEventGroupSetBits(events_, RUNNING);
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(0));
    ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&adv_data_));
    ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&scan_data_));
    return ESP_OK; // Abschluss folgt asynchron; die Callbacks starten Advertising.
}

esp_err_t BleManager::start_advertising() {
    if (!events_) return ESP_ERR_INVALID_STATE;
    EventBits_t bits = xEventGroupGetBits(events_);
    if ((bits & (RUNNING | READY)) != (RUNNING | READY) ||
        (bits & (CONNECTED | STOPPING | ADV_PENDING | ADVERTISING)))
        return ESP_ERR_INVALID_STATE;
    xEventGroupSetBits(events_, ADV_PENDING);
    esp_err_t err = esp_ble_gap_start_advertising(&advertising_);
    if (err != ESP_OK) xEventGroupClearBits(events_, ADV_PENDING);
    return err;
}

esp_err_t BleManager::stop_advertising() {
    if (!events_ || !(xEventGroupGetBits(events_) & RUNNING)) return ESP_ERR_INVALID_STATE;
    xEventGroupClearBits(events_, ADV_STOPPED);
    return esp_ble_gap_stop_advertising();
}

void BleManager::try_advertise() {
    EventBits_t bits = xEventGroupGetBits(events_);
    if ((bits & READY) == READY && !(bits & (CONNECTED | STOPPING | ADV_PENDING | ADVERTISING))) {
        esp_err_t err = start_advertising();
        if (err != ESP_OK) ESP_LOGW(TAG, "Advertising request: %s", esp_err_to_name(err));
    }
}

esp_err_t BleManager::stop() {
    if (!events_ || !(xEventGroupGetBits(events_) & RUNNING)) return ESP_OK;
    xEventGroupSetBits(events_, STOPPING);

    // Advertising zuerst stoppen: danach kann kein neuer Client verbinden.
    esp_err_t err = ESP_OK;
    if (xEventGroupGetBits(events_) & (ADVERTISING | ADV_PENDING)) {
        err = stop_advertising();
        if (err != ESP_OK) return err;
        if (!(xEventGroupWaitBits(events_, ADV_STOPPED, pdFALSE, pdTRUE,
                                 pdMS_TO_TICKS(3000)) & ADV_STOPPED)) return ESP_ERR_TIMEOUT;
    }

    if (xEventGroupGetBits(events_) & CONNECTED) {
        xEventGroupClearBits(events_, DISCONNECTED);
        err = esp_ble_gap_disconnect(peer_);
        if (err != ESP_OK && (xEventGroupGetBits(events_) & CONNECTED)) return err;
        if ((xEventGroupGetBits(events_) & CONNECTED) &&
            !(xEventGroupWaitBits(events_, DISCONNECTED, pdFALSE, pdTRUE,
                                 pdMS_TO_TICKS(3000)) & DISCONNECTED)) return ESP_ERR_TIMEOUT;
    }
    // Fatal bei Abbaufehler: kein scheinbar nutzbarer, halb abgebauter Manager.
    ESP_ERROR_CHECK(esp_bluedroid_disable());
    ESP_ERROR_CHECK(esp_bluedroid_deinit());
    ESP_ERROR_CHECK(esp_bt_controller_disable());
    ESP_ERROR_CHECK(esp_bt_controller_deinit());
    xEventGroupClearBits(events_, ALL_BITS);
    return ESP_OK;
}

void BleManager::gap_callback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* p) {
    if (!instance_) return;
    auto& self = *instance_;
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        ESP_ERROR_CHECK(p->adv_data_cmpl.status == ESP_BT_STATUS_SUCCESS ? ESP_OK : ESP_FAIL);
        xEventGroupSetBits(self.events_, ADV_READY);
        self.try_advertise();
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        ESP_ERROR_CHECK(p->scan_rsp_data_cmpl.status == ESP_BT_STATUS_SUCCESS ? ESP_OK : ESP_FAIL);
        xEventGroupSetBits(self.events_, SCAN_READY);
        self.try_advertise();
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        xEventGroupClearBits(self.events_, ADV_PENDING);
        if (p->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS)
            xEventGroupSetBits(self.events_, ADVERTISING);
        ESP_LOGI(TAG, "Advertising start status=%d", p->adv_start_cmpl.status);
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        // Eine zwischenzeitliche Verbindung hat Advertising bereits beendet.
        if (p->adv_stop_cmpl.status == ESP_BT_STATUS_SUCCESS ||
            (xEventGroupGetBits(self.events_) & CONNECTED)) {
            xEventGroupClearBits(self.events_, ADVERTISING | ADV_PENDING);
            xEventGroupSetBits(self.events_, ADV_STOPPED);
        } else ESP_LOGW(TAG, "Advertising stop status=%d", p->adv_stop_cmpl.status);
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
        ESP_ERROR_CHECK(esp_ble_gatts_create_attr_tab(self.table_, i, ATTRIBUTE_COUNT, 0));
        break;
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        ESP_ERROR_CHECK(p->add_attr_tab.status == ESP_GATT_OK &&
            p->add_attr_tab.num_handle == ATTRIBUTE_COUNT ? ESP_OK : ESP_FAIL);
        memcpy(self.handles_, p->add_attr_tab.handles, sizeof(self.handles_));
        ESP_ERROR_CHECK(esp_ble_gatts_start_service(self.handles_[SERVICE]));
        break;
    case ESP_GATTS_START_EVT:
        ESP_ERROR_CHECK(p->start.status == ESP_GATT_OK ? ESP_OK : ESP_FAIL);
        xEventGroupSetBits(self.events_, SERVICE_READY);
        self.try_advertise();
        break;
    case ESP_GATTS_CONNECT_EVT:
        memcpy(self.peer_, p->connect.remote_bda, sizeof(self.peer_));
        xEventGroupClearBits(self.events_, DISCONNECTED | ADVERTISING | ADV_PENDING);
        xEventGroupSetBits(self.events_, CONNECTED);
        if (xEventGroupGetBits(self.events_) & STOPPING)
            esp_ble_gap_disconnect(self.peer_);
        else
            esp_ble_set_encryption(self.peer_, ESP_BLE_SEC_ENCRYPT);
        ESP_LOGI(TAG, "Connected");
        break;
    case ESP_GATTS_DISCONNECT_EVT:
        memset(self.ssid_, 0, sizeof(self.ssid_));
        memset(self.password_, 0, sizeof(self.password_));
        self.ssid_received_ = self.password_received_ = false;
        xEventGroupClearBits(self.events_, CONNECTED);
        xEventGroupSetBits(self.events_, DISCONNECTED);
        ESP_LOGI(TAG, "Disconnected");
        // Diese Zeile entfernen, wenn du nach Disconnect nicht automatisch werben willst.
        self.try_advertise();
        break;
    case ESP_GATTS_WRITE_EVT: {
        esp_gatt_status_t status;
        if (p->write.is_prep) status = ESP_GATT_REQ_NOT_SUPPORTED;
        else if (p->write.offset != 0) status = ESP_GATT_INVALID_OFFSET;
        else if (xEventGroupGetBits(self.events_) & STOPPING) status = ESP_GATT_WRITE_NOT_PERMIT;
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
        // Auch nach einem ungültigen neuen Wert keine alten Zugangsdaten anwenden.
        if (is_ssid) ssid_received_ = false;
        else password_received_ = false;
        if ((is_ssid && (len == 0 || len > 32)) ||
            (!is_ssid && len != 0 && (len < 8 || len > 63))) return ESP_GATT_INVALID_ATTR_LEN;
        if (len && memchr(data, 0, len)) return static_cast<esp_gatt_status_t>(0x80);
        char* target = is_ssid ? ssid_ : password_;
        memset(target, 0, is_ssid ? sizeof(ssid_) : sizeof(password_));
        if (len) memcpy(target, data, len);
        if (is_ssid) ssid_received_ = true;
        else password_received_ = true;
        return ESP_GATT_OK;
    }
    if (handle == handles_[APPLY_VALUE]) {
        if (len != 1) return ESP_GATT_INVALID_ATTR_LEN;
        if (data[0] != 1 || !ssid_received_ || !password_received_)
            return static_cast<esp_gatt_status_t>(0x80);
        if (service_layer::connect_wifi(ssid_, password_) != ESP_OK)
            return static_cast<esp_gatt_status_t>(0x80);
        memset(ssid_, 0, sizeof(ssid_));
        memset(password_, 0, sizeof(password_));
        ssid_received_ = password_received_ = false;
        return ESP_GATT_OK;
    }
    return ESP_GATT_WRITE_NOT_PERMIT;
}

// Externe Anwendungsmethoden: vorerst Platzhalter, keine tatsächlichen Hardwareaktionen.
// Später diese Definitionen in service_layer.cpp verschieben/ersetzen.
// Daten dort SOFORT KOPIEREN und per Queue an deinen Anwendungstask übergeben.
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