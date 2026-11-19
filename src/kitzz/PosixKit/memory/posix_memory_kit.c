#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "PosixKit/memory/posix_memory_kit.h"

// http://renatocunha.com/blog/2015/12/msync-pointer-validity/
bool zz_posix_vm_check_address_valid_via_msync(const zz_ptr_t p) {
    int ret = 0;
    zz_size_t page_size;
    zz_ptr_t base;
    /* get the page size */
    page_size = zz_posix_vm_get_page_size();
    /* find the address of the page that contains p */
    base = (void *)((((size_t)p) / page_size) * page_size);
    /* call msync, if it returns non-zero, return FALSE */
    ret = msync(base, page_size, MS_ASYNC) != -1;
    return ret ? ret : errno != ENOMEM;
}

// ATTENTION !!!
// lldb is still catch EXC_BAD_ACCESS, without lldb is ok.
// https://www.cocoawithlove.com/2010/10/testing-if-arbitrary-pointer-is-valid.html
// https://stackoverflow.com/questions/26829119/how-to-make-lldb-ignore-exc-bad-access-exception
// ---check start---
#include <setjmp.h>
#include <signal.h>

static sigjmp_buf sigjmp_env;

void PointerReadFailedHandler(int signum) { siglongjmp(sigjmp_env, 1); }

bool zz_posix_vm_check_address_valid_via_signal(zz_ptr_t p) {
    // Set up SIGSEGV and SIGBUS handlers
    struct sigaction new_segv_action, old_segv_action;
    struct sigaction new_bus_action, old_bus_action;
    new_segv_action.sa_handler = PointerReadFailedHandler;
    new_bus_action.sa_handler  = PointerReadFailedHandler;
    sigemptyset(&new_segv_action.sa_mask);
    sigemptyset(&new_bus_action.sa_mask);
    new_segv_action.sa_flags = 0;
    new_bus_action.sa_flags  = 0;
    sigaction(SIGSEGV, &new_segv_action, &old_segv_action);
    sigaction(SIGBUS, &new_bus_action, &old_bus_action);

    // The signal handler will return us to here if a signal is raised
    if (sigsetjmp(sigjmp_env, 1)) {
        sigaction(SIGSEGV, &old_segv_action, NULL);
        sigaction(SIGBUS, &old_bus_action, NULL);
        return FALSE;
    }
    // ATTENTION !!! this function is conflict with LLDB, reason is below.
    // lldb is still catch EXC_BAD_ACCESS, without lldb is ok.
    // or you can use `zz_check_address_valid_via_mem` replace
    // https://stackoverflow.com/questions/26829119/how-to-make-lldb-ignore-exc-bad-access-exception
    char x = *(char *)p;
    return TRUE;
}

zz_size_t zz_posix_vm_get_page_size() { return getpagesize(); }

// int mprotect(void *addr, size_t len, int prot);
bool zz_posix_vm_protect(const zz_addr_t address, zz_size_t size, int page_prot) {
    int r;

    zz_size_t page_size;
    zz_addr_t aligned_addr;
    zz_size_t aligned_size;

    page_size    = zz_posix_vm_get_page_size();
    aligned_addr = (zz_addr_t)address & ~(page_size - 1);
    aligned_size = (1 + ((address + size - 1 - aligned_addr) / page_size)) * page_size;

    r = mprotect((zz_ptr_t)aligned_addr, aligned_size, page_prot);
    if (r == -1) {
        ZZ_ERROR_LOG("r = %d, at (%p) error!", r, (zz_ptr_t)address);
        return FALSE;
    }
    return TRUE;
}

bool zz_posix_vm_protect_as_executable(const zz_addr_t address, zz_size_t size) {
    return zz_posix_vm_protect(address, size, (PROT_READ | PROT_EXEC | PROT_WRITE));
}

bool zz_posxi_vm_protect_as_writable(const zz_addr_t address, zz_size_t size) {
    if (!zz_posix_vm_protect(address, size, (PROT_READ | PROT_EXEC | PROT_WRITE)))
        return FALSE;
    return TRUE;
}

