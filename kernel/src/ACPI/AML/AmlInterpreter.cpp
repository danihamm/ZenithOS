/*
    * AmlInterpreter.cpp
    * AML bytecode interpreter — loads DSDT/SSDT into namespace, executes methods
    * Copyright (c) 2026 Daniel Hammer
*/

#include "AmlInterpreter.hpp"
#include <ACPI/ACPI.hpp>
#include <Terminal/Terminal.hpp>
#include <CppLib/Stream.hpp>
#include <Io/IoPort.hpp>
#include <Memory/HHDM.hpp>
#include <Libraries/Memory.hpp>

using namespace Kt;

namespace Hal {
    namespace AML {

        // ============================================================================

        // Global instance

        // ============================================================================
        static Interpreter g_interpreter;

        Interpreter& GetInterpreter() {
            return g_interpreter;
        }

        // ============================================================================

        // Helper: is a byte a lead name character?

        // ============================================================================
        static bool IsLeadNameChar(uint8_t c) {
            return (c >= 'A' && c <= 'Z') || c == '_';
        }

        static bool IsNameChar(uint8_t c) {
            return IsLeadNameChar(c) || (c >= '0' && c <= '9');
        }

        // ============================================================================

        // Constructor

        // ============================================================================
        Interpreter::Interpreter()
            : m_dsdt(nullptr), m_dsdtLength(0), m_initialized(false) {}

        // ============================================================================

        // PkgLength decoding

        // ============================================================================
        uint32_t Interpreter::DecodePkgLength(const uint8_t* aml, uint32_t* pos) {
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

        // ============================================================================

        // Integer decoding (data objects)

        // ============================================================================
        uint64_t Interpreter::DecodeInteger(const uint8_t* aml, uint32_t* pos) {
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
                    return 0xFFFFFFFFFFFFFFFFULL;
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
                case QWordPrefix: {
                    (*pos)++;
                    uint64_t val = 0;
                    for (int i = 0; i < 8; i++)
                        val |= (uint64_t)aml[*pos + i] << (8 * i);
                    *pos += 8;
                    return val;
                }
                default:
                    (*pos)++;
                    return 0;
            }
        }

        // ============================================================================

        // NameSeg reading

        // ============================================================================
        void Interpreter::ReadNameSeg(const uint8_t* aml, uint32_t* pos, char* outSeg) {
            for (int i = 0; i < 4; i++) {
                outSeg[i] = (char)aml[*pos + i];
            }
            outSeg[4] = '\0';
            *pos += 4;
        }

        // ============================================================================

        // NameString reading

        // ============================================================================
        // Reads a NameString (which may include root prefix, parent prefixes,
        // dual/multi name prefix) and produces an absolute path.
        int Interpreter::ReadNameString(const uint8_t* aml, uint32_t* pos, int32_t scopeNode,
                                        char* outPath, int maxLen) {
            int pathPos = 0;
            bool isAbsolute = false;
            int parentPrefixCount = 0;

            // Handle root prefix
            if (aml[*pos] == '\\') {
                isAbsolute = true;
                (*pos)++;
            }

            // Handle parent prefixes (^)
            while (aml[*pos] == '^') {
                parentPrefixCount++;
                (*pos)++;
            }

            // Determine segment count
            int segCount = 0;
            char segments[MaxPathDepth][5];

            if (aml[*pos] == 0x00) {
                // NullName — zero segments
                (*pos)++;
                segCount = 0;
            } else if (aml[*pos] == DualNamePrefix) {
                (*pos)++;
                segCount = 2;
                ReadNameSeg(aml, pos, segments[0]);
                ReadNameSeg(aml, pos, segments[1]);
            } else if (aml[*pos] == MultiNamePrefix) {
                (*pos)++;
                segCount = aml[*pos];
                (*pos)++;
                for (int i = 0; i < segCount && i < MaxPathDepth; i++) {
                    ReadNameSeg(aml, pos, segments[i]);
                }
            } else if (IsLeadNameChar(aml[*pos])) {
                segCount = 1;
                ReadNameSeg(aml, pos, segments[0]);
            }

            // Build the path
            if (isAbsolute) {
                outPath[pathPos++] = '\\';
            } else {
                // Build path from scope, walking up for parent prefixes
                int32_t baseScope = scopeNode;
                for (int i = 0; i < parentPrefixCount && baseScope > 0; i++) {
                    auto* node = m_ns.GetNode(baseScope);
                    if (node && node->ParentIndex >= 0)
                        baseScope = node->ParentIndex;
                }
                char scopePath[256];
                m_ns.GetNodePath(baseScope, scopePath, 256);
                int sp = 0;
                while (scopePath[sp] && pathPos < maxLen - 1) {
                    outPath[pathPos++] = scopePath[sp++];
                }
                if (pathPos > 1 && segCount > 0)
                    outPath[pathPos++] = '.';
            }

            // Append segments
            for (int i = 0; i < segCount; i++) {
                if (i > 0 && pathPos < maxLen - 1)
                    outPath[pathPos++] = '.';
                for (int j = 0; j < 4 && pathPos < maxLen - 1; j++)
                    outPath[pathPos++] = segments[i][j];
            }

            outPath[pathPos] = '\0';
            return pathPos;
        }

        // ============================================================================

        // LoadTable

        // ============================================================================
        bool Interpreter::LoadTable(void* tableData) {
            auto* header = (ACPI::CommonSDTHeader*)tableData;

            if (!ACPI::TestChecksum(header)) {
                KernelLogStream(ERROR, "AML") << "Table checksum failed";
                return false;
            }

            char sig[5] = {};
            memcpy(sig, header->Signature, 4);

            m_dsdt = (const uint8_t*)tableData;
            m_dsdtLength = header->Length;

            uint32_t dataStart = sizeof(ACPI::CommonSDTHeader);
            KernelLogStream(OK, "AML") << "Loading " << sig << " table ("
                << base::dec << (uint64_t)(m_dsdtLength - dataStart) << " bytes of AML)";

            // Create standard predefined scopes
            m_ns.CreateNode("\\_GPE");
            m_ns.CreateNode("\\_PR_");
            m_ns.CreateNode("\\_SB_");
            m_ns.CreateNode("\\_SI_");
            m_ns.CreateNode("\\_TZ_");

            bool result = ParseBlock(m_dsdt, dataStart, m_dsdtLength, m_ns.RootIndex());

            m_initialized = true;

            KernelLogStream(OK, "AML") << "Namespace loaded: "
                << base::dec << (uint64_t)m_ns.NodeCount() << " nodes";

            return result;
        }

        // ============================================================================

        // ParseBlock

