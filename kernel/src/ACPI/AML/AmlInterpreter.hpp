/*
    * AmlInterpreter.hpp
    * AML bytecode interpreter — parses DSDT/SSDT into the ACPI namespace
    * and evaluates methods, fields, and device status
    * Copyright (c) 2026 Daniel Hammer
*/

#pragma once
#include "AmlNamespace.hpp"
#include <cstdint>

namespace Hal {
    namespace AML {

        // ============================================================================

        // Extended AML Opcodes

        // ============================================================================
        // Single-byte opcodes
        static constexpr uint8_t  ZeroOp        = 0x00;
        static constexpr uint8_t  OneOp         = 0x01;
        static constexpr uint8_t  AliasOp       = 0x06;
        static constexpr uint8_t  NameOp        = 0x08;
        static constexpr uint8_t  BytePrefix    = 0x0A;
        static constexpr uint8_t  WordPrefix    = 0x0B;
        static constexpr uint8_t  DWordPrefix   = 0x0C;
        static constexpr uint8_t  StringPrefix  = 0x0D;
        static constexpr uint8_t  QWordPrefix   = 0x0E;
        static constexpr uint8_t  ScopeOp       = 0x10;
        static constexpr uint8_t  BufferOp      = 0x11;
        static constexpr uint8_t  PackageOp     = 0x12;
        static constexpr uint8_t  VarPackageOp  = 0x13;
        static constexpr uint8_t  MethodOp      = 0x14;
        static constexpr uint8_t  DualNamePrefix = 0x2E;
        static constexpr uint8_t  MultiNamePrefix = 0x2F;
        static constexpr uint8_t  LocalPrefix   = 0x60; // Local0..Local7 = 0x60..0x67
        static constexpr uint8_t  ArgPrefix     = 0x68; // Arg0..Arg6 = 0x68..0x6E
        static constexpr uint8_t  StoreOp       = 0x70;
        static constexpr uint8_t  AddOp         = 0x72;
        static constexpr uint8_t  SubtractOp    = 0x74;
        static constexpr uint8_t  MultiplyOp    = 0x77;
        static constexpr uint8_t  ShiftLeftOp   = 0x79;
        static constexpr uint8_t  ShiftRightOp  = 0x7A;
        static constexpr uint8_t  AndOp         = 0x7B;
        static constexpr uint8_t  NandOp        = 0x7C;
        static constexpr uint8_t  OrOp          = 0x7D;
        static constexpr uint8_t  NorOp         = 0x7E;
        static constexpr uint8_t  XorOp         = 0x7F;
        static constexpr uint8_t  NotOp         = 0x80;
        static constexpr uint8_t  DerefOfOp     = 0x83;
        static constexpr uint8_t  SizeOfOp      = 0x87;
        static constexpr uint8_t  IndexOp       = 0x88;
        static constexpr uint8_t  CreateDWordFieldOp = 0x8A;
        static constexpr uint8_t  CreateWordFieldOp  = 0x8B;
        static constexpr uint8_t  CreateByteFieldOp  = 0x8C;
        static constexpr uint8_t  CreateBitFieldOp   = 0x8D;
        static constexpr uint8_t  OnesOp        = 0xFF;
        static constexpr uint8_t  ReturnOp      = 0xA4;
        static constexpr uint8_t  BreakOp       = 0xA5;
        static constexpr uint8_t  IfOp          = 0xA0;
        static constexpr uint8_t  ElseOp        = 0xA1;
        static constexpr uint8_t  WhileOp       = 0xA2;
        static constexpr uint8_t  NoopOp        = 0xA3;
        static constexpr uint8_t  ConcatOp      = 0x73;
        static constexpr uint8_t  ToIntegerOp   = 0x99;
        static constexpr uint8_t  ToBufferOp    = 0x96;
        static constexpr uint8_t  RevisionOp    = 0x30; // not a real AML op, used internally

        // ExtOp prefix (0x5B) followed by second byte
        static constexpr uint8_t  ExtOpPrefix   = 0x5B;
        static constexpr uint8_t  MutexOp       = 0x01; // after ExtOpPrefix
        static constexpr uint8_t  EventOp       = 0x02;
        static constexpr uint8_t  OpRegionOp    = 0x80;
        static constexpr uint8_t  FieldOp       = 0x81;
        static constexpr uint8_t  DeviceOp      = 0x82;
        static constexpr uint8_t  ProcessorOp   = 0x83;
        static constexpr uint8_t  PowerResOp    = 0x84;
        static constexpr uint8_t  ThermalZoneOp = 0x85;
        static constexpr uint8_t  IndexFieldOp  = 0x86;
        static constexpr uint8_t  BankFieldOp   = 0x87;
        static constexpr uint8_t  AcquireOp     = 0x23;
        static constexpr uint8_t  ReleaseOp     = 0x27;
        static constexpr uint8_t  SleepOp       = 0x22;
        static constexpr uint8_t  StallOp       = 0x21;
        static constexpr uint8_t  LNotOp        = 0x92; // single-byte, actually
        static constexpr uint8_t  LEqualOp      = 0x93; // single-byte
        static constexpr uint8_t  LGreaterOp    = 0x94; // single-byte
        static constexpr uint8_t  LLessOp       = 0x95; // single-byte
        static constexpr uint8_t  LAndOp        = 0x90; // single-byte
        static constexpr uint8_t  LOrOp         = 0x91; // single-byte
        static constexpr uint8_t  IncrementOp   = 0x75;
        static constexpr uint8_t  DecrementOp   = 0x76;
        static constexpr uint8_t  DivideOp      = 0x78;
        static constexpr uint8_t  ModOp         = 0x85;
        static constexpr uint8_t  ConcatResOp   = 0x84;
        static constexpr uint8_t  ToHexStringOp = 0x98;
        static constexpr uint8_t  ToDecimalStringOp = 0x97;

