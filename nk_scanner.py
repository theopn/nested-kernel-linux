#!/usr/bin/env python3

import subprocess
import re
import sys

# The target binary
VMLINUX = "vmlinux"

# Matches: mov %reg,%cr[034]
FORBIDDEN_REGEX = re.compile(r"mov\s+%[r|e][a-z0-9]+,%cr[034]")

# We will populate this later with the addresses of our trusted nk_ assembly gates
WHITELISTED_ADDRESSES = set([])


def run_objdump(binary_path):
    print(f"[*] Scanning {binary_path} for control register writes...")
    try:
        process = subprocess.Popen(
            ["objdump", "-d", "--no-show-raw-insn", binary_path],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        return process
    except FileNotFoundError:
        print("[!] ERROR: objdump not found")
        sys.exit(1)


def scan_binary():
    process = run_objdump(VMLINUX)
    violations = []

    for line in process.stdout:
        if "%cr" not in line:
            continue

        match = FORBIDDEN_REGEX.search(line)
        if match:
            address = line.split(":")[0].strip()
            if address not in WHITELISTED_ADDRESSES:
                violations.append((address, line.strip()))

    process.wait()

    if violations:
        print(
            f"[!] Found {len(violations)} forbidden hardware instructions outside the NK boundary:\n"
        )
        for addr, instruction in violations:
            print(f"    0x{addr}: {instruction}")
        print("\n[!] Build halted.")
        sys.exit(1)
    else:
        print("\n[+] Binary verification passed. No forbidden instructions found.")
        sys.exit(0)


if __name__ == "__main__":
    scan_binary()
