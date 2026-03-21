/*
    * libc.c
    * Minimal C standard library for MontaukOS userspace programs
    * Based on the proven libc from the DOOM port.
    * Copyright (c) 2025-2026 Daniel Hammer
*/

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>

/* ========================================================================
   Raw syscall wrappers (C versions matching kernel ABI)
   ======================================================================== */

static inline long _zos_syscall0(long nr) {
    long ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(nr)
        : "rcx", "r11", "rdi", "rsi", "rdx", "r8", "r9", "r10", "memory");
    return ret;
}

static inline long _zos_syscall1(long nr, long a1) {
    long ret;
    __asm__ volatile(
        "mov %[a1], %%rdi\n\t"
        "syscall"
        : "=a"(ret)
        : "a"(nr), [a1] "r"(a1)
        : "rcx", "r11", "rdi", "rsi", "rdx", "r8", "r9", "r10", "memory");
    return ret;
}

static inline long _zos_syscall2(long nr, long a1, long a2) {
    long ret;
    __asm__ volatile(
        "mov %[a1], %%rdi\n\t"
        "mov %[a2], %%rsi\n\t"
        "syscall"
        : "=a"(ret)
        : "a"(nr), [a1] "r"(a1), [a2] "r"(a2)
        : "rcx", "r11", "rdi", "rsi", "rdx", "r8", "r9", "r10", "memory");
    return ret;
}

static inline long _zos_syscall3(long nr, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile(
        "mov %[a1], %%rdi\n\t"
        "mov %[a2], %%rsi\n\t"
        "mov %[a3], %%rdx\n\t"
        "syscall"
        : "=a"(ret)
        : "a"(nr), [a1] "r"(a1), [a2] "r"(a2), [a3] "r"(a3)
        : "rcx", "r11", "rdi", "rsi", "rdx", "r8", "r9", "r10", "memory");
    return ret;
}

static inline long _zos_syscall4(long nr, long a1, long a2, long a3, long a4) {
    long ret;
    __asm__ volatile(
        "mov %[a1], %%rdi\n\t"
        "mov %[a2], %%rsi\n\t"
        "mov %[a3], %%rdx\n\t"
        "mov %[a4], %%r10\n\t"
        "syscall"
        : "=a"(ret)
        : "a"(nr), [a1] "r"(a1), [a2] "r"(a2), [a3] "r"(a3), [a4] "r"(a4)
        : "rcx", "r11", "rdi", "rsi", "rdx", "r8", "r9", "r10", "memory");
    return ret;
}

/* Syscall numbers */
#define SYS_EXIT    0
#define SYS_PRINT   4
#define SYS_PUTCHAR 5
#define SYS_OPEN    6
#define SYS_READ    7
#define SYS_GETSIZE 8
#define SYS_CLOSE   9
#define SYS_READDIR 10
#define SYS_ALLOC   11
#define SYS_FREE    12
#define SYS_GETCHAR 18
#define SYS_SPAWN   20
#define SYS_WAITPID 23
#define SYS_GETARGS 25
#define SYS_FWRITE  41
#define SYS_FCREATE 42
#define SYS_FDELETE 77
#define SYS_FMKDIR  78

/* ========================================================================
   errno
   ======================================================================== */

int errno = 0;

/* ========================================================================
   string.h functions
   ======================================================================== */

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    /* Byte copy until 8-byte aligned */
    while (n && ((uint64_t)d & 7)) { *d++ = *s++; n--; }

    /* Bulk 8-byte copy */
    uint64_t *d8 = (uint64_t *)d;
    const uint64_t *s8 = (const uint64_t *)s;
    size_t words = n / 8;
    for (size_t i = 0; i < words; i++) d8[i] = s8[i];

    /* Remainder */
    d = (unsigned char *)(d8 + words);
    s = (const unsigned char *)(s8 + words);
    for (size_t i = 0; i < (n & 7); i++) d[i] = s[i];

    return dest;
}

void *memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char *)s;
    unsigned char v = (unsigned char)c;

    /* Byte fill until 8-byte aligned */
    while (n && ((uint64_t)p & 7)) { *p++ = v; n--; }

    /* Bulk 8-byte fill */
    uint64_t v8 = v;
    v8 |= v8 << 8;  v8 |= v8 << 16;  v8 |= v8 << 32;
    uint64_t *p8 = (uint64_t *)p;
    size_t words = n / 8;
    for (size_t i = 0; i < words; i++) p8[i] = v8;

    /* Remainder */
    p = (unsigned char *)(p8 + words);
    for (size_t i = 0; i < (n & 7); i++) p[i] = v;

    return s;
}

void *memmove(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s || d >= s + n) {
        memcpy(dest, src, n);
    } else {
        /* Backward copy — bulk 8 bytes at a time from end */
        d += n; s += n;
        while (n && ((uint64_t)d & 7)) { *--d = *--s; n--; }
        uint64_t *d8 = (uint64_t *)d;
        const uint64_t *s8 = (const uint64_t *)s;
        size_t words = n / 8;
        for (size_t i = 1; i <= words; i++) d8[-(long)i] = s8[-(long)i];
        d = (unsigned char *)(d8 - words);
        s = (const unsigned char *)(s8 - words);
        for (size_t i = 1; i <= (n & 7); i++) d[-(long)i] = s[-(long)i];
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *a = (const unsigned char *)s1;
    const unsigned char *b = (const unsigned char *)s2;
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    unsigned char uc = (unsigned char)c;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == uc)
            return (void *)(p + i);
    }
    return (void *)0;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || a[i] == 0)
            return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = '\0';
    return dest;
}

