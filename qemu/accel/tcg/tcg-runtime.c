/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"
#include "exec/tb-lookup.h"
#include "tcg/tcg.h"

#include <uc_priv.h>

/* 32-bit helpers */

int32_t HELPER(div_i32)(int32_t arg1, int32_t arg2)
{
    return arg1 / arg2;
}

int32_t HELPER(rem_i32)(int32_t arg1, int32_t arg2)
{
    return arg1 % arg2;
}

uint32_t HELPER(divu_i32)(uint32_t arg1, uint32_t arg2)
{
    return arg1 / arg2;
}

uint32_t HELPER(remu_i32)(uint32_t arg1, uint32_t arg2)
{
    return arg1 % arg2;
}

/* 64-bit helpers */

uint64_t HELPER(shl_i64)(uint64_t arg1, uint64_t arg2)
{
    return arg1 << arg2;
}

uint64_t HELPER(shr_i64)(uint64_t arg1, uint64_t arg2)
{
    return arg1 >> arg2;
}

int64_t HELPER(sar_i64)(int64_t arg1, int64_t arg2)
{
    return arg1 >> arg2;
}

int64_t HELPER(div_i64)(int64_t arg1, int64_t arg2)
{
    return arg1 / arg2;
}

int64_t HELPER(rem_i64)(int64_t arg1, int64_t arg2)
{
    return arg1 % arg2;
}

uint64_t HELPER(divu_i64)(uint64_t arg1, uint64_t arg2)
{
    return arg1 / arg2;
}

uint64_t HELPER(remu_i64)(uint64_t arg1, uint64_t arg2)
{
    return arg1 % arg2;
}

uint64_t HELPER(muluh_i64)(uint64_t arg1, uint64_t arg2)
{
    uint64_t l, h;
    mulu64(&l, &h, arg1, arg2);
    return h;
}

int64_t HELPER(mulsh_i64)(int64_t arg1, int64_t arg2)
{
    uint64_t l, h;
    muls64(&l, &h, arg1, arg2);
    return h;
}

uint32_t HELPER(clz_i32)(uint32_t arg, uint32_t zero_val)
{
    return arg ? clz32(arg) : zero_val;
}

uint32_t HELPER(ctz_i32)(uint32_t arg, uint32_t zero_val)
{
    return arg ? ctz32(arg) : zero_val;
}

uint64_t HELPER(clz_i64)(uint64_t arg, uint64_t zero_val)
{
    return arg ? clz64(arg) : zero_val;
}

uint64_t HELPER(ctz_i64)(uint64_t arg, uint64_t zero_val)
{
    return arg ? ctz64(arg) : zero_val;
}

uint32_t HELPER(clrsb_i32)(uint32_t arg)
{
    return clrsb32(arg);
}

uint64_t HELPER(clrsb_i64)(uint64_t arg)
{
    return clrsb64(arg);
}

uint32_t HELPER(ctpop_i32)(uint32_t arg)
{
    return ctpop32(arg);
}

uint64_t HELPER(ctpop_i64)(uint64_t arg)
{
    return ctpop64(arg);
}

void *HELPER(lookup_tb_ptr)(CPUArchState *env)
{
    CPUState *cpu = env_cpu(env);
    TranslationBlock *tb;
    target_ulong cs_base, pc;
    uint32_t flags;
    struct uc_struct *uc = (struct uc_struct *)cpu->uc;
    uint32_t cf_mask;
    uint64_t epoch = uc->embedder_tb_epoch;
    unsigned slot;

#if defined(TARGET_ARM) && !defined(TARGET_AARCH64)
    if (likely(arm_tb_state_fast(env, &pc, &flags))) {
        cs_base = 0;
    } else
#endif
    {
        cpu_get_tb_cpu_state(env, &pc, &cs_base, &flags);
    }
    cf_mask = (curr_cflags(cpu) & ~CF_CLUSTER_MASK) |
              ((uint32_t)cpu->cluster_index << CF_CLUSTER_SHIFT);
    slot = (unsigned)((pc >> 2) & (UC_EMBEDDER_TARGET_CACHE_SIZE - 1));
    if (likely(cpu->embedder_target_cache[slot].valid &&
               cpu->embedder_target_cache[slot].epoch == epoch &&
               cpu->embedder_target_cache[slot].pc == pc &&
               cpu->embedder_target_cache[slot].cs_base == cs_base &&
               cpu->embedder_target_cache[slot].flags == flags &&
               cpu->embedder_target_cache[slot].cf_mask == cf_mask)) {
        return (void *)(uintptr_t)cpu->embedder_target_cache[slot].code;
    }

    tb = tb_lookup__state(cpu, pc, cs_base, flags, cf_mask);
    if (tb == NULL) {
        cpu->embedder_target_cache[slot].valid = 0;
        return uc->tcg_ctx->code_gen_epilogue;
    }
    cpu->embedder_target_cache[slot].pc = pc;
    cpu->embedder_target_cache[slot].cs_base = cs_base;
    cpu->embedder_target_cache[slot].flags = flags;
    cpu->embedder_target_cache[slot].cf_mask = cf_mask;
    cpu->embedder_target_cache[slot].code = (uint64_t)(uintptr_t)tb->tc.ptr;
    cpu->embedder_target_cache[slot].epoch = epoch;
    cpu->embedder_target_cache[slot].valid = 1;
    return tb->tc.ptr;
}