        // ============================================================================
        // Parse a block of AML opcodes, creating namespace objects.
        bool Interpreter::ParseBlock(const uint8_t* aml, uint32_t offset, uint32_t endOffset,
                                     int32_t scopeNode) {
            uint32_t pos = offset;

            while (pos < endOffset) {
                uint8_t op = aml[pos];

                // Extended opcode
                if (op == ExtOpPrefix) {
                    if (pos + 1 >= endOffset) break;
                    if (!ParseExtendedOp(aml, &pos, endOffset, scopeNode))
                        return false;
                    continue;
                }

                // Named object opcodes
                if (op == NameOp || op == ScopeOp || op == MethodOp ||
                    op == BufferOp || op == PackageOp) {
                    if (!ParseNamedObject(aml, &pos, endOffset, scopeNode))
                        return false;
                    continue;
                }

                // Skip opcodes we don't handle during loading
                // We need to advance past them correctly
                if (op == BytePrefix) { pos += 2; continue; }
                if (op == WordPrefix) { pos += 3; continue; }
                if (op == DWordPrefix) { pos += 5; continue; }
                if (op == QWordPrefix) { pos += 9; continue; }
                if (op == StringPrefix) {
                    pos++;
                    while (pos < endOffset && aml[pos] != 0) pos++;
                    pos++; // skip null terminator
                    continue;
                }
                if (op == ZeroOp || op == OneOp || op == OnesOp || op == NoopOp) {
                    pos++;
                    continue;
                }

                // If/Else/While — skip the block (we only parse structure during load)
                if (op == IfOp || op == ElseOp || op == WhileOp) {
                    pos++;
                    uint32_t pkgStart = pos;
                    uint32_t pkgLen = DecodePkgLength(aml, &pos);
                    uint32_t blockEnd = pkgStart + pkgLen;
                    if (blockEnd > endOffset) blockEnd = endOffset;
                    // Parse inside for nested definitions
                    ParseBlock(aml, pos, blockEnd, scopeNode);
                    pos = blockEnd;
                    continue;
                }

                // Store, arithmetic, logical, etc. — skip during load
                if (op == StoreOp || op == ReturnOp || op == BreakOp ||
                    op == AddOp || op == SubtractOp || op == MultiplyOp ||
                    op == AndOp || op == OrOp || op == XorOp || op == NotOp ||
                    op == ShiftLeftOp || op == ShiftRightOp ||
                    op == IncrementOp || op == DecrementOp ||
                    op == DivideOp || op == IndexOp || op == SizeOfOp ||
                    op == DerefOfOp || op == ConcatOp ||
                    (op >= LAndOp && op <= LLessOp) ||
                    op == ToIntegerOp || op == ToBufferOp) {
                    // Can't easily skip these without parsing — just advance one byte
                    // and hope the next byte resynchronizes. This is imprecise but
                    // works for top-level definitions which are what we need.
                    pos++;
                    continue;
                }

                // Name characters (part of a name reference we don't need during load)
                if (IsLeadNameChar(op) || op == '\\' || op == '^' ||
                    op == DualNamePrefix || op == MultiNamePrefix) {
                    // Skip the name
                    if (op == '\\' || op == '^') { pos++; continue; }
                    if (op == DualNamePrefix) { pos += 9; continue; } // prefix + 2*4
                    if (op == MultiNamePrefix) {
                        uint8_t count = aml[pos + 1];
                        pos += 2 + count * 4;
                        continue;
                    }
                    // Single NameSeg
                    pos += 4;
                    continue;
                }

                // Locals and args
                if ((op >= 0x60 && op <= 0x67) || (op >= 0x68 && op <= 0x6E)) {
                    pos++;
                    continue;
                }

                // Unknown — advance
                pos++;
            }

            return true;
        }

        // ============================================================================

        // ParseNamedObject

        // ============================================================================
        bool Interpreter::ParseNamedObject(const uint8_t* aml, uint32_t* pos, uint32_t endOffset,
                                           int32_t scopeNode) {
            uint8_t op = aml[*pos];
            (*pos)++;

            if (op == ScopeOp) {
                uint32_t pkgStart = *pos;
                uint32_t pkgLen = DecodePkgLength(aml, pos);
                uint32_t blockEnd = pkgStart + pkgLen;
                if (blockEnd > endOffset) blockEnd = endOffset;

                char path[256];
                ReadNameString(aml, pos, scopeNode, path, 256);

                int32_t node = m_ns.CreateNode(path);
                if (node < 0) {
                    *pos = blockEnd;
                    return true;
                }

                ParseBlock(aml, *pos, blockEnd, node);
                *pos = blockEnd;
                return true;
            }

            if (op == NameOp) {
                char path[256];
                ReadNameString(aml, pos, scopeNode, path, 256);

                int32_t node = m_ns.CreateNode(path);
                if (node < 0) return true;

                auto* nsNode = m_ns.GetNode(node);
                if (!nsNode) return true;

                // Parse the data object
                uint8_t dataOp = aml[*pos];

                if (dataOp == ZeroOp || dataOp == OneOp || dataOp == OnesOp ||
                    dataOp == BytePrefix || dataOp == WordPrefix ||
                    dataOp == DWordPrefix || dataOp == QWordPrefix) {
                    nsNode->Obj.Type = ObjectType::Integer;
                    nsNode->Obj.Integer = DecodeInteger(aml, pos);
                } else if (dataOp == StringPrefix) {
                    nsNode->Obj.Type = ObjectType::String;
                    (*pos)++; // skip prefix
                    int i = 0;
                    while (*pos < endOffset && aml[*pos] != 0 && i < MaxStringLen - 1) {
                        nsNode->Obj.String.Data[i++] = (char)aml[*pos];
                        (*pos)++;
                    }
                    nsNode->Obj.String.Data[i] = '\0';
                    nsNode->Obj.String.Length = (uint16_t)i;
                    if (*pos < endOffset) (*pos)++; // skip null
                } else if (dataOp == BufferOp) {
                    nsNode->Obj.Type = ObjectType::Buffer;
                    (*pos)++;
                    uint32_t pkgStart = *pos;
                    uint32_t pkgLen = DecodePkgLength(aml, pos);
                    uint32_t bufEnd = pkgStart + pkgLen;
                    if (bufEnd > endOffset) bufEnd = endOffset;

                    // Buffer size is an integer term
                    uint64_t bufSize = DecodeInteger(aml, pos);
                    if (bufSize > MaxBufferLen) bufSize = MaxBufferLen;
                    nsNode->Obj.Buffer.Length = (uint32_t)bufSize;

                    // Copy initializer data
                    uint32_t initLen = bufEnd - *pos;
                    if (initLen > bufSize) initLen = (uint32_t)bufSize;
                    memcpy(nsNode->Obj.Buffer.Data, &aml[*pos], initLen);

                    *pos = bufEnd;
                } else if (dataOp == PackageOp || dataOp == VarPackageOp) {
                    nsNode->Obj.Type = ObjectType::Package;
                    (*pos)++;
                    uint32_t pkgStart = *pos;
                    uint32_t pkgLen = DecodePkgLength(aml, pos);
                    uint32_t pkgEnd = pkgStart + pkgLen;
                    if (pkgEnd > endOffset) pkgEnd = endOffset;

                    // Store the raw package data as a buffer for later evaluation
                    uint32_t dataLen = pkgEnd - *pos;
                    if (dataLen > MaxBufferLen) dataLen = MaxBufferLen;
                    nsNode->Obj.Buffer.Length = dataLen;
                    memcpy(nsNode->Obj.Buffer.Data, &aml[*pos], dataLen);

                    *pos = pkgEnd;
                } else {
                    // Unknown data object — might be a method call or complex expression.
                    // Just set it as integer 0 for now.
                    nsNode->Obj.Type = ObjectType::Integer;
                    nsNode->Obj.Integer = 0;
                    // Try to skip past a reasonable amount
                    if (IsLeadNameChar(dataOp)) {
                        *pos += 4; // NameSeg reference
                    }
                }

                return true;
            }

            if (op == MethodOp) {
                uint32_t pkgStart = *pos;
                uint32_t pkgLen = DecodePkgLength(aml, pos);
                uint32_t methodEnd = pkgStart + pkgLen;
                if (methodEnd > endOffset) methodEnd = endOffset;

                char path[256];
                ReadNameString(aml, pos, scopeNode, path, 256);

                int32_t node = m_ns.CreateNode(path);
                if (node < 0) {
                    *pos = methodEnd;
                    return true;
                }

                auto* nsNode = m_ns.GetNode(node);
                if (!nsNode) { *pos = methodEnd; return true; }

                // Method flags byte
                uint8_t flags = aml[*pos];
                (*pos)++;

                nsNode->Obj.Type = ObjectType::Method;
                nsNode->Obj.Method.ArgCount = flags & 0x07;
                nsNode->Obj.Method.Serialized = (flags >> 3) & 1;
                nsNode->Obj.Method.AmlOffset = *pos;
                nsNode->Obj.Method.AmlLength = methodEnd - *pos;

                // Don't parse method body during load — it's executed on demand.
                // But we do scan it for nested definitions (devices, scopes).
                ParseBlock(aml, *pos, methodEnd, node);

                *pos = methodEnd;
                return true;
            }

            // Buffer/Package at top level (not as part of a Name)
            if (op == BufferOp || op == PackageOp) {
                uint32_t pkgStart = *pos;
                uint32_t pkgLen = DecodePkgLength(aml, pos);
                uint32_t blockEnd = pkgStart + pkgLen;
                if (blockEnd > endOffset) blockEnd = endOffset;
                *pos = blockEnd;
                return true;
            }

            return true;
        }

