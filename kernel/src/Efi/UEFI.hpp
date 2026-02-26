/*
    * UEFI.hpp
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>
#include <limine.h>
#include <Memory/HHDM.hpp>
#include <Memory/Paging.hpp>
#include <Timekeeping/Time.hpp>

namespace Efi {
    typedef void* EFI_HANDLE;

    struct EfiHeaderRevision {
        uint16_t MinorRevision;
        uint16_t MajorRevision;
    } __attribute__((packed));
    
    struct TableHeader {
        std::uint64_t Signature;
        EfiHeaderRevision Revision;
        std::uint32_t HeaderSize;
        std::uint32_t CRC32;
        std::uint32_t Reserved;
    }__attribute__((packed));

    /* EFI typedefs (unsigned ints) */

    typedef uint8_t UINT8;
    typedef uint16_t UINT16;
    typedef uint32_t UINT32;
    typedef uint64_t UINT64;

    /* EFI typedefs (signed ints) */

    typedef int8_t INT8;
    typedef int16_t INT16;
    typedef int32_t INT32;
    typedef int64_t INT64;

    /* EFI typedefs (misc) */
    typedef bool BOOLEAN;
    typedef void VOID;

    typedef INT64 INTN;
    typedef UINT64 UINTN;

    typedef UINTN RETURN_STATUS;
    typedef RETURN_STATUS EFI_STATUS;

    typedef unsigned short CHAR16;

    ///
    /// 64-bit physical memory address.
    ///
    typedef UINT64 EFI_PHYSICAL_ADDRESS;
    
    ///
    /// 64-bit virtual memory address.
    ///
    typedef UINT64 EFI_VIRTUAL_ADDRESS;

    #define EFIAPI __attribute__((__ms_abi__))

    /* EFI structs */
    typedef struct {
        UINT16    Year;
        UINT8     Month;
        UINT8     Day;
        UINT8     Hour;
        UINT8     Minute;
        UINT8     Second;
        UINT8     Pad1;
        UINT32    Nanosecond;
        INT16     TimeZone;
        UINT8     Daylight;
        UINT8     Pad2;
    } EFI_TIME;

    typedef struct {
        ///
        /// Provides the reporting resolution of the real-time clock device in
        /// counts per second. For a normal PC-AT CMOS RTC device, this
        /// value would be 1 Hz, or 1, to indicate that the device only reports
        /// the time to the resolution of 1 second.
        ///
        UINT32     Resolution;
        ///
        /// Provides the timekeeping accuracy of the real-time clock in an
        /// error rate of 1E-6 parts per million. For a clock with an accuracy
        /// of 50 parts per million, the value in this field would be
        /// 50,000,000.
        ///
        UINT32     Accuracy;
        ///
        /// A TRUE indicates that a time set operation clears the device's
        /// time below the Resolution reporting level. A FALSE
        /// indicates that the state below the Resolution level of the
        /// device is not cleared when the time is set. Normal PC-AT CMOS
        /// RTC devices set this value to FALSE.
        ///
        BOOLEAN    SetsToZero;
    } EFI_TIME_CAPABILITIES;

    ///
    /// Definition of an EFI memory descriptor.
    ///
    typedef struct {
        ///
        /// Type of the memory region.
        /// Type EFI_MEMORY_TYPE is defined in the
        /// AllocatePages() function description.
        ///
        UINT32                  Type;
        ///
        /// Physical address of the first byte in the memory region. PhysicalStart must be
        /// aligned on a 4 KiB boundary, and must not be above 0xfffffffffffff000. Type
        /// EFI_PHYSICAL_ADDRESS is defined in the AllocatePages() function description
        ///
        EFI_PHYSICAL_ADDRESS    PhysicalStart;
        ///
        /// Virtual address of the first byte in the memory region.
        /// VirtualStart must be aligned on a 4 KiB boundary,
        /// and must not be above 0xfffffffffffff000.
        ///
        EFI_VIRTUAL_ADDRESS     VirtualStart;
        ///
        /// NumberOfPagesNumber of 4 KiB pages in the memory region.
        /// NumberOfPages must not be 0, and must not be any value
        /// that would represent a memory page with a start address,
        /// either physical or virtual, above 0xfffffffffffff000.
        ///
        UINT64                  NumberOfPages;
        ///
        /// Attributes of the memory region that describe the bit mask of capabilities
        /// for that memory region, and not necessarily the current settings for that
        /// memory region.
        ///
        UINT64                  Attribute;
    } EFI_MEMORY_DESCRIPTOR;

    ///
    /// 128 bit buffer containing a unique identifier value.
    /// Unless otherwise specified, aligned on a 64 bit boundary.
    ///
    typedef struct {
        UINT32    Data1;
        UINT16    Data2;
        UINT16    Data3;
        UINT8     Data4[8];
    } GUID;

    typedef GUID EFI_GUID;

    ///
    /// Enumeration of reset types.
    ///
    typedef enum {
        ///
        /// Used to induce a system-wide reset. This sets all circuitry within the
        /// system to its initial state.  This type of reset is asynchronous to system
        /// operation and operates withgout regard to cycle boundaries.  EfiColdReset
        /// is tantamount to a system power cycle.
        ///
        EfiResetCold,
        ///
        /// Used to induce a system-wide initialization. The processors are set to their
        /// initial state, and pending cycles are not corrupted.  If the system does
        /// not support this reset type, then an EfiResetCold must be performed.
        ///
        EfiResetWarm,
        ///
        /// Used to induce an entry into a power state equivalent to the ACPI G2/S5 or G3
        /// state.  If the system does not support this reset type, then when the system
        /// is rebooted, it should exhibit the EfiResetCold attributes.
        ///
        EfiResetShutdown,
        ///
        /// Used to induce a system-wide reset. The exact type of the reset is defined by
        /// the EFI_GUID that follows the Null-terminated Unicode string passed into
        /// ResetData. If the platform does not recognize the EFI_GUID in ResetData the
        /// platform must pick a supported reset type to perform. The platform may
        /// optionally log the parameters from any non-normal reset that occurs.
        ///
        EfiResetPlatformSpecific
    } EFI_RESET_TYPE;

    ///
    /// EFI Capsule Header.
    ///
    typedef struct {
        ///
        /// A GUID that defines the contents of a capsule.
        ///
        EFI_GUID    CapsuleGuid;
        ///
        /// The size of the capsule header. This may be larger than the size of
        /// the EFI_CAPSULE_HEADER since CapsuleGuid may imply
        /// extended header entries
        ///
        UINT32      HeaderSize;
        ///
        /// Bit-mapped list describing the capsule attributes. The Flag values
        /// of 0x0000 - 0xFFFF are defined by CapsuleGuid. Flag values
        /// of 0x10000 - 0xFFFFFFFF are defined by this specification
        ///
        UINT32      Flags;
        ///
        /// Size in bytes of the capsule (including capsule header).
        ///
        UINT32      CapsuleImageSize;
    } EFI_CAPSULE_HEADER;
        

    /* Typedefs EFI runtime service APIs */
    typedef EFI_STATUS(EFIAPI *EFI_GET_TIME) (EFI_TIME *Time, EFI_TIME_CAPABILITIES *Capabilities);
    typedef EFI_STATUS(EFIAPI *EFI_SET_TIME) (EFI_TIME *Time);
    typedef EFI_STATUS(EFIAPI *EFI_GET_WAKEUP_TIME) (BOOLEAN *Enabled, BOOLEAN *Pending, EFI_TIME *Time);
    typedef EFI_STATUS(EFIAPI *EFI_SET_WAKEUP_TIME) (BOOLEAN Enable, EFI_TIME *Time);
    typedef EFI_STATUS(EFIAPI * EFI_SET_VIRTUAL_ADDRESS_MAP) (UINTN MemoryMapSize, UINTN DescriptorSize, UINT32 DescriptorVersion, EFI_MEMORY_DESCRIPTOR *VirtualMap);
    typedef EFI_STATUS(EFIAPI * EFI_CONVERT_POINTER) (UINTN DebugDisposition, VOID **Address);
    typedef EFI_STATUS(EFIAPI * EFI_GET_VARIABLE) (CHAR16 *VariableName, EFI_GUID *VendorGuid, UINT32 *Attributes, UINTN *DataSize, VOID *Data);
    typedef EFI_STATUS(EFIAPI * EFI_GET_NEXT_VARIABLE_NAME) (UINTN *VariableNameSize, CHAR16 *VariableName, EFI_GUID *VendorGuid);
    typedef EFI_STATUS(EFIAPI * EFI_SET_VARIABLE) (CHAR16 *VariableName, EFI_GUID *VendorGuid, UINT32 Attributes, UINTN DataSize, VOID *Data);
    typedef EFI_STATUS(EFIAPI * EFI_GET_NEXT_HIGH_MONO_COUNT) (UINT32 *HighCount);
    typedef VOID(EFIAPI * EFI_RESET_SYSTEM) (EFI_RESET_TYPE ResetType, EFI_STATUS ResetStatus, UINTN DataSize, VOID *ResetData);
    typedef EFI_STATUS(EFIAPI * EFI_UPDATE_CAPSULE) (EFI_CAPSULE_HEADER **CapsuleHeaderArray, UINTN CapsuleCount, EFI_PHYSICAL_ADDRESS ScatterGatherList);
    typedef EFI_STATUS(EFIAPI * EFI_QUERY_CAPSULE_CAPABILITIES) (EFI_CAPSULE_HEADER **CapsuleHeaderArray, UINTN CapsuleCount, UINT64 *MaximumCapsuleSize, EFI_RESET_TYPE *ResetType);
    typedef EFI_STATUS(EFIAPI * EFI_QUERY_VARIABLE_INFO) (UINT32 Attributes, UINT64 *MaximumVariableStorageSize, UINT64 *RemainingVariableStorageSize, UINT64 *MaximumVariableSize);
    
    struct RuntimeServicesTable {
        TableHeader Header;
        EFI_GET_TIME GetTime;
        EFI_SET_TIME SetTime;
        EFI_GET_WAKEUP_TIME GetWakeupTime;
        EFI_SET_WAKEUP_TIME SetWakeupTime;
        EFI_SET_VIRTUAL_ADDRESS_MAP SetVirtualAddressMap;
        EFI_CONVERT_POINTER ConvertPointer;
        EFI_GET_VARIABLE GetVariable;
        EFI_GET_NEXT_VARIABLE_NAME GetNextVariableName;
        EFI_SET_VARIABLE SetVariable;
        EFI_GET_NEXT_HIGH_MONO_COUNT GetNextHighMonotonicCount;
        EFI_RESET_SYSTEM ResetSystem;
        EFI_UPDATE_CAPSULE UpdateCapsule;
        EFI_QUERY_CAPSULE_CAPABILITIES QueryCapsuleCapabilities;
        EFI_QUERY_VARIABLE_INFO QueryVariableInfo;
    };

    struct SystemTable {
        TableHeader Header;
        void* FirmwareVendor; // Pointer to a CHAR16 string of the fw vendor name string
        std::uint32_t FirmwareRevision;
        
        EFI_HANDLE ConsoleInHandle;
        void* ConIn;
        EFI_HANDLE ConsoleOutHandle;
        void* ConOut;

        EFI_HANDLE StandardErrorHandle;
        void* StdErr;

        // Jackpot
        RuntimeServicesTable *RuntimeServices;

        void *BootServices;

        std::uint64_t NumberOfTableEntries;

        void *ConfigurationTable;
    };
    
    inline EFI_RESET_SYSTEM g_ResetSystem = nullptr;

    inline void Init(SystemTable* ST, limine_efi_memmap_response* efiMemmap) {
        Kt::KernelLogStream(Kt::OK, "UEFI") << "ST Minor Revision: " << ST->Header.Revision.MinorRevision;
        Kt::KernelLogStream(Kt::OK, "UEFI") << "ST Major Revision: " << ST->Header.Revision.MajorRevision;

        RuntimeServicesTable* RT = (RuntimeServicesTable*)Memory::HHDM(ST->RuntimeServices);

        if (ST->RuntimeServices != nullptr) {
            Kt::KernelLogStream(Kt::OK, "UEFI") << "EFI Runtime Service API is available.";

            /* Identity-map EFI runtime service regions so firmware code
               can reference its own data at physical addresses */
            if (Memory::VMM::g_paging) {
                Memory::VMM::g_paging->MapEfiRuntime(efiMemmap);
            }

            EFI_TIME Time;
            EFI_TIME_CAPABILITIES TimeCapabilities;

            EFI_GET_TIME _GetTime = (EFI_GET_TIME)Memory::HHDM((void*)RT->GetTime);
            _GetTime(&Time, &TimeCapabilities);

            Timekeeping::Init(Time.Year, Time.Month, Time.Day, Time.Hour, Time.Minute, Time.Second);

            g_ResetSystem = (EFI_RESET_SYSTEM)Memory::HHDM((void*)RT->ResetSystem);
        }
    }
};