/*
 *  x86 exception helpers
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "qemu/log.h"
#include "sysemu/runstate.h"
#include "exec/helper-proto.h"
#include "helper-tcg.h"
#include "seg_helper.h"
#include "exec/cpu_ldst.h"

static __inline void print_exception(int exception_index, CPUX86State *env, int error_code, uintptr_t p_handler)
{
    printf("FILE: %s, LINE: %d, FUNC: %s, ", __FILE__, __LINE__, __func__);
    switch(exception_index) {
        case EXCP0D_GPF:
            printf("Exception General Protection Fault, code_p = 0x%llX, Will goto 0x%llX\n", env->eip, p_handler);
            break;
        case EXCP0B_NOSEG:
            printf("Exception No Segment, code_p = 0x%llX, Will goto 0x%llX\n", env->eip, p_handler);
            break;
        case EXCP0E_PAGE:
            printf("Exception Page Fault, code_p = 0x%llX, CS = 0x%X, DS = 0x%X, SS = 0x%X, CR3 = 0x%llX, CR2 = 0x%llX, ", env->eip, env->segs[R_CS].selector, env->segs[R_DS].selector, env->segs[R_SS].selector, env->cr[3], env->cr[2]);
            if ((error_code & 1) == 1) // bit 0
                printf("Page Fault caused by a Page-Protection violation, ");
            else
                printf("Page Fault caused by a Non-Present page, ");
            if ((error_code & 2) == 2) // bit 1
                printf("by a Write access, ");
            else
                printf("by a Read access, ");
            if ((error_code & 4) == 4) // bit 2
                printf("in Ring3, ");
            else
                printf("in Ring0, ");
            if ((error_code & 8) == 8) // bit 3
                printf("by reserved bit violation, ");
            if ((error_code & 16) == 16) // bit 4
                printf("by an instruction fetch on NX, ");
            if ((error_code & 32) == 32) // bit 5
                printf("by a protection key violation, ");
            if ((error_code & 64) == 64) // bit 6
                printf("by a SGX access violation, ");
            printf("Handler = 0x%llX\n", p_handler);
            break;
        case EXCP08_DBLE:
            printf("Exception Double Fault, code_p = 0x%llX, CS = 0x%X, DS = 0x%X, SS = 0x%X, CR3 = 0x%llX, CR2 = 0x%llX, Handler = 0x%llX\n", env->eip, env->segs[R_CS].selector, env->segs[R_DS].selector, env->segs[R_SS].selector, env->cr[3], env->cr[2], p_handler);
            break;
        case EXCP0C_STACK:
            printf("Exception Stack Fault, code_p = 0x%llX, Will goto 0x%llX\n", env->eip, p_handler);
            break;
        default:
            printf("Exception %d, code_p = 0x%llX, Will goto 0x%llX\n", exception_index, env->eip, p_handler);
            break;
    }
}

G_NORETURN void helper_raise_interrupt(CPUX86State *env, int intno,
                                          int next_eip_addend)
{
    raise_interrupt(env, intno, 1, 0, next_eip_addend);
}

G_NORETURN void helper_raise_exception(CPUX86State *env, int exception_index)
{
    raise_exception(env, exception_index);
}

/*
 * Check nested exceptions and change to double or triple fault if
 * needed. It should only be called, if this is not an interrupt.
 * Returns the new exception number.
 */
