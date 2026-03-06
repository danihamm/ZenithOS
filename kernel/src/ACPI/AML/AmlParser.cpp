/*
    * AmlParser.cpp
    * Primitive AML bytecode parser for extracting ACPI sleep state values
    * Copyright (c) 2026 Daniel Hammer
*/

#include "AmlParser.hpp"
#include <ACPI/ACPI.hpp>
#include <Terminal/Terminal.hpp>
#include <CppLib/Stream.hpp>

using namespace Kt;

namespace Hal {
    namespace AML {

        // Decode a PkgLength field and return its value.
        // Advances *pos past the PkgLength bytes.
        static uint32_t DecodePkgLength(const uint8_t* aml, uint32_t* pos) {
            uint8_t lead = aml[*pos];
            uint32_t byteCount = (lead >> 6) & 0x03;

            if (byteCount == 0) {
                // Single byte encoding: bits 0-5 are the length
                (*pos)++;
                return lead & 0x3F;
            }

            // Multi-byte: lead bits 0-3 are low nibble, followed by byteCount bytes
            uint32_t length = lead & 0x0F;
            (*pos)++;

            for (uint32_t i = 0; i < byteCount; i++) {
                length |= (uint32_t)aml[*pos] << (4 + 8 * i);
                (*pos)++;
            }

            return length;
        }

        // Decode an AML integer at position *pos.
        // Handles ZeroOp, OneOp, OnesOp, BytePrefix, WordPrefix, DWordPrefix.
        // Returns the decoded value and advances *pos.
        static uint32_t DecodeInteger(const uint8_t* aml, uint32_t* pos) {
            uint8_t op = aml[*pos];

            switch (op) {
                case ZeroOp:
                    (*pos)++;
                    return 0;
                case OneOp:
                    (*pos)++;
                    return 1;
                case OnesOp:
                    (*pos)++;
                    return 0xFFFFFFFF;
                case BytePrefix: {
                    (*pos)++;
                    uint8_t val = aml[*pos];
                    (*pos)++;
                    return val;
                }
                case WordPrefix: {
                    (*pos)++;
                    uint16_t val = aml[*pos] | ((uint16_t)aml[*pos + 1] << 8);
                    *pos += 2;
                    return val;
                }
                case DWordPrefix: {
                    (*pos)++;
                    uint32_t val = aml[*pos]
                                 | ((uint32_t)aml[*pos + 1] << 8)
                                 | ((uint32_t)aml[*pos + 2] << 16)
                                 | ((uint32_t)aml[*pos + 3] << 24);
                    *pos += 4;
                    return val;
                }
                default:
                    // Unknown encoding — treat as zero and skip
                    (*pos)++;
                    return 0;
            }
        }

        S5Object FindS5(void* dsdtData) {
            S5Object result{};
            result.Valid = false;

            auto* header = (ACPI::CommonSDTHeader*)dsdtData;

            if (!ACPI::TestChecksum(header)) {
                KernelLogStream(ERROR, "AML") << "DSDT checksum failed";
                return result;
            }

            // The AML bytecode starts right after the CommonSDTHeader
            const uint8_t* aml = (const uint8_t*)dsdtData;
            uint32_t amlLength = header->Length;
            uint32_t dataStart = sizeof(ACPI::CommonSDTHeader);

            // Scan for the \_S5_ name in the AML stream.
            // We look for the 4-byte sequence '_S5_' preceded by a NameOp (0x08)
            // or preceded by a scope path like '\' (0x5C).
            for (uint32_t i = dataStart; i + 4 < amlLength; i++) {
                if (aml[i] == '_' && aml[i+1] == 'S' && aml[i+2] == '5' && aml[i+3] == '_') {
                    // Verify a valid AML context: either NameOp before it,
                    // or '\' + NameOp pattern, or just the name in a scope
                    bool validContext = false;

                    if (i >= 1 && aml[i-1] == NameOp) {
                        validContext = true;
                    } else if (i >= 2 && aml[i-2] == NameOp && aml[i-1] == '\\') {
                        validContext = true;
                    }

                    if (!validContext)
                        continue;

                    KernelLogStream(OK, "AML") << "Found \\_S5_ object at offset " << base::hex << (uint64_t)i;

                    // Move past the name
                    uint32_t pos = i + 4;

                    // Expect PackageOp
                    if (pos >= amlLength || aml[pos] != PackageOp) {
                        KernelLogStream(ERROR, "AML") << "Expected PackageOp after \\_S5_, got " << base::hex << (uint64_t)aml[pos];
                        continue;
                    }
                    pos++;

                    // Decode package length (we don't actually need the value,
                    // but must advance past it)
                    DecodePkgLength(aml, &pos);

                    // Number of elements in the package
                    if (pos >= amlLength) continue;
                    uint8_t numElements = aml[pos];
                    pos++;

                    if (numElements < 1) {
                        KernelLogStream(ERROR, "AML") << "\\_S5_ package has no elements";
                        continue;
                    }

                    // First element: SLP_TYPa
                    result.SLP_TYPa = (uint16_t)DecodeInteger(aml, &pos);

                    // Second element: SLP_TYPb (if present)
                    if (numElements >= 2 && pos < amlLength) {
                        result.SLP_TYPb = (uint16_t)DecodeInteger(aml, &pos);
                    } else {
                        result.SLP_TYPb = 0;
                    }

                    result.Valid = true;

                    KernelLogStream(OK, "AML") << "SLP_TYPa=" << base::hex << (uint64_t)result.SLP_TYPa
                        << " SLP_TYPb=" << base::hex << (uint64_t)result.SLP_TYPb;

                    return result;
                }
            }

            KernelLogStream(ERROR, "AML") << "\\_S5_ object not found in DSDT";
            return result;
        }

    };
};
