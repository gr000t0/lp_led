#include "power_manager.hpp"
#include <cmath>
esp_err_t PowerManager::configure(int min_mhz, int max_mhz, bool automatic) {
    esp_pm_config_t cfg{};
    cfg.min_freq_mhz = min_mhz;
    cfg.max_freq_mhz = max_mhz;
    cfg.light_sleep_enable = automatic;
    return esp_pm_configure(&cfg);
}
esp_err_t PowerManager::clear_wakeup_sources() {
    return esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
}
esp_err_t PowerManager::light_sleep_for(uint64_t us) {
    if (!us) return ESP_ERR_INVALID_ARG;
    esp_err_t e = clear_wakeup_sources();
    if (e == ESP_OK) e = esp_sleep_enable_timer_wakeup(us);
    return e == ESP_OK ? esp_light_sleep_start() : e;
}
esp_err_t PowerManager::deep_sleep_for(uint64_t us) {
    if (!us) return ESP_ERR_INVALID_ARG;
    esp_err_t e = clear_wakeup_sources();
    if (e == ESP_OK) e = esp_sleep_enable_timer_wakeup(us);
    if (e != ESP_OK) return e;
    esp_deep_sleep_start();
    return ESP_OK;
}
esp_err_t PowerManager::light_sleep_until_button(gpio_num_t gpio, int level) {
    if (!GPIO_IS_VALID_GPIO(gpio) || (level != 0 && level != 1)) return ESP_ERR_INVALID_ARG;
    esp_err_t e = clear_wakeup_sources();
    if (e == ESP_OK) e = gpio_set_direction(gpio, GPIO_MODE_INPUT);
    if (e == ESP_OK) e = gpio_wakeup_enable(gpio, level ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
    if (e == ESP_OK) e = esp_sleep_enable_gpio_wakeup();
    if (e == ESP_OK) e = esp_light_sleep_start();
    gpio_wakeup_disable(gpio);
    return e;
}
esp_err_t PowerManager::deep_sleep_until_button(gpio_num_t gpio, bool high) {
    if (!GPIO_IS_VALID_GPIO(gpio) || gpio >= 64) return ESP_ERR_INVALID_ARG;
    esp_err_t e = clear_wakeup_sources();
    if (e == ESP_OK) e = esp_sleep_enable_ext1_wakeup_io(1ULL << gpio,
        high ? ESP_EXT1_WAKEUP_ANY_HIGH : ESP_EXT1_WAKEUP_ANY_LOW);
    if (e != ESP_OK) return e;
    esp_deep_sleep_start();
    return ESP_OK;
}
double PowerManager::duty_cycle(double active, double period) {
    return period > 0 && active >= 0 && active <= period ? active / period : NAN;
}
double PowerManager::break_even_seconds(double extra, double awake, double sleep) {
    return extra >= 0 && sleep >= 0 && awake > sleep ? extra / (awake - sleep) : NAN;
}
