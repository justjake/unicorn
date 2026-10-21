/* Optional wasm32 TCI execution tier. Host compilation caches are not CPU state. */
#ifndef UNICORN_TCI_WASM_H
#define UNICORN_TCI_WASM_H
#include "unicorn.h"
#ifdef __cplusplus
extern "C" {
#endif
/* event: 1 offer, 2 invalidate one generation, 3 flush engine. slot is writable
 * only while that generation remains live. All callbacks run on the owner. */
typedef void (*uc_tci_wasm_notice)(uc_engine *engine, uint32_t event,
    uint32_t generation, uint32_t *slot, const void *code, uint32_t bytes);
/* Configure once before translation. dispatcher is a Wasm table slot with
 * signature (env, registers, helper_args, first_block_slot, first_code_base) -> uint64_t.
 * Code bases point immediately after the six-byte TCI block prefix. */
UNICORN_EXPORT uc_err uc_embedder_tci_wasm_configure(uint32_t threshold, uc_tci_wasm_notice notice, uint32_t dispatcher);
UNICORN_EXPORT const char *uc_embedder_tci_wasm_opname(uc_engine *engine, uint32_t opcode);
/* Copied by the owner before compilation; version 1 has 22 wasm32 words:
 * version, page bits, env-relative TLB descriptors, descriptor stride/table,
 * entry bits/read/write/addend, env-relative uc pointer, uc-relative page-hook
 * flag and read/write/read-after hook heads, then MemOp size/sign/byte-swap,
 * alignment shift/mask/unaligned/natural and MMU mode count. Offsets are signed.
 */
UNICORN_EXPORT const uint32_t *uc_embedder_tci_wasm_layout(uc_engine *engine);
/* Version 2: version, TranslationBlock slot and tc.ptr offsets, then the
 * address of contiguous u64 compiled/entry/chain counters. */
UNICORN_EXPORT const uint32_t *uc_embedder_tci_wasm_dispatch_layout(void);
/* Version, precise-PC word address, env-relative uc pointer, uc-relative
 * normal-exit flag, env-relative ARM CF, uc-relative retry and CPU pointers,
 * CPU-relative instruction-count decrement. */
UNICORN_EXPORT const uint32_t *uc_embedder_tci_wasm_helper_layout(void);
/* Version 1, 35 words: CPU uc pointer; uc stop/accounting offsets; block index,
 * fast/count masks; TB generation/slot/code offsets; counters; sixteen GPR and
 * four NZCV field offsets. State is materialized before observable boundaries. */
UNICORN_EXPORT const uint32_t *uc_embedder_tci_wasm_state_layout(void);
UNICORN_EXPORT const char *uc_embedder_tci_wasm_helper_name(uint32_t function);
/* Original typed target for a known TCI wrapper, or zero. The caller must
 * match the helper's declared argument and result signature exactly. */
UNICORN_EXPORT uint32_t uc_embedder_tci_wasm_helper_target(uint32_t function);
UNICORN_EXPORT uint64_t uc_embedder_tci_wasm_count(uint32_t counter);
UNICORN_EXPORT uint64_t uc_embedder_tci_wasm_load(void *env, uint32_t address, uint32_t oi, uint32_t pc);
UNICORN_EXPORT void uc_embedder_tci_wasm_store(void *env, uint32_t address, uint64_t value, uint32_t oi, uint32_t pc);
UNICORN_EXPORT uint32_t uc_embedder_tci_wasm_call(void *env, uint32_t function,
    uint32_t *regs, uint64_t *args, uint32_t words, uint32_t mask, uint32_t pc);
#ifdef __cplusplus
}
#endif
#endif
