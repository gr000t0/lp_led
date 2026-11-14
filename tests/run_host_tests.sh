#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_binary=$(mktemp /tmp/lpsd-protocol-test.XXXXXX)
trap 'rm -f "$test_binary"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -Icomponents/device-service -Icomponents/settings-manager \
    tests/protocol_test.cpp -o "$test_binary"
ASAN_OPTIONS=detect_leaks=0 "$test_binary"
