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
 * embedder: budget accounting inside translated code. Fast ARM and opted-in
 * Thumb blocks load the interactive budget, subtract their instruction count and fall through;
 * a miss or a slow block enters the ordinary helper, which owns pause,
 * exact-policy ring writes, hook dispatch and refusal. The fast flag, the
 * instruction count and the helper's size immediate are patched after the
 * block is translated, when its instruction set and observers are known.
 */
struct EmbedderBlockGuard {
    TCGOp *fast;
    TCGOp *slow_reason;
    TCGOp *classifier_sample_pc;
    TCGOp *classifier_sample_word;
    TCGOp *classifier_histogram_class;
    TCGOp *classifier_histogram_icount;
    TCGOp *count_low;
    TCGOp *count_high;
    TCGOp *size;
    TCGOp *packed;
    TCGOp *record_bytes;
    TCGOp *record_icount;
    TCGv_i32 fast_value;
    TCGv_i64 count_value;
    uint64_t thumb_mode;
    bool active;
};

static void gen_embedder_guard_counter(TCGContext *tcg_ctx,
                                      struct uc_struct *uc,
                                      enum uc_embedder_exit_counter counter)
{
    TCGv_i64 value = tcg_temp_new_i64(tcg_ctx);
    TCGv_ptr ucp = tcg_const_ptr(tcg_ctx, uc);
    tcg_gen_ld_i64(tcg_ctx, value, ucp,
                   offsetof(struct uc_struct, embedder_exit_counters) +
                       counter * sizeof(uint64_t));
    tcg_gen_addi_i64(tcg_ctx, value, value, 1);
    tcg_gen_st_i64(tcg_ctx, value, ucp,
                   offsetof(struct uc_struct, embedder_exit_counters) +
                       counter * sizeof(uint64_t));
    tcg_temp_free_i64(tcg_ctx, value);
    tcg_temp_free_ptr(tcg_ctx, ucp);
}

static unsigned int embedder_classifier_class(uint32_t word, bool thumb)
{
    if (!thumb) {
        return UC_M4_CLASSIFIER_UNKNOWN_CLASS;
    }
    if (word <= UINT16_MAX) {
        unsigned int family = word >> 12;
        if (family == 4) {
            if ((word & 0xff00) == 0x4700) {
                return 5; // BX/BLX register.
            }
            if ((word & 0xf800) == 0x4800) {
                return 6; // LDR literal.
            }
            return 4; // Register/data-processing operations.
        }
        if (family == 0xb) {
            if ((word & 0xff00) == 0xbf00) {
                return 13; // IT and hints.
            }
            if ((word & 0xfe00) == 0xb400) {
                return 14; // PUSH.
            }
            if ((word & 0xfe00) == 0xbc00) {
                return 15; // POP, including PC.
            }
            if ((word & 0xf500) == 0xb100) {
                return 16; // CBZ/CBNZ.
            }
            return 17; // Other miscellaneous 0xb encodings.
        }
        if (family < 4) {
            return family;
        }
        return family < 0xb ? family + 2 : family + 6;
    }

    unsigned int first = word >> 16;
    unsigned int prefix = first >> 11;
    if (prefix < 0x1d || prefix > 0x1f) {
        return UC_M4_CLASSIFIER_UNKNOWN_CLASS;
    }
    return UC_M4_CLASSIFIER_NARROW_CLASSES +
           (prefix - 0x1d) * 8 + ((first >> 8) & 0x7);
}

