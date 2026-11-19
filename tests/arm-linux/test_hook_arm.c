#include <stdio.h>
#include "hookzz.h"

typedef int (*target_fn)(int);
static target_fn original_target = 0;
static volatile int replacement_calls = 0;

__attribute__((noinline, used, optimize("O0")))
static int target_add(int value) {
    return value * 3 + 7;
}

__attribute__((noinline, used, optimize("O0")))
static int replacement_add(int value) {
    replacement_calls++;
    if (!original_target)
        return -10000;
    return original_target(value) + 100;
}

int main(void) {
    volatile target_fn invoke = target_add;
    int before = invoke(5);
    ZZSTATUS status = ZzHookReplace((void *)target_add, (void *)replacement_add,
                                    (void **)&original_target);
    int hooked = invoke(5);
    int calls = replacement_calls;
    ZZSTATUS disabled = ZzDisableHook((void *)target_add);
    int after = invoke(5);

    printf("before=%d hooked=%d calls=%d after=%d hook_status=%d disable_status=%d\n",
           before, hooked, calls, after, (int)status, (int)disabled);

    if (before != 22 || hooked != 122 || calls != 1 || after != 22)
        return 1;
    return 0;
}