        // ============================================================================

        // ParseExtendedOp

        // ============================================================================
        bool Interpreter::ParseExtendedOp(const uint8_t* aml, uint32_t* pos, uint32_t endOffset,
                                          int32_t scopeNode) {
            (*pos)++; // skip ExtOpPrefix
            if (*pos >= endOffset) return true;

            uint8_t extOp = aml[*pos];
            (*pos)++;

            switch (extOp) {
                case DeviceOp: {
                    uint32_t pkgStart = *pos;
                    uint32_t pkgLen = DecodePkgLength(aml, pos);
                    uint32_t blockEnd = pkgStart + pkgLen;
                    if (blockEnd > endOffset) blockEnd = endOffset;

                    char path[256];
                    ReadNameString(aml, pos, scopeNode, path, 256);

                    int32_t node = m_ns.CreateNode(path);
                    if (node >= 0) {
                        auto* nsNode = m_ns.GetNode(node);
                        if (nsNode) nsNode->Obj.Type = ObjectType::Device;
                        ParseBlock(aml, *pos, blockEnd, node);
                    }

                    *pos = blockEnd;
                    return true;
                }

                case OpRegionOp: {
                    char path[256];
                    ReadNameString(aml, pos, scopeNode, path, 256);

                    int32_t node = m_ns.CreateNode(path);
                    if (node >= 0) {
                        auto* nsNode = m_ns.GetNode(node);
                        if (nsNode) {
                            nsNode->Obj.Type = ObjectType::OperationRegion;
                            nsNode->Obj.Region.Space = (RegionSpace)aml[*pos];
                            (*pos)++;
                            nsNode->Obj.Region.Offset = DecodeInteger(aml, pos);
                            nsNode->Obj.Region.Length = DecodeInteger(aml, pos);
                        }
                    }
                    return true;
                }

                case FieldOp: {
                    uint32_t pkgStart = *pos;
                    uint32_t pkgLen = DecodePkgLength(aml, pos);
                    uint32_t fieldEnd = pkgStart + pkgLen;
                    if (fieldEnd > endOffset) fieldEnd = endOffset;

                    // Region name
                    char regionPath[256];
                    ReadNameString(aml, pos, scopeNode, regionPath, 256);
                    int32_t regionNode = m_ns.FindNode(regionPath);
                    if (regionNode < 0) {
                        regionNode = m_ns.ResolveName(regionPath, scopeNode);
                    }

                    // Field flags
                    uint8_t fieldFlags = aml[*pos];
                    (*pos)++;
                    uint8_t accessType = fieldFlags & 0x0F;

                    // Parse field elements
                    uint32_t bitOffset = 0;
                    while (*pos < fieldEnd) {
                        uint8_t fb = aml[*pos];

                        if (fb == 0x00) {
                            // ReservedField — skip bits
                            (*pos)++;
                            uint32_t bits = DecodePkgLength(aml, pos);
                            bitOffset += bits;
                        } else if (fb == 0x01) {
                            // AccessField
                            (*pos)++;
                            (*pos)++; // access type
                            (*pos)++; // access attrib
                        } else if (fb == 0x03) {
                            // ExtendedAccessField
                            (*pos)++;
                            (*pos)++; // access type
                            (*pos)++; // access attrib
                            (*pos)++; // access length
                        } else if (IsLeadNameChar(fb)) {
                            // Named field
                            char fieldSeg[5];
                            ReadNameSeg(aml, pos, fieldSeg);
                            uint32_t bitLen = DecodePkgLength(aml, pos);

                            // Build full path for the field
                            char scopePath[256];
                            m_ns.GetNodePath(scopeNode, scopePath, 256);
                            char fieldPath[256];
                            int fp = 0;
                            int sp = 0;
                            while (scopePath[sp] && fp < 250)
                                fieldPath[fp++] = scopePath[sp++];
                            fieldPath[fp++] = '.';
                            for (int j = 0; j < 4 && fp < 255; j++)
                                fieldPath[fp++] = fieldSeg[j];
                            fieldPath[fp] = '\0';

                            int32_t fieldNode = m_ns.CreateNode(fieldPath);
                            if (fieldNode >= 0) {
                                auto* fn = m_ns.GetNode(fieldNode);
                                if (fn) {
                                    fn->Obj.Type = ObjectType::Field;
                                    fn->Obj.Field.RegionNodeIndex = (uint32_t)regionNode;
                                    fn->Obj.Field.BitOffset = bitOffset;
                                    fn->Obj.Field.BitLength = bitLen;
                                    fn->Obj.Field.AccessType = accessType;
                                }
                            }

                            bitOffset += bitLen;
                        } else {
                            // Unknown field element, skip
                            (*pos)++;
                        }
                    }

                    *pos = fieldEnd;
                    return true;
                }

                case ProcessorOp: {
                    uint32_t pkgStart = *pos;
                    uint32_t pkgLen = DecodePkgLength(aml, pos);
                    uint32_t blockEnd = pkgStart + pkgLen;
                    if (blockEnd > endOffset) blockEnd = endOffset;

                    char path[256];
                    ReadNameString(aml, pos, scopeNode, path, 256);

                    int32_t node = m_ns.CreateNode(path);
                    if (node >= 0) {
                        auto* nsNode = m_ns.GetNode(node);
                        if (nsNode) {
                            nsNode->Obj.Type = ObjectType::Processor;
                            nsNode->Obj.Processor.ProcId = aml[*pos]; (*pos)++;
                            nsNode->Obj.Processor.PblkAddr = aml[*pos]
                                | ((uint32_t)aml[*pos + 1] << 8)
                                | ((uint32_t)aml[*pos + 2] << 16)
                                | ((uint32_t)aml[*pos + 3] << 24);
                            *pos += 4;
                            nsNode->Obj.Processor.PblkLen = aml[*pos]; (*pos)++;
                        }
                        ParseBlock(aml, *pos, blockEnd, node);
                    }

                    *pos = blockEnd;
                    return true;
                }

                case ThermalZoneOp: {
                    uint32_t pkgStart = *pos;
                    uint32_t pkgLen = DecodePkgLength(aml, pos);
                    uint32_t blockEnd = pkgStart + pkgLen;
                    if (blockEnd > endOffset) blockEnd = endOffset;

                    char path[256];
                    ReadNameString(aml, pos, scopeNode, path, 256);

                    int32_t node = m_ns.CreateNode(path);
                    if (node >= 0) {
                        auto* nsNode = m_ns.GetNode(node);
                        if (nsNode) nsNode->Obj.Type = ObjectType::ThermalZone;
                        ParseBlock(aml, *pos, blockEnd, node);
                    }

                    *pos = blockEnd;
                    return true;
                }

                case PowerResOp: {
                    uint32_t pkgStart = *pos;
                    uint32_t pkgLen = DecodePkgLength(aml, pos);
                    uint32_t blockEnd = pkgStart + pkgLen;
                    if (blockEnd > endOffset) blockEnd = endOffset;

                    char path[256];
                    ReadNameString(aml, pos, scopeNode, path, 256);

                    int32_t node = m_ns.CreateNode(path);
                    if (node >= 0) {
                        auto* nsNode = m_ns.GetNode(node);
                        if (nsNode) nsNode->Obj.Type = ObjectType::PowerResource;
                        // Skip SystemLevel and ResourceOrder
                        if (*pos + 3 <= blockEnd) *pos += 3;
                        ParseBlock(aml, *pos, blockEnd, node);
                    }

                    *pos = blockEnd;
                    return true;
                }

                case MutexOp: {
                    char path[256];
                    ReadNameString(aml, pos, scopeNode, path, 256);
                    (*pos)++; // SyncFlags

                    int32_t node = m_ns.CreateNode(path);
                    if (node >= 0) {
                        auto* nsNode = m_ns.GetNode(node);
                        if (nsNode) nsNode->Obj.Type = ObjectType::Mutex;
                    }
                    return true;
                }

                case IndexFieldOp: {
                    // Similar to FieldOp but with index/data register pair
                    uint32_t pkgStart = *pos;
                    uint32_t pkgLen = DecodePkgLength(aml, pos);
                    uint32_t blockEnd = pkgStart + pkgLen;
                    if (blockEnd > endOffset) blockEnd = endOffset;
                    *pos = blockEnd; // Skip for now
                    return true;
                }

                case BankFieldOp: {
                    uint32_t pkgStart = *pos;
                    uint32_t pkgLen = DecodePkgLength(aml, pos);
                    uint32_t blockEnd = pkgStart + pkgLen;
                    if (blockEnd > endOffset) blockEnd = endOffset;
                    *pos = blockEnd;
                    return true;
                }

                // Stall/Sleep at top level (rare)
                case StallOp:
                case SleepOp:
                    DecodeInteger(aml, pos);
                    return true;

                case AcquireOp: {
                    // NameString + Timeout(word)
                    char dummy[256];
                    ReadNameString(aml, pos, scopeNode, dummy, 256);
                    *pos += 2; // timeout
                    return true;
                }

                case ReleaseOp: {
                    char dummy[256];
                    ReadNameString(aml, pos, scopeNode, dummy, 256);
                    return true;
                }

                default:
                    // Unknown extended opcode — skip one byte
                    return true;
            }
        }

