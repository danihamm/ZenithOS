/*
    * AcpiDevices.cpp
    * ACPI device enumeration via AML namespace
    * Copyright (c) 2026 Daniel Hammer
*/

#include "AcpiDevices.hpp"
#include <ACPI/AML/AmlInterpreter.hpp>
#include <Terminal/Terminal.hpp>
#include <CppLib/Stream.hpp>
#include <Libraries/String.hpp>
#include <Libraries/Memory.hpp>

using namespace Kt;

namespace Hal {
    namespace AcpiDevices {

        // ── EISAID decoding ─────────────────────────────────────────────
        // ACPI encodes PNP IDs as compressed 32-bit EISAIDs.
        static void DecodeEisaId(uint32_t id, char* out) {
            // EISA ID encoding:
            // Bits 31-16: 3 compressed letters (5 bits each, '@' based)
            // Bits 15-0:  4 hex digits (product number)
            out[0] = (char)(((id >> 26) & 0x1F) + '@');
            out[1] = (char)(((id >> 21) & 0x1F) + '@');
            out[2] = (char)(((id >> 16) & 0x1F) + '@');

            // Product ID as 4 hex digits
            static const char hex[] = "0123456789ABCDEF";
            out[3] = hex[(id >> 12) & 0xF];
            out[4] = hex[(id >>  8) & 0xF];
            out[5] = hex[(id >>  4) & 0xF];
            out[6] = hex[(id >>  0) & 0xF];
            out[7] = '\0';
        }

        // ── String comparison ───────────────────────────────────────────
        static bool StrEqual(const char* a, const char* b) {
            while (*a && *b) {
                if (*a != *b) return false;
                a++; b++;
            }
            return *a == *b;
        }

        static void StrCopy(char* dst, const char* src, int maxLen) {
            int i = 0;
            while (src[i] && i < maxLen - 1) {
                dst[i] = src[i];
                i++;
            }
            dst[i] = '\0';
        }

        // ── DeviceList methods ──────────────────────────────────────────
        const DeviceInfo* DeviceList::FindByHid(const char* hid) const {
            for (int i = 0; i < Count; i++) {
                if (StrEqual(Devices[i].HardwareId, hid))
                    return &Devices[i];
            }
            return nullptr;
        }

        const DeviceInfo* DeviceList::FindByPath(const char* path) const {
            for (int i = 0; i < Count; i++) {
                if (StrEqual(Devices[i].Path, path))
                    return &Devices[i];
            }
            return nullptr;
        }

        // ── EvaluateSta ─────────────────────────────────────────────────
        uint32_t EvaluateSta(int32_t deviceNodeIndex) {
            auto& interp = AML::GetInterpreter();
            auto& ns = interp.GetNamespace();

            // Look for _STA child
            int32_t staNode = -1;
            ns.ForEachChild(deviceNodeIndex, AML::ObjectType::None,
                [&](int32_t idx, const AML::NamespaceNode* node) -> bool {
                    if (node->Name[0] == '_' && node->Name[1] == 'S' &&
                        node->Name[2] == 'T' && node->Name[3] == 'A') {
                        staNode = idx;
                        return false; // stop
                    }
                    return true;
                });

            if (staNode < 0) return STA_DEFAULT;

            auto* staObj = ns.GetNode(staNode);
            if (!staObj) return STA_DEFAULT;

            if (staObj->Obj.Type == AML::ObjectType::Method) {
                AML::Object result{};
                char path[256];
                ns.GetNodePath(staNode, path, 256);
                if (interp.EvaluateObject(path, result))
                    return (uint32_t)result.Integer;
                return STA_DEFAULT;
            }

            if (staObj->Obj.Type == AML::ObjectType::Integer)
                return (uint32_t)staObj->Obj.Integer;

            return STA_DEFAULT;
        }

        // ── EvaluateAdr ─────────────────────────────────────────────────
        uint64_t EvaluateAdr(int32_t deviceNodeIndex) {
            auto& interp = AML::GetInterpreter();
            auto& ns = interp.GetNamespace();

            int32_t adrNode = -1;
            ns.ForEachChild(deviceNodeIndex, AML::ObjectType::None,
                [&](int32_t idx, const AML::NamespaceNode* node) -> bool {
                    if (node->Name[0] == '_' && node->Name[1] == 'A' &&
                        node->Name[2] == 'D' && node->Name[3] == 'R') {
                        adrNode = idx;
                        return false;
                    }
                    return true;
                });

            if (adrNode < 0) return 0;

            auto* adrObj = ns.GetNode(adrNode);
            if (!adrObj) return 0;

            if (adrObj->Obj.Type == AML::ObjectType::Method) {
                AML::Object result{};
                char path[256];
                ns.GetNodePath(adrNode, path, 256);
                if (interp.EvaluateObject(path, result))
                    return result.Integer;
                return 0;
            }

            if (adrObj->Obj.Type == AML::ObjectType::Integer)
                return adrObj->Obj.Integer;

            return 0;
        }

