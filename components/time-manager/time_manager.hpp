#pragma once
#include <atomic>
#include <ctime>
#include <string>
#include "esp_err.h"
class TimeManager {
public:
    esp_err_t start(const char* server = "pool.ntp.org");
    void stop();
    void set_timezone(const char* tz = "CET-1CEST,M3.5.0,M10.5.0/3");
    bool synchronized() const { return synced_.load(); }
    time_t now() const { return time(nullptr); }
    std::string local_time() const;
private:
    static void on_sync(struct timeval*);
    static std::atomic<bool> synced_;
    bool started_ = false;
    std::string server_;
};