        // ============================================================================

        // EvaluateObject

        // ============================================================================
        bool Interpreter::EvaluateObject(const char* path, Object& result) {
            int32_t node = m_ns.FindNode(path);
            if (node < 0) return false;

            auto* nsNode = m_ns.GetNode(node);
            if (!nsNode) return false;

            if (nsNode->Obj.Type == ObjectType::Method) {
                return EvaluateMethod(path, nullptr, 0, result);
            }

            // For non-method objects, just return the stored value
            result = nsNode->Obj;

            // For fields, read the actual hardware value
            if (nsNode->Obj.Type == ObjectType::Field) {
                uint64_t val = 0;
                if (ReadField(node, val)) {
                    result.Type = ObjectType::Integer;
                    result.Integer = val;
                    return true;
                }
                return false;
            }

            return true;
        }

        // ============================================================================

        // EvaluateMethod

        // ============================================================================
        bool Interpreter::EvaluateMethod(const char* path, const Object* args, int argCount,
                                         Object& result) {
            int32_t node = m_ns.FindNode(path);
            if (node < 0) return false;

            auto* nsNode = m_ns.GetNode(node);
            if (!nsNode || nsNode->Obj.Type != ObjectType::Method) return false;

            ExecContext ctx{};
            ctx.Aml = m_dsdt;
            ctx.AmlBase = nsNode->Obj.Method.AmlOffset;
            ctx.AmlLength = nsNode->Obj.Method.AmlLength;
            ctx.ScopeNode = node;
            ctx.Returned = false;
            ctx.Broken = false;
            ctx.Depth = 0;

            // Set up arguments
            for (int i = 0; i < argCount && i < MaxMethodArgs; i++) {
                ctx.Args[i] = args[i];
            }

            // Initialize locals to zero
            for (int i = 0; i < MaxMethodLocals; i++) {
                ctx.Locals[i].Type = ObjectType::Integer;
                ctx.Locals[i].Integer = 0;
            }

            bool ok = ExecuteBlock(ctx, ctx.AmlBase, ctx.AmlBase + ctx.AmlLength);

            result = ctx.ReturnValue;
            if (result.Type == ObjectType::None) {
                result.Type = ObjectType::Integer;
                result.Integer = 0;
            }

            return ok;
        }

        // ============================================================================

        // ExecuteBlock

        // ============================================================================
        bool Interpreter::ExecuteBlock(ExecContext& ctx, uint32_t offset, uint32_t endOffset) {
            uint32_t pos = offset;

            while (pos < endOffset && !ctx.Returned && !ctx.Broken) {
                if (!ExecuteOpcode(ctx, &pos, endOffset))
                    return false;
            }

            return true;
        }

