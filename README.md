# Nested Kernel

Theo's port of Nested Kernel paper (https://dl.acm.org/doi/10.1145/2694344.2694386) in Linux.


## Workflow

I use NixOS, btw. Use the included `shell.nix` to take care of the compile dependencies.

```sh
# Initially, generate the Linux config
make mrproper
#make x86_64_defconfig
#make kvm_guest.config
#make savedefconfig
make nk_defconfig

# Compilation
make -j$(nproc)

# Boot
qemu-system-x86_64  -kernel arch/x86/boot/bzImage   \
                    -nographic                      \
                    -m 2G                           \
                    -smp 2                          \
                    -append "console=ttyS0 loglevel=8 earlyprintk=serial"
```

## Changelog

- `nk_scanner.py`: scans for illegal `mov` instructions for control registers (`cr[034]`)


### CR0 hijack & Security module init testing

- `arch/x86/`:
    - Create `include/asm/nk_mmu.h`: standard header
    - Create `kernel/nk_mmu.c`: For now, just print the acknowledgement and do the typical `mov` instruction so that the kernel doesn't crash
    - Modify `kernel/cpu/common.c`: include `<asm/nk_mmu.h>` in header, strip `native_write_cr0` and change it to call `nk_write_cr0`
    - Modify `kernel/Makefile`: add `obj-y += nk_mmu.o`

- `security/nk`: Create generic `core.c` and `Makefile` for now
- `security/Makefile`: add `obj-y += nk/`
- `include/linux/nk.h`: header file
- `init/main.c`: include `<linux/nk.h>`, and in `start_kernel` function, right before the wrap-up `rest_init` function, call `nk_init()`

### CR3 & CR4 hijacking

- CR4 is identical to CR0; change in `arch/x86/kernel/cpu/common.c`

- CR3 is inline in `arch/x86/include/asm/special_insns.h`
    - When changing in the header, in the early boot decompression stage, call assembly directly
    - `arch/x86/boot/compressed/Makefile`: add `KBUILD_CFLAGS += -D__NK_DECOMPRESSOR`
- Proceed as usual in `arch/x86/kernel/nk_mmu.c` and the header