static void gen_embedder_classifier_histogram(TCGContext *tcg_ctx,
                                              struct uc_struct *uc,
                                              struct EmbedderBlockGuard *guard)
{
    // Both constants are patched after translation; guest execution only
    // updates the selected callback/opportunity pair.
    TCGv_i64 class_id = tcg_const_i64(tcg_ctx, 0);
    guard->classifier_histogram_class = tcg_last_op(tcg_ctx);
    TCGv_i64 icount = tcg_const_i64(tcg_ctx, 0);
    guard->classifier_histogram_icount = tcg_last_op(tcg_ctx);
    TCGv_i64 offset = tcg_temp_new_i64(tcg_ctx);
    tcg_gen_shli_i64(tcg_ctx, offset, class_id, 4);
    TCGv_ptr base = tcg_const_ptr(
        tcg_ctx, &uc->embedder_exit_counters[UC_COUNT_M4_CLASSIFIER_FIRST]);
    TCGv_ptr offset_ptr = tcg_temp_new_ptr(tcg_ctx);
    tcg_gen_trunc_i64_ptr(tcg_ctx, offset_ptr, offset);
    TCGv_ptr counters = tcg_temp_new_ptr(tcg_ctx);
    tcg_gen_add_ptr(tcg_ctx, counters, base, offset_ptr);

    TCGv_i64 callbacks = tcg_temp_new_i64(tcg_ctx);
    tcg_gen_ld_i64(tcg_ctx, callbacks, counters, 0);
    tcg_gen_addi_i64(tcg_ctx, callbacks, callbacks, 1);
    tcg_gen_st_i64(tcg_ctx, callbacks, counters, 0);
    tcg_gen_addi_ptr(tcg_ctx, counters, counters, sizeof(uint64_t));
    TCGv_i64 opportunities = tcg_temp_new_i64(tcg_ctx);
    tcg_gen_ld_i64(tcg_ctx, opportunities, counters, 0);
    tcg_gen_add_i64(tcg_ctx, opportunities, opportunities, icount);
    tcg_gen_st_i64(tcg_ctx, opportunities, counters, 0);

    tcg_temp_free_i64(tcg_ctx, class_id);
    tcg_temp_free_i64(tcg_ctx, icount);
    tcg_temp_free_i64(tcg_ctx, offset);
    tcg_temp_free_i64(tcg_ctx, callbacks);
    tcg_temp_free_i64(tcg_ctx, opportunities);
    tcg_temp_free_ptr(tcg_ctx, base);
    tcg_temp_free_ptr(tcg_ctx, offset_ptr);
    tcg_temp_free_ptr(tcg_ctx, counters);
}

static void gen_embedder_classifier_sample(TCGContext *tcg_ctx,
                                           struct uc_struct *uc,
                                           struct EmbedderBlockGuard *guard)
{
    gen_embedder_classifier_histogram(tcg_ctx, uc, guard);
    // Power-of-two refusal counts spread bounded samples from startup into a
    // long run without random state or per-callback work outside this slow path.
    TCGLabel *skip = gen_new_label(tcg_ctx);
    TCGv_i64 refusal_count = tcg_temp_local_new_i64(tcg_ctx);
    TCGv_ptr ucp = tcg_const_ptr(tcg_ctx, uc);
    tcg_gen_ld_i64(tcg_ctx, refusal_count, ucp,
                   offsetof(struct uc_struct, embedder_exit_counters) +
                       UC_COUNT_GUARD_CLASSIFIER_SLOW * sizeof(uint64_t));
    tcg_temp_free_ptr(tcg_ctx, ucp);
    TCGv_i64 prior = tcg_temp_new_i64(tcg_ctx);
    tcg_gen_subi_i64(tcg_ctx, prior, refusal_count, 1);
    TCGv_i64 power_test = tcg_temp_new_i64(tcg_ctx);
    tcg_gen_and_i64(tcg_ctx, power_test, refusal_count, prior);
    tcg_gen_brcondi_i64(tcg_ctx, TCG_COND_NE, power_test, 0, skip);