        // ============================================================================

        // ExecuteOpcode

        // ============================================================================
        bool Interpreter::ExecuteOpcode(ExecContext& ctx, uint32_t* pos, uint32_t endOffset) {
            if (*pos >= endOffset) return true;

            uint8_t op = ctx.Aml[*pos];

            // Return
            if (op == ReturnOp) {
                (*pos)++;
                Object val{};
                EvalTerm(ctx, pos, endOffset, val);
                ctx.ReturnValue = val;
                ctx.Returned = true;
                return true;
            }

            // Break
            if (op == BreakOp) {
                (*pos)++;
                ctx.Broken = true;
                return true;
            }

            // Noop
            if (op == NoopOp) {
                (*pos)++;
                return true;
            }

            // If
            if (op == IfOp) {
                (*pos)++;
                uint32_t pkgStart = *pos;
                uint32_t pkgLen = DecodePkgLength(ctx.Aml, pos);
                uint32_t ifEnd = pkgStart + pkgLen;
                if (ifEnd > endOffset) ifEnd = endOffset;

                Object predicate{};
                EvalTerm(ctx, pos, ifEnd, predicate);

                bool condition = (predicate.Type == ObjectType::Integer && predicate.Integer != 0);

                if (condition) {
                    ExecuteBlock(ctx, *pos, ifEnd);
                }

                *pos = ifEnd;

                // Check for Else
                if (*pos < endOffset && ctx.Aml[*pos] == ElseOp) {
                    (*pos)++;
                    uint32_t elsePkgStart = *pos;
                    uint32_t elsePkgLen = DecodePkgLength(ctx.Aml, pos);
                    uint32_t elseEnd = elsePkgStart + elsePkgLen;
                    if (elseEnd > endOffset) elseEnd = endOffset;

                    if (!condition) {
                        ExecuteBlock(ctx, *pos, elseEnd);
                    }

                    *pos = elseEnd;
                }

                return true;
            }

            // While
            if (op == WhileOp) {
                (*pos)++;
                uint32_t pkgStart = *pos;
                uint32_t pkgLen = DecodePkgLength(ctx.Aml, pos);
                uint32_t whileEnd = pkgStart + pkgLen;
                if (whileEnd > endOffset) whileEnd = endOffset;

                uint32_t bodyStart = *pos;

                for (int iter = 0; iter < MaxLoopIterations; iter++) {
                    uint32_t condPos = bodyStart;
                    Object predicate{};
                    EvalTerm(ctx, &condPos, whileEnd, predicate);

                    if (predicate.Type != ObjectType::Integer || predicate.Integer == 0)
                        break;

                    ctx.Broken = false;
                    ExecuteBlock(ctx, condPos, whileEnd);

                    if (ctx.Returned) break;
                    if (ctx.Broken) { ctx.Broken = false; break; }
                }

                *pos = whileEnd;
                return true;
            }

            // Store
            if (op == StoreOp) {
                (*pos)++;
                Object value{};
                EvalTerm(ctx, pos, endOffset, value);

                int32_t targetNode = -1;
                bool isLocal = false, isArg = false;
                int localIdx = 0, argIdx = 0;
                EvalTarget(ctx, pos, targetNode, isLocal, localIdx, isArg, argIdx);
                StoreToTarget(ctx, value, targetNode, isLocal, localIdx, isArg, argIdx);
                return true;
            }

            // Increment/Decrement
            if (op == IncrementOp || op == DecrementOp) {
                (*pos)++;
                int32_t targetNode = -1;
                bool isLocal = false, isArg = false;
                int localIdx = 0, argIdx = 0;
                EvalTarget(ctx, pos, targetNode, isLocal, localIdx, isArg, argIdx);

                Object current{};
                if (isLocal) current = ctx.Locals[localIdx];
                else if (isArg) current = ctx.Args[argIdx];
                else if (targetNode >= 0) {
                    auto* n = m_ns.GetNode(targetNode);
                    if (n) current = n->Obj;
                }

                if (current.Type == ObjectType::Integer) {
                    current.Integer += (op == IncrementOp) ? 1 : -1;
                }
                StoreToTarget(ctx, current, targetNode, isLocal, localIdx, isArg, argIdx);
                return true;
            }

            // Extended opcodes during execution
            if (op == ExtOpPrefix) {
                (*pos)++;
                if (*pos >= endOffset) return true;
                uint8_t extOp = ctx.Aml[*pos];
                (*pos)++;

                // Acquire mutex — just skip
                if (extOp == AcquireOp) {
                    char dummy[256];
                    ReadNameString(ctx.Aml, pos, ctx.ScopeNode, dummy, 256);
                    *pos += 2; // timeout
                    return true;
                }
                // Release mutex — just skip
                if (extOp == ReleaseOp) {
                    char dummy[256];
                    ReadNameString(ctx.Aml, pos, ctx.ScopeNode, dummy, 256);
                    return true;
                }
                // Sleep
                if (extOp == SleepOp) {
                    Object msObj{};
                    EvalTerm(ctx, pos, endOffset, msObj);
                    // We don't actually sleep in the kernel interpreter
                    return true;
                }
                // Stall
                if (extOp == StallOp) {
                    Object usObj{};
                    EvalTerm(ctx, pos, endOffset, usObj);
                    // Brief stall via IO port wait
                    if (usObj.Integer > 0 && usObj.Integer <= 100) {
                        for (uint64_t i = 0; i < usObj.Integer; i++)
                            Io::IoPortWait();
                    }
                    return true;
                }
                // OpRegion/Field/Device during method execution — handle as in ParseExtendedOp
                if (extOp == OpRegionOp || extOp == FieldOp || extOp == DeviceOp ||
                    extOp == ProcessorOp || extOp == ThermalZoneOp || extOp == PowerResOp ||
                    extOp == MutexOp || extOp == IndexFieldOp || extOp == BankFieldOp) {
                    *pos -= 2; // back up to re-parse
                    return ParseExtendedOp(ctx.Aml, pos, endOffset, ctx.ScopeNode);
                }

                return true;
            }

            // Name/Scope/Method definitions can appear inside methods
            if (op == NameOp || op == ScopeOp || op == MethodOp) {
                return ParseNamedObject(ctx.Aml, pos, endOffset, ctx.ScopeNode);
            }

            // Anything else — try to evaluate as an expression and discard the result
            Object discard{};
            return EvalTerm(ctx, pos, endOffset, discard);
        }

        // ============================================================================

        // EvalTerm

