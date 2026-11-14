idf_component_register(SRCS "main.cpp" "application.cpp" INCLUDE_DIRS "."
    REQUIRES led-manager wifi-manager ble-manager device-service power-manager time-manager
             settings-manager esp_timer esp_event esp_netif)
