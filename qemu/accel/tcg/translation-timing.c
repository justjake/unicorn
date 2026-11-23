/* Opt-in diagnostics for time spent generating translation blocks. */

#include "qemu/osdep.h"
#include "uc_priv.h"
#include "translation-timing.h"

#define UC_TRANSLATION_TIMING_TABLE_CAPACITY (1U << 20)
#define UC_TRANSLATION_TIMING_TABLE_MASK (UC_TRANSLATION_TIMING_TABLE_CAPACITY - 1)
#define UC_TRANSLATION_TIMING_MAX_PROBES 256

typedef struct {
    UcTranslationTimingKey key;
    bool occupied;
} UcTranslationTimingSlot;

struct UcTranslationTiming {
    UcTranslationTimingSlot *slots;
};

static uint64_t uc_translation_timing_hash(const UcTranslationTimingKey *key)
{
    uint32_t hash = qemu_xxhash7(key->pc, key->physical_page1,
                                 key->physical_page2, key->cs_base,
                                 key->flags);
    return qemu_xxhash7(hash, key->cflags, key->trace_vcpu_dstate,
                        key->physical_page2, key->pc);
}

static bool uc_translation_timing_key_equal(const UcTranslationTimingKey *left,
                                            const UcTranslationTimingKey *right)
{
    return left->pc == right->pc &&
           left->physical_page1 == right->physical_page1 &&
           left->physical_page2 == right->physical_page2 &&
           left->cs_base == right->cs_base && left->flags == right->flags &&
           left->cflags == right->cflags &&
           left->trace_vcpu_dstate == right->trace_vcpu_dstate;
}

struct UcTranslationTiming *uc_translation_timing_new(void)
{
    UcTranslationTiming *timing = calloc(1, sizeof(*timing));

    if (!timing) {
        return NULL;
    }
    timing->slots = calloc(UC_TRANSLATION_TIMING_TABLE_CAPACITY,
                           sizeof(*timing->slots));
    if (!timing->slots) {
        free(timing);
        return NULL;
    }
    return timing;
}

void uc_translation_timing_free(struct UcTranslationTiming *timing)
{
    if (timing) {
        free(timing->slots);
        free(timing);
    }
}

void uc_translation_timing_record(struct uc_struct *uc,
                                  const UcTranslationTimingKey *key,
                                  uint64_t elapsed_ns)
{
    UcTranslationTiming *timing = uc->embedder_translation_timing;
    uint64_t hash = uc_translation_timing_hash(key);
    unsigned int index = hash & UC_TRANSLATION_TIMING_TABLE_MASK;
    unsigned int probe;
    enum uc_embedder_exit_counter count;
    enum uc_embedder_exit_counter duration;

    for (probe = 0; probe < UC_TRANSLATION_TIMING_MAX_PROBES; ++probe) {
        UcTranslationTimingSlot *slot = &timing->slots[index];

        if (!slot->occupied) {
            slot->key = *key;
            slot->occupied = true;
            count = UC_COUNT_TRANSLATION_FIRST_LOOKUP_KEY;
            duration = UC_COUNT_TRANSLATION_FIRST_LOOKUP_KEY_NS;
            goto record;
        }
        if (uc_translation_timing_key_equal(&slot->key, key)) {
            count = UC_COUNT_TRANSLATION_REPEATED_LOOKUP_KEY;
            duration = UC_COUNT_TRANSLATION_REPEATED_LOOKUP_KEY_NS;
            goto record;
        }
        index = (index + 1) & UC_TRANSLATION_TIMING_TABLE_MASK;
    }

    count = UC_COUNT_TRANSLATION_UNTRACKED_LOOKUP_KEY;
    duration = UC_COUNT_TRANSLATION_UNTRACKED_LOOKUP_KEY_NS;

record:
    uc->embedder_exit_counters[count]++;
    uc->embedder_exit_counters[duration] += elapsed_ns;
}