        // ============================================================================

        // Interpreter Configuration

        // ============================================================================
        static constexpr int MaxCallDepth       = 16;
        static constexpr int MaxLoopIterations  = 1024;

        // ============================================================================

        // Interpreter

        // ============================================================================
        class Interpreter {
        public:
            Interpreter();

            // Parse a DSDT or SSDT table into the namespace.
            // tableData points to the CommonSDTHeader (HHDM-mapped).
            bool LoadTable(void* tableData);

            // Evaluate a named object, returning its value.
            // For methods, executes them with no arguments.
            bool EvaluateObject(const char* path, Object& result);

            // Evaluate a method with arguments.
            bool EvaluateMethod(const char* path, const Object* args, int argCount, Object& result);

            // Read a field value. Returns integer.
            bool ReadField(int32_t nodeIndex, uint64_t& value);

            // Write a field value.
            bool WriteField(int32_t nodeIndex, uint64_t value);

            // Get the namespace for direct queries.
            Namespace& GetNamespace() { return m_ns; }
            const Namespace& GetNamespace() const { return m_ns; }

            // Check if the interpreter has been initialized
            bool IsInitialized() const { return m_initialized; }

        private:
            // ============================================================================
            // Parsing (table load)
            // ============================================================================
            bool ParseBlock(const uint8_t* aml, uint32_t offset, uint32_t endOffset,
                            int32_t scopeNode);

            bool ParseNamedObject(const uint8_t* aml, uint32_t* pos, uint32_t endOffset,
                                  int32_t scopeNode);

            bool ParseExtendedOp(const uint8_t* aml, uint32_t* pos, uint32_t endOffset,
                                 int32_t scopeNode);

            // ============================================================================

            // Name resolution

            // ============================================================================
            // Read a NameString from AML and produce an absolute path.
            // Advances *pos past the name.
            int ReadNameString(const uint8_t* aml, uint32_t* pos, int32_t scopeNode,
                               char* outPath, int maxLen);

            // Read a single 4-char NameSeg from AML. Advances *pos.
            void ReadNameSeg(const uint8_t* aml, uint32_t* pos, char* outSeg);

            // ============================================================================

            // Value decoding

            // ============================================================================
            uint32_t DecodePkgLength(const uint8_t* aml, uint32_t* pos);
            uint64_t DecodeInteger(const uint8_t* aml, uint32_t* pos);

            // ============================================================================

            // Method execution

            // ============================================================================
            struct ExecContext {
                const uint8_t* Aml;
                uint32_t       AmlBase;     // start of the block within the table
                uint32_t       AmlLength;
                int32_t        ScopeNode;
                Object         Locals[MaxMethodLocals];
                Object         Args[MaxMethodArgs];
                Object         ReturnValue;
                bool           Returned;
                bool           Broken;
                int            Depth;
            };

            bool ExecuteBlock(ExecContext& ctx, uint32_t offset, uint32_t endOffset);
            bool ExecuteOpcode(ExecContext& ctx, uint32_t* pos, uint32_t endOffset);

            // Evaluate a term (expression that produces a value) within an execution context.
            bool EvalTerm(ExecContext& ctx, uint32_t* pos, uint32_t endOffset, Object& result);

            // Evaluate a "SuperName" target for Store operations.
            // Returns the node index for named targets, or handles locals/args.
            // If isLocal/isArg is set, localIdx/argIdx contains the index.
            bool EvalTarget(ExecContext& ctx, uint32_t* pos,
                            int32_t& nodeIndex, bool& isLocal, int& localIdx,
                            bool& isArg, int& argIdx);

            // Store a value to a target (node, local, or arg).
            void StoreToTarget(ExecContext& ctx, const Object& value,
                               int32_t nodeIndex, bool isLocal, int localIdx,
                               bool isArg, int argIdx);

            // ============================================================================

            // Field I/O

            // ============================================================================
            bool ReadRegion(RegionSpace space, uint64_t address, uint32_t bitWidth, uint64_t& value);
            bool WriteRegion(RegionSpace space, uint64_t address, uint32_t bitWidth, uint64_t value);

            // ============================================================================

            // State

            // ============================================================================
            Namespace     m_ns;
            const uint8_t* m_dsdt;
            uint32_t      m_dsdtLength;
            bool          m_initialized;
        };

        // ============================================================================

        // Global interpreter instance

        // ============================================================================
        Interpreter& GetInterpreter();

    };
};
