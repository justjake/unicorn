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
void uc_translation_timing_record(struct uc_struct *uc,
                                  const UcTranslationTimingKey *key,
                                  uint64_t elapsed_ns);
void uc_translation_timing_global_flush(struct uc_struct *uc);
void uc_translation_timing_note_invalidation(struct uc_struct *uc,
                                             const UcTranslationTimingKey *key,
                                             uint32_t cause);
const uint64_t *uc_translation_timing_repeat_counters(struct uc_struct *uc,
                                                       size_t *count);

#endif
