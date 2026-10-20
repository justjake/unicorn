/* Optional wasm32 tier over TCI's lowered TCG operations.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Memory operations and helper packing follow tci.c, retaining its softmmu
 * paths, precise original-bytecode return addresses and normal exit protocol.
 */
#include "unicorn/tci_wasm.h"
#include "exec/exec-all.h"
#if !defined(__EMSCRIPTEN__) || TCG_TARGET_REG_BITS != 32 || TARGET_LONG_BITS != 32
#error The optional TCI Wasm tier requires a wasm32 host and 32-bit guest
#endif

static uint32_t tci_wasm_threshold;
static uint32_t tci_wasm_generation;
static bool tci_wasm_started;
static uc_tci_wasm_notice tci_wasm_notice;
static uint64_t tci_wasm_entries, tci_wasm_executions;

uc_err uc_embedder_tci_wasm_configure(uint32_t threshold, uc_tci_wasm_notice notice)
{
    /* A module chooses its tier before constructing/running any CPU. */
    if (tci_wasm_started || tci_wasm_notice || !notice || !threshold || threshold > 1000000) {
        return UC_ERR_ARG;
    }
    QEMU_BUILD_BUG_ON(TCG_TARGET_NB_REGS != 16 || TCG_AREG0 != 14 || TCG_REG_CALL_STACK != 15);
    QEMU_BUILD_BUG_ON(TCG_COND_EQ != 8 || TCG_COND_NE != 9 || TCG_COND_LT != 2 || TCG_COND_GE != 3);
    QEMU_BUILD_BUG_ON(TCG_COND_LE != 10 || TCG_COND_GT != 11 || TCG_COND_LTU != 4 || TCG_COND_GEU != 5);
    QEMU_BUILD_BUG_ON(TCG_COND_LEU != 12 || TCG_COND_GTU != 13);
    tci_wasm_threshold = threshold;
    tci_wasm_notice = notice;
    return UC_ERR_OK;
}
uint32_t uc_embedder_tci_wasm_threshold(void) { return tci_wasm_threshold; }
const uint32_t *uc_embedder_tci_wasm_layout(uc_engine *uc)
{
    static uint32_t layout[22];
    const uint32_t value[22] = {
        1, TARGET_PAGE_BITS, TLB_MASK_TABLE_OFS(0), sizeof(CPUTLBDescFast),
        offsetof(CPUTLBDescFast, table), CPU_TLB_ENTRY_BITS,
        offsetof(CPUTLBEntry, addr_read), offsetof(CPUTLBEntry, addr_write),
        offsetof(CPUTLBEntry, addend), offsetof(CPUArchState, uc),
        offsetof(struct uc_struct, embedder_page_hooks),
        offsetof(struct uc_struct, hook[UC_HOOK_MEM_READ_IDX].head),
        offsetof(struct uc_struct, hook[UC_HOOK_MEM_WRITE_IDX].head),
        offsetof(struct uc_struct, hook[UC_HOOK_MEM_READ_AFTER_IDX].head),
        MO_SIZE, MO_SIGN, MO_BSWAP, MO_ASHIFT, MO_AMASK, MO_UNALN, MO_ALIGN,
        NB_MMU_MODES
    };
    memcpy(layout, value, sizeof(layout));
    return layout;
}
uint64_t uc_embedder_tci_wasm_count(uint32_t counter)
{
    return counter ? tci_wasm_entries : tci_wasm_executions;
}
void uc_embedder_tci_wasm_init(TranslationBlock *tb)
{
    tci_wasm_started = true;
    tb->tci_wasm_slot = tb->tci_wasm_hits = tb->tci_wasm_id = 0;
    if (tci_wasm_threshold && tci_wasm_generation != UINT32_MAX) {
        tb->tci_wasm_id = ++tci_wasm_generation;
    }
}
void uc_embedder_tci_wasm_invalidate(struct uc_struct *uc, TranslationBlock *tb)
{
    if (tb) {
        tb->tci_wasm_slot = 0;
        if (tb->tci_wasm_id && tci_wasm_notice) {
            tci_wasm_notice(uc, 2, tb->tci_wasm_id, NULL, NULL, 0);
        }
        tb->tci_wasm_id = 0;
    } else if (tci_wasm_notice) {
        tci_wasm_notice(uc, 3, 0, NULL, NULL, 0);
    }
}
const char *uc_embedder_tci_wasm_opname(uc_engine *uc, uint32_t opcode)
{
    return opcode < NB_OPS ? uc->tcg_ctx->tcg_op_defs[opcode].name : NULL;
}
static uintptr_t tci_wasm_return_address(uint32_t pc)
{
    tci_tb_ptr = pc;
    return pc - 2 + ((uint8_t *)(uintptr_t)pc)[-1];
}
uint64_t uc_embedder_tci_wasm_load(void *opaque, uint32_t address, uint32_t oi, uint32_t pc)
{
    CPUArchState *env = opaque;
    uintptr_t ra = tci_wasm_return_address(pc);
    switch (get_memop(oi) & (MO_BSWAP | MO_SSIZE)) {
    case MO_UB: return tci_ld_ub(env, address, oi, ra);
    case MO_SB: return (int64_t)(int8_t)tci_ld_ub(env, address, oi, ra);
    case MO_LEUW: return tci_ld_leuw(env, address, oi, ra);
    case MO_LESW: return (int64_t)(int16_t)tci_ld_leuw(env, address, oi, ra);
    case MO_LEUL: return tci_ld_leul(env, address, oi, ra);
    case MO_LESL: return (int64_t)(int32_t)tci_ld_leul(env, address, oi, ra);
    case MO_LEQ: return tci_ld_leq(env, address, oi, ra);
    case MO_BEUW: return tci_ld_beuw(env, address, oi, ra);
    case MO_BESW: return (int64_t)(int16_t)tci_ld_beuw(env, address, oi, ra);
    case MO_BEUL: return tci_ld_beul(env, address, oi, ra);
    case MO_BESL: return (int64_t)(int32_t)tci_ld_beul(env, address, oi, ra);
    case MO_BEQ: return tci_ld_beq(env, address, oi, ra);
    default: tcg_abort();
    }
}
void uc_embedder_tci_wasm_store(void *opaque, uint32_t address, uint64_t value, uint32_t oi, uint32_t pc)
{
    CPUArchState *env = opaque;
    uintptr_t ra = tci_wasm_return_address(pc);
    switch (get_memop(oi) & (MO_BSWAP | MO_SIZE)) {
    case MO_UB: tci_st_b(env, address, value, oi, ra); return;
    case MO_LEUW: tci_st_lew(env, address, value, oi, ra); return;
    case MO_LEUL: tci_st_lel(env, address, value, oi, ra); return;
    case MO_LEQ: tci_st_leq(env, address, value, oi, ra); return;
    case MO_BEUW: tci_st_bew(env, address, value, oi, ra); return;
    case MO_BEUL: tci_st_bel(env, address, value, oi, ra); return;
    case MO_BEQ: tci_st_beq(env, address, value, oi, ra); return;
    default: tcg_abort();
    }
}
uint32_t uc_embedder_tci_wasm_call(void *opaque, uint32_t function,
    uint32_t *regs, uint64_t *args, uint32_t words, uint32_t mask, uint32_t pc)
{
    CPUArchState *env = opaque;
    static const uint8_t call_regs[] = {0,1,2,3,5,6,7,8,9,10,11,12};
    unsigned word = 0, arg = 0;
    uint64_t result;
    tci_tb_ptr = pc;
    assert(words <= ARRAY_SIZE(call_regs));
    while (word < words) {
        uint64_t value = regs[call_regs[word++]];
        if ((mask >> (2 * (arg + 1))) & 1) {
            assert(word < words);
            value |= (uint64_t)regs[call_regs[word++]] << 32;
        }
        assert(arg < MAX_OPC_PARAM_IARGS);
        args[arg++] = value;
    }
    result = ((helper_function)(uintptr_t)function)(args);
    if (unlikely(env->uc->tci_return_pending)) {
        env->uc->tci_return_pending = false;
        return 1;
    }
    regs[0] = result;
    regs[1] = result >> 32;
    return 0;
}
