# Nested Kernel

Theo's port of Nested Kernel paper (https://dl.acm.org/doi/10.1145/2694344.2694386) in Linux.


## Workflow

I use NixOS, btw. Use the included `shell.nix` to take care of the compile dependencies.

```sh
# Compilation (produces `vmlinux`)
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