#if defined(TARGET_ARM) && !defined(TARGET_AARCH64) && defined(__aarch64__)
/* Keep lookup, code translation and the uncommon complete ARM state out of
 * the hit path so it can return without building a stack frame. */
static QEMU_NOINLINE void *lookup_a32_miss(CPUState *cpu, target_ulong pc,
                                         uint32_t flags, uint64_t config,
                                         struct embedder_a32_target *entry)
{
    struct uc_struct *uc = cpu->uc;
    TranslationBlock *tb;

    tb = tb_lookup__state(cpu, pc, 0, flags, (uint32_t)config);
    if (!tb) {
        entry->code = 0;
        return uc->tcg_ctx->code_gen_epilogue;
    }
    entry->key = ((uint64_t)flags << 32) | pc;
    entry->epoch = uc->embedder_a32_dispatch_epoch;
    entry->config = config;
    entry->code = (uintptr_t)tb->tc.ptr;
    return tb->tc.ptr;
}

void *HELPER(lookup_a32_compact)(CPUArchState *env)
{
    CPUState *cpu = env_cpu(env);
    struct uc_struct *uc = cpu->uc;
    struct embedder_a32_target *entry;
    target_ulong pc;
    uint32_t flags, cflags;
    uint64_t key, config, code;
    unsigned slot;

    if (unlikely(!arm_tb_state_fast(env, &pc, &flags))) {
        return HELPER(lookup_tb_ptr)(env);
    }
    cflags = curr_cflags(cpu) | ((uint32_t)cpu->cluster_index << CF_CLUSTER_SHIFT);
    key = ((uint64_t)flags << 32) | pc;
    config = ((uint64_t)*cpu->trace_dstate << 32) | cflags;
    slot = ((pc >> 2) ^ (pc >> 12)) & (UC_A32_DISPATCH_SLOTS - 1);
    entry = &uc->embedder_a32_dispatch[slot];
    code = entry->code;
    if (likely(code && entry->key == key && entry->config == config &&
               entry->epoch == uc->embedder_a32_dispatch_epoch)) {
        return (void *)(uintptr_t)code;
    }
    return lookup_a32_miss(cpu, pc, flags, config, entry);
}
#endif

void HELPER(exit_atomic)(CPUArchState *env)
{
    cpu_loop_exit_atomic(env_cpu(env), GETPC());
}

void HELPER(check_exit_request)(void *p, uint32_t in_delay_slot) {
    uc_engine *uc = p;

    // embedder: the block hook asked for this block with fewer instructions. A
    // pending exit wins; the block is then reconsidered when execution resumes.
    if (unlikely(uc->embedder_retry_limit)) {
        uint32_t limit = uc->embedder_retry_limit;
        uc->embedder_retry_limit = 0;
        if (!cpu_loop_exit_requested(uc->cpu) && !in_delay_slot) {
            uc->cpu->cflags_next_tb = curr_cflags(uc->cpu) | (limit & CF_COUNT_MASK);
#if defined(CONFIG_TCG_INTERPRETER)
            cpu_restore_state(uc->cpu, GETPC(), true);
            uc->embedder_exit_counters[UC_EXIT_NORMAL_RETRY]++;
            uc->tci_return_pending = true;
            return;
#else
            uc->embedder_exit_site = UC_EXIT_RETRY_JUMP;
            if (uc->nested_level == 1 && !uc_embedder_keep_jit_rx(uc)) {
                tb_exec_unlock(uc);
            }
            cpu_loop_exit_restore(uc->cpu, GETPC());
#endif
        }
    }

    if (cpu_loop_exit_requested(uc->cpu) && !in_delay_slot) {
#if defined(CONFIG_TCG_INTERPRETER)
        // A hook check precedes a guest instruction (possibly within a TB).
        // Restore precisely the same PC as the jump path, then let the interpreter
        // unwind normally through cpu_tb_exec (including its I/O/JIT cleanup).
        // Return unchained: a code-hook PC write must not be overwritten by
        // synchronize_from_tb, and a retry must not trigger icount refill.
        if (!uc->skip_sync_pc_on_exit) {
            cpu_restore_state(uc->cpu, GETPC(), true);
        }
        uc->skip_sync_pc_on_exit = false;
        uc->cpu->tcg_exit_req = 0;
        uc->embedder_exit_counters[UC_EXIT_NORMAL_CHECK]++;
        uc->tci_return_pending = true;
        return;
#else
        // There are stil something we have to before exiting to be compatible with previous behaviors

        // from cpu_tb_exec
        if (uc->nested_level == 1 && !uc_embedder_keep_jit_rx(uc)) {
            // Only unlock (allow writing to JIT area) if we are the outmost uc_emu_start
            tb_exec_unlock(uc);
        }
        uc->cpu->tcg_exit_req = 0;

        if (uc->skip_sync_pc_on_exit) {
            uc->embedder_exit_site = UC_EXIT_CHECK_NO_SYNC;
            uc->skip_sync_pc_on_exit = false;
            cpu_loop_exit(uc->cpu);
        } else {
            uc->embedder_exit_site = UC_EXIT_CHECK_RESTORE;
            cpu_loop_exit_restore(uc->cpu, GETPC());
        }
#endif
    }
}
