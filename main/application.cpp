#include "application.hpp"
#include "app_config.hpp"
#include <cstdio>
#include <cstring>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
namespace { constexpr char TAG[] = "APP"; }
WifiCredentials Application::default_credentials() {
    WifiCredentials c{};
    snprintf(c.ssid,sizeof(c.ssid),"%s",app_config::wifi_ssid);
    snprintf(c.password,sizeof(c.password),"%s",app_config::wifi_password);
    return c;
}
BleConfig Application::ble_config() const {
    BleConfig cfg{};
    cfg.name = app_config::ble_name;
    cfg.encrypted_link = app_config::ble_encrypt_writes;
    cfg.restart_advertising_on_disconnect = app_config::ble_advertise_after_disconnect;
    return cfg;
}
esp_err_t Application::init() {
    // Initialization failures are fatal at app_main; runtime command errors are not.
    esp_err_t e = settings_.init();
    if (e != ESP_OK) return e;
    e = esp_netif_init();
    if (e != ESP_OK) return e;
    e = esp_event_loop_create_default();
    if (e != ESP_OK) return e;
    e = power_.configure(app_config::cpu_min_mhz,app_config::cpu_max_mhz,app_config::automatic_light_sleep);
    if (e != ESP_OK) return e;
    ESP_LOGI(TAG,"Wakeup causes mask: 0x%lx",static_cast<unsigned long>(power_.wakeup_causes()));
    e = led_.init(app_config::led_gpio);
    if (e != ESP_OK) return e;
    e = led_.set_color(app_config::initial_rgb);
    if (e != ESP_OK) return e;
    commands_ = xQueueCreate(8,sizeof(DeviceCommand));
    if (!commands_) return ESP_ERR_NO_MEM;
    e = service_.init(commands_,app_config::ble_encrypt_writes);
    if (e != ESP_OK) return e;
    service_.set_displayed_color(led_.color());
    e = wifi_.init();
    if (e != ESP_OK) return e;
    wifi_.set_retry_limit(app_config::wifi_retries);
    time_.set_timezone(app_config::timezone);
    e = ble_.start(service_,ble_config());
    if (e != ESP_OK) return e;
    WifiCredentials c = default_credentials();
    if (app_config::load_saved_credentials) {
        WifiCredentials saved{};
        e = settings_.load_wifi(saved);
        if (e == ESP_OK) c = saved;
        else if (e != ESP_ERR_NVS_NOT_FOUND) ESP_LOGW(TAG,"Saved Wi-Fi ignored: %s",esp_err_to_name(e));
    }
    e = connect_wifi(c);
    if (e != ESP_OK) ESP_LOGW(TAG,"Wi-Fi start: %s; BLE remains available",esp_err_to_name(e));
    return ESP_OK;
}

