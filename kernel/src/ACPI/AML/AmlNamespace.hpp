/*
    * AmlNamespace.hpp
    * AML object types and ACPI namespace tree
    * Copyright (c) 2026 Daniel Hammer
*/

#pragma once
#include <cstdint>
#include <Libraries/Memory.hpp>

namespace Hal {
    namespace AML {

        // ============================================================================

        // AML Object Types

        // ============================================================================
        enum class ObjectType : uint8_t {
            None = 0,
            Integer,
            String,
            Buffer,
            Package,
            Device,
            Method,
            OperationRegion,
            Field,
            Mutex,
            Processor,
            ThermalZone,
            PowerResource,
            BufferField,
        };

        // ============================================================================

        // Region address spaces (OperationRegion)

        // ============================================================================
        enum class RegionSpace : uint8_t {
            SystemMemory    = 0x00,
            SystemIO        = 0x01,
            PciConfig       = 0x02,
            EmbeddedControl = 0x03,
            SMBus           = 0x04,
            CMOS            = 0x05,
            PciBarTarget    = 0x06,
        };

        // ============================================================================

        // Constants

        // ============================================================================
        static constexpr int MaxNameSegLen     = 4;
        static constexpr int MaxPathDepth      = 16;
        static constexpr int MaxChildren       = 32;
        static constexpr int MaxStringLen      = 64;
        static constexpr int MaxBufferLen      = 256;
        static constexpr int MaxPackageElements = 16;
        static constexpr int MaxMethodArgs     = 7;
        static constexpr int MaxMethodLocals   = 8;
        static constexpr int MaxNamespaceNodes = 256;

        // ============================================================================

        // AML Object

        // ============================================================================
        // Tagged union representing any AML value. Kept small for kernel use.
        struct Object {
            ObjectType Type = ObjectType::None;

            union {
                uint64_t Integer;

                struct {
                    char     Data[MaxStringLen];
                    uint16_t Length;
                } String;

                struct {
                    uint8_t  Data[MaxBufferLen];
                    uint32_t Length;
                } Buffer;

                struct {
                    uint8_t  ArgCount;    // bits 0-2 of method flags
                    bool     Serialized;  // bit 3
                    uint32_t AmlOffset;   // offset into DSDT AML where the method body starts
                    uint32_t AmlLength;   // length of the method body
                } Method;

                struct {
                    RegionSpace Space;
                    uint64_t    Offset;
                    uint64_t    Length;
                } Region;

                struct {
                    uint32_t RegionNodeIndex; // index of the parent OperationRegion node
                    uint32_t BitOffset;
                    uint32_t BitLength;
                    uint8_t  AccessType;      // 0=Any, 1=Byte, 2=Word, 3=DWord, 4=QWord, 5=Buffer
                } Field;

                struct {
                    uint8_t  ProcId;
                    uint32_t PblkAddr;
                    uint8_t  PblkLen;
                } Processor;
            };

            Object() : Type(ObjectType::None), Integer(0) {}
        };

        // ============================================================================

        // Namespace Node

        // ============================================================================
        // Each node has a 4-char name segment and an associated object.
        struct NamespaceNode {
            char         Name[MaxNameSegLen + 1]; // null-terminated 4-char segment
            Object       Obj;
            int32_t      ParentIndex;             // -1 for root
            int32_t      ChildIndices[MaxChildren];
            int32_t      ChildCount;

            void Clear() {
                Name[0] = 0;
                Obj = Object{};
                ParentIndex = -1;
                ChildCount = 0;
                for (int i = 0; i < MaxChildren; i++)
                    ChildIndices[i] = -1;
            }
        };

        // ============================================================================

        // Namespace

        // ============================================================================
        // Flat array of nodes forming a tree via parent/child indices.
        class Namespace {
        public:
            Namespace();

            // Create or find a node at the given absolute path (e.g. "\\_SB_.PCI0").
            // Returns the node index, or -1 on failure.
            int32_t CreateNode(const char* absolutePath);

            // Find a node by absolute path. Returns index or -1.
            int32_t FindNode(const char* absolutePath) const;

            // Find a node relative to a scope. Tries:
            //   1. scopePath + name
            //   2. Walk up parent scopes
            //   3. Root scope
            int32_t ResolveName(const char* name, int32_t scopeNodeIndex) const;

            // Get a node by index.
            NamespaceNode* GetNode(int32_t index);
            const NamespaceNode* GetNode(int32_t index) const;

            // Get the root node index (always 0).
            int32_t RootIndex() const { return 0; }

            // Build the absolute path of a node into outBuf. Returns outBuf.
            char* GetNodePath(int32_t index, char* outBuf, int maxLen) const;

            // Get the number of nodes in the namespace.
            int32_t NodeCount() const { return m_nodeCount; }

            // Iterate children of a node matching a given object type.
            // callback returns true to continue, false to stop.
            // Returns the index of the node that stopped iteration, or -1.
            template<typename Fn>
            int32_t ForEachChild(int32_t parentIndex, ObjectType type, Fn callback) const {
                auto* parent = GetNode(parentIndex);
                if (!parent) return -1;
                for (int32_t i = 0; i < parent->ChildCount; i++) {
                    int32_t ci = parent->ChildIndices[i];
                    auto* child = GetNode(ci);
                    if (!child) continue;
                    if (type != ObjectType::None && child->Obj.Type != type) continue;
                    if (!callback(ci, child)) return ci;
                }
                return -1;
            }

            // Recursively find all descendants of a given type.
            template<typename Fn>
            void WalkDescendants(int32_t nodeIndex, ObjectType type, Fn callback) const {
                auto* node = GetNode(nodeIndex);
                if (!node) return;
                for (int32_t i = 0; i < node->ChildCount; i++) {
                    int32_t ci = node->ChildIndices[i];
                    auto* child = GetNode(ci);
                    if (!child) continue;
                    if (type == ObjectType::None || child->Obj.Type == type)
                        callback(ci, child);
                    WalkDescendants(ci, type, callback);
                }
            }

        private:
            int32_t AllocNode();
            int32_t FindChildByName(int32_t parentIndex, const char* seg) const;

            // Parse an absolute path into segments. Returns number of segments.
            static int ParsePath(const char* path, char segments[][MaxNameSegLen + 1], int maxSegments);
            static bool SegmentEqual(const char* a, const char* b);
            static void PadSegment(const char* src, char* dst); // pad to 4 chars with '_'

            NamespaceNode m_nodes[MaxNamespaceNodes];
            int32_t       m_nodeCount;
        };

    };
};