        // ── EvaluateHid ─────────────────────────────────────────────────
        bool EvaluateHid(int32_t deviceNodeIndex, char* outHid, int maxLen) {
            auto& interp = AML::GetInterpreter();
            auto& ns = interp.GetNamespace();

            int32_t hidNode = -1;
            ns.ForEachChild(deviceNodeIndex, AML::ObjectType::None,
                [&](int32_t idx, const AML::NamespaceNode* node) -> bool {
                    if (node->Name[0] == '_' && node->Name[1] == 'H' &&
                        node->Name[2] == 'I' && node->Name[3] == 'D') {
                        hidNode = idx;
                        return false;
                    }
                    return true;
                });

            if (hidNode < 0) return false;

            AML::Object result{};
            char path[256];
            ns.GetNodePath(hidNode, path, 256);

            if (!interp.EvaluateObject(path, result))
                return false;

            if (result.Type == AML::ObjectType::Integer) {
                // EISA ID
                DecodeEisaId((uint32_t)result.Integer, outHid);
                return true;
            }

            if (result.Type == AML::ObjectType::String) {
                StrCopy(outHid, result.String.Data, maxLen);
                return true;
            }

            return false;
        }

        // ── EvaluateUid ─────────────────────────────────────────────────
        bool EvaluateUid(int32_t deviceNodeIndex, char* outUid, int maxLen) {
            auto& interp = AML::GetInterpreter();
            auto& ns = interp.GetNamespace();

            int32_t uidNode = -1;
            ns.ForEachChild(deviceNodeIndex, AML::ObjectType::None,
                [&](int32_t idx, const AML::NamespaceNode* node) -> bool {
                    if (node->Name[0] == '_' && node->Name[1] == 'U' &&
                        node->Name[2] == 'I' && node->Name[3] == 'D') {
                        uidNode = idx;
                        return false;
                    }
                    return true;
                });

            if (uidNode < 0) return false;

            AML::Object result{};
            char path[256];
            ns.GetNodePath(uidNode, path, 256);

            if (!interp.EvaluateObject(path, result))
                return false;

            if (result.Type == AML::ObjectType::Integer) {
                // Convert integer to string
                uint64_t val = result.Integer;
                char buf[21];
                int i = 0;
                if (val == 0) {
                    buf[i++] = '0';
                } else {
                    char tmp[21];
                    int t = 0;
                    while (val > 0) {
                        tmp[t++] = '0' + (val % 10);
                        val /= 10;
                    }
                    for (int j = t - 1; j >= 0; j--)
                        buf[i++] = tmp[j];
                }
                buf[i] = '\0';
                StrCopy(outUid, buf, maxLen);
                return true;
            }

            if (result.Type == AML::ObjectType::String) {
                StrCopy(outUid, result.String.Data, maxLen);
                return true;
            }

            return false;
        }

        // ── EvaluateCrs ─────────────────────────────────────────────────
        bool EvaluateCrs(int32_t deviceNodeIndex, AML::ResourceList& result) {
            auto& interp = AML::GetInterpreter();
            auto& ns = interp.GetNamespace();

            int32_t crsNode = -1;
            ns.ForEachChild(deviceNodeIndex, AML::ObjectType::None,
                [&](int32_t idx, const AML::NamespaceNode* node) -> bool {
                    if (node->Name[0] == '_' && node->Name[1] == 'C' &&
                        node->Name[2] == 'R' && node->Name[3] == 'S') {
                        crsNode = idx;
                        return false;
                    }
                    return true;
                });

            if (crsNode < 0) return false;

            AML::Object crsResult{};
            char path[256];
            ns.GetNodePath(crsNode, path, 256);

            if (!interp.EvaluateObject(path, crsResult))
                return false;

            if (crsResult.Type != AML::ObjectType::Buffer)
                return false;

            return AML::ParseResourceTemplate(crsResult.Buffer.Data, crsResult.Buffer.Length, result);
        }

