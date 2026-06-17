#!/usr/bin/env python3
import subprocess
import re
import sys

VMLINUX = "vmlinux"
FORBIDDEN_REGEX = re.compile(r"mov\s+%[r|e][a-z0-9]+,%cr[034]")


def run_objdump(binary_path):
    print(f"[*] Scanning {binary_path} for control register writes...")
    try:
        return subprocess.Popen(
            ["objdump", "-d", "--no-show-raw-insn", binary_path],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except FileNotFoundError:
        print("[!] ERROR: objdump not found.")
        sys.exit(1)


def scan_binary():
    process = run_objdump(VMLINUX)
    violations = []
    current_func = "unknown"

    for line in process.stdout:
        # Catch function headers (e.g., "ffffffff810000a0 <nk_write_cr0>:")
        if ">:" in line:
            current_func = line.split("<")[-1].split(">")[0]
            continue

        if "%cr" not in line:
            continue

        match = FORBIDDEN_REGEX.search(line)
        if match:
            # Trust the Nested Kernel perimeter!
            if current_func.startswith("nk_"):
                continue

            address = line.split(":")[0].strip()
            violations.append((current_func, address, line.strip()))

    process.wait()

    if violations:
        print(
            f"[!] Found {len(violations)} forbidden hardware instructions outside the NK boundary:\n"
        )
        for func, addr, instruction in violations:
            # We now print the function name so you know exactly who is violating the policy
            print(f"    [<{func}>] 0x{addr}: {instruction}")
        print("\n[!] Build halted.")
        sys.exit(1)
    else:
        print("\n[+] 0 forbidden instructions found.")
        sys.exit(0)


if __name__ == "__main__":
    scan_binary()
