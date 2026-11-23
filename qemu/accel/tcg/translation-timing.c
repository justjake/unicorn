/* Opt-in diagnostics for time spent generating translation blocks. */

#include "qemu/osdep.h"
#include "uc_priv.h"
#include "translation-timing.h"

#define UC_TRANSLATION_TIMING_TABLE_CAPACITY (1U << 20)
#define UC_TRANSLATION_TIMING_TABLE_MASK (UC_TRANSLATION_TIMING_TABLE_CAPACITY - 1)
#define UC_TRANSLATION_TIMING_MAX_PROBES 256
#define UC_TRANSLATION_REPEAT_CAUSE_COUNT 9

enum uc_translation_repeat_counter {
    UC_REPEAT_ACROSS_GLOBAL_FLUSH_COUNT,
    UC_REPEAT_ACROSS_GLOBAL_FLUSH_NS,
    UC_REPEAT_SAME_GENERATION_COUNT,
    UC_REPEAT_SAME_GENERATION_NS,
    UC_REPEAT_SAME_GENERATION_UNKNOWN_CAUSE_COUNT,
    UC_REPEAT_SAME_GENERATION_UNKNOWN_CAUSE_NS,
    UC_REPEAT_SAME_GENERATION_CAUSE_BASE,
    UC_REPEAT_COUNTER_COUNT = UC_REPEAT_SAME_GENERATION_CAUSE_BASE +
                              UC_TRANSLATION_REPEAT_CAUSE_COUNT * 2
};

#define UC_TRANSLATION_INVALIDATION_UNOBSERVED 0
#define UC_TRANSLATION_INVALIDATION_AMBIGUOUS UINT32_MAX

typedef struct {
    UcTranslationTimingKey key;
    uint64_t last_generation;
    uint32_t invalidation_cause;
    bool occupied;
} UcTranslationTimingSlot;

struct UcTranslationTiming {
    UcTranslationTimingSlot *slots;
    uint64_t generation;
    uint64_t repeat_counters[UC_REPEAT_COUNTER_COUNT];
};

static const uint64_t uc_translation_timing_zero_counters[UC_REPEAT_COUNTER_COUNT];

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

static unsigned int uc_translation_timing_cause_index(uint32_t cause)
{
    switch (cause) {
    case UC_COUNT_INVALIDATE_RESTORE_NOCACHE:
        return 0;
    case UC_COUNT_INVALIDATE_EXEC_NOCACHE:
        return 1;
    case UC_COUNT_INVALIDATE_IO_RECOMPILE:
        return 2;
    case UC_COUNT_INVALIDATE_GUEST_STORE:
        return 3;
    case UC_COUNT_INVALIDATE_EXIT_ADDRESS:
        return 4;
    case UC_COUNT_INVALIDATE_EMBEDDER:
        return 5;
    case UC_COUNT_INVALIDATE_WATCHPOINT:
        return 6;
    case UC_COUNT_INVALIDATE_RESTART:
        return 7;
    case UC_COUNT_INVALIDATE_PHYSICAL_WRITE:
        return 8;
    default:
        return UC_TRANSLATION_REPEAT_CAUSE_COUNT;
    }
}

static void uc_translation_timing_count(uint64_t *counters,
                                        unsigned int index,
                                        uint64_t elapsed_ns)
{
    counters[index]++;
    counters[index + 1] += elapsed_ns;
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

    if (!timing) {
        return;
    }
    for (probe = 0; probe < UC_TRANSLATION_TIMING_MAX_PROBES; ++probe) {
        UcTranslationTimingSlot *slot = &timing->slots[index];

        if (!slot->occupied) {
            slot->key = *key;
            slot->last_generation = timing->generation;
            slot->invalidation_cause = UC_TRANSLATION_INVALIDATION_UNOBSERVED;
            slot->occupied = true;
            count = UC_COUNT_TRANSLATION_FIRST_LOOKUP_KEY;
            duration = UC_COUNT_TRANSLATION_FIRST_LOOKUP_KEY_NS;
            goto record;
        }
        if (uc_translation_timing_key_equal(&slot->key, key)) {
            unsigned int cause_index;

            count = UC_COUNT_TRANSLATION_REPEATED_LOOKUP_KEY;
            duration = UC_COUNT_TRANSLATION_REPEATED_LOOKUP_KEY_NS;
            if (slot->last_generation != timing->generation) {
                uc_translation_timing_count(
                    timing->repeat_counters,
                    UC_REPEAT_ACROSS_GLOBAL_FLUSH_COUNT, elapsed_ns);
            } else {
                uc_translation_timing_count(
                    timing->repeat_counters,
                    UC_REPEAT_SAME_GENERATION_COUNT, elapsed_ns);
                cause_index = uc_translation_timing_cause_index(
                    slot->invalidation_cause);
                if (cause_index >= UC_TRANSLATION_REPEAT_CAUSE_COUNT) {
                    uc_translation_timing_count(
                        timing->repeat_counters,
                        UC_REPEAT_SAME_GENERATION_UNKNOWN_CAUSE_COUNT,
                        elapsed_ns);
                } else {
                    uc_translation_timing_count(
                        timing->repeat_counters,
                        UC_REPEAT_SAME_GENERATION_CAUSE_BASE + cause_index * 2,
                        elapsed_ns);
                }
            }
            slot->last_generation = timing->generation;
            slot->invalidation_cause = UC_TRANSLATION_INVALIDATION_UNOBSERVED;
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

void uc_translation_timing_global_flush(struct uc_struct *uc)
{
    UcTranslationTiming *timing = uc->embedder_translation_timing;

    if (uc->embedder_guard_diagnostics && timing) {
        timing->generation++;
    }
}

void uc_translation_timing_note_invalidation(struct uc_struct *uc,
                                             const UcTranslationTimingKey *key,
                                             uint32_t cause)
{
    UcTranslationTiming *timing = uc->embedder_translation_timing;
    uint64_t hash;
    unsigned int index;
    unsigned int probe;

    if (!uc->embedder_guard_diagnostics || !timing) {
        return;
    }
    hash = uc_translation_timing_hash(key);
    index = hash & UC_TRANSLATION_TIMING_TABLE_MASK;
    for (probe = 0; probe < UC_TRANSLATION_TIMING_MAX_PROBES; ++probe) {
        UcTranslationTimingSlot *slot = &timing->slots[index];

        if (!slot->occupied) {
            return;
        }
        if (uc_translation_timing_key_equal(&slot->key, key)) {
            if (slot->last_generation != timing->generation) {
                return;
            }
            if (slot->invalidation_cause ==
                UC_TRANSLATION_INVALIDATION_UNOBSERVED) {
                slot->invalidation_cause = cause;
            } else if (slot->invalidation_cause != cause) {
                slot->invalidation_cause =
                    UC_TRANSLATION_INVALIDATION_AMBIGUOUS;
            }
            return;
        }
        index = (index + 1) & UC_TRANSLATION_TIMING_TABLE_MASK;
    }
}

const uint64_t *uc_translation_timing_repeat_counters(struct uc_struct *uc,
                                                       size_t *count)
{
    if (count) {
        *count = UC_REPEAT_COUNTER_COUNT;
    }
    return uc->embedder_translation_timing
               ? uc->embedder_translation_timing->repeat_counters
               : uc_translation_timing_zero_counters;
}
