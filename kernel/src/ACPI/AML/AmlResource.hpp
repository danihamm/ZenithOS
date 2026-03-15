/*
    * AmlResource.hpp
    * ACPI resource descriptor parsing (_CRS, _PRS, _SRS buffers)
    * Copyright (c) 2026 Daniel Hammer
*/

#pragma once
#include <cstdint>

namespace Hal {
    namespace AML {

        // ============================================================================

        // Resource Types

        // ============================================================================
        enum class ResourceType : uint8_t {
            None = 0,
            Irq,
            Dma,
            IoPort,
            FixedIoPort,
            Memory16,
            Memory32,
            Memory32Fixed,
            QWordAddress,
            DWordAddress,
            WordAddress,
            ExtendedIrq,
            GpioConnection,
        };

        // ============================================================================

        // Single Resource Descriptor

        // ============================================================================
        struct ResourceDescriptor {
            ResourceType Type;

            union {
                struct {
                    uint16_t Mask;          // bitmask of supported IRQs
                    uint8_t  Flags;
                    uint8_t  Irq;           // decoded first IRQ number
                } Irq;

                struct {
                    uint32_t Interrupt;     // GSI number
                    uint8_t  Flags;         // edge/level, active high/low
                    bool     Shareable;
                } ExtendedIrq;

                struct {
                    uint8_t  Mask;          // bitmask of supported DMA channels
                    uint8_t  Flags;
                    uint8_t  Channel;       // decoded first channel
                } Dma;

                struct {
                    uint16_t Base;
                    uint16_t Length;
                    uint8_t  Alignment;
                    bool     Decode16Bit;   // true = 16-bit decode, false = 10-bit
                } IoPort;

                struct {
                    uint16_t Base;
                    uint8_t  Length;
                } FixedIoPort;

                struct {
                    uint32_t Base;
                    uint32_t Length;
                    bool     ReadWrite;     // true = R/W, false = read-only
                } Memory32;

                struct {
                    uint64_t Base;
                    uint64_t Length;
                    uint64_t GranularityMin;
                    uint64_t GranularityMax;
                } AddressSpace;
            };

            ResourceDescriptor() : Type(ResourceType::None) {
                // Zero the largest union member
                AddressSpace = {};
            }
        };

        // ============================================================================

        // Parsed Resource List

        // ============================================================================
        static constexpr int MaxResources = 16;

        struct ResourceList {
            ResourceDescriptor Resources[MaxResources];
            int                Count;

            ResourceList() : Count(0) {}

            // Find the first resource of a given type. Returns nullptr if not found.
            const ResourceDescriptor* FindFirst(ResourceType type) const {
                for (int i = 0; i < Count; i++) {
                    if (Resources[i].Type == type)
                        return &Resources[i];
                }
                return nullptr;
            }

            // Get the IRQ number from the first IRQ or ExtendedIrq resource.
            // Returns -1 if no IRQ found.
            int GetIrq() const {
                for (int i = 0; i < Count; i++) {
                    if (Resources[i].Type == ResourceType::Irq)
                        return Resources[i].Irq.Irq;
                    if (Resources[i].Type == ResourceType::ExtendedIrq)
                        return (int)Resources[i].ExtendedIrq.Interrupt;
                }
                return -1;
            }

            // Get the first IO port base and length.
            // Returns false if no IO port found.
            bool GetIoPort(uint16_t& base, uint16_t& length) const {
                for (int i = 0; i < Count; i++) {
                    if (Resources[i].Type == ResourceType::IoPort) {
                        base = Resources[i].IoPort.Base;
                        length = Resources[i].IoPort.Length;
                        return true;
                    }
                    if (Resources[i].Type == ResourceType::FixedIoPort) {
                        base = Resources[i].FixedIoPort.Base;
                        length = Resources[i].FixedIoPort.Length;
                        return true;
                    }
                }
                return false;
            }

            // Get the first memory base and length.
            bool GetMemory(uint64_t& base, uint64_t& length) const {
                for (int i = 0; i < Count; i++) {
                    if (Resources[i].Type == ResourceType::Memory32) {
                        base = Resources[i].Memory32.Base;
                        length = Resources[i].Memory32.Length;
                        return true;
                    }
                    if (Resources[i].Type == ResourceType::QWordAddress ||
                        Resources[i].Type == ResourceType::DWordAddress ||
                        Resources[i].Type == ResourceType::WordAddress) {
                        base = Resources[i].AddressSpace.Base;
                        length = Resources[i].AddressSpace.Length;
                        return true;
                    }
                }
                return false;
            }
        };

        // Parse a resource template buffer (as returned by _CRS evaluation)
        // into a structured ResourceList.
        bool ParseResourceTemplate(const uint8_t* data, uint32_t length, ResourceList& result);

    };
};
