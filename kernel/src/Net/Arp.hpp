/*
    * Arp.hpp
    * Address Resolution Protocol
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>

namespace Net::Arp {

    constexpr uint16_t HW_TYPE_ETHERNET = 1;
    constexpr uint16_t PROTO_TYPE_IPV4  = 0x0800;

    constexpr uint16_t OP_REQUEST = 1;
    constexpr uint16_t OP_REPLY   = 2;

    struct Packet {
        uint16_t HardwareType;
        uint16_t ProtocolType;
        uint8_t  HardwareAddrLen;
        uint8_t  ProtocolAddrLen;
        uint16_t Operation;
        uint8_t  SenderMac[6];
        uint32_t SenderIp;
        uint8_t  TargetMac[6];
        uint32_t TargetIp;
    } __attribute__((packed));

    // Initialize the ARP subsystem
    void Initialize();

    // Handle an incoming ARP packet (called by Ethernet layer)
    void OnPacketReceived(const uint8_t* data, uint16_t length);

    // Resolve an IP address to a MAC address. Returns true if found in cache.
    // If not cached, sends an ARP request and returns false.
    bool Resolve(uint32_t ip, uint8_t* outMac);

    // Send an ARP request for the given IP
    void SendRequest(uint32_t targetIp);

}