static int check_exception(CPUX86State *env, int intno, int *error_code,
                           uintptr_t retaddr)
{
    int first_contributory = env->old_exception == 0 ||
                              (env->old_exception >= 10 &&
                               env->old_exception <= 13);
    int second_contributory = intno == 0 ||
                               (intno >= 10 && intno <= 13);

    qemu_log_mask(CPU_LOG_INT, "check_exception old: 0x%x new 0x%x\n",
                env->old_exception, intno);

#if !defined(CONFIG_USER_ONLY)
    if (env->old_exception == EXCP08_DBLE) {
        if (env->hflags & HF_GUEST_MASK) {
            cpu_vmexit(env, SVM_EXIT_SHUTDOWN, 0, retaddr); /* does not return */
        }

        qemu_log_mask(CPU_LOG_RESET, "Triple fault\n");

        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        return EXCP_HLT;
    }
#endif

    if ((first_contributory && second_contributory)
        || (env->old_exception == EXCP0E_PAGE &&
            (second_contributory || (intno == EXCP0E_PAGE)))) {
        intno = EXCP08_DBLE;
        *error_code = 0;
    }

    if (second_contributory || (intno == EXCP0E_PAGE) ||
        (intno == EXCP08_DBLE)) {
        env->old_exception = intno;
    }

    #ifdef TARGET_X86_64
    uint64_t handler = 0;
    if ((intno * 16 + 15) > env->idt.limit) {
        goto pr_;
    }
    uintptr_t p_idt_item = env->idt.base + intno * 16;
    uint32_t e0 = cpu_ldl_kernel(env, p_idt_item);
    uint32_t e1 = cpu_ldl_kernel(env, p_idt_item + 4);
    uint32_t e2 = cpu_ldl_kernel(env, p_idt_item + 8);
    handler = ((target_ulong)e2 << 32) | (e1 & 0xffff0000) | (e0 & 0x0000ffff);
    #else
    uint32_t handler = 0
    if ((intno * 8 + 7) > env->idt.limit) {
        goto pr_;
    }
    uintptr_t p_idt_item = env->idt.base + intno * 8;
    uint32_t e0 = cpu_ldl_kernel(env, p_idt_item);
    uint32_t e1 = cpu_ldl_kernel(env, p_idt_item + 4);
    handler = (e1 & 0xffff0000) | (e0 & 0x0000ffff);
    #endif
    pr_:
    print_exception(intno, env, *error_code, handler);
    return intno;
}

/*
 * Signal an interruption. It is executed in the main CPU loop.
 * is_int is TRUE if coming from the int instruction. next_eip is the
 * env->eip value AFTER the interrupt instruction. It is only relevant if
 * is_int is TRUE.
 */
static G_NORETURN
void raise_interrupt2(CPUX86State *env, int intno,
                      int is_int, int error_code,
                      int next_eip_addend,
                      uintptr_t retaddr)
{
    CPUState *cs = env_cpu(env);

    if (!is_int) {
        cpu_svm_check_intercept_param(env, SVM_EXIT_EXCP_BASE + intno,
                                      error_code, retaddr);
        intno = check_exception(env, intno, &error_code, retaddr);
    } else {
        cpu_svm_check_intercept_param(env, SVM_EXIT_SWINT, 0, retaddr);
    }

    cs->exception_index = intno;
    env->error_code = error_code;
    env->exception_is_int = is_int;
    env->exception_next_eip = env->eip + next_eip_addend;
    cpu_loop_exit_restore(cs, retaddr);
}

/* shortcuts to generate exceptions */

G_NORETURN void raise_interrupt(CPUX86State *env, int intno, int is_int,
                                int error_code, int next_eip_addend)
{
    raise_interrupt2(env, intno, is_int, error_code, next_eip_addend, 0);
}

G_NORETURN void raise_exception_err(CPUX86State *env, int exception_index,
                                    int error_code)
{
    raise_interrupt2(env, exception_index, 0, error_code, 0, 0);
}

G_NORETURN void raise_exception_err_ra(CPUX86State *env, int exception_index,
                                       int error_code, uintptr_t retaddr)
{
    raise_interrupt2(env, exception_index, 0, error_code, 0, retaddr);
}

G_NORETURN void raise_exception(CPUX86State *env, int exception_index)
{
    raise_interrupt2(env, exception_index, 0, 0, 0, 0);
}

G_NORETURN void raise_exception_ra(CPUX86State *env, int exception_index,
                                   uintptr_t retaddr)
{
    raise_interrupt2(env, exception_index, 0, 0, 0, retaddr);
}

G_NORETURN void handle_unaligned_access(CPUX86State *env, vaddr vaddr,
                                        MMUAccessType access_type,
                                        uintptr_t retaddr)
{
    /*
     * Unaligned accesses are currently only triggered by SSE/AVX
     * instructions that impose alignment requirements on memory
     * operands. These instructions raise #GP(0) upon accessing an
     * unaligned address.
     */
    raise_exception_ra(env, EXCP0D_GPF, retaddr);
}
