#ifndef QEMU_TRANSLATION_TIMING_H
#define QEMU_TRANSLATION_TIMING_H

#include <stddef.h>
#include <stdint.h>

struct uc_struct;
struct UcTranslationTiming;
typedef struct UcTranslationTiming UcTranslationTiming;

/* Mirrors the cached-TB lookup identity; equality does not compare code bytes. */
typedef struct {
    uint64_t pc;
    uint64_t physical_page1;
    uint64_t physical_page2;
    uint64_t cs_base;
    uint32_t flags;
    uint32_t cflags;
    uint32_t trace_vcpu_dstate;
} UcTranslationTimingKey;

struct UcTranslationTiming *uc_translation_timing_new(void);
void uc_translation_timing_free(struct UcTranslationTiming *timing);
/*
 * Why a fresh tb_gen_code ran, as (count, elapsed_ns) pairs. The table already
 * remembers each key's generation and invalidation cause, and the key's cflags
 * carry the one-shot form, so the cause comes from state the diagnostic keeps
 * and never changes which instructions run.
 */
#define UC_GENERATION_FIRST 0
#define UC_GENERATION_AFTER_INVALIDATION 1
#define UC_GENERATION_AFTER_FLUSH 2
#define UC_GENERATION_LAST_IO 3
#define UC_GENERATION_CACHED_VARIANT 4
#define UC_GENERATION_LIMITED 5
#define UC_GENERATION_COUNT 6
void uc_translation_timing_record(struct uc_struct *uc,
                                  const UcTranslationTimingKey *key,
                                  uint64_t elapsed_ns);
void uc_translation_timing_global_flush(struct uc_struct *uc);
void uc_translation_timing_note_invalidation(struct uc_struct *uc,
                                             const UcTranslationTimingKey *key,
                                             uint32_t cause);
const uint64_t *uc_translation_timing_repeat_counters(struct uc_struct *uc,
                                                       size_t *count);
const uint64_t *uc_translation_timing_generation_counters(struct uc_struct *uc,
                                                          size_t *count);

#endif
