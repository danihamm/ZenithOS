#include "ACPI.hpp"
#include <Terminal/Terminal.hpp>
#include <CppLib/Stream.hpp>
#include <CppLib/Vector.hpp>
#include <Libraries/String.hpp>
#include <Memory/HHDM.hpp>

using namespace Kt;

namespace Hal {
    kcp::vector<const char*>* ACPITables;

    bool ACPI::XSDP::TestChecksum() {
        std::uint8_t sum = 0;

        for (std::size_t i = 0; i < this->Length; i++) {
            sum += ((char*)this)[i];
        }

        return sum == 0;
    }

    bool ACPI::TestChecksum(ACPI::CommonSDTHeader* header) {
        std::uint8_t sum = 0;

        for (std::size_t i = 0; i < header->Length; i++) {
            sum += ((char*)header)[i];
        }

        return sum == 0;
    }

    const char* ACPI::XSDP::GetOEMID() {
        kcp::cstringstream oemIdStream{};

        oemIdStream << this->OEMID[0]
                    << this->OEMID[1]
                    << this->OEMID[2]
                    << this->OEMID[3]
                    << this->OEMID[4]
                    << this->OEMID[5];

        return oemIdStream.c_str();
    }
    
    void ACPI::HandleXSDT(CommonSDTHeader* sdtHeader) {
        auto length = sdtHeader->Length;
        auto entryCount = (sdtHeader->Length - sizeof(CommonSDTHeader)) / 8;

        kcp::vector<CommonSDTHeader*> discoveredTables{};
        auto currentTable = sdtHeader;
    }

    void ACPI::HandleRSDT(CommonSDTHeader* sdtHeader) {
        KernelLogStream(ERROR, "ACPI") << "Unimplemented - HandleRSDT";
    }

    ACPI::ACPI(XSDP* xsdp) {
        if (xsdp->TestChecksum() != true) {
            KernelLogStream(ERROR, "ACPI") << "Checksum failed for SDT!";
            return;
        }

        KernelLogStream(OK, "ACPI") << "Checksum passed for SDT";


        KernelLogStream(INFO, "ACPI") << "OEM ID: " << xsdp->GetOEMID();
        KernelLogStream(INFO, "ACPI") << "ACPI version: " << xsdp->Revision;

        std::uint64_t nextTableAddress;
        
        if (xsdp->Revision >= 2) {
            nextTableAddress = xsdp->XSDTAddress;
            m_xsdt = (CommonSDTHeader*)Memory::HHDM(nextTableAddress);
            HandleXSDT(m_xsdt);
        }
        else
        {
            nextTableAddress = xsdp->RSDTAddress;
            HandleRSDT((CommonSDTHeader*)Memory::HHDM(nextTableAddress));
        }
    }
};