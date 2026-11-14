#pragma once
#include <cstdint>
#include "esp_err.h"
#include "esp_sleep.h"
#include "esp_pm.h"
#include "driver/gpio.h"
class PowerManager {
public:
    esp_err_t configure(int min_mhz = 40, int max_mhz = 160, bool automatic_light_sleep = false);
    // Manual sleep: caller must stop BLE/Wi-Fi and release LED RMT resources first.
    esp_err_t light_sleep_for(uint64_t microseconds);
    esp_err_t deep_sleep_for(uint64_t microseconds); // success does not return
    esp_err_t light_sleep_until_button(gpio_num_t gpio, int active_level = 0);
    // C6 EXT1 only supports LP GPIOs; SDK validates mask. External pull recommended.
    esp_err_t deep_sleep_until_button(gpio_num_t gpio, bool high = false);
    uint32_t wakeup_causes() const { return esp_sleep_get_wakeup_causes(); }
    static double duty_cycle(double active_ms, double period_ms);
    static double break_even_seconds(double transition_extra_j, double awake_w, double sleep_w);
private:
    esp_err_t clear_wakeup_sources();
};
