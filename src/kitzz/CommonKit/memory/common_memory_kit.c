#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "CommonKit/memory/common_memory_kit.h"

char *zz_vm_read_string(const zz_ptr_t address) {
    const char *start_addr = (const char *)address;
    zz_size_t string_limit = 1024;
    unsigned int i;
    char *result;

    for (i = 0; i < string_limit; i++) {
        if (*(start_addr + i) == '\0')
            break;
    }
    if (i == string_limit)
        return NULL;
    else {
        result = (char *)malloc(i + 1);
        memcpy(result, (const zz_ptr_t)start_addr, i + 1);
        return result;
    }
}

zz_ptr_t zz_vm_search_data(const zz_ptr_t start_addr, zz_ptr_t end_addr, char *data, zz_size_t data_len) {
    uintptr_t current = (uintptr_t)start_addr;
    uintptr_t end = (uintptr_t)end_addr;

    if (!current || !data || data_len == 0 || current >= end)
        return NULL;

    /* ARM and Thumb code caves are at least halfword aligned. Search every
     * halfword and never read beyond the mapping end. */
    while (current <= end && (zz_size_t)(end - current) >= data_len) {
        if (!memcmp((const void *)current, data, data_len))
            return (zz_ptr_t)current;
        current += 2;
    }
    return NULL;
}

zz_addr_t zz_vm_align_floor(zz_addr_t address, zz_size_t range_size) {
    zz_addr_t result;
    result = address & ~(range_size - 1);
    return result;
}

zz_addr_t zz_vm_align_ceil(zz_addr_t address, zz_size_t range_size) {
    zz_addr_t result;
    result = (address + range_size - 1) & ~(range_size - 1);
    return result;
}