char *strcat(char *dest, const char *src) {
    char *d = dest + strlen(dest);
    while ((*d++ = *src++));
    return dest;
}

char *strncat(char *dest, const char *src, size_t n) {
    char *d = dest + strlen(dest);
    size_t i;
    for (i = 0; i < n && src[i]; i++)
        d[i] = src[i];
    d[i] = '\0';
    return dest;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == 0) ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == 0) return (char *)s;
    return (char *)last;
}

int strcasecmp(const char *a, const char *b) {
    while (*a && ((*a >= 'A' && *a <= 'Z') ? *a + 32 : *a) ==
                 ((*b >= 'A' && *b <= 'Z') ? *b + 32 : *b)) { a++; b++; }
    int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
    int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
    return ca - cb;
}

int strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int ca = (a[i] >= 'A' && a[i] <= 'Z') ? a[i] + 32 : a[i];
        int cb = (b[i] >= 'A' && b[i] <= 'Z') ? b[i] + 32 : b[i];
        if (ca != cb || ca == 0) return ca - cb;
    }
    return 0;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    size_t nlen = strlen(needle);
    while (*haystack) {
        if (strncmp(haystack, needle, nlen) == 0)
            return (char *)haystack;
        haystack++;
    }
    return NULL;
}

/* Forward declaration for strdup */
void *malloc(size_t size);

char *strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *d = (char *)malloc(len);
    if (d) memcpy(d, s, len);
    return d;
}

/* ========================================================================
   ctype.h functions
   ======================================================================== */

