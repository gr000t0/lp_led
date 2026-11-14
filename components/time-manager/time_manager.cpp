#include "time_manager.hpp"
#include <cstdlib>
#include "esp_netif_sntp.h"
std::atomic<bool> TimeManager::synced_{false};
esp_err_t TimeManager::start(const char* server) {
    if (started_) return ESP_OK;
    if (!server || !*server) return ESP_ERR_INVALID_ARG;
    server_ = server;
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(server_.c_str());
    cfg.sync_cb = on_sync;
    cfg.wait_for_sync = false;
    esp_err_t e = esp_netif_sntp_init(&cfg);
    started_ = (e == ESP_OK);
    return e;
}
void TimeManager::stop() { if (started_) esp_netif_sntp_deinit(); started_ = false; }
void TimeManager::on_sync(struct timeval*) { synced_ = true; }
void TimeManager::set_timezone(const char* tz) { setenv("TZ", tz, 1); tzset(); }
std::string TimeManager::local_time() const {
    if (!synchronized()) return "not-synchronized";
    time_t t = now();
    struct tm local{};
    localtime_r(&t, &local);
    char result[48];
    strftime(result, sizeof(result), "%Y-%m-%d %H:%M:%S %Z", &local);
    return result;
}
