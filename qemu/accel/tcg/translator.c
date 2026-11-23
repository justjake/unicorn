/*
 * Generic intermediate code generation.
 *
 * Copyright (C) 2016-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "tcg/tcg.h"
#include "tcg/tcg-op.h"
#include "exec/exec-all.h"
#include "exec/gen-icount.h"
#include "exec/translator.h"

#include <uc_priv.h>

/* Pairs with tcg_clear_temp_count.
   To be called by #TranslatorOps.{translate_insn,tb_stop} if
   (1) the target is sufficiently clean to support reporting,
   (2) as and when all temporaries are known to be consumed.
   For most targets, (2) is at the end of translate_insn.  */
void translator_loop_temp_check(DisasContextBase *db)
{
#if 0
    if (tcg_check_temp_count()) {
        qemu_log("warning: TCG temporary leaks before "
                 TARGET_FMT_lx "\n", db->pc_next);
    }
#endif
}

/*
 * embedder: budget accounting inside translated code. Fast ARM blocks load
 * the interactive budget, subtract their instruction count and fall through;
 * a miss or a slow block enters the ordinary helper, which owns pause,
 * exact-policy ring writes, hook dispatch and refusal. The fast flag, the
 * instruction count and the helper's size immediate are patched after the
 * block is translated, when its instruction set and observers are known.
 */
struct EmbedderBlockGuard {
    TCGOp *fast;
    TCGOp *count_low;
    TCGOp *count_high;
    TCGOp *size;
    TCGOp *packed;
    TCGv_i32 fast_value;
    TCGv_i64 count_value;
    bool active;
};

static void gen_embedder_block_guard(TCGContext *tcg_ctx,
                                     struct uc_struct *uc, target_ulong pc,
                                     struct EmbedderBlockGuard *guard)
{
    TCGLabel *helper = gen_new_label(tcg_ctx);
    TCGLabel *body = gen_new_label(tcg_ctx);
    TCGLabel *pause_checked = gen_new_label(tcg_ctx);
    TCGOp *before_helper;

    guard->active = true;
    guard->fast_value = tcg_const_i32(tcg_ctx, 1);
    guard->fast = tcg_last_op(tcg_ctx);
    tcg_gen_brcondi_i32(tcg_ctx, TCG_COND_EQ, guard->fast_value, 0, helper);

    // A pending stop is handled by the block-entry exit test; it must not be
    // debited as an executed block.
    TCGv_i32 stop = tcg_temp_new_i32(tcg_ctx);
    TCGv_ptr ucp = tcg_const_ptr(tcg_ctx, uc);
    tcg_gen_ld8u_i32(tcg_ctx, stop, ucp, offsetof(struct uc_struct, stop_request));
    tcg_temp_free_ptr(tcg_ctx, ucp);
    tcg_gen_brcondi_i32(tcg_ctx, TCG_COND_NE, stop, 0, body);

    TCGv_ptr pause_requested = tcg_temp_local_new_ptr(tcg_ctx);
    ucp = tcg_const_ptr(tcg_ctx, uc);
    tcg_gen_ld_ptr(tcg_ctx, pause_requested, ucp,
                   offsetof(struct uc_struct, embedder_acct.pause_requested));
    tcg_temp_free_ptr(tcg_ctx, ucp);
    tcg_gen_brcondi_ptr(tcg_ctx, TCG_COND_EQ, pause_requested, 0, pause_checked);
    TCGv_i32 paused = tcg_temp_new_i32(tcg_ctx);
    tcg_gen_ld_i32(tcg_ctx, paused, pause_requested, 0);
    tcg_gen_brcondi_i32(tcg_ctx, TCG_COND_NE, paused, 0, helper);
    gen_set_label(tcg_ctx, pause_checked);

