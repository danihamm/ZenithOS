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

    void Initialize() {
        KernelLogStream(OK, "Net") << "ICMP initialized";
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
        }
    }

}
