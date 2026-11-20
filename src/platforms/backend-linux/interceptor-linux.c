#include "interceptor-linux.h"

/*
 * ELF GOT interception is not implemented in this backend yet. Do not report
 * success for a no-op: callers must be able to distinguish unsupported hooks.
 */
ZZSTATUS ZzHookGOT(const char *name, zz_ptr_t replace_ptr, zz_ptr_t *origin_ptr, PRECALL pre_call_ptr,
                   POSTCALL post_call_ptr) {
    (void)name;
    (void)replace_ptr;
    (void)origin_ptr;
    (void)pre_call_ptr;
    (void)post_call_ptr;
    return ZZ_FAILED;
}

ZZSTATUS ZzDisableHookGOT(const char *name) {
    (void)name;
    return ZZ_FAILED;
}