    TCGv_ptr sample_ucp = tcg_const_ptr(tcg_ctx, uc);
    TCGv_i64 sample_count = tcg_temp_new_i64(tcg_ctx);
    tcg_gen_ld_i64(tcg_ctx, sample_count, sample_ucp,
                   offsetof(struct uc_struct, embedder_exit_counters) +
                       UC_COUNT_GUARD_SAMPLE_COUNT * sizeof(uint64_t));
    TCGv_i64 slot = tcg_temp_new_i64(tcg_ctx);
    tcg_gen_andi_i64(tcg_ctx, slot, sample_count,
                     UC_EMBEDDER_GUARD_SAMPLE_MASK);
    TCGv_i64 index = tcg_temp_new_i64(tcg_ctx);
    tcg_gen_shli_i64(tcg_ctx, index, slot, 1);
    tcg_gen_add_i64(tcg_ctx, index, index, slot);
    tcg_gen_addi_i64(tcg_ctx, index, index, UC_COUNT_GUARD_SAMPLE_RING);
    tcg_gen_shli_i64(tcg_ctx, index, index, 3);
    TCGv_ptr offset = tcg_temp_new_ptr(tcg_ctx);
    tcg_gen_trunc_i64_ptr(tcg_ctx, offset, index);
    TCGv_ptr samples = tcg_temp_new_ptr(tcg_ctx);
    TCGv_ptr base = tcg_const_ptr(tcg_ctx, uc);
    tcg_gen_addi_ptr(tcg_ctx, base, base,
                     offsetof(struct uc_struct, embedder_exit_counters));
    tcg_gen_add_ptr(tcg_ctx, samples, base, offset);
    tcg_gen_st_i64(tcg_ctx, refusal_count, samples, 0);

    TCGv_i64 sample_pc = tcg_const_i64(tcg_ctx, 0);
    guard->classifier_sample_pc = tcg_last_op(tcg_ctx);
    tcg_gen_st_i64(tcg_ctx, sample_pc, samples, sizeof(uint64_t));
    TCGv_i64 sample_word = tcg_const_i64(tcg_ctx, 0);
    guard->classifier_sample_word = tcg_last_op(tcg_ctx);
    tcg_gen_st_i64(tcg_ctx, sample_word, samples, 2 * sizeof(uint64_t));
    tcg_temp_free_i64(tcg_ctx, sample_pc);
    tcg_temp_free_i64(tcg_ctx, sample_word);

    tcg_gen_addi_i64(tcg_ctx, sample_count, sample_count, 1);
    tcg_gen_st_i64(tcg_ctx, sample_count, sample_ucp,
                   offsetof(struct uc_struct, embedder_exit_counters) +
                       UC_COUNT_GUARD_SAMPLE_COUNT * sizeof(uint64_t));

    gen_set_label(tcg_ctx, skip);
    tcg_temp_free_i64(tcg_ctx, refusal_count);
    tcg_temp_free_i64(tcg_ctx, prior);
    tcg_temp_free_i64(tcg_ctx, power_test);
    tcg_temp_free_i64(tcg_ctx, sample_count);
    tcg_temp_free_i64(tcg_ctx, slot);
    tcg_temp_free_i64(tcg_ctx, index);
    tcg_temp_free_ptr(tcg_ctx, offset);
    tcg_temp_free_ptr(tcg_ctx, samples);
    tcg_temp_free_ptr(tcg_ctx, base);
    tcg_temp_free_ptr(tcg_ctx, sample_ucp);
}

static void gen_embedder_block_guard(TCGContext *tcg_ctx,
                                     struct uc_struct *uc, target_ulong pc,
                                     struct EmbedderBlockGuard *guard)
{
    TCGLabel *helper = gen_new_label(tcg_ctx);
    TCGLabel *body = gen_new_label(tcg_ctx);
    TCGLabel *pause_checked = gen_new_label(tcg_ctx);
    TCGLabel *slow = uc->embedder_guard_diagnostics ? gen_new_label(tcg_ctx) : helper;
    TCGLabel *pause = uc->embedder_guard_diagnostics ? gen_new_label(tcg_ctx) : helper;
    TCGLabel *disarmed = uc->embedder_guard_diagnostics ? gen_new_label(tcg_ctx) : helper;
    TCGLabel *page_mismatch = uc->embedder_guard_diagnostics ? gen_new_label(tcg_ctx) : helper;
    TCGLabel *budget_miss = uc->embedder_guard_diagnostics ? gen_new_label(tcg_ctx) : helper;
    TCGOp *before_helper;

