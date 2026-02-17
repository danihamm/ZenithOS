#pragma once
#include <cstdint>

namespace Hal {
    class ACPI {
    
    public:
        struct XSDP {
            char Signature[8];
            std::uint8_t Checksum;
            char OEMID[6];
            std::uint8_t Revision;
            std::uint32_t RSDTAddress; // Deprecated(!)

            // Xtended values
            std::uint32_t Length;
            std::uint64_t XSDTAddress;
            std::uint8_t ExtendedChecksum;
            std::uint8_t Reserved[3];

            bool TestChecksum();
            const char* GetOEMID();
        }__attribute__((packed));

        struct CommonSDTHeader {
            char Signature[4];
            std::uint32_t Length;
            std::uint8_t Revision;
            std::uint8_t Checksum;
            char OEMID[6];
            char OEMTableID[8];
            std::uint32_t OEMRevision;
            std::uint32_t CreatorID;
            std::uint32_t CreatorRevision;
        }__attribute__((packed));

        ACPI(XSDP* xsdp);

        void HandleXSDT(CommonSDTHeader* sdtHeader);
        void HandleRSDT(CommonSDTHeader* sdtHeader);

        static bool TestChecksum(CommonSDTHeader* header);

        CommonSDTHeader* FindNextTable(CommonSDTHeader* table);

        CommonSDTHeader* GetXSDT() { return m_xsdt; }

    private:
        CommonSDTHeader* m_xsdt = nullptr;
    };
};