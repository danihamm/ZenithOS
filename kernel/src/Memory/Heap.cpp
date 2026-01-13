/*
    * MemoryAllocator.cpp
    * Heap memory allocator
    * Copyright (c) 2025 Daniel Hammer
*/

#include "Heap.hpp"
#include "Memmap.hpp"
#include "HHDM.hpp"

#include <Libraries/Memory.hpp>
#include <Gui/DebugGui.hpp>
#include <Common/Panic.hpp>
#include <Platform/Util.hpp>

#include <Common/Panic.hpp>
#include <CppLib/Vector.hpp>

#include "PageFrameAllocator.hpp"

namespace Memory
{
    HeapAllocator::Header* HeapAllocator::GetHeader(void* block) {
        return (Header*)(block - sizeof(Header));
    }

    size_t HeapAllocator::GetAllocatedBlockSize(void* ptr) {
        Header* header = GetHeader(ptr);
        return header->size;
    }

    void HeapAllocator::InsertToFreelist(void* ptr, std::size_t size) {
        auto prev_next = head.next;
        
        head.next = (Node*)ptr;
        head.next->next = prev_next;
        head.next->size = size;
    }

    void HeapAllocator::InsertPageToFreelist() {
        InsertToFreelist(Memory::g_pfa->Allocate(), 0x1000);
    }

    void HeapAllocator::InsertPagesToFreelist(std::size_t n) {
        auto ptr = Memory::g_pfa->ReallocConsecutive(nullptr, n);
        size_t size = 0x1000 * n;

        InsertToFreelist(ptr, size);
    }

    HeapAllocator::HeapAllocator()
    {
        InsertPagesToFreelist(0x32);
        m_totalBlocks = 1;  // Start with 1 large block
        m_freeBlocks = 1;
        m_totalFreeMemory = 0x1000 * 0x32;
        m_largestFreeBlock = 0x1000 * 0x32;
    }

    void* HeapAllocator::Request(size_t size) {
        Lock.Acquire();

        Node* current = head.next;
        Node* prev = &head;

        size_t sizeNeeded = size + sizeof(Header);

        while (current != nullptr) {
            if (current->size >= sizeNeeded) {
                // Unlink the node
                auto locatedBlockSize = current->size;

                prev->next = current->next;
                Header* header = (Header*)current;

                header->magic = headerMagic;
                header->size = size;

                void* block = (void*)((uintptr_t)header + sizeof(Header));

                if (locatedBlockSize > sizeNeeded) {
                    void* rest = (void*)((uintptr_t)header + sizeNeeded);
                    auto newBlockSize = locatedBlockSize - sizeNeeded;

                    InsertToFreelist(rest, newBlockSize);
                    // Block was split, so we still have same number of free blocks
                    // (removed one, added one back)
                } else {
                    // Entire block consumed
                    m_freeBlocks--;
                }
                
                m_totalFreeMemory -= sizeNeeded;  // Track actual memory used including header
                m_totalAllocated += sizeNeeded;   // Track cumulative allocations
                
                Lock.Release();
                return block;
            }

            prev = current;
            current = current->next;

            Lock.Release();
        }

        // First pass allocation failed - need more memory
        size_t pagesNeeded = (size / 0x1000) + 1;
        size_t allocatedSize = pagesNeeded * 0x1000;
        InsertPagesToFreelist(pagesNeeded);
        
        // Added new pages as one large free block
        m_freeBlocks++;
        m_totalFreeMemory += allocatedSize;
        if (allocatedSize > m_largestFreeBlock) {
            m_largestFreeBlock = allocatedSize;
        }

        m_totalAllocated += allocatedSize;

        return Request(size);
    }

    void* HeapAllocator::Realloc(void* ptr, size_t size) {
        auto new_block = Request(size);

        if (ptr != nullptr && new_block != nullptr) {
            memcpy(new_block, ptr, size);
            Free(ptr);
        }

        return new_block;
    }

    void HeapAllocator::Free(void* ptr) {
        Lock.Acquire();
        
        Header* header = GetHeader(ptr);
        auto size = header->size;
        
        if (header->magic != headerMagic) {
            Panic("Bad magic in HeapAllocator header", nullptr);
            return;
        }

        auto actualSize = size + sizeof(Header);
        void* actualBlock = (void*)header;

        InsertToFreelist(actualBlock, size);
        m_freeBlocks++;
        m_totalFreeMemory += actualSize;  // Include header in free memory
        
        // Update largest free block if needed
        if (actualSize > m_largestFreeBlock) {
            m_largestFreeBlock = actualSize;
        }

        Lock.Release();
    }
    
    // Traverses the Allocator's linked list for debugging
    void HeapAllocator::Walk() {
        Node* current = {head.next};
        size_t i{0};

        while (current != nullptr) {
            Gui::GuiLogStream(Gui::LogLevel::Debug, "HeapAllocator") << Gui::base::dec() << i << " " << current->size << " bytes & address 0x" << Gui::base::hex() << (uint64_t)current;
            current = current->next;
            i++;
        }
    }

};