#pragma once
#include <cstddef>
#include <cstring>
struct WifiCredentials {
    char ssid[33]{};
    char password[65]{};
};
inline bool valid_credentials(const WifiCredentials& c) {
    const size_t n = strnlen(c.ssid, sizeof(c.ssid));
    const size_t p = strnlen(c.password, sizeof(c.password));
    if (n == 0 || n > 32 || p > 64) return false;
    if (p == 0 || (p >= 8 && p <= 63)) return true;
    if (p != 64) return false;
    for (size_t i = 0; i < p; ++i) {
        char x = c.password[i];
        if (!((x >= '0' && x <= '9') || (x >= 'a' && x <= 'f') || (x >= 'A' && x <= 'F'))) return false;
    }
    return true;
}
