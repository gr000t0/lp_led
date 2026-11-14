#pragma once
#include <cstdint>
#include "esp_err.h"
#include "led_strip.h"

// Owned by the application task. Calls are intentionally not thread-safe.
class led {
public:
    led() = default;
    ~led();
    led(const led&) = delete;
    led& operator=(const led&) = delete;
    esp_err_t init(int gpio = 8);
    esp_err_t deinit();
    esp_err_t set_color(uint32_t rgb); // remembers color and displays it
    esp_err_t turn_off();            // retains remembered color
    esp_err_t turn_on();
    esp_err_t blink(uint32_t color, uint32_t times); // blocking demo helper
    bool initialized() const { return led_ != nullptr; }
    uint32_t color() const { return color_; }
private:
    led_strip_handle_t led_ = nullptr;
    uint32_t color_ = 0x100000;
};
