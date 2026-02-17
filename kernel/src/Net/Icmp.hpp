/*
    * Icmp.hpp
    * Internet Control Message Protocol
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>

namespace Net::Icmp {

    constexpr uint8_t TYPE_ECHO_REPLY   = 0;
    constexpr uint8_t TYPE_ECHO_REQUEST = 8;

    struct Header {
        uint8_t  Type;
        uint8_t  Code;
        uint16_t Checksum;
        uint16_t Identifier;
        uint16_t Sequence;
    } __attribute__((packed));

    // Initialize the ICMP subsystem
    void Initialize();

    // Handle an incoming ICMP packet (called by IPv4 layer)
    void OnPacketReceived(uint32_t srcIp, const uint8_t* data, uint16_t length);

}
