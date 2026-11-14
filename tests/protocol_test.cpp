#include "protocol.hpp"
#include <cassert>
#include <iostream>
int main() {
    WifiCredentials c{};
    assert(!valid_credentials(c));
    strcpy(c.ssid,"lpsd"); strcpy(c.password,"lpsd2024");
    assert(valid_credentials(c));
    strcpy(c.password,"short"); assert(!valid_credentials(c));
    c.password[0]=0; assert(valid_credentials(c)); // open network
    memset(c.password,'a',64); c.password[64]=0; assert(valid_credentials(c));
    c.password[10]='z'; assert(!valid_credentials(c)); // raw PSK must be hexadecimal
    memset(c.ssid,'s',33); assert(!valid_credentials(c)); // missing terminator
    char out[33];
    uint8_t exact[32]; memset(exact,'s',sizeof(exact));
    assert(protocol::text_field(out,sizeof(out),exact,32));
    assert(strlen(out)==32);
    assert(!protocol::text_field(out,sizeof(out),exact,33));
    uint8_t nul[]={'a',0,'b'};
    assert(!protocol::text_field(out,sizeof(out),nul,3));
    uint8_t rgb[]={0x12,0x34,0xab}; uint32_t color=0;
    assert(protocol::rgb(rgb,3,color) && color==0x1234ab);
    assert(!protocol::rgb(rgb,2,color)); assert(!protocol::rgb(rgb,4,color));
    protocol::PreparedWrite p;
    assert(p.append(10,0,exact,18));
    assert(!p.append(11,18,exact,10)); // different characteristic
    assert(!p.append(10,19,exact,10)); // gap
    assert(!p.append(10,0,exact,10)); // overlap
    assert(p.append(10,18,exact,32));
    assert(!p.append(10,50,exact,15)); // overflow
    assert(p.append(10,50,exact,14));
    assert(p.length==64);
    p.clear(); assert(p.handle==0 && p.length==0);
    for (auto b:p.bytes) assert(b==0);
    std::cout << "PASS: credential bounds, password rules, RGB, prepared-write fragmentation/cancel\n";
}