    // Coarse (2) and exact (1) accounting are translated inline; exact
    // accounting also writes its ring entry. Anything else enters the helper.
    TCGv_i64 enabled = tcg_temp_local_new_i64(tcg_ctx);
    ucp = tcg_const_ptr(tcg_ctx, uc);
    tcg_gen_ld_i64(tcg_ctx, enabled, ucp,
                   offsetof(struct uc_struct, embedder_acct.enabled));
    tcg_temp_free_ptr(tcg_ctx, ucp);
    if (uc->embedder_tb_thumb) {
        // Translation mode is fixed by this TB's flags. Emit a mode-specific
        // guard so cached ARM and Thumb blocks cannot consume each other's mode.
        tcg_gen_brcondi_i64(tcg_ctx, TCG_COND_NE, enabled, 3, helper);
        TCGv_i64 thumb_page = tcg_temp_new_i64(tcg_ctx);
        ucp = tcg_const_ptr(tcg_ctx, uc);
        tcg_gen_ld_i64(tcg_ctx, thumb_page, ucp,
                       offsetof(struct uc_struct, embedder_acct.thumb_page));
        tcg_temp_free_ptr(tcg_ctx, ucp);
        tcg_gen_brcondi_i64(tcg_ctx, TCG_COND_NE, thumb_page,
                            (int64_t)(pc & ~UINT64_C(0xfff)), helper);
        tcg_temp_free_i64(tcg_ctx, thumb_page);
    } else {
#if TCG_TARGET_REG_BITS == 64
        TCGv_i64 mode = tcg_temp_new_i64(tcg_ctx);
        tcg_gen_subi_i64(tcg_ctx, mode, enabled, 1);
        tcg_gen_brcondi_i64(tcg_ctx, TCG_COND_GTU, mode, 1, helper);
        tcg_temp_free_i64(tcg_ctx, mode);
#else
        tcg_gen_brcondi_i64(tcg_ctx, TCG_COND_NE, enabled, 2, helper);
#endif
    }

    guard->count_value = tcg_temp_new_i64(tcg_ctx);
#if TCG_TARGET_REG_BITS == 32
    tcg_gen_movi_i32(tcg_ctx, TCGV_LOW(tcg_ctx, guard->count_value), 0);
    guard->count_low = tcg_last_op(tcg_ctx);
    tcg_gen_movi_i32(tcg_ctx, TCGV_HIGH(tcg_ctx, guard->count_value), 0);
    guard->count_high = tcg_last_op(tcg_ctx);
#else
    tcg_gen_movi_i64(tcg_ctx, guard->count_value, 0);
    guard->count_low = tcg_last_op(tcg_ctx);
    guard->count_high = NULL;
#endif

    TCGv_i64 budget = tcg_temp_local_new_i64(tcg_ctx);
    ucp = tcg_const_ptr(tcg_ctx, uc);
    tcg_gen_ld_i64(tcg_ctx, budget, ucp,
                   offsetof(struct uc_struct, embedder_acct.budget));
    tcg_temp_free_ptr(tcg_ctx, ucp);
    tcg_gen_sub_i64(tcg_ctx, budget, budget, guard->count_value);
    tcg_gen_brcondi_i64(tcg_ctx, TCG_COND_LT, budget, 0, helper);
    ucp = tcg_const_ptr(tcg_ctx, uc);
    tcg_gen_st_i64(tcg_ctx, budget, ucp,
                   offsetof(struct uc_struct, embedder_acct.budget));
    tcg_temp_free_ptr(tcg_ctx, ucp);
#if TCG_TARGET_REG_BITS == 64
    tcg_gen_brcondi_i64(tcg_ctx, TCG_COND_NE, enabled, 1, body);
    ucp = tcg_const_ptr(tcg_ctx, uc);
    // Exact accounting: ring[head++ & mask] = start << 32 | count.
    TCGv_i64 head = tcg_temp_new_i64(tcg_ctx);
    TCGv_i64 mask = tcg_temp_new_i64(tcg_ctx);
    TCGv_i64 index = tcg_temp_new_i64(tcg_ctx);
    TCGv_ptr ring = tcg_temp_new_ptr(tcg_ctx);
    TCGv_i64 packed = tcg_temp_new_i64(tcg_ctx);
    tcg_gen_ld_i64(tcg_ctx, head, ucp, offsetof(struct uc_struct, embedder_acct.head));
    tcg_gen_ld_i64(tcg_ctx, mask, ucp, offsetof(struct uc_struct, embedder_acct.mask));
    tcg_gen_ld_ptr(tcg_ctx, ring, ucp, offsetof(struct uc_struct, embedder_acct.ring));
    tcg_gen_and_i64(tcg_ctx, index, head, mask);
    tcg_gen_shli_i64(tcg_ctx, index, index, 3);
    TCGv_ptr slot = tcg_temp_new_ptr(tcg_ctx);
    tcg_gen_trunc_i64_ptr(tcg_ctx, slot, index);
    tcg_gen_add_ptr(tcg_ctx, slot, slot, ring);
    tcg_gen_movi_i64(tcg_ctx, packed, 0);
    guard->packed = tcg_last_op(tcg_ctx);
    tcg_gen_st_i64(tcg_ctx, packed, slot, 0);
    tcg_gen_addi_i64(tcg_ctx, head, head, 1);
    tcg_gen_st_i64(tcg_ctx, head, ucp, offsetof(struct uc_struct, embedder_acct.head));
    tcg_temp_free_ptr(tcg_ctx, slot);
    tcg_temp_free_i64(tcg_ctx, packed);
    tcg_temp_free_ptr(tcg_ctx, ring);
    tcg_temp_free_i64(tcg_ctx, index);
    tcg_temp_free_i64(tcg_ctx, mask);
    tcg_temp_free_i64(tcg_ctx, head);
    tcg_temp_free_ptr(tcg_ctx, ucp);
#endif
    tcg_gen_br(tcg_ctx, body);

