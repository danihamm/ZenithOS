/*
    * Icmp.cpp
    * Internet Control Message Protocol
    * Copyright (c) 2025 Daniel Hammer
*/

#include "Icmp.hpp"
#include <Net/Ipv4.hpp>
#include <Net/ByteOrder.hpp>
#include <Libraries/Memory.hpp>
#include <Terminal/Terminal.hpp>
#include <CppLib/Stream.hpp>

using namespace Kt;

namespace Net::Icmp {

    // Reply tracking for outgoing pings
    static volatile bool    g_replyReceived = false;
    static volatile uint16_t g_replyId  = 0;
    static volatile uint16_t g_replySeq = 0;

    void Initialize() {
        KernelLogStream(OK, "Net") << "ICMP initialized";
    }

    void ResetReply() {
        g_replyReceived = false;
    }

    bool HasReply(uint16_t identifier, uint16_t sequence) {
        return g_replyReceived
            && g_replyId  == identifier
            && g_replySeq == sequence;
    }

    void SendEchoRequest(uint32_t destIp, uint16_t identifier, uint16_t sequence) {
        uint8_t packet[sizeof(Header)];
        Header* hdr = (Header*)packet;

        hdr->Type       = TYPE_ECHO_REQUEST;
        hdr->Code       = 0;
        hdr->Checksum   = 0;
        hdr->Identifier = Htons(identifier);
        hdr->Sequence   = Htons(sequence);

        hdr->Checksum = Ipv4::Checksum(packet, sizeof(Header));

        Ipv4::Send(destIp, Ipv4::PROTO_ICMP, packet, sizeof(Header));
    }

    void OnPacketReceived(uint32_t srcIp, const uint8_t* data, uint16_t length) {
        if (length < sizeof(Header)) {
            return;
        }

        const Header* hdr = (const Header*)data;

        // Verify checksum
        if (Ipv4::Checksum(data, length) != 0) {
            return;
        }

        if (hdr->Type == TYPE_ECHO_REQUEST && hdr->Code == 0) {
            KernelLogStream(INFO, "Net") << "ICMP echo request from "
                << base::dec
                << (uint64_t)(srcIp & 0xFF) << "."
                << (uint64_t)((srcIp >> 8) & 0xFF) << "."
                << (uint64_t)((srcIp >> 16) & 0xFF) << "."
                << (uint64_t)((srcIp >> 24) & 0xFF);

            // Build echo reply -- same payload, different type
            uint8_t reply[1500];
            if (length > sizeof(reply)) {
                return;
            }

            memcpy(reply, data, length);

            Header* replyHdr = (Header*)reply;
            replyHdr->Type = TYPE_ECHO_REPLY;
            replyHdr->Code = 0;
            replyHdr->Checksum = 0;
            replyHdr->Checksum = Ipv4::Checksum(reply, length);

            Ipv4::Send(srcIp, Ipv4::PROTO_ICMP, reply, length);
        } else if (hdr->Type == TYPE_ECHO_REPLY && hdr->Code == 0) {
            g_replyId  = Ntohs(hdr->Identifier);
            g_replySeq = Ntohs(hdr->Sequence);
            g_replyReceived = true;
        }
    }

}