    guard->active = true;
    guard->fast_value = tcg_const_i32(tcg_ctx, 1);
    guard->fast = tcg_last_op(tcg_ctx);
    tcg_gen_brcondi_i32(tcg_ctx, TCG_COND_EQ, guard->fast_value, 0, slow);

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
    tcg_gen_brcondi_i32(tcg_ctx, TCG_COND_NE, paused, 0, pause);
    gen_set_label(tcg_ctx, pause_checked);

    // Coarse (2) and exact (1) accounting are translated inline; exact
    // accounting also writes its ring entry. Anything else enters the helper.
    TCGv_i64 enabled = tcg_temp_local_new_i64(tcg_ctx);
    ucp = tcg_const_ptr(tcg_ctx, uc);
    tcg_gen_ld_i64(tcg_ctx, enabled, ucp,
                   offsetof(struct uc_struct, embedder_acct.enabled));
    tcg_temp_free_ptr(tcg_ctx, ucp);
    if (uc->embedder_tb_thumb) {
        // Translation mode is fixed by this TB's flags and accounting mode.
        // Distinct constants keep cached mode3 and mode4 TBs isolated.
        guard->thumb_mode = uc->embedder_acct.enabled == 4 ? 4 : 3;
        tcg_gen_brcondi_i64(tcg_ctx, TCG_COND_NE, enabled,
                            guard->thumb_mode, disarmed);
        uint64_t page_idx = (uint64_t)pc >> 10;
        if (page_idx < 1024) {
            unsigned word_idx = (unsigned)(page_idx >> 6);
            uint64_t bit_mask = UINT64_C(1) << (page_idx & 63);
            TCGv_i64 word = tcg_temp_new_i64(tcg_ctx);
            ucp = tcg_const_ptr(tcg_ctx, uc);
            tcg_gen_ld_i64(tcg_ctx, word, ucp,
                           offsetof(struct uc_struct,
                                    embedder_acct.thumb_page_bitmap) +
                               word_idx * sizeof(uint64_t));
            tcg_temp_free_ptr(tcg_ctx, ucp);
            tcg_gen_andi_i64(tcg_ctx, word, word, (int64_t)bit_mask);
            tcg_gen_brcondi_i64(tcg_ctx, TCG_COND_EQ, word, 0, page_mismatch);
            tcg_temp_free_i64(tcg_ctx, word);
        } else {
            TCGv_i64 thumb_page = tcg_temp_new_i64(tcg_ctx);
            ucp = tcg_const_ptr(tcg_ctx, uc);
            tcg_gen_ld_i64(tcg_ctx, thumb_page, ucp,
                           offsetof(struct uc_struct, embedder_acct.thumb_page));
            tcg_temp_free_ptr(tcg_ctx, ucp);
            tcg_gen_brcondi_i64(tcg_ctx, TCG_COND_NE, thumb_page,
                                (int64_t)(pc & ~UINT64_C(0x3ff)), page_mismatch);
            tcg_temp_free_i64(tcg_ctx, thumb_page);
        }
    } else {
#if TCG_TARGET_REG_BITS == 64
        TCGv_i64 mode = tcg_temp_new_i64(tcg_ctx);
        tcg_gen_subi_i64(tcg_ctx, mode, enabled, 1);
        tcg_gen_brcondi_i64(tcg_ctx, TCG_COND_GTU, mode, 1, disarmed);
        tcg_temp_free_i64(tcg_ctx, mode);
#else
        tcg_gen_brcondi_i64(tcg_ctx, TCG_COND_NE, enabled, 2, disarmed);
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
    tcg_gen_brcondi_i64(tcg_ctx, TCG_COND_LT, budget, 0, budget_miss);
    ucp = tcg_const_ptr(tcg_ctx, uc);
    tcg_gen_st_i64(tcg_ctx, budget, ucp,
                   offsetof(struct uc_struct, embedder_acct.budget));
    tcg_temp_free_ptr(tcg_ctx, ucp);
    if (uc->embedder_tb_thumb && guard->thumb_mode == 4) {
        ucp = tcg_const_ptr(tcg_ctx, uc);
        TCGv_i64 start = tcg_const_i64(tcg_ctx, pc);
        tcg_gen_st_i64(tcg_ctx, start, ucp,
                       offsetof(struct uc_struct, embedder_acct.last_block_start));
        tcg_temp_free_i64(tcg_ctx, start);
        tcg_gen_st_i64(tcg_ctx, budget, ucp,
                       offsetof(struct uc_struct,
                                embedder_acct.last_block_remaining_budget));
        TCGv_i32 bytes = tcg_const_i32(tcg_ctx, 0);
        guard->record_bytes = tcg_last_op(tcg_ctx);
        tcg_gen_st_i32(tcg_ctx, bytes, ucp,
                       offsetof(struct uc_struct, embedder_acct.last_block_bytes));
        TCGv_i32 icount = tcg_const_i32(tcg_ctx, 0);
        guard->record_icount = tcg_last_op(tcg_ctx);
        tcg_gen_st_i32(tcg_ctx, icount, ucp,
                       offsetof(struct uc_struct, embedder_acct.last_block_icount));
        tcg_temp_free_i32(tcg_ctx, bytes);
        tcg_temp_free_i32(tcg_ctx, icount);
        tcg_temp_free_ptr(tcg_ctx, ucp);
    }
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

    if (uc->embedder_guard_diagnostics) {
        TCGLabel *classifier_slow = gen_new_label(tcg_ctx);
        TCGLabel *page_span = gen_new_label(tcg_ctx);
        TCGLabel *slow_other = gen_new_label(tcg_ctx);

        gen_set_label(tcg_ctx, slow);
        TCGv_i32 reason = tcg_const_i32(tcg_ctx, 0);
        guard->slow_reason = tcg_last_op(tcg_ctx);
        tcg_gen_brcondi_i32(tcg_ctx, TCG_COND_EQ, reason,
                            UC_EMBEDDER_TB_SLOW_CLASSIFIER, classifier_slow);
        tcg_gen_brcondi_i32(tcg_ctx, TCG_COND_EQ, reason, 3, page_span);
        tcg_gen_br(tcg_ctx, slow_other);

        gen_set_label(tcg_ctx, classifier_slow);
        gen_embedder_guard_counter(tcg_ctx, uc, UC_COUNT_GUARD_CLASSIFIER_SLOW);
        gen_embedder_classifier_sample(tcg_ctx, uc, guard);
        tcg_gen_br(tcg_ctx, helper);
        gen_set_label(tcg_ctx, page_span);
        gen_embedder_guard_counter(tcg_ctx, uc, UC_COUNT_GUARD_THUMB_PAGE_SPAN);
        tcg_gen_br(tcg_ctx, helper);
        gen_set_label(tcg_ctx, slow_other);
        gen_embedder_guard_counter(tcg_ctx, uc, UC_COUNT_GUARD_OTHER_SLOW);
        tcg_gen_br(tcg_ctx, helper);

        gen_set_label(tcg_ctx, pause);
        gen_embedder_guard_counter(tcg_ctx, uc, UC_COUNT_GUARD_PAUSE);
        tcg_gen_br(tcg_ctx, helper);
        gen_set_label(tcg_ctx, disarmed);
        gen_embedder_guard_counter(tcg_ctx, uc, UC_COUNT_GUARD_DISARMED);
        tcg_gen_br(tcg_ctx, helper);
        gen_set_label(tcg_ctx, page_mismatch);
        gen_embedder_guard_counter(tcg_ctx, uc, UC_COUNT_GUARD_PAGE_MISMATCH);
        tcg_gen_br(tcg_ctx, helper);
        gen_set_label(tcg_ctx, budget_miss);
        gen_embedder_guard_counter(tcg_ctx, uc, UC_COUNT_GUARD_BUDGET);
        tcg_gen_br(tcg_ctx, helper);
        tcg_temp_free_i32(tcg_ctx, reason);
    }

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
    uc->embedder_tb_slow_reason = 0;
    uc->embedder_tb_slow_pc = 0;
    uc->embedder_tb_slow_word = 0;

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
        if (db->num_insns > 0 && uc->embedder_insn_filter &&
            uc->embedder_acct.enabled == 4 &&
            uc->embedder_tb_thumb &&
            uc->embedder_watched_store_pcs &&
            g_hash_table_lookup(uc->embedder_watched_store_pcs,
                                (gconstpointer)(uintptr_t)embedder_pc)) {
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
                    if (uc->embedder_tb_thumb &&
                        (uc->embedder_acct.enabled == 3 ||
                         uc->embedder_acct.enabled == 4)) {
                        // A breakpoint can stop before a translated instruction
                        // retires; do not pre-debit this Thumb block.
                        UC_EMBEDDER_MARK_TB_SLOW(uc, UC_EMBEDDER_TB_SLOW_OTHER);
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
            UC_EMBEDDER_MARK_TB_SLOW(uc, UC_EMBEDDER_TB_SLOW_CLASSIFIER);
        }
        /* Stop translation if translate_insn so indicated.  */
        if (db->is_jmp != DISAS_NEXT) {
            break;
        }

        // embedder: a hooked special or replaced instruction or a device access
        // ends its block
        if (uc->embedder_insn_filter && ((uc->embedder_insn_decision & 3) >= 2 ||
            (uc->embedder_io_pcs &&
             g_hash_table_lookup(uc->embedder_io_pcs, (gconstpointer)(uintptr_t)embedder_pc)) ||
            (uc->embedder_acct.enabled == 4 && uc->embedder_tb_thumb &&
             uc->embedder_watched_store_pcs &&
             g_hash_table_lookup(uc->embedder_watched_store_pcs,
                                 (gconstpointer)(uintptr_t)embedder_pc)))) {
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
                (db->pc_first & ~UINT64_C(0x3ff)) ==
                    ((block_end - 1) & ~UINT64_C(0x3ff));
            bool thumb_mode = embedder_guard.thumb_mode ==
                                  uc->embedder_acct.enabled &&
                              (embedder_guard.thumb_mode == 3 ||
                               embedder_guard.thumb_mode == 4);
            bool fast = !uc->embedder_tb_slow &&
                        (!uc->embedder_tb_thumb || !db->singlestep_enabled) &&
                        block_end <= (UINT64_C(1) << 32) &&
                        (!uc->embedder_tb_thumb ||
                         (thumb_mode && thumb_page));
            tcg_set_insn_param(embedder_guard.fast, 1, fast ? 1 : 0);
            if (uc->embedder_guard_diagnostics && !fast) {
                uint32_t reason = UC_EMBEDDER_TB_SLOW_OTHER;
                if (uc->embedder_tb_slow_reason ==
                    UC_EMBEDDER_TB_SLOW_CLASSIFIER) {
                    reason = UC_EMBEDDER_TB_SLOW_CLASSIFIER;
                } else if (uc->embedder_tb_thumb && !thumb_page) {
                    reason = 3; // translated bytes cross the enabled 1 KiB page
                }
                tcg_set_insn_param(embedder_guard.slow_reason, 1, reason);
                if (reason == UC_EMBEDDER_TB_SLOW_CLASSIFIER) {
                    tcg_set_insn_param(embedder_guard.classifier_sample_pc, 1,
                                       uc->embedder_tb_slow_pc);
                    tcg_set_insn_param(embedder_guard.classifier_sample_word, 1,
                                       uc->embedder_tb_slow_word);
                    tcg_set_insn_param(
                        embedder_guard.classifier_histogram_class, 1,
                        embedder_classifier_class(uc->embedder_tb_slow_word,
                                                  uc->embedder_tb_thumb));
                    tcg_set_insn_param(embedder_guard.classifier_histogram_icount, 1,
                                       db->tb->icount);
                }
            }
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
            if (embedder_guard.record_bytes) {
                tcg_set_insn_param(embedder_guard.record_bytes, 1,
                                   db->tb->size);
                tcg_set_insn_param(embedder_guard.record_icount, 1,
                                   db->tb->icount);
            }
        }
    }
}