int isalpha(int c)  { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
int isdigit(int c)  { return c >= '0' && c <= '9'; }
int isalnum(int c)  { return isalpha(c) || isdigit(c); }
int isspace(int c)  { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
int isupper(int c)  { return c >= 'A' && c <= 'Z'; }
int islower(int c)  { return c >= 'a' && c <= 'z'; }
int isprint(int c)  { return c >= 0x20 && c <= 0x7E; }
int ispunct(int c)  { return isprint(c) && !isalnum(c) && c != ' '; }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int iscntrl(int c)  { return (c >= 0 && c < 0x20) || c == 0x7F; }
int isgraph(int c)  { return c > 0x20 && c <= 0x7E; }
int toupper(int c)  { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
int tolower(int c)  { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* ========================================================================
   Heap allocator (free-list, backed by SYS_ALLOC)
   ======================================================================== */

#define HEAP_MAGIC  0x5A484541ULL  /* "ZHEA" */
#define FREED_MAGIC 0xDEADFEEEULL

struct HeapHeader {
    uint64_t magic;
    uint64_t size;
} __attribute__((packed));

struct FreeNode {
    uint64_t       size;
    struct FreeNode *next;
};

/* Segregated free lists: power-of-2 size classes for blocks <= 4096 bytes */
#define NUM_BUCKETS 8
static const uint64_t BUCKET_SIZES[NUM_BUCKETS] = {
    32, 64, 128, 256, 512, 1024, 2048, 4096
};

static struct FreeNode *g_buckets[NUM_BUCKETS] = {};
static struct FreeNode g_overflow = { 0, NULL };
static int g_heapInit = 0;

static int heap_bucket_index(uint64_t blockSize) {
    if (blockSize <= 32)   return 0;
    if (blockSize <= 64)   return 1;
    if (blockSize <= 128)  return 2;
    if (blockSize <= 256)  return 3;
    if (blockSize <= 512)  return 4;
    if (blockSize <= 1024) return 5;
    if (blockSize <= 2048) return 6;
    if (blockSize <= 4096) return 7;
    return -1;
}

/* Insert into overflow list (sorted by address, with coalescing) */
static void heap_insert_overflow(void *ptr, uint64_t size) {
    struct FreeNode *node = (struct FreeNode *)ptr;
    node->size = size;

    struct FreeNode *prev = &g_overflow;
    struct FreeNode *cur  = g_overflow.next;
    while (cur != NULL && cur < node) {
        prev = cur;
        cur  = cur->next;
    }

    int merged_prev = 0;
    if (prev != &g_overflow &&
        (uint8_t *)prev + prev->size == (uint8_t *)node) {
        prev->size += size;
        node = prev;
        merged_prev = 1;
    }

    if (cur != NULL &&
        (uint8_t *)node + node->size == (uint8_t *)cur) {
        node->size += cur->size;
        node->next  = cur->next;
        if (!merged_prev) prev->next = node;
    } else if (!merged_prev) {
        node->next = cur;
        prev->next = node;
    }
}

/* Take a block >= needed from overflow. Splits remainder back. */
static void *heap_take_overflow(uint64_t needed) {
    struct FreeNode *prev = &g_overflow;
    struct FreeNode *cur  = g_overflow.next;

    while (cur != NULL) {
        if (cur->size >= needed) {
            uint64_t blockSize = cur->size;
            prev->next = cur->next;

            if (blockSize > needed + sizeof(struct FreeNode) + 16) {
                heap_insert_overflow((uint8_t *)cur + needed, blockSize - needed);
            }
            return (void *)cur;
        }
        prev = cur;
        cur  = cur->next;
    }
    return NULL;
}

static void heap_grow(uint64_t bytes) {
    uint64_t pages = (bytes + 0xFFF) / 0x1000;
    if (pages < 4) pages = 4;
    void *mem = (void *)_zos_syscall1(SYS_ALLOC, (long)(pages * 0x1000));
    if (mem != NULL)
        heap_insert_overflow(mem, pages * 0x1000);
}

/* Refill a small-block bucket from overflow */
static int heap_refill_bucket(int idx) {
    uint64_t bsize = BUCKET_SIZES[idx];
    uint64_t chunk = (bsize < 4096) ? 4096 : bsize;

    void *block = heap_take_overflow(chunk);
    if (block == NULL) {
        heap_grow(chunk);
        block = heap_take_overflow(chunk);
        if (block == NULL) return 0;
    }

    uint64_t count = chunk / bsize;
    for (uint64_t i = 0; i < count; i++) {
        struct FreeNode *node = (struct FreeNode *)((uint8_t *)block + i * bsize);
        node->size = bsize;
        node->next = g_buckets[idx];
        g_buckets[idx] = node;
    }
    return 1;
}

void *malloc(size_t size) {
    if (!g_heapInit) {
        heap_grow(16 * 0x1000);
        g_heapInit = 1;
    }

    /* Guard against overflow: size + Header must not wrap */
    if (size > (uint64_t)-1 - sizeof(struct HeapHeader) - 15)
        return NULL;

    uint64_t needed = size + sizeof(struct HeapHeader);
    needed = (needed + 15) & ~15ULL;

    int idx = heap_bucket_index(needed);

    if (idx >= 0) {
        /* Small allocation — use segregated bucket (O(1)) */
        if (g_buckets[idx] == NULL && !heap_refill_bucket(idx))
            return NULL;

        struct FreeNode *node = g_buckets[idx];
        g_buckets[idx] = node->next;

        struct HeapHeader *hdr = (struct HeapHeader *)node;
        hdr->magic = HEAP_MAGIC;
        hdr->size  = size;
        return (void *)((uint8_t *)hdr + sizeof(struct HeapHeader));
    }

    /* Large allocation — search overflow list */
    void *block = heap_take_overflow(needed);
    if (block == NULL) {
        heap_grow(needed);
        block = heap_take_overflow(needed);
        if (block == NULL) return NULL;
    }

    struct HeapHeader *hdr = (struct HeapHeader *)block;
    hdr->magic = HEAP_MAGIC;
    hdr->size  = size;
    return (void *)((uint8_t *)hdr + sizeof(struct HeapHeader));
}

void free(void *ptr) {
    if (ptr == NULL) return;

    struct HeapHeader *hdr = (struct HeapHeader *)((uint8_t *)ptr - sizeof(struct HeapHeader));

    if (hdr->magic == FREED_MAGIC) return;    /* double-free */
    if (hdr->magic != HEAP_MAGIC) return;     /* corrupt */
    hdr->magic = FREED_MAGIC;

    uint64_t blockSize = hdr->size + sizeof(struct HeapHeader);
    blockSize = (blockSize + 15) & ~15ULL;

    int idx = heap_bucket_index(blockSize);

    if (idx >= 0) {
        /* Small block — push onto bucket (O(1)) */
        struct FreeNode *node = (struct FreeNode *)hdr;
        node->size = BUCKET_SIZES[idx];
        node->next = g_buckets[idx];
        g_buckets[idx] = node;
    } else {
        /* Large block — sorted insert with coalescing */
        heap_insert_overflow((void *)hdr, blockSize);
    }
}

void *calloc(size_t nmemb, size_t size) {
    /* Check for multiplication overflow */
    if (nmemb != 0 && size > (size_t)-1 / nmemb)
        return NULL;
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *realloc(void *ptr, size_t size) {
    if (ptr == NULL) return malloc(size);
    if (size == 0) { free(ptr); return NULL; }

    struct HeapHeader *hdr = (struct HeapHeader *)((uint8_t *)ptr - sizeof(struct HeapHeader));
    uint64_t old = hdr->size;

    /* Compute actual block size (accounting for bucket rounding) */
    uint64_t oldBlock = (old + sizeof(struct HeapHeader) + 15) & ~15ULL;
    int idx = heap_bucket_index(oldBlock);
    if (idx >= 0) oldBlock = BUCKET_SIZES[idx];

    uint64_t newNeed = (size + sizeof(struct HeapHeader) + 15) & ~15ULL;
    if (newNeed <= oldBlock) {
        hdr->size = size;
        return ptr;
    }

    void *newp = malloc(size);
    if (newp == NULL) return NULL;

    size_t copySize = old < size ? old : size;
    memcpy(newp, ptr, copySize);
    free(ptr);
    return newp;
}

/* ========================================================================
   stdlib.h functions
   ======================================================================== */

int abs(int x)   { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }

int atoi(const char *s) {
    int neg = 0, val = 0;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }
    while (isdigit((unsigned char)*s)) {
        val = val * 10 + (*s - '0');
        s++;
    }
    return neg ? -val : val;
}

long strtol(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    long val = 0;
    int neg = 0;

    while (isspace((unsigned char)*s)) s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }

    if (base == 0) {
        if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (*s == '0') { base = 8; s++; }
        else base = 10;
    } else if (base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    while (*s) {
        int digit;
        if (*s >= '0' && *s <= '9') digit = *s - '0';
        else if (*s >= 'a' && *s <= 'z') digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') digit = *s - 'A' + 10;
        else break;
        if (digit >= base) break;
        val = val * base + digit;
        s++;
    }

    if (endptr) *endptr = (char *)s;
    return neg ? -val : val;
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    unsigned long val = 0;

    while (isspace((unsigned char)*s)) s++;
    /* strtoul allows an optional leading + or - (result is negated for -) */
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }

    if (base == 0) {
        if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (*s == '0') { base = 8; s++; }
        else base = 10;
    } else if (base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    while (*s) {
        int digit;
        if (*s >= '0' && *s <= '9') digit = *s - '0';
        else if (*s >= 'a' && *s <= 'z') digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') digit = *s - 'A' + 10;
        else break;
        if (digit >= base) break;
        val = val * (unsigned long)base + (unsigned long)digit;
        s++;
    }

    if (endptr) *endptr = (char *)s;
    return neg ? (unsigned long)(-(long)val) : val;
}

char *getenv(const char *name) {
    (void)name;
    return NULL;
}

void exit(int status) {
    _zos_syscall1(SYS_EXIT, (long)status);
    __builtin_unreachable();
}

void abort(void) {
    _zos_syscall1(SYS_PRINT, (long)"abort() called\n");
    _zos_syscall1(SYS_EXIT, 1);
    __builtin_unreachable();
}

int system(const char *command) {
    if (command == NULL) return -1;
    int pid = (int)_zos_syscall2(SYS_SPAWN, (long)command, 0L);
    if (pid < 0) return -1;
    _zos_syscall1(SYS_WAITPID, (long)pid);
    return 0;
}

/* ========================================================================
   printf family — vsnprintf core
   ======================================================================== */

struct _pf_state {
    char *buf;
    size_t pos;
    size_t max;
};

static void _pf_putc(struct _pf_state *st, char c) {
    if (st->pos < st->max)
        st->buf[st->pos] = c;
    st->pos++;
}

static void _pf_puts(struct _pf_state *st, const char *s) {
    while (*s) _pf_putc(st, *s++);
}

static void _pf_putnum(struct _pf_state *st, unsigned long val, int base, int upper, int width, char pad, int neg, int precision) {
    char tmp[24];
    int i = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

    if (val == 0) {
        tmp[i++] = '0';
    } else {
        while (val > 0) {
            tmp[i++] = digits[val % base];
            val /= base;
        }
    }

    int digitCount = i;
    int precPad = 0;
    if (precision > digitCount)
        precPad = precision - digitCount;

    int total = (neg ? 1 : 0) + precPad + digitCount;

    if (neg && pad == '0' && precision < 0) {
        _pf_putc(st, '-');
    }
    if (precision < 0 && pad == '0') {
        for (int w = total; w < width; w++)
            _pf_putc(st, '0');
    } else {
        for (int w = total; w < width; w++)
            _pf_putc(st, ' ');
    }
    if (neg && !(pad == '0' && precision < 0)) {
        _pf_putc(st, '-');
    }

    for (int p = 0; p < precPad; p++)
        _pf_putc(st, '0');

    while (i > 0)
        _pf_putc(st, tmp[--i]);
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    struct _pf_state st;
    st.buf = buf;
    st.pos = 0;
    st.max = size > 0 ? size - 1 : 0;

    while (*fmt) {
        if (*fmt != '%') {
            _pf_putc(&st, *fmt++);
            continue;
        }
        fmt++;

        char pad = ' ';
        int left_align = 0;
        while (*fmt == '0' || *fmt == '-' || *fmt == '+' || *fmt == ' ') {
            if (*fmt == '0') pad = '0';
            if (*fmt == '-') { left_align = 1; pad = ' '; }
            fmt++;
        }

        int width = 0;
        if (*fmt == '*') {
            width = va_arg(ap, int);
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
        }

        int precision = -1;
        if (*fmt == '.') {
            fmt++;
            precision = 0;
            if (*fmt == '*') {
                precision = va_arg(ap, int);
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9') {
                    precision = precision * 10 + (*fmt - '0');
                    fmt++;
                }
            }
        }

        int is_long = 0;
        if (*fmt == 'l') { is_long = 1; fmt++; if (*fmt == 'l') { is_long = 2; fmt++; } }
        else if (*fmt == 'h') { fmt++; if (*fmt == 'h') fmt++; }
        else if (*fmt == 'z') { is_long = 1; fmt++; }

        switch (*fmt) {
        case 'd': case 'i': {
            long val;
            if (is_long >= 1) val = va_arg(ap, long);
            else val = va_arg(ap, int);
            int neg = 0;
            unsigned long uval;
            if (val < 0) { neg = 1; uval = (unsigned long)(-val); }
            else uval = (unsigned long)val;
            if (left_align) {
                size_t before = st.pos;
                _pf_putnum(&st, uval, 10, 0, 0, pad, neg, precision);
                size_t len = st.pos - before;
                for (size_t w = len; (int)w < width; w++) _pf_putc(&st, ' ');
            } else {
                _pf_putnum(&st, uval, 10, 0, width, pad, neg, precision);
            }
            break;
        }
        case 'u': {
            unsigned long val;
            if (is_long >= 1) val = va_arg(ap, unsigned long);
            else val = va_arg(ap, unsigned int);
            if (left_align) {
                size_t before = st.pos;
                _pf_putnum(&st, val, 10, 0, 0, ' ', 0, precision);
                size_t len = st.pos - before;
                for (size_t w = len; (int)w < width; w++) _pf_putc(&st, ' ');
            } else {
                _pf_putnum(&st, val, 10, 0, width, pad, 0, precision);
            }
            break;
        }
        case 'x': case 'X': {
            unsigned long val;
            if (is_long >= 1) val = va_arg(ap, unsigned long);
            else val = va_arg(ap, unsigned int);
            int upper = (*fmt == 'X');
            if (left_align) {
                size_t before = st.pos;
                _pf_putnum(&st, val, 16, upper, 0, pad, 0, precision);
                size_t len = st.pos - before;
                for (size_t w = len; (int)w < width; w++) _pf_putc(&st, ' ');
            } else {
                _pf_putnum(&st, val, 16, upper, width, pad, 0, precision);
            }
            break;
        }
        case 'p': {
            void *val = va_arg(ap, void *);
            _pf_puts(&st, "0x");
            _pf_putnum(&st, (unsigned long)val, 16, 0, 0, '0', 0, -1);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (s == NULL) s = "(null)";
            int slen = (int)strlen(s);
            if (precision >= 0 && precision < slen) slen = precision;
            if (!left_align) {
                for (int w = slen; w < width; w++) _pf_putc(&st, ' ');
            }
            for (int i = 0; i < slen; i++) _pf_putc(&st, s[i]);
            if (left_align) {
                for (int w = slen; w < width; w++) _pf_putc(&st, ' ');
            }
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            _pf_putc(&st, c);
            break;
        }
        case '%':
            _pf_putc(&st, '%');
            break;
        default:
            _pf_putc(&st, '%');
            _pf_putc(&st, *fmt);
            break;
        }
        if (*fmt) fmt++;
    }

    if (size > 0) {
        if (st.pos < size)
            st.buf[st.pos] = '\0';
        else
            st.buf[size - 1] = '\0';
    }

    return (int)st.pos;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, (size_t)-1, fmt, ap);
    va_end(ap);
    return ret;
}

static char _printbuf[4096];

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(_printbuf, sizeof(_printbuf), fmt, ap);
    va_end(ap);
    _zos_syscall1(SYS_PRINT, (long)_printbuf);
    return ret;
}

int puts(const char *s) {
    _zos_syscall1(SYS_PRINT, (long)s);
    _zos_syscall1(SYS_PUTCHAR, (long)'\n');
    return 0;
}

int putchar(int c) {
    _zos_syscall1(SYS_PUTCHAR, (long)c);
    return c;
}

/* ========================================================================
   assert.h support
   ======================================================================== */

void __assert_fail(const char *expr, const char *file, int line, const char *func) {
    _zos_syscall1(SYS_PRINT, (long)"Assertion failed: ");
    _zos_syscall1(SYS_PRINT, (long)expr);
    _zos_syscall1(SYS_PRINT, (long)" at ");
    _zos_syscall1(SYS_PRINT, (long)file);
    _zos_syscall1(SYS_PRINT, (long)"\n");
    (void)line; (void)func;
    abort();
}

/* ========================================================================
   stdio.h FILE-based I/O
   ======================================================================== */

/* Static FILE objects for standard streams */
static FILE _stdin_file  = { .handle = -1, .pos = 0, .size = 0, .eof = 0,
                             .error = 0, .is_std = 1, .ungetc_buf = -1 };
static FILE _stdout_file = { .handle = -1, .pos = 0, .size = 0, .eof = 0,
                             .error = 0, .is_std = 2, .ungetc_buf = -1 };
static FILE _stderr_file = { .handle = -1, .pos = 0, .size = 0, .eof = 0,
                             .error = 0, .is_std = 3, .ungetc_buf = -1 };

FILE *stdin  = &_stdin_file;
FILE *stdout = &_stdout_file;
FILE *stderr = &_stderr_file;

FILE *fopen(const char *path, const char *mode) {
    if (path == NULL || mode == NULL) return NULL;

    int handle = -1;
    int want_read  = 0;
    int want_write = 0;
    int want_append = 0;

    if (mode[0] == 'r') {
        want_read = 1;
        if (mode[1] == '+') want_write = 1;
    } else if (mode[0] == 'w') {
        want_write = 1;
        if (mode[1] == '+') want_read = 1;
        /* Truncate: delete then create */
        _zos_syscall1(SYS_FDELETE, (long)path);
        handle = (int)_zos_syscall1(SYS_FCREATE, (long)path);
        if (handle < 0) return NULL;
    } else if (mode[0] == 'a') {
        want_write = 1;
        want_append = 1;
        if (mode[1] == '+') want_read = 1;
        /* Try open existing, create if not found */
        handle = (int)_zos_syscall1(SYS_OPEN, (long)path);
        if (handle < 0) {
            handle = (int)_zos_syscall1(SYS_FCREATE, (long)path);
            if (handle < 0) return NULL;
        }
    } else {
        return NULL;
    }

    /* For read and read+ modes, just open existing */
    if (mode[0] == 'r') {
        handle = (int)_zos_syscall1(SYS_OPEN, (long)path);
        if (handle < 0) return NULL;
    }

    unsigned long fileSize = (unsigned long)_zos_syscall1(SYS_GETSIZE, (long)handle);

    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (f == NULL) {
        _zos_syscall1(SYS_CLOSE, (long)handle);
        return NULL;
    }

    f->handle = handle;
    f->size = fileSize;
    f->pos = want_append ? fileSize : 0;
    f->eof = 0;
    f->error = 0;
    f->is_std = 0;
    f->ungetc_buf = -1;

    (void)want_read;
    (void)want_write;

    return f;
}

int fclose(FILE *stream) {
    if (stream == NULL) return EOF;
    if (stream->is_std) return 0;

    _zos_syscall1(SYS_CLOSE, (long)stream->handle);
    free(stream);
    return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (stream == NULL || ptr == NULL || size == 0 || nmemb == 0) return 0;

    if (stream->is_std == 1) {
        /* stdin: read characters via SYS_GETCHAR */
        size_t total = size * nmemb;
        char *dst = (char *)ptr;
        size_t i;
        for (i = 0; i < total; i++) {
            int c = (int)_zos_syscall0(SYS_GETCHAR);
            if (c <= 0) { stream->eof = 1; break; }
            dst[i] = (char)c;
        }
        return i / size;
    }

    size_t total = size * nmemb;
    size_t remaining = (stream->pos < stream->size) ? stream->size - stream->pos : 0;
    if (total > remaining) {
        total = remaining;
        stream->eof = 1;
    }
    if (total == 0) return 0;

    int ret = (int)_zos_syscall4(SYS_READ, (long)stream->handle,
                                  (long)ptr, (long)stream->pos, (long)total);
    if (ret < 0) {
        stream->error = 1;
        return 0;
    }

    stream->pos += (unsigned long)ret;
    return (size_t)ret / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (stream == NULL || ptr == NULL || size == 0 || nmemb == 0) return 0;

    if (stream->is_std) {
        /* stdout/stderr: print via SYS_PRINT/SYS_PUTCHAR */
        size_t total = size * nmemb;
        const char *src = (const char *)ptr;
        for (size_t i = 0; i < total; i++)
            _zos_syscall1(SYS_PUTCHAR, (long)(unsigned char)src[i]);
        return nmemb;
    }

    size_t total = size * nmemb;
    int ret = (int)_zos_syscall4(SYS_FWRITE, (long)stream->handle,
                                  (long)ptr, (long)stream->pos, (long)total);
    if (ret < 0) {
        stream->error = 1;
        return 0;
    }

    stream->pos += (unsigned long)ret;
    /* Update size if we wrote past the old end */
    if (stream->pos > stream->size)
        stream->size = stream->pos;
    return (size_t)ret / size;
}

int fseek(FILE *stream, long offset, int whence) {
    if (stream == NULL || stream->is_std) return -1;

    unsigned long newpos;
    switch (whence) {
    case SEEK_SET: newpos = (unsigned long)offset; break;
    case SEEK_CUR: newpos = stream->pos + (unsigned long)offset; break;
    case SEEK_END: newpos = stream->size + (unsigned long)offset; break;
    default: return -1;
    }

    stream->pos = newpos;
    stream->eof = 0;
    return 0;
}

long ftell(FILE *stream) {
    if (stream == NULL) return -1;
    return (long)stream->pos;
}

int fflush(FILE *stream) {
    /* No buffering in this implementation */
    (void)stream;
    return 0;
}

int feof(FILE *stream) {
    if (stream == NULL) return 0;
    return stream->eof;
}

int ferror(FILE *stream) {
    if (stream == NULL) return 0;
    return stream->error;
}

void clearerr(FILE *stream) {
    if (stream == NULL) return;
    stream->eof = 0;
    stream->error = 0;
}

int fgetc(FILE *stream) {
    if (stream == NULL) return EOF;

    /* Check ungetc buffer first */
    if (stream->ungetc_buf >= 0) {
        int c = stream->ungetc_buf;
        stream->ungetc_buf = -1;
        return c;
    }

    if (stream->is_std == 1) {
        int c = (int)_zos_syscall0(SYS_GETCHAR);
        if (c <= 0) { stream->eof = 1; return EOF; }
        return c;
    }

    if (stream->pos >= stream->size) {
        stream->eof = 1;
        return EOF;
    }

    unsigned char c;
    int ret = (int)_zos_syscall4(SYS_READ, (long)stream->handle,
                                  (long)&c, (long)stream->pos, 1L);
    if (ret <= 0) {
        stream->eof = 1;
        return EOF;
    }

    stream->pos++;
    return (int)c;
}

int getc(FILE *stream) {
    return fgetc(stream);
}

int ungetc(int c, FILE *stream) {
    if (stream == NULL || c == EOF) return EOF;
    stream->ungetc_buf = c;
    stream->eof = 0;
    return c;
}

char *fgets(char *s, int size, FILE *stream) {
    if (s == NULL || size <= 0 || stream == NULL) return NULL;

    int i = 0;
    while (i < size - 1) {
        int c = fgetc(stream);
        if (c == EOF) {
            if (i == 0) return NULL;
            break;
        }
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    s[i] = '\0';
    return s;
}

int fputs(const char *s, FILE *stream) {
    if (s == NULL || stream == NULL) return EOF;

    size_t len = strlen(s);
    size_t written = fwrite(s, 1, len, stream);
    return (written == len) ? 0 : EOF;
}

int fprintf(FILE *stream, const char *fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (stream == NULL || stream->is_std) {
        _zos_syscall1(SYS_PRINT, (long)buf);
    } else {
        fwrite(buf, 1, (size_t)(n > 0 ? n : 0), stream);
    }
    return n;
}

int vfprintf(FILE *stream, const char *fmt, va_list ap) {
    char buf[4096];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);

    if (stream == NULL || stream->is_std) {
        _zos_syscall1(SYS_PRINT, (long)buf);
    } else {
        fwrite(buf, 1, (size_t)(n > 0 ? n : 0), stream);
    }
    return n;
}

int vprintf(const char *fmt, va_list ap) {
    return vfprintf(stdout, fmt, ap);
}

int vsprintf(char *str, const char *fmt, va_list ap) {
    return vsnprintf(str, (size_t)-1, fmt, ap);
}

int remove(const char *path) {
    if (path == NULL) return -1;
    return (int)_zos_syscall1(SYS_FDELETE, (long)path);
}

int rename(const char *oldpath, const char *newpath) {
    /* No atomic rename syscall -- copy + delete */
    if (oldpath == NULL || newpath == NULL) return -1;

    int src = (int)_zos_syscall1(SYS_OPEN, (long)oldpath);
    if (src < 0) return -1;

    unsigned long sz = (unsigned long)_zos_syscall1(SYS_GETSIZE, (long)src);

    /* Create destination */
    _zos_syscall1(SYS_FDELETE, (long)newpath);
    int dst = (int)_zos_syscall1(SYS_FCREATE, (long)newpath);
    if (dst < 0) {
        _zos_syscall1(SYS_CLOSE, (long)src);
        return -1;
    }

    /* Copy in chunks */
    char copybuf[4096];
    unsigned long off = 0;
    while (off < sz) {
        unsigned long chunk = sz - off;
        if (chunk > sizeof(copybuf)) chunk = sizeof(copybuf);
        int r = (int)_zos_syscall4(SYS_READ, (long)src,
                                    (long)copybuf, (long)off, (long)chunk);
        if (r <= 0) break;
        _zos_syscall4(SYS_FWRITE, (long)dst,
                       (long)copybuf, (long)off, (long)r);
        off += (unsigned long)r;
    }

    _zos_syscall1(SYS_CLOSE, (long)src);
    _zos_syscall1(SYS_CLOSE, (long)dst);
    _zos_syscall1(SYS_FDELETE, (long)oldpath);
    return 0;
}

void perror(const char *s) {
    if (s != NULL && s[0] != '\0') {
        _zos_syscall1(SYS_PRINT, (long)s);
        _zos_syscall1(SYS_PRINT, (long)": ");
    }
    _zos_syscall1(SYS_PRINT, (long)"error\n");
}

FILE *tmpfile(void) {
    return NULL;
}

char *tmpnam(char *s) {
    (void)s;
    return NULL;
}

/* ========================================================================
   sscanf (minimal: %d, %x, %s, %c, %u, %ld, %lu)
   ======================================================================== */

int sscanf(const char *str, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int matched = 0;
    const char *s = str;

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            int is_long = 0;
            if (*fmt == 'l') { is_long = 1; fmt++; }

            if (*fmt == 'd' || *fmt == 'i') {
                while (isspace((unsigned char)*s)) s++;
                int neg = 0;
                if (*s == '-') { neg = 1; s++; }
                else if (*s == '+') s++;
                if (!isdigit((unsigned char)*s)) goto done;
                long val = 0;
                while (isdigit((unsigned char)*s))
                    val = val * 10 + (*s++ - '0');
                if (neg) val = -val;
                if (is_long) *va_arg(ap, long *) = val;
                else *va_arg(ap, int *) = (int)val;
                matched++;
            } else if (*fmt == 'u') {
                while (isspace((unsigned char)*s)) s++;
                if (!isdigit((unsigned char)*s)) goto done;
                unsigned long val = 0;
                while (isdigit((unsigned char)*s))
                    val = val * 10 + (unsigned long)(*s++ - '0');
                if (is_long) *va_arg(ap, unsigned long *) = val;
                else *va_arg(ap, unsigned int *) = (unsigned int)val;
                matched++;
            } else if (*fmt == 'x' || *fmt == 'X') {
                while (isspace((unsigned char)*s)) s++;
                if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
                if (!isxdigit((unsigned char)*s)) goto done;
                unsigned long val = 0;
                while (isxdigit((unsigned char)*s)) {
                    int d;
                    if (*s >= '0' && *s <= '9') d = *s - '0';
                    else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
                    else d = *s - 'A' + 10;
                    val = val * 16 + (unsigned long)d;
                    s++;
                }
                if (is_long) *va_arg(ap, unsigned long *) = val;
                else *va_arg(ap, unsigned int *) = (unsigned int)val;
                matched++;
            } else if (*fmt == 's') {
                while (isspace((unsigned char)*s)) s++;
                char *dst = va_arg(ap, char *);
                if (*s == '\0') goto done;
                while (*s && !isspace((unsigned char)*s))
                    *dst++ = *s++;
                *dst = '\0';
                matched++;
            } else if (*fmt == 'c') {
                if (*s == '\0') goto done;
                *va_arg(ap, char *) = *s++;
                matched++;
            } else if (*fmt == '%') {
                if (*s != '%') goto done;
                s++;
            }
            fmt++;
        } else if (isspace((unsigned char)*fmt)) {
            while (isspace((unsigned char)*s)) s++;
            fmt++;
        } else {
            if (*s != *fmt) goto done;
            s++;
            fmt++;
        }
    }

