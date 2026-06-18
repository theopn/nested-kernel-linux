#ifndef _ASM_X86_NK_MMU_H
#define _ASM_X86_NK_MMU_H

#ifndef __ASSEMBLER__

#include <asm/pgtable_types.h>

void nk_write_cr0(unsigned long val);
void nk_write_cr3(unsigned long val);
void nk_write_cr4(unsigned long val);

void nk_set_pte(pte_t *ptep, pte_t pte);

#endif /* __ASSEMBLER__ */

#endif /* _ASM_X86_NK_MMU_H */
