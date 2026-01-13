#pragma once
#include "Memmap.hpp"
#include <CppLib/Spinlock.hpp>

namespace Memory {
    class HeapAllocator {
        static constexpr std::size_t headerMagic = 0x6CEF9AB4;
        kcp::Spinlock Lock;

        struct Node {
            size_t size;
            Node* next;
        };

        struct Header {
            std::size_t magic;
            std::size_t size;
        }__attribute__((packed)) ;

        Node head{};

        /* Heap statistics values */
        size_t m_totalBlocks = 0;
        size_t m_freeBlocks = 0;
        size_t m_totalFreeMemory = 0;
        size_t m_largestFreeBlock = 0;
        size_t m_totalAllocated = 0;  // Total bytes allocated since boot

        Header* GetHeader(void* block);
        void InsertToFreelist(void* ptr, std::size_t size);
        void InsertPageToFreelist();
        void InsertPagesToFreelist(std::size_t n);
    public:
        HeapAllocator();
        void* Request(size_t size);
        void* Realloc(void* ptr, size_t size);
        void Free(void *pagePtr);
        void Walk();
        size_t GetAllocatedBlockSize(void* ptr);

        /* Heap statistics getter/setters */
        size_t GetTotalBlocks() const { return m_totalBlocks; }
        size_t GetFreeBlocks() const { return m_freeBlocks; }
        size_t GetTotalFreeMemory() const { return m_totalFreeMemory; }
        size_t GetLargestFreeBlock() const { return m_largestFreeBlock; }
        size_t GetTotalAllocated() const { return m_totalAllocated; }
        
    };

    extern HeapAllocator* g_heap;
};