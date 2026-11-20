# ARMv5 Linux port status

Target: Anyka ARMv5TEJ / ARM32 / AAPCS Linux / soft-float / uClibc 1.0.28.

Development branch: `port/armv5-linux-uclibc`.

## Current verified state

- A separate Linux build target exists in `Makefile.armv5`; it does not alter the upstream Android/iOS build files.
- The library cross-compiles as ELF32 ARM, EABI5, soft-float, ARMv5TEJ.
- The ARM-state inline replacement-hook smoke test passes under QEMU's ARM926 CPU model. It verifies replacement dispatch, calling the original trampoline, and restoring the original prologue.
- Linux code patching now records/restores the original page protections and flushes the instruction cache after patching.
- Generated trampoline pages are flushed after code emission.
- Near-page allocation no longer uses `MAP_FIXED`, which could overwrite an existing process mapping.
- Linux executable code-cave scanning now bounds-checks reads, closes `/proc/self/maps`, and allocates a zero-search buffer sized to the requested code slice.
- Linux GOT hooks explicitly return failure; they are not silently reported as installed.

## Important limitations

1. **The current CI cross-toolchain is glibc.** Its ARMv5 build and QEMU tests validate the instruction set and basic Linux ABI only. They do not prove compatibility with the camera's uClibc 1.0.28 runtime.
2. **Thumb hooks are intentionally rejected on pre-ARMv7 builds.** The inherited Thumb writer emits Thumb-2 encodings, while ARMv5TEJ implements Thumb-1. The port refuses the hook before writing target code rather than risking a crash. ARM-state hooks are the first supported target.
3. **No remote injector is included.** Dobby is an in-process hook library. GDB/gdbserver is the initial remote inspection mechanism; a controlled library-loading method is still needed for unattended hook deployment.
4. **No real camera test has been performed.** QEMU is not a substitute for the camera kernel, memory map, uClibc, or vendor process.

## Build with the actual Buildroot uClibc toolchain

Use the exact cross compiler and sysroot generated for the camera:

```sh
make -f Makefile.armv5 \
  CROSS_COMPILE=/path/to/buildroot/host/bin/<target-triplet>- \
  SYSROOT=/path/to/buildroot/host/<target-triplet>/sysroot
```

The output is `build/armv5-linux/libhookzz.so` and `libhookzz.a`. Verify it with the matching target `readelf`; it must report ARM, EABI5, soft-float, and CPU arch v5TEJ. Check its NEEDED entries against the camera's uClibc libraries before copying it to the device.

## Next gates before camera deployment

1. Complete the uClibc cross-build and inspect dynamic dependencies.
2. Confirm the exact audio function addresses, load bias, and ARM/Thumb state with GDB on the camera.
3. Test a harmless ARM-state function hook in a separate test process on the camera.
4. Only then prepare an `anyka_ipc` probe for the native audio path.
5. Port Thumb-1 relocation separately if the target audio functions require Thumb hooks.
6. Implement Linux ELF GOT hooks and a remote injector only if the native audio work actually needs them.
