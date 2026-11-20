#include "allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define DEFAULT_ALLOCATOR_CAPACITY 4

ZzAllocator *ZzNewAllocator() {
    if (!ZzMemoryIsSupportAllocateRXPage())
        return NULL;

    ZzAllocator *allocator;
    allocator = (ZzAllocator *)zz_malloc_with_zero(sizeof(ZzAllocator));
    if (!allocator)
        return NULL;
    allocator->memory_pages = (ZzMemoryPage **)zz_malloc_with_zero(sizeof(ZzMemoryPage *) * DEFAULT_ALLOCATOR_CAPACITY);
    if (!allocator->memory_pages) {
        free(allocator);
        return NULL;
    }
    allocator->size     = 0;
    allocator->capacity = DEFAULT_ALLOCATOR_CAPACITY;
    return allocator;
}

ZzMemoryPage *ZzNewMemoryPage() {
    zz_size_t page_size = ZzMemoryGetPageSzie();
    zz_ptr_t page_ptr   = NULL;
    ZzMemoryPage *page  = NULL;

    page_ptr = ZzMemoryAllocatePages(1);
    if (!page_ptr) {
        return NULL;
    }
    if (!ZzMemoryProtectAsExecutable((zz_addr_t)page_ptr, page_size)) {
        ZZ_ERROR_LOG("ZzMemoryProtectAsExecutable error at %p", page_ptr);
        munmap(page_ptr, page_size);
        return NULL;
    }

    page = (ZzMemoryPage *)zz_malloc_with_zero(sizeof(ZzMemoryPage));
    if (!page) {
        munmap(page_ptr, page_size);
        return NULL;
    }
    page->base      = page_ptr;
    page->curr_pos  = page_ptr;
    page->size      = page_size;
    page->used_size = 0;
    return page;
}

ZzMemoryPage *ZzNewNearMemoryPage(zz_addr_t address, zz_size_t redirect_range_size) {
    zz_size_t page_size = ZzMemoryGetPageSzie();
    zz_ptr_t page_ptr   = NULL;
    ZzMemoryPage *page  = NULL;

    page_ptr = ZzMemoryAllocateNearPages(address, redirect_range_size, 1);
    if (!page_ptr) {
        return NULL;
    }

    if (!ZzMemoryProtectAsExecutable((zz_addr_t)page_ptr, page_size)) {
        ZZ_ERROR_LOG("ZzMemoryProtectAsExecutable error at %p", page_ptr);
        munmap(page_ptr, page_size);
        return NULL;
    }

    page = (ZzMemoryPage *)zz_malloc_with_zero(sizeof(ZzMemoryPage));
    if (!page) {
        munmap(page_ptr, page_size);
        return NULL;
    }

    page->base = page_ptr;

    if ((zz_addr_t)page_ptr > address && ((zz_addr_t)page_ptr + page_size) > (address + redirect_range_size)) {
        page->size      = (address + redirect_range_size) - (zz_addr_t)page_ptr;
        page->used_size = 0;
        page->curr_pos  = page_ptr;
    } else if ((zz_addr_t)page_ptr < address && (zz_addr_t)page_ptr < (address - redirect_range_size)) {
        page->size      = page_size;
        page->used_size = (address - redirect_range_size) - (zz_addr_t)page_ptr;
        page->curr_pos  = (zz_ptr_t)(address - redirect_range_size);
    } else {
        page->size      = page_size;
        page->used_size = 0;
        page->curr_pos  = page_ptr;
    }
    page->isCodeCave = FALSE;
    return page;
}

ZzMemoryPage *ZzNewNearCodeCave(zz_addr_t address, zz_size_t redirect_range_size, zz_size_t code_slice_size) {
    zz_ptr_t cave_ptr = NULL;
    ZzMemoryPage *page = NULL;
    zz_size_t cave_size = code_slice_size;

    cave_ptr = ZzMemorySearchCodeCave(address, redirect_range_size, cave_size);

    if (!cave_ptr)
        return NULL;

    page = (ZzMemoryPage *)zz_malloc_with_zero(sizeof(ZzMemoryPage));
    if (!page)
        return NULL;
    page->base       = cave_ptr;
    page->curr_pos   = cave_ptr;
    page->size       = cave_size;
    page->used_size  = 0;
    page->isCodeCave = TRUE;
    return page;
}

ZZSTATUS ZzAddMemoryPage(ZzAllocator *allocator, ZzMemoryPage *page) {
    if (!allocator || !page)
        return ZZ_FAILED;
    if (allocator->size >= allocator->capacity) {
        ZzMemoryPage **pages = realloc(allocator->memory_pages, sizeof(ZzMemoryPage *) * (allocator->capacity) * 2);
        if (!pages) {
            return ZZ_FAILED;
        }
        allocator->capacity     = allocator->capacity * 2;
        allocator->memory_pages = pages;
    }
    allocator->memory_pages[allocator->size++] = page;
    return ZZ_SUCCESS;
}

