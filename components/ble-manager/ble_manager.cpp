#include "ble_manager.hpp"
#include <cstring>
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"

namespace { constexpr char TAG[] = "BLE"; constexpr EventBits_t DISCONNECTED = BIT0; }
BleManager* BleManager::instance_ = nullptr;

esp_err_t BleManager::start(GattService& service, const BleConfig& cfg) {

    if (running_) return ESP_ERR_INVALID_STATE;


    instance_ = this;
    service_ = &service;
    config_ = cfg;
    name_ = cfg.name;

    stopping_ = false;
    ready_ = false;
    adv_data_ready_ = false;
    scan_data_ready_ = false;
    service_ready_ = false;

    
    esp_bt_controller_config_t bt = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    esp_ble_adv_data_t data{};
    data.flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT;
    data.service_uuid_len = 16;
    data.p_service_uuid = const_cast<uint8_t*>(service.uuid128());

    esp_ble_adv_data_t scan{};
    scan.set_scan_rsp = true;
    scan.include_name = true;

    advertising_ = {};
    advertising_.adv_int_min = cfg.advertising_min;
    advertising_.adv_int_max = cfg.advertising_max;
    advertising_.adv_type = ADV_TYPE_IND;
    advertising_.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    advertising_.channel_map = ADV_CHNL_ALL;
    advertising_.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;

    
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_callback));
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_callback));
    ESP_ERROR_CHECK(esp_ble_gatt_set_local_mtu(185));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(0));
    ESP_ERROR_CHECK(esp_ble_gap_set_device_name(name_.c_str()));
    ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&data));
    ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&scan));

    running_ = true;
    return ESP_OK;

}


esp_err_t BleManager::start_advertising() {
    if (connected_ || stopping_) return ESP_ERR_INVALID_STATE;
    return esp_ble_gap_start_advertising(&advertising_);
}
esp_err_t BleManager::stop_advertising() { return esp_ble_gap_stop_advertising(); }
void BleManager::try_advertise() {
    if (adv_data_ready_ && scan_data_ready_ && service_ready_ && !stopping_) {
        ready_ = true;
        esp_err_t e = start_advertising();
        if (e != ESP_OK) ESP_LOGE(TAG, "Advertising request: %s", esp_err_to_name(e));
    }
}
esp_err_t BleManager::stop() {
    if (!running_) return ESP_OK;
    stopping_ = true;
    ready_ = false;
    esp_ble_gap_stop_advertising();
    if (connected_) {
        xEventGroupClearBits(events_, DISCONNECTED);
        esp_err_t e = esp_ble_gap_disconnect(peer_);
        if (e != ESP_OK && connected_) return e;
        if (connected_ && !(xEventGroupWaitBits(events_, DISCONNECTED, pdTRUE, pdFALSE,
                                                pdMS_TO_TICKS(3000)) & DISCONNECTED)) return ESP_ERR_TIMEOUT;
    }
    esp_err_t e = esp_bluedroid_disable();
    if (e == ESP_OK) e = esp_bluedroid_deinit();
    if (e == ESP_OK) e = esp_bt_controller_disable();
    if (e == ESP_OK) e = esp_bt_controller_deinit();
    
    return e;
}
void BleManager::gap_callback(esp_gap_ble_cb_event_t e, esp_ble_gap_cb_param_t* p) {
    if (instance_) instance_->on_gap_event(e, p);
}
void BleManager::gatts_callback(esp_gatts_cb_event_t e, esp_gatt_if_t i, esp_ble_gatts_cb_param_t* p) {
    if (instance_) instance_->on_gatts_event(e, i, p);
}
void BleManager::on_gap_event(esp_gap_ble_cb_event_t e, esp_ble_gap_cb_param_t* p) {
    switch (e) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        adv_data_ready_ = p->adv_data_cmpl.status == ESP_BT_STATUS_SUCCESS; try_advertise(); break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        scan_data_ready_ = p->scan_rsp_data_cmpl.status == ESP_BT_STATUS_SUCCESS; try_advertise(); break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        ESP_LOGI(TAG, "Advertising status=%d", p->adv_start_cmpl.status); break;
    case ESP_GAP_BLE_SEC_REQ_EVT:
        esp_ble_gap_security_rsp(p->ble_security.ble_req.bd_addr, config_.encrypted_link); break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        ESP_LOGI(TAG, "Encryption %s", p->ble_security.auth_cmpl.success ? "ready" : "failed"); break;
    default: break;
    }
}
void BleManager::on_gatts_event(esp_gatts_cb_event_t e, esp_gatt_if_t i, esp_ble_gatts_cb_param_t* p) {
    if (e == ESP_GATTS_CONNECT_EVT) {
        memcpy(peer_, p->connect.remote_bda, sizeof(peer_));
        connected_ = true;
    }
    if (service_) service_->on_gatts_event(e, i, p);
    if (e == ESP_GATTS_START_EVT) {
        service_ready_ = p->start.status == ESP_GATT_OK; try_advertise();
    } else if (e == ESP_GATTS_DISCONNECT_EVT) {
        connected_ = false;
        xEventGroupSetBits(events_, DISCONNECTED);
        if (!stopping_ && config_.restart_advertising_on_disconnect) start_advertising();
    }
}