    gen_set_label(tcg_ctx, helper);
    before_helper = tcg_last_op(tcg_ctx);
    gen_uc_tracecode(tcg_ctx, 0xf8f8f8f8, UC_HOOK_BLOCK_IDX, (void *)uc, pc);
    guard->size = before_helper ? QTAILQ_NEXT(before_helper, link)
                                : QTAILQ_FIRST(&tcg_ctx->ops);
    gen_set_label(tcg_ctx, body);
}

/*
 * embedder: block chains (uc_priv.h, uc_embedder_chain). The block's cell
 * holds what a block hook admitted. While the guard word of the block's page
 * slot equals the cell's key and the budget covers the cell's weight, the
 * block spends the weight, leaves its tag in `last` and goes to `body`,
 * past its hooks and the exit request helper. Otherwise it records its cell
 * for uc_embedder_admit_block and falls through to the hooks.
 *
 * A requested exit (uc_emu_stop, also from another thread) fails the test,
 * so the hooks' path and its exit test serve it; the engine never writes
 * the budget itself. No hook ran since the embedder last set the budget, so
 * nothing can have requested a retry or a restart of this block.
 */
static TCGLabel *gen_embedder_chain_guard(TCGContext *tcg_ctx,
                                          struct uc_struct *uc,
                                          target_ulong pc)
{
    uc_embedder_chain_cell *cell = uc_embedder_chain_cell_new(uc);
    uint32_t *guard =
        &uc->embedder_chain.guards[(pc >> 12) & (UC_EMBEDDER_CHAIN_GUARDS - 1)];
    TCGLabel *hooks, *body;
    TCGv_ptr base;
    TCGv_i32 key, word;
    TCGv_i64 weight, budget, tag;

    if (!cell) {
        return NULL;
    }
    hooks = gen_new_label(tcg_ctx);
    body = gen_new_label(tcg_ctx);

    word = tcg_temp_new_i32(tcg_ctx);
    tcg_gen_ld_i32(tcg_ctx, word, tcg_ctx->cpu_env,
                   offsetof(ArchCPU, neg.icount_decr.u32) -
                   offsetof(ArchCPU, env));
    tcg_gen_brcondi_i32(tcg_ctx, TCG_COND_LT, word, 0, hooks);
    tcg_temp_free_i32(tcg_ctx, word);

    key = tcg_temp_new_i32(tcg_ctx);
    word = tcg_temp_new_i32(tcg_ctx);
    base = tcg_const_ptr(tcg_ctx, cell);
    tcg_gen_ld_i32(tcg_ctx, key, base, offsetof(uc_embedder_chain_cell, key));
    tcg_temp_free_ptr(tcg_ctx, base);
    base = tcg_const_ptr(tcg_ctx, guard);
    tcg_gen_ld_i32(tcg_ctx, word, base, 0);
    tcg_temp_free_ptr(tcg_ctx, base);
    tcg_gen_brcond_i32(tcg_ctx, TCG_COND_NE, key, word, hooks);
    tcg_temp_free_i32(tcg_ctx, key);
    tcg_temp_free_i32(tcg_ctx, word);

    weight = tcg_temp_new_i64(tcg_ctx);
    budget = tcg_temp_local_new_i64(tcg_ctx);
    base = tcg_const_ptr(tcg_ctx, cell);
    tcg_gen_ld32u_i64(tcg_ctx, weight, base,
                      offsetof(uc_embedder_chain_cell, weight));
    tcg_temp_free_ptr(tcg_ctx, base);
    base = tcg_const_ptr(tcg_ctx, &uc->embedder_chain);
    tcg_gen_ld_i64(tcg_ctx, budget, base, offsetof(uc_embedder_chain, budget));
    tcg_temp_free_ptr(tcg_ctx, base);
    tcg_gen_sub_i64(tcg_ctx, budget, budget, weight);
    tcg_temp_free_i64(tcg_ctx, weight);
    tcg_gen_brcondi_i64(tcg_ctx, TCG_COND_LT, budget, 0, hooks);

    base = tcg_const_ptr(tcg_ctx, &uc->embedder_chain);
    tcg_gen_st_i64(tcg_ctx, budget, base, offsetof(uc_embedder_chain, budget));
    tcg_temp_free_ptr(tcg_ctx, base);
    tcg_temp_free_i64(tcg_ctx, budget);
    tag = tcg_temp_new_i64(tcg_ctx);
    base = tcg_const_ptr(tcg_ctx, cell);
    tcg_gen_ld_i64(tcg_ctx, tag, base, offsetof(uc_embedder_chain_cell, tag));
    tcg_temp_free_ptr(tcg_ctx, base);
    base = tcg_const_ptr(tcg_ctx, &uc->embedder_chain);
    tcg_gen_st_i64(tcg_ctx, tag, base, offsetof(uc_embedder_chain, last));
    tcg_temp_free_ptr(tcg_ctx, base);
    tcg_temp_free_i64(tcg_ctx, tag);
    tcg_gen_br(tcg_ctx, body);

    gen_set_label(tcg_ctx, hooks);
    base = tcg_const_ptr(tcg_ctx, cell);
    {
        TCGv_ptr owner = tcg_const_ptr(tcg_ctx, uc);
        tcg_gen_st_ptr(tcg_ctx, base, owner,
                       offsetof(struct uc_struct, embedder_chain_cell));
        tcg_temp_free_ptr(tcg_ctx, owner);
    }
    tcg_temp_free_ptr(tcg_ctx, base);
    return body;
}

