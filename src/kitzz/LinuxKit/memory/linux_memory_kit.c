#include "LinuxKit/memory/linux_memory_kit.h"
#include "CommonKit/memory/common_memory_kit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ZZ_LINUX_MAX_MAPPINGS 4096

MemoryLayout *zz_linux_vm_get_memory_layout_via_pid(pid_t pid) {
    char filename[64];
    char buf[512];
    FILE *fp;
    MemoryLayout *mlayout;

    mlayout = (MemoryLayout *)calloc(1, sizeof(MemoryLayout));
    if (!mlayout)
        return NULL;

    if (pid > 0)
        snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);
    else
        snprintf(filename, sizeof(filename), "/proc/self/maps");

    fp = fopen(filename, "r");
    if (!fp) {
        free(mlayout);
        return NULL;
    }

    while (fgets(buf, sizeof(buf), fp) != NULL) {
        unsigned long start, end, inode;
        unsigned long long offset;
        unsigned dev, sdev;
        char prot[5];

        if (mlayout->size >= ZZ_LINUX_MAX_MAPPINGS)
            break;
        if (sscanf(buf, "%lx-%lx %4s %llx %x:%x %lu",
                   &start, &end, prot, &offset, &dev, &sdev, &inode) != 7)
            continue;

        mlayout->mem[mlayout->size].start = (zz_ptr_t)start;
        mlayout->mem[mlayout->size].end = (zz_ptr_t)end;
        mlayout->mem[mlayout->size].flags =
            (prot[0] == 'r' ? (1 << 0) : 0) |
            (prot[1] == 'w' ? (1 << 1) : 0) |
            (prot[2] == 'x' ? (1 << 2) : 0);
        mlayout->size++;
    }

    fclose(fp);
    return mlayout;
}

zz_ptr_t zz_linux_vm_search_code_cave(zz_addr_t address, zz_size_t range_size, zz_size_t size) {
    zz_addr_t lower, upper;
    zz_ptr_t result = NULL;
    char *zero_array;
    MemoryLayout *mlayout;
    int i;

    if (!address || !size)
        return NULL;

    lower = address > range_size ? address - range_size : 0;
    upper = address + range_size;
    if (upper < address)
        upper = (zz_addr_t)-1;

    zero_array = (char *)calloc(1, size);
    if (!zero_array)
        return NULL;

    mlayout = zz_linux_vm_get_memory_layout_via_pid(-1);
    if (!mlayout) {
        free(zero_array);
        return NULL;
    }

    for (i = 0; i < mlayout->size; ++i) {
        zz_addr_t start, end;

        if (mlayout->mem[i].flags != ((1 << 0) | (1 << 2)))
            continue;

        start = (zz_addr_t)mlayout->mem[i].start;
        end = (zz_addr_t)mlayout->mem[i].end;
        if (start < lower)
            start = lower;
        if (end > upper)
            end = upper;
        if (end <= start || end - start < size)
            continue;

        result = zz_vm_search_data((zz_ptr_t)start, (zz_ptr_t)end, zero_array, size);
        if (result)
            break;
    }

    free(mlayout);
    free(zero_array);
    return result;
}
