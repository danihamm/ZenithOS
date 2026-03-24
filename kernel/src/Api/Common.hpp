#pragma once
#include <Sched/Scheduler.hpp>

namespace Montauk {
    // Find the process that owns the I/O ring buffers for a redirected process.
    // If proc owns buffers itself (spawned via spawn_redir), returns proc.
    // If proc inherited redirection (spawned via spawn from a redirected parent),
    // follows parentPid to find the buffer owner.
    static Sched::Process* GetRedirTarget(Sched::Process* proc) {
        if (!proc || !proc->redirected) return nullptr;
        if (proc->outBuf) return proc;  // owns buffers
        return Sched::GetProcessByPid(proc->parentPid);
    }

    // SPSC ring buffer helpers. On x86 TSO, atomic-width aligned
    // loads/stores are naturally atomic. The compiler barrier ensures
    // the data write is visible before the head update.
    static void RingWrite(uint8_t* buf, volatile uint32_t& head, uint32_t /*tail*/, uint32_t size, uint8_t byte) {
        uint32_t h = head;
        buf[h] = byte;
        asm volatile("" ::: "memory");  // compiler barrier
        head = (h + 1) % size;
    }

    static int RingRead(uint8_t* buf, volatile uint32_t& head, volatile uint32_t& tail, uint32_t size, uint8_t* out, int maxLen) {
        int count = 0;
        uint32_t t = tail;
        uint32_t h = head;
        while (t != h && count < maxLen) {
            out[count++] = buf[t];
            t = (t + 1) % size;
        }
        asm volatile("" ::: "memory");  // compiler barrier
        tail = t;
        return count;
    }
}