void translator_loop(const TranslatorOps *ops, DisasContextBase *db,
                     CPUState *cpu, TranslationBlock *tb, int max_insns)
{
    int bp_insn = 0;
    struct uc_struct *uc = (struct uc_struct *)cpu->uc;
    TCGContext *tcg_ctx = uc->tcg_ctx;
    TCGOp *prev_op = NULL;
    bool block_hook = false;
    struct EmbedderBlockGuard embedder_guard = { 0 };
    TCGLabel *chain_body = NULL;

    /* Initialize DisasContext */
    db->tb = tb;
    db->pc_first = tb->pc;
    db->pc_next = db->pc_first;
    db->is_jmp = DISAS_NEXT;
    db->num_insns = 0;
    db->max_insns = max_insns;
    db->singlestep_enabled = cpu->singlestep_enabled;

    ops->init_disas_context(db, cpu);
    tcg_debug_assert(db->is_jmp == DISAS_NEXT);  /* no early exit */

    /* Reset the temp count so that we can identify leaks */
    tcg_clear_temp_count();

    /* Unicorn: early check to see if the address of this block is
     * the "run until" address. */
    if (uc_addr_is_exit(uc, tb->pc)) {
        // This should catch that instruction is at the end
        // and generate appropriate halting code.
        gen_tb_start(tcg_ctx, db->tb);
        ops->tb_start(db, cpu);
        db->num_insns++;
        ops->insn_start(db, cpu);
        ops->translate_insn(db, cpu);
        goto _end_loop;
    }

    /* Unicorn: trace this block on request
     * Only hook this block if it is not broken from previous translation due to
     * full translation cache
     */
    if (HOOK_EXISTS_BOUNDED(uc, UC_HOOK_BLOCK, tb->pc)) {
        ops->pc_sync(db, cpu);
        // embedder: an admitted block may pass its hooks (block chains). The
        // helper form of the exit test is passed with them; the inline forms
        // stay on both paths. A delay-slot flag cannot live across a label.
        if (uc->embedder_chains && !tcg_ctx->delay_slot_flag) {
            chain_body = gen_embedder_chain_guard(tcg_ctx, uc, db->pc_first);
        }
        prev_op = tcg_last_op(tcg_ctx);
        block_hook = true;
        if (uc->embedder_insn_filter) {
            gen_embedder_block_guard(tcg_ctx, uc, db->pc_first, &embedder_guard);
        } else {
            gen_uc_tracecode(tcg_ctx, 0xf8f8f8f8, UC_HOOK_BLOCK_IDX, uc,
                             db->pc_first);
        }
    }

    // tcg_dump_ops(tcg_ctx, false, "translator loop");

    uc->embedder_tb_slow = false;

    /* Start translating.  */
    if (chain_body && (uc->embedder_block_exits ||
                       uc->embedder_instruction_counter)) {
        gen_set_label(tcg_ctx, chain_body);
        chain_body = NULL;
    }
    gen_tb_start(tcg_ctx, db->tb);
    if (chain_body) {
        gen_set_label(tcg_ctx, chain_body);
    }
    // tcg_dump_ops(tcg_ctx, false, "tb start");

    ops->tb_start(db, cpu);
    // tcg_dump_ops(tcg_ctx, false, "tb start 2");

    tcg_debug_assert(db->is_jmp == DISAS_NEXT);  /* no early exit */

    while (true) {
        target_ulong embedder_pc = db->pc_next;

        // embedder: an instruction that touched a hooked device page starts its
        // own block (it is also the last of it, below)
        if (db->num_insns > 0 && uc->embedder_io_pcs &&
            g_hash_table_lookup(uc->embedder_io_pcs, (gconstpointer)(uintptr_t)embedder_pc)) {
            db->is_jmp = DISAS_TOO_MANY;
            break;
        }
        uc->embedder_insn_decision = 1;

        db->num_insns++;

        ops->insn_start(db, cpu);
        tcg_debug_assert(db->is_jmp == DISAS_NEXT);  /* no early exit */

        /* Pass breakpoint hits to target for further processing */
        if (!db->singlestep_enabled
            && unlikely(!QTAILQ_EMPTY(&cpu->breakpoints))) {
            CPUBreakpoint *bp;
            QTAILQ_FOREACH(bp, &cpu->breakpoints, entry) {
                if (bp->pc == db->pc_next) {
                    if (uc->embedder_tb_thumb && uc->embedder_acct.enabled == 3) {
                        // A breakpoint can stop before a translated instruction
                        // retires; do not pre-debit this Thumb block.
                        uc->embedder_tb_slow = true;
                    }
                    if (ops->breakpoint_check(db, cpu, bp)) {
                        bp_insn = 1;
                        break;
                    }
                }
            }
            /* The breakpoint_check hook may use DISAS_TOO_MANY to indicate
               that only one more instruction is to be executed.  Otherwise
               it should use DISAS_NORETURN when generating an exception,
               but may use a DISAS_TARGET_* value for Something Else.  */
            if (db->is_jmp > DISAS_TOO_MANY) {
                break;
            }
        }

        /* Disassemble one instruction.  The translate_insn hook should
           update db->pc_next and db->is_jmp to indicate what should be
           done next -- either exiting this loop or locate the start of
           the next instruction.  */
        ops->translate_insn(db, cpu);
        // tcg_dump_ops(tcg_ctx, false, "insn translate");

        // embedder: the filter may ask for the block hook on every entry (4).
        // Code-coherence watches are installed by the filter before execution,
        // so this bit is reserved for observers that need each block entry.
        if (uc->embedder_insn_decision & 4) {
            uc->embedder_tb_slow = true;
        }
        /* Stop translation if translate_insn so indicated.  */
        if (db->is_jmp != DISAS_NEXT) {
            break;
        }

        // embedder: a hooked special or replaced instruction or a device access
        // ends its block
        if (uc->embedder_insn_filter && ((uc->embedder_insn_decision & 3) >= 2 ||
            (uc->embedder_io_pcs &&
             g_hash_table_lookup(uc->embedder_io_pcs, (gconstpointer)(uintptr_t)embedder_pc)))) {
            db->is_jmp = DISAS_TOO_MANY;
            break;
        }

        /* Stop translation if the output buffer is full,
           or we have executed all of the allowed instructions.  */
        if (tcg_op_buf_full(tcg_ctx) || db->num_insns >= db->max_insns) {
            db->is_jmp = DISAS_TOO_MANY;
            break;
        }
    }

_end_loop:
    /* Emit code to exit the TB, as indicated by db->is_jmp.  */
    ops->tb_stop(db, cpu);
    gen_tb_end(tcg_ctx, db->tb, db->num_insns - bp_insn);
    // tcg_dump_ops(tcg_ctx, false, "tb end");

    /* The disas_log hook may use these values rather than recompute.  */
    db->tb->size = db->pc_next - db->pc_first;
    db->tb->icount = db->num_insns;

    hooked_regions_check(uc, db->tb->pc, db->tb->size);

    if (block_hook) {
        TCGOp *tcg_op;

        // Unicorn: patch the callback to have the proper block size.
        if (embedder_guard.active) {
            tcg_op = embedder_guard.size;
        } else if (prev_op) {
            // As explained further up in the function where prev_op is
            // assigned, we move forward in the tail queue, so we're modifying the
            // move instruction generated by gen_uc_tracecode() that contains
            // the instruction size to assign the proper size (replacing 0xF1F1F1F1).
            tcg_op = QTAILQ_NEXT(prev_op, link);
        } else {
            // this basic block is the first emulated code ever,
            // so the basic block operand is the first operand
            tcg_op = QTAILQ_FIRST(&tcg_ctx->ops);
        }

        tcg_op->args[1] = db->tb->size;
        // The A64 uc_emu_start counter needs actual instruction entries,
        // including a lower EL's variable-width A32/Thumb blocks. The helper
        // strips this metadata before calling ordinary block observers.
        if (uc->count_hook && uc->count_hook_is_block) {
            assert(db->tb->size <= 0xffff && db->tb->icount <= 0x3fff);
            tcg_op->args[1] |= db->tb->icount << 16;
        }
        // embedder: with a translation filter installed, bit 31 of the block
        // hook's size reports Thumb state, so the hook need not read CPSR.
        if (uc->embedder_insn_filter && uc->embedder_tb_thumb) {
            tcg_op->args[1] |= 0x80000000u;
        }
        // embedder: bit 30 marks an ARM block budgeted accounting may enter
        // without hooks after translation has installed any code-write watches.
        if (uc->embedder_insn_filter && !uc->embedder_tb_thumb && !uc->embedder_tb_slow) {
            tcg_op->args[1] |= 0x40000000u;
        }
        if (embedder_guard.active) {
            uint64_t block_end = (uint64_t)db->pc_first + db->tb->size;
            bool thumb_page = db->tb->size != 0 &&
                (db->pc_first & ~UINT64_C(0xfff)) ==
                    ((block_end - 1) & ~UINT64_C(0xfff));
            bool fast = !uc->embedder_tb_slow &&
                        (!uc->embedder_tb_thumb || !db->singlestep_enabled) &&
                        block_end <= (UINT64_C(1) << 32) &&
                        (!uc->embedder_tb_thumb ||
                         (uc->embedder_acct.enabled == 3 && thumb_page));
            tcg_set_insn_param(embedder_guard.fast, 1, fast ? 1 : 0);
            tcg_set_insn_param(embedder_guard.count_low, 1,
                               (uint32_t)db->tb->icount);
            if (embedder_guard.count_high) {
                tcg_set_insn_param(embedder_guard.count_high, 1, 0);
            }
            if (embedder_guard.packed) {
                tcg_set_insn_param(embedder_guard.packed, 1,
                                   ((uint64_t)(uint32_t)db->pc_first << 32) |
                                       (uint32_t)(db->tb->size / 4));
            }
        }
    }
}
