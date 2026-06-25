#include <linux/linkage.h>
#include <asm/page_types.h>

.bss
.align PAGE_SIZE
nk_secure_stack:
    .space 16384 /* 16KB stack */
nk_secure_stack_top:

.data
.align 8
nk_outer_stack:
    .quad 0

.text
SYM_FUNC_START(nk_enter)
    /* Save current RFLAGS and GPRs */
    pushfq
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* Disable interrupts */
    cli

    /* Clear the WP-bit in CR0 (bit 16) */
    movq %cr0, %rax
    btrq $16, %rax
    movq %rax, %cr0

    /* Switch the stack pointer to the secure NK stack */
    movq %rsp, nk_outer_stack(%rip)
    leaq nk_secure_stack_top(%rip), %rsp

    /*
     * Call the inner NK payload function
     * C prototype: void nk_enter(void (*payload)(void *, void *), void *arg1, void *arg2);
     * At this point, the old stack contains the pushed registers.
     * rdi (payload) is at offset 64
     * rsi (arg1) is at offset 72
     * rdx (arg2) is at offset 88
     */
    movq nk_outer_stack(%rip), %rax
    movq 64(%rax), %r12   /* payload function pointer */
    movq 72(%rax), %rdi   /* arg1 */
    movq 88(%rax), %rsi   /* arg2 */

    /* Execute payload */
    call *%r12

    /* Fall through to nk_exit */
SYM_FUNC_END(nk_enter)

SYM_FUNC_START(nk_exit)
    /* Restore the original Outer Kernel stack pointer */
    movq nk_outer_stack(%rip), %rsp

1:
    /* Set the WP-bit in CR0 */
    movq %cr0, %rax
    btsq $16, %rax
    movq %rax, %cr0

    /* Crucial Security Check: Validate WP-bit is active */
    movq %cr0, %rax
    btq $16, %rax
    jnc 1b  /* If CF=0 (WP bit not set), loop back and try again */

    /* Restore registers and RFLAGS */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    popfq

    ret
SYM_FUNC_END(nk_exit)