done:
    va_end(ap);
    return matched;
}

/* ========================================================================
   fcntl.h / unistd.h POSIX-style file I/O
   ======================================================================== */

/* fd position tracking for POSIX read/write (MontaukOS uses explicit offsets) */
#define _FD_POS_MAX 64
static unsigned long _fd_pos[_FD_POS_MAX];

int open(const char *path, int flags, ...) {
    if (path == NULL) return -1;
    int h;

    if (flags & 0x40 /* O_CREAT */) {
        _zos_syscall1(SYS_FDELETE, (long)path);
        h = (int)_zos_syscall1(SYS_FCREATE, (long)path);
    } else {
        h = (int)_zos_syscall1(SYS_OPEN, (long)path);
    }

    if (h >= 0 && h < _FD_POS_MAX)
        _fd_pos[h] = 0;
    return h;
}

int read(int fd, void *buf, size_t count) {
    if (buf == NULL) return -1;
    unsigned long pos = (fd >= 0 && fd < _FD_POS_MAX) ? _fd_pos[fd] : 0;
    int ret = (int)_zos_syscall4(SYS_READ, (long)fd, (long)buf, (long)pos, (long)count);
    if (ret > 0 && fd >= 0 && fd < _FD_POS_MAX)
        _fd_pos[fd] += (unsigned long)ret;
    return ret;
}

