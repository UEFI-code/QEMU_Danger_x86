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
    printf("FILE: %s, LINE: %d, FUNC: %s, raise_exception_err\n", __FILE__, __LINE__, __func__);
    raise_interrupt2(env, exception_index, 0, error_code, 0, 0);
}

G_NORETURN void raise_exception_err_ra(CPUX86State *env, int exception_index,
                                       int error_code, uintptr_t retaddr)
{
    printf("FILE: %s, LINE: %d, FUNC: %s, raise_exception_err_ra\n", __FILE__, __LINE__, __func__);
    switch(exception_index) {
        case EXCP0D_GPF:
            #ifdef TARGET_X86_64
            printf("Exception General Protection Fault, code_p = 0x%llX\n", env->eip);
            #else
            printf("Exception General Protection Fault, code_p = 0x%X\n", env->eip);
            #endif
            break;
        case EXCP0B_NOSEG:
            #ifdef TARGET_X86_64
            printf("Exception No Segment, code_p = 0x%llX\n", env->eip);
            #else
            printf("Exception No Segment, code_p = 0x%X\n", env->eip);
            #endif
            break;
        case EXCP0E_PAGE:
            #ifdef TARGET_X86_64
            printf("Exception Page Fault, code_p = 0x%llX, CS = 0x%X, DS = 0x%X, CR3 = 0x%llX\n", env->eip, env->segs[R_CS].selector, env->segs[R_DS].selector, env->cr[3]);
            #else
            printf("Exception Page Fault, code_p = 0x%X, CS = 0x%X, DS = 0x%X, CR3 = 0x%X\n", env->eip, env->segs[R_CS].selector, env->segs[R_DS].selector, env->cr[3]);
            #endif
            
            if (error_code & 1 == 1) // bit 0
                printf("Page Fault caused by a page-protection violation, ");
            else
                printf("Page Fault caused by a non-present page, ");
            if (error_code & 2 == 2) // bit 1
                printf("by a write access, ");
            else
                printf("by a read access, ");
            if (error_code & 4 == 4) // bit 2
                printf("in Ring3, ");
            else
                printf("in Ring0, ");
            if (error_code & 8 == 8) // bit 3
                printf("by reserved bit violation, ");
            if (error_code & 16 == 16) // bit 4
                printf("by an instruction fetch on NX, ");
            if (error_code & 32 == 32) // bit 5
                printf("by a protection key violation, ");
            if (error_code & 64 == 64) // bit 6
                printf("by a SGX access violation, ");
            printf("\n");
            
            break;
        case EXCP08_DBLE:
            #ifdef TARGET_X86_64
            printf("Exception Double Fault, code_p = 0x%llX\n", env->eip);
            #else
            printf("Exception Double Fault, code_p = 0x%X\n", env->eip);
            #endif
            break;
        case EXCP0C_STACK:
            #ifdef TARGET_X86_64
            printf("Exception Stack Fault, code_p = 0x%llX\n", env->eip);
            #else
            printf("Exception Stack Fault, code_p = 0x%X\n", env->eip);
            #endif
            break;
        default:
            #ifdef TARGET_X86_64
            printf("Exception %d, code_p = 0x%llX\n", exception_index, env->eip);
            #else
            printf("Exception %d, code_p = 0x%X\n", exception_index, env->eip);
            #endif
            break;
    }
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