        // ── EnumerateAll ────────────────────────────────────────────────
        void EnumerateAll(DeviceList& result) {
            auto& interp = AML::GetInterpreter();
            if (!interp.IsInitialized()) return;

            auto& ns = interp.GetNamespace();
            result.Count = 0;

            // Walk all namespace nodes looking for Device objects
            ns.WalkDescendants(ns.RootIndex(), AML::ObjectType::Device,
                [&](int32_t idx, [[maybe_unused]] const AML::NamespaceNode* node) {
                    if (result.Count >= MaxDevices) return;

                    auto& dev = result.Devices[result.Count];
                    dev.NodeIndex = idx;

                    // Get the path
                    ns.GetNodePath(idx, dev.Path, 128);

                    // Evaluate _STA
                    dev.Status = EvaluateSta(idx);
                    dev.IsPresent = (dev.Status & STA_PRESENT) && (dev.Status & STA_FUNCTIONAL);

                    // Evaluate _HID
                    if (!EvaluateHid(idx, dev.HardwareId, 16))
                        dev.HardwareId[0] = '\0';

                    // Evaluate _UID
                    if (!EvaluateUid(idx, dev.UniqueId, 16))
                        dev.UniqueId[0] = '\0';

                    // Evaluate _ADR
                    dev.Address = EvaluateAdr(idx);

                    result.Count++;
                });

            KernelLogStream(OK, "ACPI") << "Enumerated " << base::dec
                << (uint64_t)result.Count << " ACPI devices";

            // Log discovered devices
            for (int i = 0; i < result.Count; i++) {
                auto& dev = result.Devices[i];
                if (dev.HardwareId[0]) {
                    KernelLogStream(DEBUG, "ACPI") << "  " << dev.Path
                        << " HID=" << dev.HardwareId
                        << (dev.IsPresent ? " [present]" : " [not present]");
                } else if (dev.Address != 0) {
                    KernelLogStream(DEBUG, "ACPI") << "  " << dev.Path
                        << " ADR=" << base::hex << dev.Address
                        << (dev.IsPresent ? " [present]" : " [not present]");
                }
            }
        }

        // ── GetSleepState ───────────────────────────────────────────────
        SleepState GetSleepState(int state) {
            SleepState result{};
            result.Valid = false;

            if (state < 0 || state > 5) return result;

            char path[8] = "\\_Sx_";
            path[2] = 'S';
            path[3] = '0' + (char)state;
            path[4] = '_';
            path[5] = '\0';

            auto& interp = AML::GetInterpreter();
            AML::Object obj{};

            if (!interp.EvaluateObject(path, obj))
                return result;

            // The sleep state object should be a Package with at least 2 integers.
            // During loading, we stored Package data as a raw buffer.
            // For the common case, the \_S5_ was already extracted by the old parser.
            // Try to get it from namespace directly.
            auto& ns = interp.GetNamespace();
            int32_t node = ns.FindNode(path);
            if (node < 0) return result;

            auto* nsNode = ns.GetNode(node);
            if (!nsNode) return result;

            if (nsNode->Obj.Type == AML::ObjectType::Package) {
                // Parse the raw package buffer to extract SLP_TYP values
                const uint8_t* data = nsNode->Obj.Buffer.Data;
                uint32_t len = nsNode->Obj.Buffer.Length;
                if (len < 2) return result;

                // NumElements
                uint8_t numElements = data[0];
                uint32_t pos = 1;

                if (numElements >= 1 && pos < len) {
                    // Decode first element
                    uint8_t elem0 = data[pos];
                    if (elem0 == AML::ZeroOp) { result.SLP_TYPa = 0; pos++; }
                    else if (elem0 == AML::OneOp) { result.SLP_TYPa = 1; pos++; }
                    else if (elem0 == AML::BytePrefix && pos + 1 < len) { result.SLP_TYPa = data[pos + 1]; pos += 2; }
                    else { pos++; }
                }

                if (numElements >= 2 && pos < len) {
                    uint8_t elem1 = data[pos];
                    if (elem1 == AML::ZeroOp) { result.SLP_TYPb = 0; pos++; }
                    else if (elem1 == AML::OneOp) { result.SLP_TYPb = 1; pos++; }
                    else if (elem1 == AML::BytePrefix && pos + 1 < len) { result.SLP_TYPb = data[pos + 1]; pos += 2; }
                    else { pos++; }
                }

                result.Valid = true;
            } else if (nsNode->Obj.Type == AML::ObjectType::Integer) {
                result.SLP_TYPa = (uint16_t)nsNode->Obj.Integer;
                result.SLP_TYPb = 0;
                result.Valid = true;
            }

            return result;
        }

    };
};
