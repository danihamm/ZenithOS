/*
    * Dns.hpp
    * DNS resolver (kernel-level)
    * Copyright (c) 2025-2026 Daniel Hammer
*/

#pragma once
#include <cstdint>

namespace Net::Dns {

    // Resolve a hostname to an IPv4 address.
    // Returns the IP in network byte order, or 0 on failure.
    uint32_t Resolve(const char* hostname, uint32_t timeoutMs = 5000);

}
