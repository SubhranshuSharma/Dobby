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
    if (entry->hook_type == HOOK_TYPE_ONE_INSTRUCTION) {
        ZzPrepareTrampoline(self, entry);
        ZzBuildEnterTrampoline(self, entry);
        ZzBuildInsnLeaveTrampoline(self, entry);
        ZzBuildInvokeTrampoline(self, entry);
    } else if (entry->hook_type == HOOK_TYPE_FUNCTION_via_PRE_POST) {
        ZzPrepareTrampoline(self, entry);
        ZzBuildEnterTrampoline(self, entry);
        ZzBuildInvokeTrampoline(self, entry);
        ZzBuildLeaveTrampoline(self, entry);
    } else if (entry->hook_type == HOOK_TYPE_FUNCTION_via_REPLACE) {
        ZzPrepareTrampoline(self, entry);
        ZzBuildEnterTransferTrampoline(self, entry);
        ZzBuildInvokeTrampoline(self, entry);
    } else if (entry->hook_type == HOOK_TYPE_FUNCTION_via_GOT) {
        ZzBuildEnterTrampoline(self, entry);
        ZzBuildLeaveTrampoline(self, entry);
    } else if (entry->hook_type == HOOK_TYPE_DBI) {
        ZzPrepareTrampoline(self, entry);
        ZzBuildDynamicBinaryInstrumentationTrampoline(self, entry);
        ZzBuildInvokeTrampoline(self, entry);
    }

    zz_flush_trampoline_page(entry->on_enter_transfer_trampoline);
    zz_flush_trampoline_page(entry->on_enter_trampoline);
    zz_flush_trampoline_page(entry->on_insn_leave_trampoline);
    zz_flush_trampoline_page(entry->on_invoke_trampoline);
    zz_flush_trampoline_page(entry->on_leave_trampoline);
    zz_flush_trampoline_page(entry->on_dynamic_binary_instrumentation_trampoline);
    return ZZ_DONE;
}