        // ============================================================================
        // Evaluate an AML term that produces a value.
        bool Interpreter::EvalTerm(ExecContext& ctx, uint32_t* pos, uint32_t endOffset,
                                   Object& result) {
            if (*pos >= endOffset) return false;

            uint8_t op = ctx.Aml[*pos];

            // Integer constants
            if (op == ZeroOp || op == OneOp || op == OnesOp ||
                op == BytePrefix || op == WordPrefix ||
                op == DWordPrefix || op == QWordPrefix) {
                result.Type = ObjectType::Integer;
                result.Integer = DecodeInteger(ctx.Aml, pos);
                return true;
            }

            // String
            if (op == StringPrefix) {
                (*pos)++;
                result.Type = ObjectType::String;
                int i = 0;
                while (*pos < endOffset && ctx.Aml[*pos] != 0 && i < MaxStringLen - 1) {
                    result.String.Data[i++] = (char)ctx.Aml[*pos];
                    (*pos)++;
                }
                result.String.Data[i] = '\0';
                result.String.Length = (uint16_t)i;
                if (*pos < endOffset) (*pos)++; // null terminator
                return true;
            }

            // Buffer
            if (op == BufferOp) {
                (*pos)++;
                uint32_t pkgStart = *pos;
                uint32_t pkgLen = DecodePkgLength(ctx.Aml, pos);
                uint32_t bufEnd = pkgStart + pkgLen;
                if (bufEnd > endOffset) bufEnd = endOffset;

                Object sizeObj{};
                EvalTerm(ctx, pos, bufEnd, sizeObj);
                uint32_t sz = (uint32_t)sizeObj.Integer;
                if (sz > MaxBufferLen) sz = MaxBufferLen;

                result.Type = ObjectType::Buffer;
                result.Buffer.Length = sz;
                memset(result.Buffer.Data, 0, sz);

                uint32_t initLen = bufEnd - *pos;
                if (initLen > sz) initLen = sz;
                memcpy(result.Buffer.Data, &ctx.Aml[*pos], initLen);

                *pos = bufEnd;
                return true;
            }

            // Package
            if (op == PackageOp) {
                (*pos)++;
                uint32_t pkgStart = *pos;
                uint32_t pkgLen = DecodePkgLength(ctx.Aml, pos);
                uint32_t pkgEnd = pkgStart + pkgLen;
                if (pkgEnd > endOffset) pkgEnd = endOffset;

                // Store raw package as buffer for now
                result.Type = ObjectType::Package;
                uint32_t dataLen = pkgEnd - *pos;
                if (dataLen > MaxBufferLen) dataLen = MaxBufferLen;
                result.Buffer.Length = dataLen;
                memcpy(result.Buffer.Data, &ctx.Aml[*pos], dataLen);

                *pos = pkgEnd;
                return true;
            }

            // Locals (0x60-0x67)
            if (op >= 0x60 && op <= 0x67) {
                int idx = op - 0x60;
                result = ctx.Locals[idx];
                (*pos)++;
                return true;
            }

            // Args (0x68-0x6E)
            if (op >= 0x68 && op <= 0x6E) {
                int idx = op - 0x68;
                result = ctx.Args[idx];
                (*pos)++;
                return true;
            }

            // Logical operators (single-byte opcodes, not behind ExtOpPrefix)
            if (op == LAndOp) {
                (*pos)++;
                Object a{}, b{};
                EvalTerm(ctx, pos, endOffset, a);
                EvalTerm(ctx, pos, endOffset, b);
                result.Type = ObjectType::Integer;
                result.Integer = (a.Integer && b.Integer) ? 1 : 0;
                return true;
            }
            if (op == LOrOp) {
                (*pos)++;
                Object a{}, b{};
                EvalTerm(ctx, pos, endOffset, a);
                EvalTerm(ctx, pos, endOffset, b);
                result.Type = ObjectType::Integer;
                result.Integer = (a.Integer || b.Integer) ? 1 : 0;
                return true;
            }
            if (op == LNotOp) {
                (*pos)++;
                Object a{};
                EvalTerm(ctx, pos, endOffset, a);
                result.Type = ObjectType::Integer;
                result.Integer = (a.Integer == 0) ? 1 : 0;
                return true;
            }
            if (op == LEqualOp) {
                (*pos)++;
                Object a{}, b{};
                EvalTerm(ctx, pos, endOffset, a);
                EvalTerm(ctx, pos, endOffset, b);
                result.Type = ObjectType::Integer;
                result.Integer = (a.Integer == b.Integer) ? 1 : 0;
                return true;
            }
            if (op == LGreaterOp) {
                (*pos)++;
                Object a{}, b{};
                EvalTerm(ctx, pos, endOffset, a);
                EvalTerm(ctx, pos, endOffset, b);
                result.Type = ObjectType::Integer;
                result.Integer = (a.Integer > b.Integer) ? 1 : 0;
                return true;
            }
            if (op == LLessOp) {
                (*pos)++;
                Object a{}, b{};
                EvalTerm(ctx, pos, endOffset, a);
                EvalTerm(ctx, pos, endOffset, b);
                result.Type = ObjectType::Integer;
                result.Integer = (a.Integer < b.Integer) ? 1 : 0;
                return true;
            }

            // Arithmetic / bitwise
            if (op == AddOp || op == SubtractOp || op == MultiplyOp ||
                op == AndOp || op == OrOp || op == XorOp ||
                op == ShiftLeftOp || op == ShiftRightOp ||
                op == NandOp || op == NorOp) {
                (*pos)++;
                Object a{}, b{};
                EvalTerm(ctx, pos, endOffset, a);
                EvalTerm(ctx, pos, endOffset, b);

                result.Type = ObjectType::Integer;
                switch (op) {
                    case AddOp:        result.Integer = a.Integer + b.Integer; break;
                    case SubtractOp:   result.Integer = a.Integer - b.Integer; break;
                    case MultiplyOp:   result.Integer = a.Integer * b.Integer; break;
                    case AndOp:        result.Integer = a.Integer & b.Integer; break;
                    case OrOp:         result.Integer = a.Integer | b.Integer; break;
                    case XorOp:        result.Integer = a.Integer ^ b.Integer; break;
                    case ShiftLeftOp:  result.Integer = a.Integer << b.Integer; break;
                    case ShiftRightOp: result.Integer = a.Integer >> b.Integer; break;
                    case NandOp:       result.Integer = ~(a.Integer & b.Integer); break;
                    case NorOp:        result.Integer = ~(a.Integer | b.Integer); break;
                    default: break;
                }

                // Optional target operand — skip if present
                if (*pos < endOffset) {
                    uint8_t next = ctx.Aml[*pos];
                    if ((next >= 0x60 && next <= 0x67) || (next >= 0x68 && next <= 0x6E)) {
                        // Store result to local/arg
                        int32_t tn = -1;
                        bool il = false, ia = false;
                        int li = 0, ai = 0;
                        EvalTarget(ctx, pos, tn, il, li, ia, ai);
                        StoreToTarget(ctx, result, tn, il, li, ia, ai);
                    } else if (IsLeadNameChar(next) || next == '\\' || next == '^') {
                        int32_t tn = -1;
                        bool il = false, ia = false;
                        int li = 0, ai = 0;
                        EvalTarget(ctx, pos, tn, il, li, ia, ai);
                        StoreToTarget(ctx, result, tn, il, li, ia, ai);
                    }
                    // ZeroOp as "no target" — skip
                    else if (next == ZeroOp) {
                        (*pos)++;
                    }
                }

                return true;
            }

            // Not
            if (op == NotOp) {
                (*pos)++;
                Object a{};
                EvalTerm(ctx, pos, endOffset, a);
                result.Type = ObjectType::Integer;
                result.Integer = ~a.Integer;
                // Optional target
                if (*pos < endOffset && ctx.Aml[*pos] != ZeroOp) {
                    int32_t tn = -1;
                    bool il = false, ia = false;
                    int li = 0, ai = 0;
                    EvalTarget(ctx, pos, tn, il, li, ia, ai);
                    StoreToTarget(ctx, result, tn, il, li, ia, ai);
                } else if (*pos < endOffset && ctx.Aml[*pos] == ZeroOp) {
                    (*pos)++;
                }
                return true;
            }

            // DivideOp: Divide(Dividend, Divisor, Remainder, Result)
            if (op == DivideOp) {
                (*pos)++;
                Object dividend{}, divisor{};
                EvalTerm(ctx, pos, endOffset, dividend);
                EvalTerm(ctx, pos, endOffset, divisor);

                Object remainder{};
                remainder.Type = ObjectType::Integer;
                result.Type = ObjectType::Integer;

                if (divisor.Integer != 0) {
                    remainder.Integer = dividend.Integer % divisor.Integer;
                    result.Integer = dividend.Integer / divisor.Integer;
                } else {
                    remainder.Integer = 0;
                    result.Integer = 0;
                }

                // Remainder target
                if (*pos < endOffset) {
                    int32_t tn = -1;
                    bool il = false, ia = false;
                    int li = 0, ai = 0;
                    EvalTarget(ctx, pos, tn, il, li, ia, ai);
                    StoreToTarget(ctx, remainder, tn, il, li, ia, ai);
                }
                // Result target
                if (*pos < endOffset) {
                    int32_t tn = -1;
                    bool il = false, ia = false;
                    int li = 0, ai = 0;
                    EvalTarget(ctx, pos, tn, il, li, ia, ai);
                    StoreToTarget(ctx, result, tn, il, li, ia, ai);
                }
                return true;
            }

            // SizeOf
            if (op == SizeOfOp) {
                (*pos)++;
                Object obj{};
                EvalTerm(ctx, pos, endOffset, obj);
                result.Type = ObjectType::Integer;
                if (obj.Type == ObjectType::String)
                    result.Integer = obj.String.Length;
                else if (obj.Type == ObjectType::Buffer)
                    result.Integer = obj.Buffer.Length;
                else
                    result.Integer = 0;
                return true;
            }

            // Index
            if (op == IndexOp) {
                (*pos)++;
                Object source{}, index{};
                EvalTerm(ctx, pos, endOffset, source);
                EvalTerm(ctx, pos, endOffset, index);

                result.Type = ObjectType::Integer;
                uint64_t idx = index.Integer;

                if (source.Type == ObjectType::Buffer && idx < source.Buffer.Length) {
                    result.Integer = source.Buffer.Data[idx];
                } else {
                    result.Integer = 0;
                }

                // Optional target
                if (*pos < endOffset && ctx.Aml[*pos] != ZeroOp) {
                    int32_t tn = -1;
                    bool il = false, ia = false;
                    int li = 0, ai = 0;
                    EvalTarget(ctx, pos, tn, il, li, ia, ai);
                    StoreToTarget(ctx, result, tn, il, li, ia, ai);
                } else if (*pos < endOffset && ctx.Aml[*pos] == ZeroOp) {
                    (*pos)++;
                }
                return true;
            }

            // ToInteger
            if (op == ToIntegerOp) {
                (*pos)++;
                Object obj{};
                EvalTerm(ctx, pos, endOffset, obj);
                result.Type = ObjectType::Integer;
                result.Integer = obj.Integer;
                return true;
            }

            // Extended opcodes as terms
            if (op == ExtOpPrefix && *pos + 1 < endOffset) {
                uint8_t extOp = ctx.Aml[*pos + 1];

                // CondRefOf, etc. — not commonly needed, skip
                // For now, return 0
                *pos += 2;
                result.Type = ObjectType::Integer;
                result.Integer = 0;
                return true;
            }

            // Name reference — look up the name and return its value
            if (IsLeadNameChar(op) || op == '\\' || op == '^' ||
                op == DualNamePrefix || op == MultiNamePrefix) {
                char path[256];
                ReadNameString(ctx.Aml, pos, ctx.ScopeNode, path, 256);

                int32_t node = m_ns.FindNode(path);
                if (node < 0) {
                    node = m_ns.ResolveName(path, ctx.ScopeNode);
                }

                if (node >= 0) {
                    auto* nsNode = m_ns.GetNode(node);
                    if (nsNode) {
                        if (nsNode->Obj.Type == ObjectType::Method) {
                            // Method invocation — evaluate arguments
                            Object args[MaxMethodArgs];
                            int argc = nsNode->Obj.Method.ArgCount;
                            for (int i = 0; i < argc && *pos < endOffset; i++) {
                                EvalTerm(ctx, pos, endOffset, args[i]);
                            }

                            // Recursive method call
                            if (ctx.Depth >= MaxCallDepth) {
                                result.Type = ObjectType::Integer;
                                result.Integer = 0;
                                return true;
                            }

                            ExecContext subCtx{};
                            subCtx.Aml = ctx.Aml;
                            subCtx.AmlBase = nsNode->Obj.Method.AmlOffset;
                            subCtx.AmlLength = nsNode->Obj.Method.AmlLength;
                            subCtx.ScopeNode = node;
                            subCtx.Returned = false;
                            subCtx.Broken = false;
                            subCtx.Depth = ctx.Depth + 1;
                            for (int i = 0; i < argc; i++)
                                subCtx.Args[i] = args[i];
                            for (int i = 0; i < MaxMethodLocals; i++) {
                                subCtx.Locals[i].Type = ObjectType::Integer;
                                subCtx.Locals[i].Integer = 0;
                            }

                            ExecuteBlock(subCtx, subCtx.AmlBase, subCtx.AmlBase + subCtx.AmlLength);
                            result = subCtx.ReturnValue;
                            if (result.Type == ObjectType::None) {
                                result.Type = ObjectType::Integer;
                                result.Integer = 0;
                            }
                        } else if (nsNode->Obj.Type == ObjectType::Field) {
                            uint64_t val = 0;
                            ReadField(node, val);
                            result.Type = ObjectType::Integer;
                            result.Integer = val;
                        } else {
                            result = nsNode->Obj;
                        }
                        return true;
                    }
                }

                // Name not found — return 0
                result.Type = ObjectType::Integer;
                result.Integer = 0;
                return true;
            }

            // Unknown opcode — skip and return 0
            (*pos)++;
            result.Type = ObjectType::Integer;
            result.Integer = 0;
            return true;
        }

