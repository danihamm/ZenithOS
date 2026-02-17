/*
    * Ethernet.hpp
    * Ethernet frame layer
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>

namespace Net::Ethernet {

    constexpr uint16_t ETHERTYPE_IPV4 = 0x0800;
    constexpr uint16_t ETHERTYPE_ARP  = 0x0806;

    constexpr uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    constexpr uint16_t HEADER_SIZE = 14;
    constexpr uint16_t MAX_FRAME_SIZE = 1518;
    constexpr uint16_t MAX_PAYLOAD_SIZE = MAX_FRAME_SIZE - HEADER_SIZE;

    struct Header {
        uint8_t  DestMac[6];
        uint8_t  SrcMac[6];
        uint16_t EtherType;
    } __attribute__((packed));

    // Initialize the Ethernet layer (hooks into E1000 RX path)
    void Initialize();

    // Send an Ethernet frame with the given EtherType and payload
    bool Send(const uint8_t* destMac, uint16_t etherType, const uint8_t* payload, uint16_t payloadLen);

    // Called by E1000 RX handler to dispatch received frames
    void OnFrameReceived(const uint8_t* data, uint16_t length);

}
