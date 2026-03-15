/*
    * AmlResource.cpp
    * ACPI resource descriptor parsing
    * Copyright (c) 2026 Daniel Hammer
*/

#include "AmlResource.hpp"

namespace Hal {
    namespace AML {

        // ============================================================================

        // Small Resource Tags (bits 6:3 of the tag byte)

        // ============================================================================
        static constexpr uint8_t SmallIrqTag       = 0x04; // IRQ descriptor
        static constexpr uint8_t SmallDmaTag       = 0x05; // DMA descriptor
        static constexpr uint8_t SmallIoPortTag    = 0x08; // I/O port descriptor
        static constexpr uint8_t SmallFixedIoTag   = 0x09; // Fixed I/O port descriptor
        static constexpr uint8_t SmallEndTag       = 0x0F; // End tag

        // ============================================================================

        // Large Resource Tags (byte following the large tag prefix)

        // ============================================================================
        static constexpr uint8_t LargeMemory24Tag  = 0x01;
        static constexpr uint8_t LargeVendorTag    = 0x04;
        static constexpr uint8_t LargeMemory32Tag  = 0x05;
        static constexpr uint8_t LargeMem32FixedTag = 0x06;
        static constexpr uint8_t LargeDWordAddrTag = 0x07;
        static constexpr uint8_t LargeWordAddrTag  = 0x08;
        static constexpr uint8_t LargeExtIrqTag    = 0x09;
        static constexpr uint8_t LargeQWordAddrTag = 0x0A;
        static constexpr uint8_t LargeGpioTag      = 0x0C;

        static uint16_t Read16(const uint8_t* p) {
            return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
        }

        static uint32_t Read32(const uint8_t* p) {
            return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
                 | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        }

        static uint64_t Read64(const uint8_t* p) {
            uint64_t val = 0;
            for (int i = 0; i < 8; i++)
                val |= (uint64_t)p[i] << (i * 8);
            return val;
        }

        // Find the lowest set bit in a mask. Returns the bit number, or -1.
        static int FirstSetBit(uint16_t mask) {
            for (int i = 0; i < 16; i++) {
                if (mask & (1 << i)) return i;
            }
            return -1;
        }