        // ============================================================================

        // EvalTarget

        // ============================================================================
        bool Interpreter::EvalTarget(ExecContext& ctx, uint32_t* pos,
                                     int32_t& nodeIndex, bool& isLocal, int& localIdx,
                                     bool& isArg, int& argIdx) {
            nodeIndex = -1;
            isLocal = false;
            isArg = false;
            localIdx = 0;
            argIdx = 0;

            if (*pos >= ctx.AmlBase + ctx.AmlLength) return false;

            uint8_t op = ctx.Aml[*pos];

            // ZeroOp = null target (discard)
            if (op == ZeroOp) {
                (*pos)++;
                return true;
            }

            // Local
            if (op >= 0x60 && op <= 0x67) {
                isLocal = true;
                localIdx = op - 0x60;
                (*pos)++;
                return true;
            }

            // Arg
            if (op >= 0x68 && op <= 0x6E) {
                isArg = true;
                argIdx = op - 0x68;
                (*pos)++;
                return true;
            }

            // Named target
            if (IsLeadNameChar(op) || op == '\\' || op == '^' ||
                op == DualNamePrefix || op == MultiNamePrefix) {
                char path[256];
                ReadNameString(ctx.Aml, pos, ctx.ScopeNode, path, 256);
                nodeIndex = m_ns.FindNode(path);
                if (nodeIndex < 0)
                    nodeIndex = m_ns.ResolveName(path, ctx.ScopeNode);
                return true;
            }

            // DerefOf or other complex target — skip
            (*pos)++;
            return true;
        }

