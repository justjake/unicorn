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
/* Configure once, before any translation block has been generated. */
UNICORN_EXPORT uc_err uc_embedder_tci_wasm_configure(uint32_t threshold, uc_tci_wasm_notice notice);
UNICORN_EXPORT const char *uc_embedder_tci_wasm_opname(uc_engine *engine, uint32_t opcode);
UNICORN_EXPORT uint64_t uc_embedder_tci_wasm_count(uint32_t counter);
UNICORN_EXPORT uint64_t uc_embedder_tci_wasm_load(void *env, uint32_t address, uint32_t oi, uint32_t pc);
UNICORN_EXPORT void uc_embedder_tci_wasm_store(void *env, uint32_t address, uint64_t value, uint32_t oi, uint32_t pc);
UNICORN_EXPORT uint32_t uc_embedder_tci_wasm_call(void *env, uint32_t function,
    uint32_t *regs, uint64_t *args, uint32_t words, uint32_t mask, uint32_t pc);
#ifdef __cplusplus
}
#endif
#endif
