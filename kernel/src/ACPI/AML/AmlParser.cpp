/*
    * AmlParser.cpp
    * AML bytecode parser — S5 extraction (brute-force) and interpreter init
    * Copyright (c) 2026 Daniel Hammer
*/

#include "AmlParser.hpp"
#include "AmlInterpreter.hpp"
#include <ACPI/ACPI.hpp>
#include <Terminal/Terminal.hpp>
#include <CppLib/Stream.hpp>

using namespace Kt;

namespace Hal {
    namespace AML {

        // ── Legacy S5 extraction (brute-force scan) ─────────────────────
        // Kept for fast S5 extraction during early boot before the full
        // interpreter is loaded.

        static constexpr uint8_t NameOp_     = 0x08;
        static constexpr uint8_t PackageOp_  = 0x12;
        static constexpr uint8_t ZeroOp_     = 0x00;
        static constexpr uint8_t OneOp_      = 0x01;
        static constexpr uint8_t OnesOp_     = 0xFF;
        static constexpr uint8_t BytePrefix_ = 0x0A;
        static constexpr uint8_t WordPrefix_ = 0x0B;
        static constexpr uint8_t DWordPrefix_= 0x0C;

        static uint32_t DecodePkgLength(const uint8_t* aml, uint32_t* pos) {
            uint8_t lead = aml[*pos];
            uint32_t byteCount = (lead >> 6) & 0x03;

            if (byteCount == 0) {
                (*pos)++;
                return lead & 0x3F;
            }

            uint32_t length = lead & 0x0F;
            (*pos)++;

            for (uint32_t i = 0; i < byteCount; i++) {
                length |= (uint32_t)aml[*pos] << (4 + 8 * i);
                (*pos)++;
            }

            return length;
        }

        static uint32_t DecodeIntegerLegacy(const uint8_t* aml, uint32_t* pos) {
            uint8_t op = aml[*pos];

            switch (op) {
                case ZeroOp_:
                    (*pos)++;
                    return 0;
                case OneOp_:
                    (*pos)++;
                    return 1;
                case OnesOp_:
                    (*pos)++;
                    return 0xFFFFFFFF;
                case BytePrefix_: {
                    (*pos)++;
                    uint8_t val = aml[*pos];
                    (*pos)++;
                    return val;
                }
                case WordPrefix_: {
                    (*pos)++;
                    uint16_t val = aml[*pos] | ((uint16_t)aml[*pos + 1] << 8);
                    *pos += 2;
                    return val;
                }
                case DWordPrefix_: {
                    (*pos)++;
                    uint32_t val = aml[*pos]
                                 | ((uint32_t)aml[*pos + 1] << 8)
                                 | ((uint32_t)aml[*pos + 2] << 16)
                                 | ((uint32_t)aml[*pos + 3] << 24);
                    *pos += 4;
                    return val;
                }
                default:
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

            const uint8_t* aml = (const uint8_t*)dsdtData;
            uint32_t amlLength = header->Length;
            uint32_t dataStart = sizeof(ACPI::CommonSDTHeader);

            for (uint32_t i = dataStart; i + 4 < amlLength; i++) {
                if (aml[i] == '_' && aml[i+1] == 'S' && aml[i+2] == '5' && aml[i+3] == '_') {
                    bool validContext = false;

                    if (i >= 1 && aml[i-1] == NameOp_) {
                        validContext = true;
                    } else if (i >= 2 && aml[i-2] == NameOp_ && aml[i-1] == '\\') {
                        validContext = true;
                    }

                    if (!validContext)
                        continue;

                    KernelLogStream(OK, "AML") << "Found \\_S5_ object at offset " << base::hex << (uint64_t)i;

                    uint32_t pos = i + 4;

                    if (pos >= amlLength || aml[pos] != PackageOp_) {
                        KernelLogStream(ERROR, "AML") << "Expected PackageOp after \\_S5_, got " << base::hex << (uint64_t)aml[pos];
                        continue;
                    }
                    pos++;

                    DecodePkgLength(aml, &pos);

                    if (pos >= amlLength) continue;
                    uint8_t numElements = aml[pos];
                    pos++;

                    if (numElements < 1) {
                        KernelLogStream(ERROR, "AML") << "\\_S5_ package has no elements";
                        continue;
                    }

                    result.SLP_TYPa = (uint16_t)DecodeIntegerLegacy(aml, &pos);

                    if (numElements >= 2 && pos < amlLength) {
                        result.SLP_TYPb = (uint16_t)DecodeIntegerLegacy(aml, &pos);
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

        // ── Generalized brute-force sleep state scanner ──────────────────
        SleepObject FindSleepState(void* dsdtData, int state) {
            SleepObject result{};
            result.Valid = false;

            if (state < 0 || state > 5) return result;

            auto* header = (ACPI::CommonSDTHeader*)dsdtData;

            if (!ACPI::TestChecksum(header)) {
                KernelLogStream(ERROR, "AML") << "DSDT checksum failed";
                return result;
            }

            // Build the 4-char name we're looking for: _S0_ through _S5_
            char target[4] = { '_', 'S', (char)('0' + state), '_' };

            const uint8_t* aml = (const uint8_t*)dsdtData;
            uint32_t amlLength = header->Length;
            uint32_t dataStart = sizeof(ACPI::CommonSDTHeader);

            for (uint32_t i = dataStart; i + 4 < amlLength; i++) {
                if (aml[i] == target[0] && aml[i+1] == target[1] &&
                    aml[i+2] == target[2] && aml[i+3] == target[3]) {

                    bool validContext = false;
                    if (i >= 1 && aml[i-1] == NameOp_)
                        validContext = true;
                    else if (i >= 2 && aml[i-2] == NameOp_ && aml[i-1] == '\\')
                        validContext = true;

                    if (!validContext) continue;

                    uint32_t pos = i + 4;

                    if (pos >= amlLength || aml[pos] != PackageOp_) continue;
                    pos++;

                    DecodePkgLength(aml, &pos);

                    if (pos >= amlLength) continue;
                    uint8_t numElements = aml[pos];
                    pos++;

                    if (numElements < 1) continue;

                    result.SLP_TYPa = (uint16_t)DecodeIntegerLegacy(aml, &pos);

                    if (numElements >= 2 && pos < amlLength)
                        result.SLP_TYPb = (uint16_t)DecodeIntegerLegacy(aml, &pos);
                    else
                        result.SLP_TYPb = 0;

                    result.Valid = true;

                    KernelLogStream(OK, "AML") << "\\_S" << base::dec << (uint64_t)state
                        << "_ found: SLP_TYPa=" << base::hex << (uint64_t)result.SLP_TYPa
                        << " SLP_TYPb=" << base::hex << (uint64_t)result.SLP_TYPb;

                    return result;
                }
            }

            KernelLogStream(INFO, "AML") << "\\_S" << base::dec << (uint64_t)state
                << "_ not found in DSDT";
            return result;
        }

        // ── Full interpreter initialization ─────────────────────────────
        void InitializeInterpreter(void* dsdtData) {
            auto& interp = GetInterpreter();
            if (!interp.LoadTable(dsdtData)) {
                KernelLogStream(ERROR, "AML") << "Failed to load DSDT into AML interpreter";
            }
        }

    };
};