        // ============================================================================

        // StoreToTarget

        // ============================================================================
        void Interpreter::StoreToTarget(ExecContext& ctx, const Object& value,
                                        int32_t nodeIndex, bool isLocal, int localIdx,
                                        bool isArg, int argIdx) {
            if (isLocal && localIdx >= 0 && localIdx < MaxMethodLocals) {
                ctx.Locals[localIdx] = value;
            } else if (isArg && argIdx >= 0 && argIdx < MaxMethodArgs) {
                ctx.Args[argIdx] = value;
            } else if (nodeIndex >= 0) {
                auto* node = m_ns.GetNode(nodeIndex);
                if (node) {
                    if (node->Obj.Type == ObjectType::Field && value.Type == ObjectType::Integer) {
                        WriteField(nodeIndex, value.Integer);
                    } else {
                        node->Obj = value;
                    }
                }
            }
        }

        // ============================================================================

        // ReadField

        // ============================================================================
        bool Interpreter::ReadField(int32_t nodeIndex, uint64_t& value) {
            auto* node = m_ns.GetNode(nodeIndex);
            if (!node || node->Obj.Type != ObjectType::Field) return false;

            int32_t regionIdx = (int32_t)node->Obj.Field.RegionNodeIndex;
            auto* regionNode = m_ns.GetNode(regionIdx);
            if (!regionNode || regionNode->Obj.Type != ObjectType::OperationRegion)
                return false;

            uint64_t regBase = regionNode->Obj.Region.Offset;
            uint32_t bitOff = node->Obj.Field.BitOffset;
            uint32_t bitLen = node->Obj.Field.BitLength;

            // Calculate byte-aligned address
            uint64_t byteAddr = regBase + (bitOff / 8);
            uint32_t bitShift = bitOff % 8;

            return ReadRegion(regionNode->Obj.Region.Space, byteAddr, bitLen + bitShift, value);
        }

        // ============================================================================

        // WriteField

        // ============================================================================
        bool Interpreter::WriteField(int32_t nodeIndex, uint64_t value) {
            auto* node = m_ns.GetNode(nodeIndex);
            if (!node || node->Obj.Type != ObjectType::Field) return false;

            int32_t regionIdx = (int32_t)node->Obj.Field.RegionNodeIndex;
            auto* regionNode = m_ns.GetNode(regionIdx);
            if (!regionNode || regionNode->Obj.Type != ObjectType::OperationRegion)
                return false;

            uint64_t regBase = regionNode->Obj.Region.Offset;
            uint32_t bitOff = node->Obj.Field.BitOffset;
            uint32_t bitLen = node->Obj.Field.BitLength;

            uint64_t byteAddr = regBase + (bitOff / 8);
            uint32_t bitShift = bitOff % 8;

            // For sub-byte fields, do read-modify-write
            if (bitShift != 0 || bitLen < 8) {
                uint64_t existing = 0;
                ReadRegion(regionNode->Obj.Region.Space, byteAddr, bitLen + bitShift, existing);
                uint64_t mask = ((1ULL << bitLen) - 1) << bitShift;
                existing = (existing & ~mask) | ((value << bitShift) & mask);
                value = existing;
            }

            return WriteRegion(regionNode->Obj.Region.Space, byteAddr, bitLen + bitShift, value);
        }

        // ============================================================================

        // ReadRegion

        // ============================================================================
        bool Interpreter::ReadRegion(RegionSpace space, uint64_t address, uint32_t bitWidth,
                                     uint64_t& value) {
            value = 0;

            // Round up to access width
            uint32_t accessBytes = (bitWidth + 7) / 8;
            if (accessBytes > 8) accessBytes = 8;

            if (space == RegionSpace::SystemIO) {
                uint16_t port = (uint16_t)address;
                if (accessBytes <= 1)
                    value = Io::In8(port);
                else if (accessBytes <= 2)
                    value = Io::In16(port);
                else
                    value = Io::In32(port);
                return true;
            }

            if (space == RegionSpace::SystemMemory) {
                volatile uint8_t* mmio = (volatile uint8_t*)Memory::HHDM(address);
                if (accessBytes <= 1)
                    value = *mmio;
                else if (accessBytes <= 2)
                    value = *(volatile uint16_t*)mmio;
                else if (accessBytes <= 4)
                    value = *(volatile uint32_t*)mmio;
                else
                    value = *(volatile uint64_t*)mmio;
                return true;
            }

            if (space == RegionSpace::PciConfig) {
                // PCI config space access — address encodes bus/dev/func/offset
                // Not implemented yet; would need PCI config read support
                return false;
            }

            return false;
        }

        // ============================================================================

        // WriteRegion

        // ============================================================================
        bool Interpreter::WriteRegion(RegionSpace space, uint64_t address, uint32_t bitWidth,
                                      uint64_t value) {
            uint32_t accessBytes = (bitWidth + 7) / 8;
            if (accessBytes > 8) accessBytes = 8;

            if (space == RegionSpace::SystemIO) {
                uint16_t port = (uint16_t)address;
                if (accessBytes <= 1)
                    Io::Out8((uint8_t)value, port);
                else if (accessBytes <= 2)
                    Io::Out16((uint16_t)value, port);
                else
                    Io::Out32((uint32_t)value, port);
                return true;
            }

            if (space == RegionSpace::SystemMemory) {
                volatile uint8_t* mmio = (volatile uint8_t*)Memory::HHDM(address);
                if (accessBytes <= 1)
                    *mmio = (uint8_t)value;
                else if (accessBytes <= 2)
                    *(volatile uint16_t*)mmio = (uint16_t)value;
                else if (accessBytes <= 4)
                    *(volatile uint32_t*)mmio = (uint32_t)value;
                else
                    *(volatile uint64_t*)mmio = value;
                return true;
            }

            return false;
        }

    };
};