//  void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset);
zz_ptr_t zz_posix_vm_allocate_pages(zz_size_t n_pages) {
    zz_ptr_t page_mmap;
    int kr;
    zz_size_t page_size;
    page_size = zz_posix_vm_get_page_size();

    if (n_pages <= 0) {
        n_pages = 1;
    }

    page_mmap = mmap(0, page_size * n_pages, PROT_WRITE | PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    if (page_mmap == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }

    if (!zz_posix_vm_protect((zz_addr_t)page_mmap, page_size * n_pages, (PROT_WRITE | PROT_READ)))
        return NULL;
    return (zz_ptr_t)page_mmap;
}

zz_ptr_t zz_posix_vm_allocate(zz_size_t size) {
    zz_size_t page_size;
    zz_ptr_t result;
    zz_size_t n_pages;

    page_size = zz_posix_vm_get_page_size();
    n_pages   = ((size + page_size - 1) & ~(page_size - 1)) / page_size;

    result = zz_posix_vm_allocate_pages(n_pages);
    return (zz_ptr_t)result;
}

zz_ptr_t zz_posix_vm_allocate_near_pages(zz_addr_t address, zz_size_t range_size, zz_size_t n_pages) {
    zz_addr_t aligned_addr;
    zz_size_t page_size;
    zz_size_t length;
    zz_size_t step;
    unsigned int i;

    page_size = zz_posix_vm_get_page_size();
    if (page_size == 0 || address == 0)
        return NULL;
    if (n_pages == 0)
        n_pages = 1;
    if (n_pages > ((zz_size_t)-1) / page_size)
        return NULL;
    length = page_size * n_pages;
    aligned_addr = address & ~((zz_addr_t)page_size - 1);
    step = range_size / 32;
    if (step < page_size)
        step = page_size;
    step = (step + page_size - 1) & ~((zz_size_t)page_size - 1);

    /*
     * mmap's address is only a hint. Never use MAP_FIXED here: the old code
     * could silently replace an unrelated mapping in the target process.
     * Accept only mappings actually returned inside the requested branch
     * window; otherwise unmap and try another hint.
     */
    for (i = 0; i < 65; ++i) {
        zz_addr_t hint_addr;
        zz_ptr_t mapped;
        if (i == 0) {
            hint_addr = aligned_addr;
        } else if (i & 1) {
            zz_size_t delta = ((zz_size_t)(i + 1) / 2) * step;
            if (aligned_addr > (zz_addr_t)-1 - delta)
                continue;
            hint_addr = aligned_addr + delta;
        } else {
            zz_size_t delta = ((zz_size_t)i / 2) * step;
            if (aligned_addr < delta)
                continue;
            hint_addr = aligned_addr - delta;
        }

        mapped = mmap((zz_ptr_t)hint_addr, length, PROT_READ | PROT_WRITE,
                      MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (mapped == MAP_FAILED)
            continue;

        {
            zz_addr_t result = (zz_addr_t)mapped;
            zz_addr_t lower = aligned_addr > range_size ? aligned_addr - range_size : 0;
            zz_addr_t upper = aligned_addr + range_size;
            if (upper < aligned_addr)
                upper = (zz_addr_t)-1;
            if (result >= lower && result <= upper && length <= upper - result)
                return mapped;
        }
        munmap(mapped, length);
    }
    return NULL;
}

zz_ptr_t zz_posix_vm_search_text_code_cave(zz_addr_t address, zz_size_t range_size, zz_size_t size) {
    char zeroArray[128];
    char readZeroArray[128];
    zz_addr_t aligned_addr, tmp_addr, target_search_start, target_search_end;
    zz_size_t page_size;

    memset(zeroArray, 0, 128);

    page_size           = zz_posix_vm_get_page_size();
    aligned_addr        = (zz_addr_t)address & ~(page_size - 1);
    target_search_start = aligned_addr - range_size;
    target_search_end   = aligned_addr + range_size;

    ZZ_DEBUG_LOG("searching for %p cave, use 0x1000 interval.", (zz_ptr_t)address);
    for (tmp_addr = target_search_start; tmp_addr < target_search_end; tmp_addr += 0x1000) {
        if (zz_posix_vm_check_address_valid_via_signal((zz_ptr_t)tmp_addr))
            if (memcpy(readZeroArray, (zz_ptr_t)tmp_addr, 128)) {
                if (!memcmp(readZeroArray, zeroArray, 128)) {
                    return (void *)tmp_addr;
                }
            }
    }
    return NULL;
}

/*
  ref:
  substitute/lib/darwin/execmem.c:execmem_foreign_write_with_pc_patch
  frida-gum-master/gum/gummemory.c:gum_memory_patch_code

  frida-gum-master/gum/backend-darwin/gummemory-darwin.c:gum_alloc_n_pages

  mach mmap use __vm_allocate and __vm_map
  https://github.com/bminor/glibc/blob/master/sysdeps/mach/hurd/mmap.c
  https://github.com/bminor/glibc/blob/master/sysdeps/mach/munmap.c

  http://shakthimaan.com/downloads/hurd/A.Programmers.Guide.to.the.Mach.System.Calls.pdf
*/

static bool zz_posix_vm_query_protection(zz_addr_t address, int *protection) {
    FILE *maps;
    char line[512];
    unsigned long start, end;
    char permissions[5];

    maps = fopen("/proc/self/maps", "r");
    if (!maps)
        return FALSE;

    while (fgets(line, sizeof(line), maps)) {
        if (sscanf(line, "%lx-%lx %4s", &start, &end, permissions) != 3)
            continue;
        if (address < (zz_addr_t)start || address >= (zz_addr_t)end)
            continue;
        *protection = (permissions[0] == 'r' ? PROT_READ : 0) |
                      (permissions[1] == 'w' ? PROT_WRITE : 0) |
                      (permissions[2] == 'x' ? PROT_EXEC : 0);
        fclose(maps);
        return TRUE;
    }
    fclose(maps);
    return FALSE;
}

bool zz_posix_vm_patch_code(const zz_addr_t address, const zz_ptr_t codedata, zz_size_t codedata_size) {
    zz_size_t page_size;
    zz_addr_t first_page, last_page;
    zz_size_t page_count, i;
    int *original_protections;
    bool ok = TRUE;

    if (!address || !codedata || codedata_size == 0)
        return FALSE;

    page_size = zz_posix_vm_get_page_size();
    if (page_size == 0 || (page_size & (page_size - 1)) != 0)
        return FALSE;
    if (codedata_size - 1 > (zz_size_t)((zz_addr_t)-1 - address))
        return FALSE;

    first_page = address & ~((zz_addr_t)page_size - 1);
    last_page = (address + codedata_size - 1) & ~((zz_addr_t)page_size - 1);
    page_count = (zz_size_t)((last_page - first_page) / page_size) + 1;
    if (page_count > ((zz_size_t)-1) / sizeof(int))
        return FALSE;

    original_protections = (int *)malloc(page_count * sizeof(int));
    if (!original_protections)
        return FALSE;

    for (i = 0; i < page_count; ++i) {
        zz_addr_t page = first_page + i * page_size;
        if (!zz_posix_vm_query_protection(page, &original_protections[i])) {
            free(original_protections);
            return FALSE;
        }
    }

    /* Preserve each page's original permissions after the patch. */
    for (i = 0; i < page_count; ++i) {
        zz_addr_t page = first_page + i * page_size;
        int writable = original_protections[i] | PROT_READ | PROT_WRITE;
        if (mprotect((zz_ptr_t)page, page_size, writable) != 0) {
            size_t restore;
            for (restore = 0; restore < i; ++restore) {
                zz_addr_t old_page = first_page + restore * page_size;
                (void)mprotect((zz_ptr_t)old_page, page_size, original_protections[restore]);
            }
            free(original_protections);
            return FALSE;
        }
    }

    memcpy((void *)address, codedata, codedata_size);
    __builtin___clear_cache((char *)address, (char *)(address + codedata_size));

    for (i = 0; i < page_count; ++i) {
        zz_addr_t page = first_page + i * page_size;
        if (mprotect((zz_ptr_t)page, page_size, original_protections[i]) != 0)
            ok = FALSE;
    }

    free(original_protections);
    return ok;
}
