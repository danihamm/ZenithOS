/*
    * ByteOrder.hpp
    * Network byte order conversion utilities
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>

namespace Net {

    inline uint16_t Htons(uint16_t host) {
        return (uint16_t)((host >> 8) | (host << 8));
    }

    inline uint16_t Ntohs(uint16_t net) {
        return Htons(net);
    }

    inline uint32_t Htonl(uint32_t host) {
        return ((host >> 24) & 0x000000FF)
             | ((host >>  8) & 0x0000FF00)
             | ((host <<  8) & 0x00FF0000)
             | ((host << 24) & 0xFF000000);
    }

    inline uint32_t Ntohl(uint32_t net) {
        return Htonl(net);
    }

}