int write(int fd, const void *buf, size_t count) {
    if (buf == NULL) return -1;
    unsigned long pos = (fd >= 0 && fd < _FD_POS_MAX) ? _fd_pos[fd] : 0;
    int ret = (int)_zos_syscall4(SYS_FWRITE, (long)fd, (long)buf, (long)pos, (long)count);
    if (ret > 0 && fd >= 0 && fd < _FD_POS_MAX)
        _fd_pos[fd] += (unsigned long)ret;
    return ret;
}

int close(int fd) {
    if (fd >= 0 && fd < _FD_POS_MAX)
        _fd_pos[fd] = 0;
    _zos_syscall1(SYS_CLOSE, (long)fd);
    return 0;
}

long lseek(int fd, long offset, int whence) {
    unsigned long pos = (fd >= 0 && fd < _FD_POS_MAX) ? _fd_pos[fd] : 0;
    unsigned long newpos;
    switch (whence) {
    case 0: /* SEEK_SET */
        newpos = (unsigned long)offset;
        break;
    case 1: /* SEEK_CUR */
        newpos = pos + (unsigned long)offset;
        break;
    case 2: /* SEEK_END */
        newpos = (unsigned long)_zos_syscall1(SYS_GETSIZE, (long)fd) + (unsigned long)offset;
        break;
    default:
        return -1;
    }
    if (fd >= 0 && fd < _FD_POS_MAX)
        _fd_pos[fd] = newpos;
    return (long)newpos;
}

