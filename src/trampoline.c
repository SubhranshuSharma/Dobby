#include "trampoline.h"
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * ARM/Thumb trampolines are emitted into writable pages and then executed.
 * On ARMv5, data writes are not automatically visible to the instruction
 * cache, so flush the page containing each generated code slice after emit.
 */
static void zz_flush_trampoline_page(zz_ptr_t code) {
    long page_size;
    uintptr_t address;
    uintptr_t page;

    if (!code)
        return;
    page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0)
        return;

    address = (uintptr_t)code & ~(uintptr_t)1; /* strip Thumb state bit */
    page = address & ~((uintptr_t)page_size - 1);
    __builtin___clear_cache((char *)page, (char *)(page + (uintptr_t)page_size));
}

ZZSTATUS ZzBuildTrampoline(struct _ZzInterceptorBackend *self, ZzHookFunctionEntry *entry) {
    ZZSTATUS status;

    if (!self || !entry)
        return ZZ_FAILED;

#define ZZ_BUILD_STEP(call) do { status = (call); if (status == ZZ_FAILED) return status; } while (0)

    if (entry->hook_type == HOOK_TYPE_ONE_INSTRUCTION) {
        ZZ_BUILD_STEP(ZzPrepareTrampoline(self, entry));
        ZZ_BUILD_STEP(ZzBuildEnterTrampoline(self, entry));
        ZZ_BUILD_STEP(ZzBuildInsnLeaveTrampoline(self, entry));
        ZZ_BUILD_STEP(ZzBuildInvokeTrampoline(self, entry));
    } else if (entry->hook_type == HOOK_TYPE_FUNCTION_via_PRE_POST) {
        ZZ_BUILD_STEP(ZzPrepareTrampoline(self, entry));
        ZZ_BUILD_STEP(ZzBuildEnterTrampoline(self, entry));
        ZZ_BUILD_STEP(ZzBuildInvokeTrampoline(self, entry));
        ZZ_BUILD_STEP(ZzBuildLeaveTrampoline(self, entry));
    } else if (entry->hook_type == HOOK_TYPE_FUNCTION_via_REPLACE) {
        ZZ_BUILD_STEP(ZzPrepareTrampoline(self, entry));
        ZZ_BUILD_STEP(ZzBuildEnterTransferTrampoline(self, entry));
        ZZ_BUILD_STEP(ZzBuildInvokeTrampoline(self, entry));
    } else if (entry->hook_type == HOOK_TYPE_FUNCTION_via_GOT) {
        ZZ_BUILD_STEP(ZzBuildEnterTrampoline(self, entry));
        ZZ_BUILD_STEP(ZzBuildLeaveTrampoline(self, entry));
    } else if (entry->hook_type == HOOK_TYPE_DBI) {
        ZZ_BUILD_STEP(ZzPrepareTrampoline(self, entry));
        ZZ_BUILD_STEP(ZzBuildDynamicBinaryInstrumentationTrampoline(self, entry));
        ZZ_BUILD_STEP(ZzBuildInvokeTrampoline(self, entry));
    } else {
        return ZZ_FAILED;
    }

#undef ZZ_BUILD_STEP

    zz_flush_trampoline_page(entry->on_enter_transfer_trampoline);
    zz_flush_trampoline_page(entry->on_enter_trampoline);
    zz_flush_trampoline_page(entry->on_insn_leave_trampoline);
    zz_flush_trampoline_page(entry->on_invoke_trampoline);
    zz_flush_trampoline_page(entry->on_leave_trampoline);
    zz_flush_trampoline_page(entry->on_dynamic_binary_instrumentation_trampoline);
    return ZZ_SUCCESS;
}
