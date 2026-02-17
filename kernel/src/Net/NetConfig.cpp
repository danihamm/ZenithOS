/*
    * NetConfig.cpp
    * Network configuration (static IP, gateway, etc.)
    * Copyright (c) 2025 Daniel Hammer
*/

#include "NetConfig.hpp"

namespace Net {

    // QEMU user-mode networking defaults
    static uint32_t g_ipAddress  = Ipv4Addr(10, 0, 68, 99);
    static uint32_t g_subnetMask = Ipv4Addr(255, 255, 255, 0);
    static uint32_t g_gateway    = Ipv4Addr(10, 0, 68, 1);

    uint32_t GetIpAddress() { return g_ipAddress; }
    void SetIpAddress(uint32_t ip) { g_ipAddress = ip; }

    uint32_t GetSubnetMask() { return g_subnetMask; }
    void SetSubnetMask(uint32_t mask) { g_subnetMask = mask; }

    uint32_t GetGateway() { return g_gateway; }
    void SetGateway(uint32_t gw) { g_gateway = gw; }

    bool IsLocalSubnet(uint32_t destIp) {
        return (destIp & g_subnetMask) == (g_ipAddress & g_subnetMask);
    }

    uint32_t GetNextHop(uint32_t destIp) {
        if (IsLocalSubnet(destIp)) {
            return destIp;
        }
        return g_gateway;
    }

}