/* ========================================================================
   sys/stat.h functions
   ======================================================================== */

int mkdir(const char *path, unsigned int mode) {
    (void)mode;
    if (path == NULL) return -1;
    return (int)_zos_syscall1(SYS_FMKDIR, (long)path);
}

int stat(const char *path, struct stat *buf) {
    if (path == NULL || buf == NULL) return -1;
    int h = (int)_zos_syscall1(SYS_OPEN, (long)path);
    if (h < 0) return -1;
    buf->st_size = (unsigned long)_zos_syscall1(SYS_GETSIZE, (long)h);
    _zos_syscall1(SYS_CLOSE, (long)h);
    return 0;
}

int fstat(int fd, struct stat *buf) {
    if (buf == NULL) return -1;
    buf->st_size = (unsigned long)_zos_syscall1(SYS_GETSIZE, (long)fd);
    return 0;
}

/* ========================================================================
   stdlib.h: system() and atexit()
   ======================================================================== */

static void (*_atexit_funcs[32])(void);
static int _atexit_count = 0;

int atexit(void (*func)(void)) {
    if (_atexit_count >= 32 || func == NULL) return -1;
    _atexit_funcs[_atexit_count++] = func;
    return 0;
}

/* ========================================================================
   stdlib.h: qsort
   ======================================================================== */

