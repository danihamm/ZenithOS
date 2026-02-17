/*
    * Ipv4.hpp
    * Internet Protocol version 4
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>

namespace Net::Ipv4 {

    constexpr uint8_t PROTO_ICMP = 1;
    constexpr uint8_t PROTO_TCP  = 6;
    constexpr uint8_t PROTO_UDP  = 17;

    constexpr uint8_t DEFAULT_TTL = 64;

    constexpr uint16_t HEADER_SIZE = 20; // Without options

    struct Header {
        uint8_t  VersionIhl;     // Version (4 bits) + IHL (4 bits)
        uint8_t  Tos;            // Type of Service
        uint16_t TotalLength;
        uint16_t Identification;
        uint16_t FlagsFragment;  // Flags (3 bits) + Fragment Offset (13 bits)
        uint8_t  Ttl;
        uint8_t  Protocol;
        uint16_t Checksum;
        uint32_t SrcIp;
        uint32_t DstIp;
    } __attribute__((packed));

    // Initialize the IPv4 subsystem
    void Initialize();

    // Handle an incoming IP packet (called by Ethernet layer)
    void OnPacketReceived(const uint8_t* data, uint16_t length);

    // Send an IP packet with the given protocol and payload.
    // If ARP resolution is pending, the packet is queued and sent when the reply arrives.
    bool Send(uint32_t destIp, uint8_t protocol, const uint8_t* payload, uint16_t payloadLen);

    // Flush any packets that were waiting for ARP resolution.
    // Called by the ARP layer when a new cache entry is inserted.
    void FlushPending();

    // Compute the Internet checksum over a buffer
    uint16_t Checksum(const void* data, uint16_t length);

    // Compute TCP/UDP pseudo-header checksum
    uint16_t PseudoHeaderChecksum(uint32_t srcIp, uint32_t dstIp, uint8_t protocol,
                                   uint16_t length, const void* data, uint16_t dataLen);

}
