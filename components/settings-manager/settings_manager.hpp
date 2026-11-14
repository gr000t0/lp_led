#pragma once
#include "credentials.hpp"
#include "esp_err.h"
class SettingsManager {
public:
    esp_err_t init();
    esp_err_t load_wifi(WifiCredentials& out);
    esp_err_t save_wifi(const WifiCredentials& credentials);
    esp_err_t forget_wifi();
};
