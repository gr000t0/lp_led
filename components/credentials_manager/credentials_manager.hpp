#pragma once
#include "esp_err.h"

struct WifiCredentials{
    char ssid[32]{};
    char password[64]{};
};

class CredentialsManager {
    
public:
    esp_err_t init();
    esp_err_t load_wifi(WifiCredentials& out);
    esp_err_t save_wifi(const WifiCredentials& credentials);
    esp_err_t forget_wifi();
};
