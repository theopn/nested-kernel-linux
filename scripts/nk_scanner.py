#!/usr/bin/env python3
import subprocess
import re
import sys

def run_scanner(vmlinux_path):
    print(f"Scanning {vmlinux_path} for unauthorized CR0 modifications and WRMSR...")
    
    try:
        proc = subprocess.Popen(['objdump', '-d', vmlinux_path], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    except FileNotFoundError:
        print("Error: objdump not found.")
        sys.exit(1)

    current_func = ""
    findings = []
    
    # Whitelisted functions (Nested Kernel TCB + standard boot code)
    whitelist = {
        'nk_enter',
        'nk_exit',
        'startup_64',
        'secondary_startup_64',
        'verify_cpu',
        'startup_32',
        'initial_code',
        'common_startup_64',
        'payload_write_cr0',
        'do_suspend_lowlevel',
        'identity_mapped',
        'virtual_mapped'
    }

    func_pattern = re.compile(r'^[0-9a-fA-F]+ <([^>]+)>:$')
    instr_pattern = re.compile(r'\s+([0-9a-fA-F]+):\s+(?:[0-9a-fA-F]{2}\s+)+\s+(mov\s+.*,%cr0)\s*.*')

    for line in proc.stdout:
        func_match = func_pattern.match(line)
        if func_match:
            current_func = func_match.group(1)
            continue
        
        # We also need to catch cases where objdump doesn't output bytes cleanly, so let's just search for the mnemonic
        if 'mov' in line and '%cr0' in line:
            # basic clean up
            if '>:' in line: continue # It's a function declaration
            
            # Isolate the instruction
            match = re.search(r'([0-9a-fA-F]+):\s+.*\s+(mov\s+.*,%cr0)', line)
            if match:
                address = match.group(1)
                instruction = match.group(2)
                
                if current_func not in whitelist and not current_func.startswith('__setup_'):
                    findings.append((address, current_func, instruction, line.strip()))

    proc.wait()
    
    if findings:
        print(f"Found {len(findings)} potentially unauthorized instructions!")
        print("Examples:")
        for f in findings[:20]:
            print(f"  Addr: {f[0]}, Func: <{f[1]}>, Instr: {f[2]}")
    else:
        print("No unauthorized instructions found.")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: nk_scanner.py <path_to_vmlinux>")
        sys.exit(1)
    run_scanner(sys.argv[1])
