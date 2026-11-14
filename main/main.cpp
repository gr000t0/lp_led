#include "application.hpp"
#include "esp_log.h"
extern "C" void app_main() {
    // Static lifetime: BLE / Wi-Fi callbacks keep pointers to these objects.
    static Application app;
    esp_err_t err = app.init();
    if (err != ESP_OK) {
        ESP_LOGE("MAIN", "Initialization failed: %s", esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    }
    app.run();
}
