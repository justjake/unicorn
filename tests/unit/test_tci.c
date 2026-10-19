/* Packed TCI bytecode: constants, branches, split words and typed helpers. */
#include "unicorn_test.h"

static uc_engine *machine(const uint32_t *words, size_t count)
{
    uc_engine *uc;
    size_t i;

    OK(uc_open(UC_ARCH_ARM, UC_MODE_ARM, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_ARM_CORTEX_A15));
    OK(uc_mem_map(uc, 0x1000, 0x2000, UC_PROT_ALL));
    for (i = 0; i < count; ++i) {
        uint32_t word = LEINT32(words[i]);
        OK(uc_mem_write(uc, 0x1000 + i * 4, &word, 4));
    }
    return uc;
}

static void test_constants_arithmetic_and_branches(void)
{
    const uint32_t words[] = {
        0xe3040567, // movw r0, #0x4567
        0xe3480123, // movt r0, #0x8123
        0xe3091bdf, // movw r1, #0x9bdf
        0xe3411357, // movt r1, #0x1357
        0xe0832190, // umull r2, r3, r0, r1
        0xe0c54190, // smull r4, r5, r0, r1
        0xe16f6f11, // clz r6, r1
        0xe7ec73d0, // ubfx r7, r0, #7, #13
        0xe7ab8a50, // sbfx r8, r0, #20, #12
        0xe6bf9f30, // rev r9, r0
        0xe3a0a000, // mov r10, #0
        0xe3a0b005, // mov r11, #5
        0xe09aa00b, // adds r10, r10, r11
        0xe25bb001, // subs r11, r11, #1
        0x1afffffc, // bne back to adds
        0xe35a000f, // cmp r10, #15
        0x03a0c05a, // moveq r12, #0x5a
        0x13a0c0a5, // movne r12, #0xa5
    };
    const uint32_t expected[] = {
        0x81234567, 0x13579bdf, 0x13bfd1b9, 0x09c1cfc4,
        0x13bfd1b9, 0xf66a33e5, 3, 0x68a,
        0xfffff812, 0x67452381, 15, 0, 0x5a,
    };
    const int ids[] = {
        UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
        UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
        UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
        UC_ARM_REG_R12,
    };
    uc_engine *uc = machine(words, sizeof(words) / sizeof(words[0]));
    size_t i;

    OK(uc_emu_start(uc, 0x1000, 0x1000 + sizeof(words), 0, 0));
    for (i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        uint32_t value;
        OK(uc_reg_read(uc, ids[i], &value));
        TEST_CHECK(value == expected[i]);
        TEST_MSG("r%zu: expected %08x, got %08x", i, expected[i], value);
    }
    OK(uc_close(uc));
}

static void test_typed_helpers_with_64_bit_arguments(void)
{
    const uint32_t words[] = {
        0xee302b01, // vadd.f64 d2, d0, d1
        0xee223b01, // vmul.f64 d3, d2, d1
        0xee003b01, // vmla.f64 d3, d0, d1
        0xec510b13, // vmov r0, r1, d3
    };
    uc_engine *uc = machine(words, sizeof(words) / sizeof(words[0]));
    uc_arm_cp_reg cpacr = {0};
    uint32_t fpexc = 0x40000000, low, high;
    uint64_t d0 = 0x3ff8000000000000ULL; // 1.5
    uint64_t d1 = 0x4004000000000000ULL; // 2.5
    uint64_t d3;

    cpacr.cp = 15;
    cpacr.crn = 1;
    cpacr.opc2 = 2;
    cpacr.val = 0xf << 20;
    OK(uc_reg_write(uc, UC_ARM_REG_CP_REG, &cpacr));
    OK(uc_reg_write(uc, UC_ARM_REG_FPEXC, &fpexc));
    OK(uc_reg_write(uc, UC_ARM_REG_D0, &d0));
    OK(uc_reg_write(uc, UC_ARM_REG_D1, &d1));
    OK(uc_emu_start(uc, 0x1000, 0x1000 + sizeof(words), 0, 0));
    OK(uc_reg_read(uc, UC_ARM_REG_D3, &d3));
    OK(uc_reg_read(uc, UC_ARM_REG_R0, &low));
    OK(uc_reg_read(uc, UC_ARM_REG_R1, &high));
    TEST_CHECK(d3 == 0x402b800000000000ULL); // 13.75
    TEST_CHECK(low == 0);
    TEST_CHECK(high == 0x402b8000);
    OK(uc_close(uc));
}

static void test_signed_and_doubleword_memory(void)
{
    const uint32_t words[] = {
        0xe1d010d0, // ldrsb r1, [r0]
        0xe1d020f2, // ldrsh r2, [r0, #2]
        0xe1c040d0, // ldrd r4, r5, [r0]
        0xe1c040f8, // strd r4, r5, [r0, #8]
    };
    const uint8_t data[] = {0x80, 0x7f, 0xc3, 0xa5, 0x78, 0x56, 0x34, 0x12};
    uint8_t copy[sizeof(data)];
    uint32_t address = 0x2000, value;
    uc_engine *uc = machine(words, sizeof(words) / sizeof(words[0]));

    OK(uc_mem_write(uc, address, data, sizeof(data)));
    OK(uc_reg_write(uc, UC_ARM_REG_R0, &address));
    OK(uc_emu_start(uc, 0x1000, 0x1000 + sizeof(words), 0, 0));
    OK(uc_reg_read(uc, UC_ARM_REG_R1, &value));
    TEST_CHECK(value == 0xffffff80);
    OK(uc_reg_read(uc, UC_ARM_REG_R2, &value));
    TEST_CHECK(value == 0xffffa5c3);
    OK(uc_reg_read(uc, UC_ARM_REG_R4, &value));
    TEST_CHECK(value == 0xa5c37f80);
    OK(uc_reg_read(uc, UC_ARM_REG_R5, &value));
    TEST_CHECK(value == 0x12345678);
    OK(uc_mem_read(uc, address + 8, copy, sizeof(copy)));
    TEST_CHECK(memcmp(data, copy, sizeof(data)) == 0);
    OK(uc_close(uc));
}

TEST_LIST = {
    {"constants_arithmetic_and_branches", test_constants_arithmetic_and_branches},
    {"typed_helpers_with_64_bit_arguments", test_typed_helpers_with_64_bit_arguments},
    {"signed_and_doubleword_memory", test_signed_and_doubleword_memory},
    {NULL, NULL},
};
