/*
    * MemoryAllocator.cpp
    * Heap memory allocator
    * Copyright (c) 2025 Daniel Hammer
*/

#include "Heap.hpp"
#include "Memmap.hpp"
#include "HHDM.hpp"

#include <Libraries/Memory.hpp>
#include <Terminal/Terminal.hpp>
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
                }

                Lock.Release();
                return block;
            }

            prev = current;
            current = current->next;
        }

        Lock.Release();

        // First pass allocation failed -- grow the heap
        size_t pagesNeeded = (sizeNeeded + 0xFFF) / 0x1000;
        InsertPagesToFreelist(pagesNeeded);

        return Request(size);
    }

    void* HeapAllocator::Realloc(void* ptr, size_t size) {
        auto new_block = Request(size);

        if (ptr != nullptr && new_block != nullptr) {
            size_t oldSize = GetAllocatedBlockSize(ptr);
            size_t copySize = (oldSize < size) ? oldSize : size;
            memcpy(new_block, ptr, copySize);
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

        InsertToFreelist(actualBlock, actualSize);

        Lock.Release();
    }
    
    // Traverses the Allocator's linked list for debugging
    void HeapAllocator::Walk() {
        Node* current = {head.next};
        size_t i{0};

        while (current != nullptr) {
            Kt::KernelLogStream(Kt::DEBUG, "HeapAllocator") << base::dec << i << " " << current->size << " bytes & address 0x" << base::hex << (uint64_t)current;
            current = current->next;
            i++;
        }
    }

};