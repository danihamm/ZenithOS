/*
    * NetConfig.hpp
    * Network configuration (static IP, gateway, etc.)
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>

namespace Net {

    // Pack an IPv4 address from four octets (in host visual order: a.b.c.d)
    // Returns in network byte order
    inline uint32_t Ipv4Addr(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
        return (uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24);
    }

    // Get/set the local IP address (network byte order)
    uint32_t GetIpAddress();
    void SetIpAddress(uint32_t ip);

    // Get/set the subnet mask (network byte order)
    uint32_t GetSubnetMask();
    void SetSubnetMask(uint32_t mask);

    // Get/set the default gateway (network byte order)
    uint32_t GetGateway();
    void SetGateway(uint32_t gw);

    // Get/set the DNS server (network byte order)
    uint32_t GetDnsServer();
    void SetDnsServer(uint32_t dns);

    // Check if a destination IP is on the local subnet
    bool IsLocalSubnet(uint32_t destIp);

    // Get the next-hop IP for a given destination
    // Returns destIp if local, or gateway if remote
    uint32_t GetNextHop(uint32_t destIp);

}
