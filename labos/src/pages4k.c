#include "pages4k.h"

#include "lib.h"

#define PRESENT 1ull
#define LARGE (1ull << 7)      // PS: this entry maps a 2 MB or 1 GB page
#define PAT_LARGE (1ull << 12)  // PAT bit of a large page entry
#define PAT_4K (1ull << 7)      // PAT bit of a 4 KB entry: the same position as PS
#define ADDR 0x000ffffffffff000ull
#define TABLES_MAX 64

static u64 old_cr3;
static u64 *tables[TABLES_MAX];
static int ntables;

static u64 *new_table(void) {
    if (ntables == TABLES_MAX) return 0;
    u64 *t = alloc_pages(1);
    if (t) tables[ntables++] = t;
    return t;
}

static u64 *copy_table(const u64 *src) {
    u64 *t = new_table();
    if (t) memcpy(t, src, 4096);
    return t;
}

// The table a non-large entry points to, made private to labos: copied on the first visit
static u64 *private_child(u64 *entry) {
    u64 *child = (u64 *)(*entry & ADDR);
    for (int i = 0; i < ntables; i++) {
        if (tables[i] == child) return child;
    }
    u64 *copy = copy_table(child);
    if (copy) *entry = (*entry & ~ADDR) | (u64)copy;
    return copy;
}

// A large page entry becomes a table of 512 smaller ones with the same flags; the PAT bit moves
// to its place in the smaller entries when they are 4 KB pages
static u64 *split(u64 *entry, u64 child_size, int to_4k) {
    u64 *t = new_table();
    if (!t) return 0;
    u64 base = *entry & ADDR & ~(child_size * 512 - 1);
    u64 flags = *entry & ~ADDR & ~PAT_LARGE;
    if (to_4k) {
        flags &= ~LARGE;
        if (*entry & PAT_LARGE) flags |= PAT_4K;
    } else if (*entry & PAT_LARGE) {
        flags |= PAT_LARGE;
    }
    for (u64 i = 0; i < 512; i++) t[i] = (base + i * child_size) | flags;
    *entry = (*entry & ~ADDR & ~LARGE & ~PAT_LARGE) | (u64)t;
    return t;
}

static void release_tables(void) {
    for (int i = 0; i < ntables; i++) BS->free_pages((efi_phys_addr)tables[i], 1);
    ntables = 0;
}

int pages4k_begin(u64 base, u64 size) {
    u64 cr3, cr4;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    if (cr4 & (1ull << 12)) {  // LA57: five levels, not handled
        log_line("pages4k: five level paging, 4 KB pages not made");
        return 0;
    }
    ntables = 0;
    u64 *pml4 = copy_table((const u64 *)(cr3 & ADDR));
    if (!pml4) return 0;
    for (u64 va = base; va < base + size; va += 2ull << 20) {
        u64 *l4 = &pml4[(va >> 39) & 511];
        if (!(*l4 & PRESENT)) goto fail;
        u64 *pdpt = private_child(l4);
        if (!pdpt) goto fail;
        u64 *l3 = &pdpt[(va >> 30) & 511];
        if (!(*l3 & PRESENT)) goto fail;
        u64 *pd = *l3 & LARGE ? split(l3, 2ull << 20, 0) : private_child(l3);
        if (!pd) goto fail;
        u64 *l2 = &pd[(va >> 21) & 511];
        if (!(*l2 & PRESENT)) goto fail;
        if (*l2 & LARGE) {
            if (!split(l2, 4096, 1)) goto fail;
        } else if (!private_child(l2)) {
            goto fail;
        }
    }
    old_cr3 = cr3;
    // Loading CR3 also empties the TLB of the old entries
    __asm__ volatile("mov %0, %%cr3" ::"r"((u64)pml4 | (cr3 & ~ADDR)) : "memory");
    log_line("pages4k: %lu KB at %lx in 4 KB pages, %d tables", size / 1024, base, ntables);
    return 1;
fail:
    log_line("pages4k: unexpected page tables at %lx, 4 KB pages not made", base);
    release_tables();
    return 0;
}

void pages4k_end(void) {
    if (!old_cr3) return;
    __asm__ volatile("mov %0, %%cr3" ::"r"(old_cr3) : "memory");
    old_cr3 = 0;
    release_tables();
}
