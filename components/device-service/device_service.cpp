#include "device_service.hpp"
#include <algorithm>
#include <cstdio>
#include "esp_log.h"
namespace {
constexpr char TAG[] = "SERVICE";
// UUID in human notation: 7c2a0000-6e3b-4a2e-9f71-8b6d20c14590
// Bluedroid expects little-endian UUID byte arrays.
uint8_t uuids[6][16] = {
 {0x90,0x45,0xc1,0x20,0x6d,0x8b,0x71,0x9f,0x2e,0x4a,0x3b,0x6e,0x00,0x00,0x2a,0x7c},
 {0x90,0x45,0xc1,0x20,0x6d,0x8b,0x71,0x9f,0x2e,0x4a,0x3b,0x6e,0x01,0x00,0x2a,0x7c},
 {0x90,0x45,0xc1,0x20,0x6d,0x8b,0x71,0x9f,0x2e,0x4a,0x3b,0x6e,0x02,0x00,0x2a,0x7c},
 {0x90,0x45,0xc1,0x20,0x6d,0x8b,0x71,0x9f,0x2e,0x4a,0x3b,0x6e,0x03,0x00,0x2a,0x7c},
 {0x90,0x45,0xc1,0x20,0x6d,0x8b,0x71,0x9f,0x2e,0x4a,0x3b,0x6e,0x04,0x00,0x2a,0x7c},
 {0x90,0x45,0xc1,0x20,0x6d,0x8b,0x71,0x9f,0x2e,0x4a,0x3b,0x6e,0x05,0x00,0x2a,0x7c}};
uint16_t primary_uuid = ESP_GATT_UUID_PRI_SERVICE, declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
uint8_t properties[5] = {ESP_GATT_CHAR_PROP_BIT_WRITE, ESP_GATT_CHAR_PROP_BIT_WRITE,
    ESP_GATT_CHAR_PROP_BIT_WRITE, ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE,
    ESP_GATT_CHAR_PROP_BIT_READ};
}
const uint8_t* DeviceService::uuid128() const { return uuids[0]; }
esp_err_t DeviceService::init(QueueHandle_t q, bool encrypted) {
    if (!q || mutex_) return ESP_ERR_INVALID_STATE;
    queue_ = q;
    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_) return ESP_ERR_NO_MEM;
    auto& s = table_[Service];
    s.attr_control.auto_rsp = ESP_GATT_AUTO_RSP;
    s.att_desc = {ESP_UUID_LEN_16, reinterpret_cast<uint8_t*>(&primary_uuid), ESP_GATT_PERM_READ, 16, 16, uuids[0]};
    const uint16_t lengths[] = {32,64,1,3,191};
    for (int n=0; n<5; ++n) {
        auto& d = table_[1+n*2];
        d.attr_control.auto_rsp = ESP_GATT_AUTO_RSP;
        d.att_desc = {ESP_UUID_LEN_16, reinterpret_cast<uint8_t*>(&declaration_uuid), ESP_GATT_PERM_READ,1,1,&properties[n]};
        auto& v = table_[2+n*2];
        v.attr_control.auto_rsp = ESP_GATT_RSP_BY_APP;
        esp_gatt_perm_t perm = 0;
        if (n == 3 || n == 4) perm |= ESP_GATT_PERM_READ;
        if (n != 4) perm |= encrypted ? ESP_GATT_PERM_WRITE_ENCRYPTED : ESP_GATT_PERM_WRITE;
        v.att_desc = {ESP_UUID_LEN_128, uuids[n+1], perm, lengths[n], 0, nullptr};
    }
    return ESP_OK;
}
void DeviceService::set_status(const char* text) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    snprintf(status_, sizeof(status_), "%s", text);
    xSemaphoreGive(mutex_);
}
void DeviceService::set_displayed_color(uint32_t c) {
    xSemaphoreTake(mutex_, portMAX_DELAY); color_ = c; xSemaphoreGive(mutex_);
}
void DeviceService::clear_pending() {
    pending_ = {};
    have_ssid_ = have_password_ = false;
    prepared_.clear();
}
esp_gatt_status_t DeviceService::write(uint16_t h, const uint8_t* data, size_t n) {
    if (h == handles_[SsidValue]) {
        if (!n || !protocol::text_field(pending_.ssid,sizeof(pending_.ssid),data,n)) return ESP_GATT_INVALID_ATTR_LEN;
        have_ssid_ = true; return ESP_GATT_OK;
    }
    if (h == handles_[PasswordValue]) {
        if (!protocol::text_field(pending_.password,sizeof(pending_.password),data,n)) return ESP_GATT_INVALID_ATTR_LEN;
        have_password_ = true; return ESP_GATT_OK;
    }
    DeviceCommand cmd{};
    if (h == handles_[ApplyValue]) {
        if (n != 1) return ESP_GATT_INVALID_ATTR_LEN;
        if (data[0] == 1) {
            if (!have_ssid_ || !have_password_ || !valid_credentials(pending_)) return static_cast<esp_gatt_status_t>(0x80);
            cmd.type = DeviceCommand::Type::ConnectWifi; cmd.credentials = pending_;
        } else if (data[0] == 2) cmd.type = DeviceCommand::Type::ForgetWifi;
        else return static_cast<esp_gatt_status_t>(0x80);
    } else if (h == handles_[RgbValue]) {
        cmd.type = DeviceCommand::Type::SetColor;
        if (!protocol::rgb(data,n,cmd.color)) return ESP_GATT_INVALID_ATTR_LEN;
    } else return ESP_GATT_WRITE_NOT_PERMIT;
    // Never wait for network operations inside the Bluetooth task.
    if (xQueueSend(queue_, &cmd, 0) != pdTRUE) return ESP_GATT_INSUF_RESOURCE;
    if (h == handles_[ApplyValue]) clear_pending();
    return ESP_GATT_OK; // means queued, not yet connected/persisted; read status for result
}
void DeviceService::read(esp_gatt_if_t i, esp_ble_gatts_cb_param_t* p) {
    esp_gatt_rsp_t rsp{};
    rsp.attr_value.handle = p->read.handle;
    rsp.attr_value.offset = p->read.offset;
    uint8_t bytes[192]{};
    size_t n=0;
    esp_gatt_status_t result = ESP_GATT_OK;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (p->read.handle == handles_[StatusValue]) {
        if (p->read.offset == 0) memcpy(read_snapshot_,status_,sizeof(read_snapshot_));
        n = strlen(read_snapshot_); memcpy(bytes,read_snapshot_,n);
    }
    else if (p->read.handle == handles_[RgbValue]) {
        n=3; bytes[0]=color_>>16; bytes[1]=color_>>8; bytes[2]=color_;
    } else result = ESP_GATT_READ_NOT_PERMIT;
    xSemaphoreGive(mutex_);
    if (p->read.offset > n) result = ESP_GATT_INVALID_OFFSET;
    if (result == ESP_GATT_OK) {
        rsp.attr_value.len = std::min(n-p->read.offset, size_t(mtu_-1));
        memcpy(rsp.attr_value.value,bytes+p->read.offset,rsp.attr_value.len);
    }
    esp_ble_gatts_send_response(i,p->read.conn_id,p->read.trans_id,result,&rsp);
}
void DeviceService::on_gatts_event(esp_gatts_cb_event_t e, esp_gatt_if_t i, esp_ble_gatts_cb_param_t* p) {
    switch (e) {
    case ESP_GATTS_REG_EVT:
        memset(handles_,0,sizeof(handles_));
        if (p->reg.status == ESP_GATT_OK) {
            esp_err_t err = esp_ble_gatts_create_attr_tab(table_,i,Count,0);
            if (err != ESP_OK) ESP_LOGE(TAG,"Create table: %s",esp_err_to_name(err));
        } else ESP_LOGE(TAG,"Register failed: %d",p->reg.status);
        break;
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (p->add_attr_tab.status == ESP_GATT_OK && p->add_attr_tab.num_handle == Count) {
            memcpy(handles_,p->add_attr_tab.handles,sizeof(handles_));
            esp_ble_gatts_start_service(handles_[Service]);
        } else ESP_LOGE(TAG,"Attribute table failed: %d",p->add_attr_tab.status);
        break;
    case ESP_GATTS_CONNECT_EVT: case ESP_GATTS_DISCONNECT_EVT:
        clear_pending(); read_snapshot_[0] = 0; mtu_ = 23; break;
    case ESP_GATTS_MTU_EVT: mtu_ = p->mtu.mtu; break;
    case ESP_GATTS_READ_EVT:
        // Static service/declaration attributes use AUTO_RSP; never answer them twice.
        if (p->read.handle == handles_[StatusValue] || p->read.handle == handles_[RgbValue]) read(i,p);
        break;
    case ESP_GATTS_WRITE_EVT: {
        esp_gatt_status_t result;
        esp_gatt_rsp_t rsp{};
        if (p->write.is_prep) {
            bool allowed = p->write.handle == handles_[SsidValue] || p->write.handle == handles_[PasswordValue];
            size_t limit = p->write.handle == handles_[SsidValue] ? 32 : 64;
            if (!allowed) result = ESP_GATT_WRITE_NOT_PERMIT;
            else if (size_t(p->write.offset)+p->write.len > limit) result = ESP_GATT_INVALID_ATTR_LEN;
            else if (!prepared_.append(p->write.handle,p->write.offset,p->write.value,p->write.len)) result = ESP_GATT_INVALID_OFFSET;
            else result = ESP_GATT_OK;
            if (result == ESP_GATT_OK) {
                rsp.attr_value.handle=p->write.handle; rsp.attr_value.offset=p->write.offset;
                rsp.attr_value.len=p->write.len;
                memcpy(rsp.attr_value.value,p->write.value,p->write.len);
            } else prepared_.clear();
        } else if (p->write.offset != 0) result = ESP_GATT_INVALID_OFFSET;
        else result = write(p->write.handle,p->write.value,p->write.len);
        if (p->write.need_rsp) esp_ble_gatts_send_response(i,p->write.conn_id,p->write.trans_id,result,
                                                         p->write.is_prep ? &rsp : nullptr);
        break;
    }
    case ESP_GATTS_EXEC_WRITE_EVT: {
        esp_gatt_status_t result = ESP_GATT_OK;
        if (p->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC && prepared_.handle)
            result = write(prepared_.handle,prepared_.bytes,prepared_.length);
        prepared_.clear();
        esp_ble_gatts_send_response(i,p->exec_write.conn_id,p->exec_write.trans_id,result,nullptr);
        break;
    }
    default: break;
    }
}
