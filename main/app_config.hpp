#pragma once
#include <cstdint>
namespace app_config {
inline constexpr int led_gpio = 8; // preserved from your original project
inline constexpr uint32_t initial_rgb = 0x001000;
inline constexpr char wifi_ssid[] = "lpsd";
inline constexpr char wifi_password[] = "lpsd2024";
inline constexpr unsigned wifi_retries = 5;
inline constexpr unsigned wifi_attempt_timeout_ms = 30000;
inline constexpr bool load_saved_credentials = true;
inline constexpr bool save_ble_credentials_after_success = true;
// Opt-in fallback after a failed non-default connection. Never overwrites saved credentials.
inline constexpr bool fallback_to_default_wifi = false;
inline constexpr char ble_name[] = "LPSD-ESP32-C6";
inline constexpr bool ble_encrypt_writes = true;
inline constexpr bool ble_advertise_after_disconnect = true;
inline constexpr char ntp_server[] = "pool.ntp.org";
inline constexpr char timezone[] = "CET-1CEST,M3.5.0,M10.5.0/3"; // Austria, automatic DST
inline constexpr bool automatic_light_sleep = false;
inline constexpr int cpu_min_mhz = 40;
inline constexpr int cpu_max_mhz = 160;
// Keep the default interactive. Change in code to try the lecture's sleep experiments.
enum class Mode { Interactive, LightSleepTimerDemo, DeepSleepTimerDemo };
inline constexpr Mode mode = Mode::Interactive;
inline constexpr uint64_t demo_sleep_us = 5000000;
inline constexpr unsigned demo_awake_ms = 10000;
}