esp_err_t Application::connect_wifi(const WifiCredentials& c, bool save) {
    if (!valid_credentials(c)) return ESP_ERR_INVALID_ARG;
    time_.stop();
    persist_pending_ = false;
    attempt_pending_ = false;
    current_ = c;
    esp_err_t e = wifi_.connect(c);
    if (e == ESP_OK) {
        persist_pending_ = save;
        attempt_pending_ = true;
        connection_started_ = esp_timer_get_time();
        previous_state_ = WifiManager::State::Connecting;
        result_ = "wifi-connecting";
        e = wifi_.set_power_save(WIFI_PS_MIN_MODEM);
        if (e != ESP_OK) ESP_LOGW(TAG,"Wi-Fi power-save: %s",esp_err_to_name(e));
        return ESP_OK;
    }
    result_ = "wifi-start-error";
    return e;
}
void Application::handle(const DeviceCommand& cmd) {
    esp_err_t e = ESP_OK;
    switch (cmd.type) {
    case DeviceCommand::Type::SetColor:
        e = led_.set_color(cmd.color);
        if (e == ESP_OK) service_.set_displayed_color(cmd.color);
        result_ = e == ESP_OK ? "led-ok" : "led-error";
        break;
    case DeviceCommand::Type::ConnectWifi:
        fallback_used_ = false;
        e = connect_wifi(cmd.credentials,app_config::save_ble_credentials_after_success);
        break;
    case DeviceCommand::Type::ForgetWifi:
        e = settings_.forget_wifi();
        // Also cancel any outstanding persistence request.
        persist_pending_ = false;
        result_ = e == ESP_OK ? "credentials-forgotten" : "nvs-error";
        break;
    }
    if (e != ESP_OK) ESP_LOGW(TAG,"Command failed: %s",esp_err_to_name(e));
}
void Application::update_network() {
    auto state = wifi_.state();
    if (state == WifiManager::State::Connecting && previous_state_ == WifiManager::State::Connected) {
        connection_started_ = esp_timer_get_time();
        attempt_pending_ = true;
        time_.stop();
    }
    if (state == WifiManager::State::Connected) {
        if (previous_state_ != state || attempt_pending_) {
            attempt_pending_ = false;
            result_ = "wifi-connected";
            ESP_LOGI(TAG,"Wi-Fi has an IP address");
            if (persist_pending_) {
                esp_err_t e = settings_.save_wifi(current_);
                result_ = e == ESP_OK ? "wifi-saved" : "nvs-error";
                if (e != ESP_OK) ESP_LOGW(TAG,"Save failed: %s",esp_err_to_name(e));
                persist_pending_ = false;
            }
        }
        // Idempotent and non-blocking. SNTP updates system UTC and periodically resyncs.
        esp_err_t e = time_.start(app_config::ntp_server);
        if (e != ESP_OK) result_ = "ntp-start-error";
    }
    bool expired = attempt_pending_ && (esp_timer_get_time()-connection_started_ >=
                                         int64_t(app_config::wifi_attempt_timeout_ms)*1000);
    if ((state == WifiManager::State::Failed && previous_state_ != state) || expired) {
        ESP_LOGW(TAG,"Wi-Fi failed (reason=%u, timeout=%d); BLE stays available",
                 unsigned(wifi_.disconnect_reason()),expired);
        time_.stop();
        esp_err_t e = wifi_.stop();
        if (e != ESP_OK) ESP_LOGW(TAG,"Wi-Fi stop: %s",esp_err_to_name(e));
        persist_pending_ = false;
        attempt_pending_ = false;
        result_ = expired ? "wifi-timeout" : "wifi-failed";
        const auto defaults = default_credentials();
        if (app_config::fallback_to_default_wifi && !fallback_used_ &&
            (strcmp(current_.ssid,defaults.ssid) || strcmp(current_.password,defaults.password))) {
            fallback_used_ = true;
            connect_wifi(defaults,false);
        }
        state = wifi_.state();
    }
    previous_state_ = state;
    if (time_.synchronized() && !logged_sync_) {
        ESP_LOGI(TAG,"Time synchronized: %s",time_.local_time().c_str());
        logged_sync_ = true;
    }
}
void Application::publish_status() {
    const char* names[] = {"stopped","connecting","connected","failed"};
    char text[192];
    snprintf(text,sizeof(text),"wifi=%s;reason=%u;time=%s;result=%s",
             names[int(wifi_.state())],unsigned(wifi_.disconnect_reason()),
             time_.local_time().c_str(),result_);
    service_.set_status(text);
}
esp_err_t Application::prepare_for_sleep() {
    time_.stop();
    esp_err_t e = wifi_.stop();
    if (e == ESP_OK) e = ble_.stop();
    if (e == ESP_OK) e = led_.turn_off();
    if (e == ESP_OK) e = led_.deinit(); // release RMT driver's PM resources
    if (e != ESP_OK) {
        esp_err_t restore = resume_after_sleep();
        if (restore != ESP_OK) ESP_LOGE(TAG,"Sleep rollback: %s",esp_err_to_name(restore));
    }
    return e;
}
esp_err_t Application::resume_after_sleep() {
    esp_err_t e = led_.initialized() ? ESP_OK : led_.init(app_config::led_gpio);
    if (e == ESP_OK) e = led_.turn_on();
    if (e == ESP_OK && !ble_.running()) e = ble_.start(service_,ble_config());
    if (e == ESP_OK) e = connect_wifi(current_,persist_pending_);
    return e;
}
esp_err_t Application::sleep_light_for(uint64_t us) {
    esp_err_t e = prepare_for_sleep();
    if (e != ESP_OK) return e;
    e = power_.light_sleep_for(us);
    esp_err_t resume = resume_after_sleep();
    return e != ESP_OK ? e : resume;
}
esp_err_t Application::sleep_deep_for(uint64_t us) {
    esp_err_t e = prepare_for_sleep();
    if (e != ESP_OK) return e;
    e = power_.deep_sleep_for(us); // reboot on timer wakeup
    esp_err_t resume = resume_after_sleep(); // only reached on configuration error
    return e != ESP_OK ? e : resume;
}
esp_err_t Application::sleep_light_until_button(gpio_num_t gpio, int level) {
    esp_err_t e = prepare_for_sleep();
    if (e != ESP_OK) return e;
    e = power_.light_sleep_until_button(gpio,level);
    esp_err_t resume = resume_after_sleep();
    return e != ESP_OK ? e : resume;
}
esp_err_t Application::sleep_deep_until_button(gpio_num_t gpio, bool high) {
    esp_err_t e = prepare_for_sleep();
    if (e != ESP_OK) return e;
    e = power_.deep_sleep_until_button(gpio,high);
    esp_err_t resume = resume_after_sleep();
    return e != ESP_OK ? e : resume;
}
void Application::run() {
    int64_t awake_since = esp_timer_get_time();
    for (;;) {
        DeviceCommand cmd{};
        if (xQueueReceive(commands_,&cmd,pdMS_TO_TICKS(1000)) == pdTRUE) {
            handle(cmd);
            cmd.credentials = {};
        }
        update_network();
        publish_status();
        if (app_config::mode != app_config::Mode::Interactive &&
            esp_timer_get_time()-awake_since >= int64_t(app_config::demo_awake_ms)*1000) {
            esp_err_t e = app_config::mode == app_config::Mode::LightSleepTimerDemo
                ? sleep_light_for(app_config::demo_sleep_us) : sleep_deep_for(app_config::demo_sleep_us);
            if (e != ESP_OK) ESP_LOGE(TAG,"Sleep experiment failed: %s",esp_err_to_name(e));
            awake_since = esp_timer_get_time();
        }
    }
}