void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *)) {
    if (nmemb < 2 || base == NULL || compar == NULL) return;

    /* Simple insertion sort for small arrays, good enough for libc */
    unsigned char *b = (unsigned char *)base;
    unsigned char tmp[256];
    int use_heap = (size > sizeof(tmp));
    unsigned char *t = use_heap ? (unsigned char *)malloc(size) : tmp;
    if (t == NULL) return;

    for (size_t i = 1; i < nmemb; i++) {
        unsigned char *cur = b + i * size;
        size_t j = i;
        while (j > 0) {
            unsigned char *prev = b + (j - 1) * size;
            if (compar(prev, cur) <= 0) break;
            j--;
            cur = prev;
        }
        if (j != i) {
            unsigned char *dst = b + j * size;
            unsigned char *src = b + i * size;
            memcpy(t, src, size);
            memmove(dst + size, dst, (i - j) * size);
            memcpy(dst, t, size);
        }
    }

    if (use_heap) free(t);
}

/* ========================================================================
   stdlib.h: rand / srand
   ======================================================================== */

static unsigned int _rand_seed = 1;

int rand(void) {
    _rand_seed = _rand_seed * 1103515245 + 12345;
    return (int)((_rand_seed >> 16) & 0x7fff);
}

void srand(unsigned int seed) {
    _rand_seed = seed;
}

div_t div(int numer, int denom) {
    div_t r;
    r.quot = numer / denom;
    r.rem  = numer % denom;
    return r;
}

ldiv_t ldiv(long numer, long denom) {
    ldiv_t r;
    r.quot = numer / denom;
    r.rem  = numer % denom;
    return r;
}

long atol(const char *s) {
    return strtol(s, NULL, 10);
}
