#include "led.hpp"
#include "led_strip_rmt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
led::~led() { deinit(); }
esp_err_t led::init(int gpio) {
    if (led_) return ESP_ERR_INVALID_STATE;
    led_strip_config_t strip{};
    strip.strip_gpio_num = gpio;
    strip.max_leds = 1;
    strip.led_model = LED_MODEL_WS2812;
    strip.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    led_strip_rmt_config_t rmt{};
    rmt.resolution_hz = 10000000;
    return led_strip_new_rmt_device(&strip, &rmt, &led_);
}
esp_err_t led::deinit() {
    if (!led_) return ESP_OK;
    esp_err_t err = led_strip_del(led_);
    if (err == ESP_OK) led_ = nullptr;
    return err;
}
esp_err_t led::set_color(uint32_t rgb) {
    if (rgb > 0xFFFFFF) return ESP_ERR_INVALID_ARG;
    color_ = rgb;
    return turn_on();
}
esp_err_t led::turn_on() {
    if (!led_) return ESP_ERR_INVALID_STATE;
    esp_err_t err = led_strip_set_pixel(led_, 0, (color_ >> 16) & 255,
                                      (color_ >> 8) & 255, color_ & 255);
    return err == ESP_OK ? led_strip_refresh(led_) : err;
}
esp_err_t led::turn_off() {
    return led_ ? led_strip_clear(led_) : ESP_ERR_INVALID_STATE;
}
esp_err_t led::blink(uint32_t rgb, uint32_t times) {
    for (uint32_t i = 0; i < times; ++i) {
        esp_err_t err = set_color(rgb);
        if (err != ESP_OK) return err;
        vTaskDelay(pdMS_TO_TICKS(500));
        err = turn_off();
        if (err != ESP_OK) return err;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return ESP_OK;
}