        bool ParseResourceTemplate(const uint8_t* data, uint32_t length, ResourceList& result) {
            result.Count = 0;
            uint32_t pos = 0;

            while (pos < length && result.Count < MaxResources) {
                uint8_t tag = data[pos];

                // End tag
                if ((tag & 0x80) == 0 && ((tag >> 3) & 0x0F) == SmallEndTag)
                    break;

                if (tag & 0x80) {
                    // ============================================================================
                    // Large resource descriptor
                    // ============================================================================
                    uint8_t largeType = tag & 0x7F;
                    if (pos + 3 > length) break;
                    uint16_t resLen = Read16(&data[pos + 1]);
                    uint32_t dataStart = pos + 3;
                    uint32_t dataEnd = dataStart + resLen;
                    if (dataEnd > length) break;

                    auto& res = result.Resources[result.Count];

                    switch (largeType) {
                        case LargeExtIrqTag: {
                            if (resLen < 2) break;
                            res.Type = ResourceType::ExtendedIrq;
                            uint8_t flags = data[dataStart];
                            res.ExtendedIrq.Flags = flags;
                            res.ExtendedIrq.Shareable = (flags >> 3) & 1;
                            uint8_t irqCount = data[dataStart + 1];
                            if (irqCount > 0 && resLen >= 6) {
                                res.ExtendedIrq.Interrupt = Read32(&data[dataStart + 2]);
                            } else {
                                res.ExtendedIrq.Interrupt = 0;
                            }
                            result.Count++;
                            break;
                        }

                        case LargeMemory32Tag: {
                            if (resLen < 17) break;
                            res.Type = ResourceType::Memory32;
                            res.Memory32.ReadWrite = data[dataStart] & 1;
                            res.Memory32.Base = Read32(&data[dataStart + 1]);
                            // Max = data[dataStart + 5..8]
                            // Alignment = data[dataStart + 9..12]
                            res.Memory32.Length = Read32(&data[dataStart + 13]);
                            result.Count++;
                            break;
                        }

                        case LargeMem32FixedTag: {
                            if (resLen < 9) break;
                            res.Type = ResourceType::Memory32;
                            res.Memory32.ReadWrite = data[dataStart] & 1;
                            res.Memory32.Base = Read32(&data[dataStart + 1]);
                            res.Memory32.Length = Read32(&data[dataStart + 5]);
                            result.Count++;
                            break;
                        }

                        case LargeDWordAddrTag: {
                            if (resLen < 23) break;
                            res.Type = ResourceType::DWordAddress;
                            // ResourceType at dataStart+0, GenFlags at +1, TypeFlags at +2
                            res.AddressSpace.GranularityMin = Read32(&data[dataStart + 3]);
                            res.AddressSpace.GranularityMax = Read32(&data[dataStart + 7]);
                            // Min at +7, Max at +11, Translation at +15, Length at +19
                            res.AddressSpace.Base = Read32(&data[dataStart + 7]);
                            res.AddressSpace.Length = Read32(&data[dataStart + 19]);
                            result.Count++;
                            break;
                        }

                        case LargeQWordAddrTag: {
                            if (resLen < 43) break;
                            res.Type = ResourceType::QWordAddress;
                            res.AddressSpace.GranularityMin = Read64(&data[dataStart + 3]);
                            res.AddressSpace.GranularityMax = Read64(&data[dataStart + 11]);
                            res.AddressSpace.Base = Read64(&data[dataStart + 11]);
                            res.AddressSpace.Length = Read64(&data[dataStart + 35]);
                            result.Count++;
                            break;
                        }

                        case LargeWordAddrTag: {
                            if (resLen < 13) break;
                            res.Type = ResourceType::WordAddress;
                            res.AddressSpace.GranularityMin = Read16(&data[dataStart + 3]);
                            res.AddressSpace.GranularityMax = Read16(&data[dataStart + 5]);
                            res.AddressSpace.Base = Read16(&data[dataStart + 5]);
                            res.AddressSpace.Length = Read16(&data[dataStart + 11]);
                            result.Count++;
                            break;
                        }

                        default:
                            // Unknown large descriptor — skip
                            break;
                    }

                    pos = dataEnd;
                } else {
                    // ============================================================================
                    // Small resource descriptor
                    // ============================================================================
                    uint8_t smallType = (tag >> 3) & 0x0F;
                    uint8_t resLen = tag & 0x07;
                    uint32_t dataStart = pos + 1;
                    uint32_t dataEnd = dataStart + resLen;
                    if (dataEnd > length) break;

                    auto& res = result.Resources[result.Count];

                    switch (smallType) {
                        case SmallIrqTag: {
                            if (resLen < 2) break;
                            res.Type = ResourceType::Irq;
                            res.Irq.Mask = Read16(&data[dataStart]);
                            res.Irq.Flags = (resLen >= 3) ? data[dataStart + 2] : 0;
                            int irq = FirstSetBit(res.Irq.Mask);
                            res.Irq.Irq = (irq >= 0) ? (uint8_t)irq : 0;
                            result.Count++;
                            break;
                        }

                        case SmallDmaTag: {
                            if (resLen < 2) break;
                            res.Type = ResourceType::Dma;
                            res.Dma.Mask = data[dataStart];
                            res.Dma.Flags = data[dataStart + 1];
                            int ch = FirstSetBit(res.Dma.Mask);
                            res.Dma.Channel = (ch >= 0) ? (uint8_t)ch : 0;
                            result.Count++;
                            break;
                        }

                        case SmallIoPortTag: {
                            if (resLen < 7) break;
                            res.Type = ResourceType::IoPort;
                            res.IoPort.Decode16Bit = data[dataStart] & 1;
                            res.IoPort.Base = Read16(&data[dataStart + 1]);
                            // Max = data[dataStart + 3..4]
                            res.IoPort.Alignment = data[dataStart + 5];
                            res.IoPort.Length = data[dataStart + 6];
                            result.Count++;
                            break;
                        }

                        case SmallFixedIoTag: {
                            if (resLen < 3) break;
                            res.Type = ResourceType::FixedIoPort;
                            res.FixedIoPort.Base = Read16(&data[dataStart]);
                            res.FixedIoPort.Length = data[dataStart + 2];
                            result.Count++;
                            break;
                        }

                        default:
                            break;
                    }

                    pos = dataEnd;
                }
            }

            return result.Count > 0;
        }

    };
};