//  1. try allocate from the history pages
//  2. try allocate a new page
//  3. add it to the page manager
ZzCodeSlice *ZzNewCodeSlice(ZzAllocator *allocator, zz_size_t code_slice_size) {
    ZzCodeSlice *code_slice = NULL;
    ZzMemoryPage *page      = NULL;
    int i;

    if (!allocator || code_slice_size == 0 || code_slice_size > ZzMemoryGetPageSzie())
        return NULL;

    for (i = 0; i < allocator->size; i++) {
        page = allocator->memory_pages[i];
        if (!page)
            continue;
        // 1. page is initialized
        // 2. can't be codecave
        // 3. the rest memory of this page is enough for code_slice_size
        // 4. the page address is near

        if ((zz_addr_t)page->curr_pos % 4) {
            int t = 4 - (zz_addr_t)page->curr_pos % 4;
            page->used_size += t;
            page->curr_pos += t;
        }

        if (page && page->base && !page->isCodeCave &&
            page->used_size <= page->size && (page->size - page->used_size) >= code_slice_size) {
            code_slice = (ZzCodeSlice *)zz_malloc_with_zero(sizeof(ZzCodeSlice));
            if (!code_slice)
                return NULL;
            code_slice->data = page->curr_pos;
            code_slice->size = code_slice_size;

            page->curr_pos += code_slice_size;
            page->used_size += code_slice_size;
            return code_slice;
        }
    }

    page = ZzNewMemoryPage();
    if (!page)
        return NULL;
    if (ZzAddMemoryPage(allocator, page) != ZZ_SUCCESS) {
        munmap(page->base, page->size);
        free(page);
        return NULL;
    }

    if ((zz_addr_t)page->curr_pos % 4) {
        int t = 4 - (zz_addr_t)page->curr_pos % 4;
        page->used_size += t;
        page->curr_pos += t;
    }

    code_slice = (ZzCodeSlice *)zz_malloc_with_zero(sizeof(ZzCodeSlice));
    if (!code_slice)
        return NULL;
    code_slice->data = page->curr_pos;
    code_slice->size = code_slice_size;

    page->curr_pos += code_slice_size;
    page->used_size += code_slice_size;

    return code_slice;
}

//  1. try allocate from the history pages
//  2. try allocate a new near page
//  3. add it to the page manager
ZzCodeSlice *ZzNewNearCodeSlice(ZzAllocator *allocator, zz_addr_t address, zz_size_t redirect_range_size,
                                zz_size_t code_slice_size) {
    ZzCodeSlice *code_slice;
    ZzMemoryPage *page;
    zz_addr_t lower, upper;
    int i;

    if (!allocator || !address || code_slice_size == 0)
        return NULL;

    lower = address > redirect_range_size ? address - redirect_range_size : 0;
    upper = address + redirect_range_size;
    if (upper < address)
        upper = (zz_addr_t)-1;

    /* Reuse an existing allocated page only when the next slice is wholly
     * inside both the page and the architecture's branch window. */
    for (i = 0; i < allocator->size; ++i) {
        zz_addr_t candidate, page_start, page_end;
        zz_size_t used;

        page = allocator->memory_pages[i];
        if (!page || !page->base || page->isCodeCave)
            continue;

        page_start = (zz_addr_t)page->base;
        page_end = page_start + page->size;
        if (page_end < page_start)
            continue;

        candidate = (zz_addr_t)page->curr_pos;
        candidate = (candidate + 3) & ~((zz_addr_t)3);
        if (candidate < page_start || candidate > page_end ||
            candidate < lower || candidate > upper)
            continue;
        if (code_slice_size > page_end - candidate ||
            code_slice_size > upper - candidate)
            continue;

        used = (zz_size_t)(candidate - page_start);
        code_slice = (ZzCodeSlice *)zz_malloc_with_zero(sizeof(ZzCodeSlice));
        if (!code_slice)
            return NULL;

        code_slice->data = (zz_ptr_t)candidate;
        code_slice->size = code_slice_size;
        code_slice->isCodeCave = FALSE;
        page->curr_pos = (zz_ptr_t)(candidate + code_slice_size);
        page->used_size = used + code_slice_size;
        return code_slice;
    }

    page = ZzNewNearCodeCave(address, redirect_range_size, code_slice_size);
    if (!page)
        return NULL;

    code_slice = (ZzCodeSlice *)zz_malloc_with_zero(sizeof(ZzCodeSlice));
    if (!code_slice) {
        free(page);
        return NULL;
    }
    code_slice->data = page->curr_pos;
    code_slice->size = code_slice_size;
    code_slice->isCodeCave = TRUE;
    page->curr_pos = (zz_ptr_t)((zz_addr_t)page->curr_pos + code_slice_size);
    page->used_size = code_slice_size;

    /* Code-cave pages describe a bounded slice of an existing RX mapping and
     * are intentionally not added to the mmap-page allocator. */
    free(page);
    return code_slice;
}
