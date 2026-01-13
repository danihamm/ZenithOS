#include "Memmap.hpp"
#include <Common/Panic.hpp>

#include "Heap.hpp"

using namespace Kt;

namespace Memory {
    LargestSection Scan(limine_memmap_response* mmap) {
        LargestSection currentLargestSection{};

        for (size_t i = 0; i < mmap->entry_count; i++) {
            auto entry = mmap->entries[i];

            if (entry->base == 0) {
                continue;
            }
            
            if (entry->type == LIMINE_MEMMAP_USABLE) {
                if (entry->length > currentLargestSection.size) {
                    currentLargestSection = {
                        .address = (uint64_t)entry->base,
                        .size = entry->length
                    };
                }
            }
        }

        [[unlikely]] if (currentLargestSection.address == 0) {
            Panic("Couldn't find a usable memory section.", nullptr);
        }

        return currentLargestSection;
    }
};