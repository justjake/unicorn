#include "unicorn_test.h"

const uint64_t code_start = 0x1000;
const uint64_t code_len = 0x4000;

static void uc_common_setup(uc_engine **uc, uc_arch arch, uc_mode mode,
                            const char *code, uint64_t size, uc_cpu_arm cpu)
{
    OK(uc_open(arch, mode, uc));
    OK(uc_ctl_set_cpu_model(*uc, cpu));
    OK(uc_mem_map(*uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(*uc, code_start, code, size));
}

static void test_arm_nop(void)
{
    uc_engine *uc;
    char code[] = "\x00\xf0\x20\xe3"; // nop
    int r_r0 = 0x1234;
    int r_r2 = 0x6789;

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_ARM, code, sizeof(code) - 1,
                    UC_CPU_ARM_CORTEX_A15);
    OK(uc_reg_write(uc, UC_ARM_REG_R0, &r_r0));
    OK(uc_reg_write(uc, UC_ARM_REG_R2, &r_r2));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_ARM_REG_R0, &r_r0));
    OK(uc_reg_read(uc, UC_ARM_REG_R2, &r_r2));
    TEST_CHECK(r_r0 == 0x1234);
    TEST_CHECK(r_r2 == 0x6789);

    OK(uc_close(uc));
}

static void test_arm_thumb_sub(void)
{
    uc_engine *uc;
    char code[] = "\x83\xb0"; // sub    sp, #0xc
    int r_sp = 0x1234;

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_THUMB, code, sizeof(code) - 1,
                    UC_CPU_ARM_CORTEX_A15);
    OK(uc_reg_write(uc, UC_ARM_REG_SP, &r_sp));

    OK(uc_emu_start(uc, code_start | 1, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_ARM_REG_SP, &r_sp));
    TEST_CHECK(r_sp == 0x1228);

    OK(uc_close(uc));
}

static void test_armeb_sub(void)
{
    uc_engine *uc;
    char code[] =
        "\xe3\xa0\x00\x37\xe0\x42\x10\x03"; // mov r0, #0x37; sub r1, r2, r3
    int r_r0 = 0x1234;
    int r_r2 = 0x6789;
    int r_r3 = 0x3333;
    int r_r1;

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_ARM | UC_MODE_BIG_ENDIAN, code,
                    sizeof(code) - 1, UC_CPU_ARM_1176);
    OK(uc_reg_write(uc, UC_ARM_REG_R0, &r_r0));
    OK(uc_reg_write(uc, UC_ARM_REG_R2, &r_r2));
    OK(uc_reg_write(uc, UC_ARM_REG_R3, &r_r3));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_ARM_REG_R0, &r_r0));
    OK(uc_reg_read(uc, UC_ARM_REG_R1, &r_r1));
    OK(uc_reg_read(uc, UC_ARM_REG_R2, &r_r2));
    OK(uc_reg_read(uc, UC_ARM_REG_R3, &r_r3));

    TEST_CHECK(r_r0 == 0x37);
    TEST_CHECK(r_r2 == 0x6789);
    TEST_CHECK(r_r3 == 0x3333);
    TEST_CHECK(r_r1 == 0x3456);

    OK(uc_close(uc));
}

static void test_armeb_be8_sub(void)
{
    uc_engine *uc;
    char code[] =
        "\x37\x00\xa0\xe3\x03\x10\x42\xe0"; // mov r0, #0x37; sub r1, r2, r3
    int r_r0 = 0x1234;
    int r_r2 = 0x6789;
    int r_r3 = 0x3333;
    int r_r1;

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_ARM | UC_MODE_ARMBE8, code,
                    sizeof(code) - 1, UC_CPU_ARM_CORTEX_A15);
    OK(uc_reg_write(uc, UC_ARM_REG_R0, &r_r0));
    OK(uc_reg_write(uc, UC_ARM_REG_R2, &r_r2));
    OK(uc_reg_write(uc, UC_ARM_REG_R3, &r_r3));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_ARM_REG_R0, &r_r0));
    OK(uc_reg_read(uc, UC_ARM_REG_R1, &r_r1));
    OK(uc_reg_read(uc, UC_ARM_REG_R2, &r_r2));
    OK(uc_reg_read(uc, UC_ARM_REG_R3, &r_r3));

    TEST_CHECK(r_r0 == 0x37);
    TEST_CHECK(r_r2 == 0x6789);
    TEST_CHECK(r_r3 == 0x3333);
    TEST_CHECK(r_r1 == 0x3456);

    OK(uc_close(uc));
}

static void test_arm_thumbeb_sub(void)
{
    uc_engine *uc;
    char code[] = "\xb0\x83"; // sub    sp, #0xc
    int r_sp = 0x1234;

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_BIG_ENDIAN, code,
                    sizeof(code) - 1, UC_CPU_ARM_1176);
    OK(uc_reg_write(uc, UC_ARM_REG_SP, &r_sp));

    OK(uc_emu_start(uc, code_start | 1, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_ARM_REG_SP, &r_sp));
    TEST_CHECK(r_sp == 0x1228);

    OK(uc_close(uc));
}

static void test_arm_thumb_ite_count_callback(uc_engine *uc, uint64_t address,
                                              uint32_t size, void *user_data)
{
    uint64_t *count = (uint64_t *)user_data;

    (*count) += 1;
}

static void test_arm_thumb_ite(void)
{
    uc_engine *uc;
    uc_hook hook;
    char code[] =
        "\x9a\x42\x15\xbf\x00\x9a\x01\x9a\x78\x23\x15\x23"; // cmp r2, r3; itete
                                                            // ne; ldrne r2,
                                                            // [sp]; ldreq r2,
                                                            // [sp,#4]; movne
                                                            // r3, #0x78; moveq
                                                            // r3, #0x15
    int r_sp = 0x8000;
    int r_r2 = 0;
    int r_r3 = 1;
    int r_pc = 0;
    uint64_t count = 0;

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_THUMB, code, sizeof(code) - 1,
                    UC_CPU_ARM_CORTEX_A15);
    OK(uc_reg_write(uc, UC_ARM_REG_SP, &r_sp));
    OK(uc_reg_write(uc, UC_ARM_REG_R2, &r_r2));
    OK(uc_reg_write(uc, UC_ARM_REG_R3, &r_r3));

    OK(uc_mem_map(uc, r_sp, 0x1000, UC_PROT_ALL));
    r_r2 = LEINT32(0x68);
    OK(uc_mem_write(uc, r_sp, &r_r2, 4));
    r_r2 = LEINT32(0x4d);
    OK(uc_mem_write(uc, r_sp + 4, &r_r2, 4));

    OK(uc_hook_add(uc, &hook, UC_HOOK_CODE, test_arm_thumb_ite_count_callback,
                   &count, 1, 0));

    // Every instruction entry is observed, including failed conditions.
    OK(uc_emu_start(uc, code_start | 1, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_ARM_REG_R2, &r_r2));
    OK(uc_reg_read(uc, UC_ARM_REG_R3, &r_r3));
    TEST_CHECK(r_r2 == 0x68);
    TEST_CHECK(count == 6);

    r_pc = code_start;
    r_r2 = 0;
    count = 0;
    OK(uc_reg_write(uc, UC_ARM_REG_R2, &r_r2));
    OK(uc_reg_write(uc, UC_ARM_REG_R3, &r_r3));
    for (int i = 0; i < 6 && r_pc < code_start + sizeof(code) - 1; i++) {
        const uint32_t expected_it[] = {0, 0x15, 0x0a, 0x14, 0x08, 0};
        uint32_t cpsr;
        // Execute one instruction at a time.
        OK(uc_emu_start(uc, r_pc | 1, code_start + sizeof(code) - 1, 0, 1));

        OK(uc_reg_read(uc, UC_ARM_REG_PC, &r_pc));
        OK(uc_reg_read(uc, UC_ARM_REG_CPSR, &cpsr));
        TEST_CHECK(r_pc == code_start + (i + 1) * 2);
        TEST_CHECK((((cpsr >> 25) & 3) | ((cpsr >> 8) & 0xfc)) == expected_it[i]);
    }
    OK(uc_reg_read(uc, UC_ARM_REG_R2, &r_r2));
    OK(uc_reg_read(uc, UC_ARM_REG_R3, &r_r3));

    TEST_CHECK(r_r2 == 0x68);
    TEST_CHECK(r_r3 == 0x78);
    TEST_CHECK(count == 6);

    OK(uc_close(uc));
}

struct thumb_it_stop {
    uint64_t address;
    uint32_t expected_it;
    bool redirect;
    bool called;
};

static void test_arm_thumb_it_stop_callback(uc_engine *uc, uint64_t address,
                                           uint32_t size, void *user_data)
{
    struct thumb_it_stop *stop = user_data;
    uint32_t cpsr, pc;
    if (address != stop->address) {
        return;
    }
    OK(uc_reg_read(uc, UC_ARM_REG_CPSR, &cpsr));
    OK(uc_reg_read(uc, UC_ARM_REG_PC, &pc));
    TEST_CHECK(pc == address);
    TEST_CHECK((((cpsr >> 25) & 3) | ((cpsr >> 8) & 0xfc)) == stop->expected_it);
    stop->called = true;
    if (stop->redirect) {
        uint32_t it = stop->expected_it;
        it = (it & 7) == 0 ? 0 : (it & 0xe0) | ((it << 1) & 0x1f);
        cpsr = (cpsr & ~0x0600fc00u) | ((it & 3) << 25) | ((it & 0xfc) << 8);
        pc = (uint32_t)(address + size) | 1;
        OK(uc_reg_write(uc, UC_ARM_REG_CPSR, &cpsr));
        OK(uc_reg_write(uc, UC_ARM_REG_PC, &pc));
    }
    OK(uc_emu_stop(uc));
}

static void test_arm_thumb_it_stop_resume(void)
{
    // MOVS r0,#0; CMP r0,#0; ITE EQ; MOVEQ r1,#1; MOVNE r1,#2; NOP.
    const char code[] = "\x00\x20\x00\x28\x0c\xbf\x01\x21\x02\x21\x00\xbf";
    for (int mclass = 0; mclass < 2; mclass++) {
        for (int failed = 0; failed < 2; failed++) {
            for (int redirect = 0; redirect < 2; redirect++) {
                uc_engine *uc;
                uc_hook hook;
                uint32_t pc, r1 = 9;
                struct thumb_it_stop stop = {
                    code_start + (failed ? 8 : 6), failed ? 0x18 : 0x0c,
                    redirect, false
                };
                uc_common_setup(&uc, UC_ARCH_ARM,
                                UC_MODE_THUMB | (mclass ? UC_MODE_MCLASS : 0),
                                code, sizeof(code) - 1,
                                mclass ? UC_CPU_ARM_CORTEX_M4 : UC_CPU_ARM_CORTEX_A15);
                OK(uc_reg_write(uc, UC_ARM_REG_R1, &r1));
                OK(uc_hook_add(uc, &hook, UC_HOOK_CODE, test_arm_thumb_it_stop_callback,
                               &stop, 1, 0));
                OK(uc_emu_start(uc, code_start | 1, code_start + sizeof(code) - 1, 0, 0));
                TEST_CHECK(stop.called);
                OK(uc_reg_read(uc, UC_ARM_REG_PC, &pc));
                OK(uc_reg_read(uc, UC_ARM_REG_R1, &r1));
                TEST_CHECK(pc == stop.address + (redirect ? 2 : 0));
                TEST_CHECK(r1 == (failed ? 1 : 9));
                OK(uc_hook_del(uc, hook));
                OK(uc_emu_start(uc, pc | 1, code_start + sizeof(code) - 1, 0, 0));
                OK(uc_reg_read(uc, UC_ARM_REG_R1, &r1));
                TEST_CHECK(r1 == (redirect && !failed ? 9 : 1));
                OK(uc_close(uc));
            }
        }
    }
}

static void test_arm_m_thumb_mrs(void)
{
    uc_engine *uc;
    char code[] =
        "\xef\xf3\x14\x80\xef\xf3\x00\x81"; // mrs r0, control; mrs r1, apsr
    uint32_t r_control = 0b10;
    uint32_t r_apsr = (0b10101 << 27);
    uint32_t r_r0, r_r1;

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS, code,
                    sizeof(code) - 1, UC_CPU_ARM_CORTEX_A15);

    OK(uc_reg_write(uc, UC_ARM_REG_CONTROL, &r_control));
    OK(uc_reg_write(uc, UC_ARM_REG_APSR_NZCVQ, &r_apsr));
    OK(uc_emu_start(uc, code_start | 1, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_ARM_REG_R0, &r_r0));
    OK(uc_reg_read(uc, UC_ARM_REG_R1, &r_r1));

    TEST_CHECK(r_r0 == 0b10);
    TEST_CHECK(r_r1 == (0b10101 << 27));

    OK(uc_close(uc));
}

static void test_arm_m_control(void)
{
    uc_engine *uc;
    int r_control, r_msp, r_psp;

    OK(uc_open(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS, &uc));

    r_control = 0; // Make sure we are using MSP.
    OK(uc_reg_write(uc, UC_ARM_REG_CONTROL, &r_control));

    r_msp = 0x1000;
    OK(uc_reg_write(uc, UC_ARM_REG_R13, &r_msp));

    r_control = 0b10; // Make the switch.
    OK(uc_reg_write(uc, UC_ARM_REG_CONTROL, &r_control));

    OK(uc_reg_read(uc, UC_ARM_REG_R13, &r_psp));
    TEST_CHECK(r_psp != r_msp);

    r_psp = 0x2000;
    OK(uc_reg_write(uc, UC_ARM_REG_R13, &r_psp));

    r_control = 0; // Switch again
    OK(uc_reg_write(uc, UC_ARM_REG_CONTROL, &r_control));

    OK(uc_reg_read(uc, UC_ARM_REG_R13, &r_msp));
    TEST_CHECK(r_psp != r_msp);
    TEST_CHECK(r_msp == 0x1000);

    OK(uc_close(uc));
}

//
// Some notes:
//   Qemu raise a special exception EXCP_EXCEPTION_EXIT to handle the
//   EXC_RETURN. We can't help user handle EXC_RETURN since unicorn is designed
//   not to handle any CPU exception.
//
static void test_arm_m_exc_return_hook_interrupt(uc_engine *uc, int intno,
                                                 void *data)
{
    int r_pc;

    OK(uc_reg_read(uc, UC_ARM_REG_PC, &r_pc));
    TEST_CHECK(intno == 8); // EXCP_EXCEPTION_EXIT: Return from v7M exception.
    TEST_CHECK((r_pc | 1) == 0xFFFFFFFD);
    OK(uc_emu_stop(uc));
}

static void test_arm_m_exc_return(void)
{
    uc_engine *uc;
    char code[] = "\x6f\xf0\x02\x00\x00\x47"; // mov r0, #0xFFFFFFFD; bx r0;
    int r_ipsr;
    int r_sp = 0x8000;
    uc_hook hook;

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS, code,
                    sizeof(code) - 1, UC_CPU_ARM_CORTEX_A15);
    OK(uc_mem_map(uc, r_sp - 0x1000, 0x1000, UC_PROT_ALL));
    OK(uc_hook_add(uc, &hook, UC_HOOK_INTR,
                   test_arm_m_exc_return_hook_interrupt, NULL, 0, 0));

    r_sp -= 0x1c;
    OK(uc_reg_write(uc, UC_ARM_REG_SP, &r_sp));

    r_ipsr = 16; // We are in whatever exception.
    OK(uc_reg_write(uc, UC_ARM_REG_IPSR, &r_ipsr));

    OK(uc_emu_start(uc, code_start | 1, code_start + sizeof(code) - 1, 0,
                    2)); // Just execute 2 instructions.

    OK(uc_hook_del(uc, hook));
    OK(uc_close(uc));
}

// For details, see https://github.com/unicorn-engine/unicorn/issues/1494.
static void test_arm_und32_to_svc32(void)
{
    uc_engine *uc;
    // # MVN r0, #0
    // # MOVS pc, lr
    // # MVN r0, #0
    // # MVN r0, #0
    char code[] =
        "\x00\x00\xe0\xe3\x0e\xf0\xb0\xe1\x00\x00\xe0\xe3\x00\x00\xe0\xe3";
    int r_cpsr, r_sp, r_spsr, r_lr;

    OK(uc_open(UC_ARCH_ARM, UC_MODE_ARM, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_ARM_CORTEX_A9));

    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code) - 1));

    // https://www.keil.com/pack/doc/CMSIS/Core_A/html/group__CMSIS__CPSR__M.html
    r_cpsr = 0x40000093; // SVC32
    OK(uc_reg_write(uc, UC_ARM_REG_CPSR, &r_cpsr));
    r_sp = 0x12345678;
    OK(uc_reg_write(uc, UC_ARM_REG_SP, &r_sp));

    r_cpsr = 0x4000009b; // UND32
    OK(uc_reg_write(uc, UC_ARM_REG_CPSR, &r_cpsr));
    r_spsr = 0x40000093; // Save previous CPSR
    OK(uc_reg_write(uc, UC_ARM_REG_SPSR, &r_spsr));
    r_sp = 0xDEAD0000;
    OK(uc_reg_write(uc, UC_ARM_REG_SP, &r_sp));
    r_lr = code_start + 8;
    OK(uc_reg_write(uc, UC_ARM_REG_LR, &r_lr));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 3));

    OK(uc_reg_read(uc, UC_ARM_REG_SP, &r_sp));

    TEST_CHECK(r_sp == 0x12345678);

    OK(uc_close(uc));
}

static void test_arm_usr32_to_svc32(void)
{
    uc_engine *uc;
    int r_cpsr, r_sp, r_spsr, r_lr;

    OK(uc_open(UC_ARCH_ARM, UC_MODE_ARM, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_ARM_CORTEX_A9));

    // https://www.keil.com/pack/doc/CMSIS/Core_A/html/group__CMSIS__CPSR__M.html
    r_cpsr = 0x40000093; // SVC32
    OK(uc_reg_write(uc, UC_ARM_REG_CPSR, &r_cpsr));
    r_sp = 0x12345678;
    OK(uc_reg_write(uc, UC_ARM_REG_SP, &r_sp));
    r_lr = 0x00102220;
    OK(uc_reg_write(uc, UC_ARM_REG_LR, &r_lr));

    r_cpsr = 0x4000009b; // UND32
    OK(uc_reg_write(uc, UC_ARM_REG_CPSR, &r_cpsr));
    r_spsr = 0x40000093; // Save previous CPSR
    OK(uc_reg_write(uc, UC_ARM_REG_SPSR, &r_spsr));
    r_sp = 0xDEAD0000;
    OK(uc_reg_write(uc, UC_ARM_REG_SP, &r_sp));
    r_lr = 0x00509998;
    OK(uc_reg_write(uc, UC_ARM_REG_LR, &r_lr));

    OK(uc_reg_read(uc, UC_ARM_REG_CPSR, &r_cpsr));
    TEST_CHECK((r_cpsr & ((1 << 4) - 1)) == 0xb); // We are in UND32

    r_cpsr = 0x40000090; // USR32
    OK(uc_reg_write(uc, UC_ARM_REG_CPSR, &r_cpsr));
    r_sp = 0x0010000;
    OK(uc_reg_write(uc, UC_ARM_REG_R13, &r_sp));
    r_lr = 0x0001234;
    OK(uc_reg_write(uc, UC_ARM_REG_LR, &r_lr));

    OK(uc_reg_read(uc, UC_ARM_REG_CPSR, &r_cpsr));
    TEST_CHECK((r_cpsr & ((1 << 4) - 1)) == 0); // We are in USR32

    r_cpsr = 0x40000093; // SVC32
    OK(uc_reg_write(uc, UC_ARM_REG_CPSR, &r_cpsr));

    OK(uc_reg_read(uc, UC_ARM_REG_CPSR, &r_cpsr));
    OK(uc_reg_read(uc, UC_ARM_REG_SP, &r_sp));
    TEST_CHECK((r_cpsr & ((1 << 4) - 1)) == 3); // We are in SVC32
    TEST_CHECK(r_sp == 0x12345678);

    OK(uc_close(uc));
}

static void test_arm_v8(void)
{
    char code[] = "\xd0\xe8\xff\x17"; // LDAEXD.W R1, [R0]
    uc_engine *uc;
    uint32_t r_r1 = LEINT32(0xdeadbeef);
    uint32_t r_r0;

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_THUMB, code, sizeof(code) - 1,
                    UC_CPU_ARM_CORTEX_M33);

    r_r0 = 0x8000;
    OK(uc_mem_map(uc, r_r0, 0x1000, UC_PROT_ALL));
    OK(uc_mem_write(uc, r_r0, (void *)&r_r1, 4));
    OK(uc_reg_write(uc, UC_ARM_REG_R0, &r_r0));

    OK(uc_emu_start(uc, code_start | 1, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_ARM_REG_R1, &r_r1));

    TEST_CHECK(r_r1 == 0xdeadbeef);

    OK(uc_close(uc));
}

static void test_arm_thumb_smlabb(void)
{
    char code[] = "\x13\xfb\x01\x23";
    uint32_t r_r1, r_r2, r_r3;
    uc_engine *uc;

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_THUMB, code, sizeof(code) - 1,
                    UC_CPU_ARM_CORTEX_M7);

    r_r3 = 5;
    r_r1 = 7;
    r_r2 = 9;
    OK(uc_reg_write(uc, UC_ARM_REG_R3, &r_r3));
    OK(uc_reg_write(uc, UC_ARM_REG_R1, &r_r1));
    OK(uc_reg_write(uc, UC_ARM_REG_R2, &r_r2));

    OK(uc_emu_start(uc, code_start | 1, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_ARM_REG_R3, &r_r3));

    TEST_CHECK(r_r3 == 5 * 7 + 9);

    OK(uc_close(uc));
}

static void test_arm_not_allow_privilege_escalation(void)
{
    uc_engine *uc;
    int r_cpsr, r_sp, r_spsr, r_lr;
    // E3C6601F : BIC     r6, r6, #&1F
    // E3866013 : ORR     r6, r6, #&13
    // E121F006 : MSR     cpsr_c, r6 ; switch to SVC32 (should be ineffective
    // from USR32)
    // E1A00000 : MOV     r0,r0 EF000011 : SWI     OS_Exit
    char code[] = "\x1f\x60\xc6\xe3\x13\x60\x86\xe3\x06\xf0\x21\xe1\x00\x00\xa0"
                  "\xe1\x11\x00\x00\xef";

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_ARM, code, sizeof(code) - 1,
                    UC_CPU_ARM_CORTEX_A15);

    // https://www.keil.com/pack/doc/CMSIS/Core_A/html/group__CMSIS__CPSR.html
    r_cpsr = 0x40000013; // SVC32
    OK(uc_reg_write(uc, UC_ARM_REG_CPSR, &r_cpsr));
    r_spsr = 0x40000013;
    OK(uc_reg_write(uc, UC_ARM_REG_SPSR, &r_spsr));
    r_sp = 0x12345678;
    OK(uc_reg_write(uc, UC_ARM_REG_SP, &r_sp));
    r_lr = 0x00102220;
    OK(uc_reg_write(uc, UC_ARM_REG_LR, &r_lr));

    r_cpsr = 0x40000010; // USR32
    OK(uc_reg_write(uc, UC_ARM_REG_CPSR, &r_cpsr));
    r_sp = 0x0010000;
    OK(uc_reg_write(uc, UC_ARM_REG_SP, &r_sp));
    r_lr = 0x0001234;
    OK(uc_reg_write(uc, UC_ARM_REG_LR, &r_lr));

    uc_assert_err(
        UC_ERR_EXCEPTION,
        uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_ARM_REG_SP, &r_sp));
    OK(uc_reg_read(uc, UC_ARM_REG_LR, &r_lr));
    OK(uc_reg_read(uc, UC_ARM_REG_CPSR, &r_cpsr));

    TEST_CHECK((r_cpsr & ((1 << 4) - 1)) == 0); // Stay in USR32
    TEST_CHECK(r_lr == 0x1234);
    TEST_CHECK(r_sp == 0x10000);

    OK(uc_close(uc));
}

static void test_arm_mrc(void)
{
    uc_engine *uc;
    // mrc p15, #0, r1, c13, c0, #3
    char code[] = "\x1d\xee\x70\x1f";

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_THUMB, code, sizeof(code) - 1,
                    UC_CPU_ARM_MAX);

    OK(uc_emu_start(uc, code_start | 1, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_close(uc));
}

static void test_arm_hflags_rebuilt(void)
{
    // MRS     r6, apsr
    // BIC     r6, r6, #&1F
    // ORR     r6, r6, #&10
    // MSR     cpsr_c, r6
    // SWI     OS_EnterOS
    // MSR     cpsr_c, r6
    char code[] = "\x00\x60\x0f\xe1\x1f\x60\xc6\xe3\x10\x60\x86\xe3\x06\xf0\x21"
                  "\xe1\x16\x00\x02\xef\x06\xf0\x21\xe1";
    uc_engine *uc;
    uint32_t r_cpsr, r_spsr, r_r13, r_r14, r_pc;

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_ARM, code, sizeof(code) - 1,
                    UC_CPU_ARM_CORTEX_A9);

    r_cpsr = 0x40000013; // SVC32
    OK(uc_reg_write(uc, UC_ARM_REG_CPSR, &r_cpsr));
    r_spsr = 0x40000013;
    OK(uc_reg_write(uc, UC_ARM_REG_SPSR, &r_spsr));
    r_r13 = 0x12345678; // SP
    OK(uc_reg_write(uc, UC_ARM_REG_R13, &r_r13));
    r_r14 = 0x00102220; // LR
    OK(uc_reg_write(uc, UC_ARM_REG_R14, &r_r14));

    r_cpsr = 0x40000010; // USR32
    OK(uc_reg_write(uc, UC_ARM_REG_CPSR, &r_cpsr));
    r_r13 = 0x0010000; // SP
    OK(uc_reg_write(uc, UC_ARM_REG_R13, &r_r13));
    r_r14 = 0x0001234; // LR
    OK(uc_reg_write(uc, UC_ARM_REG_R14, &r_r14));

    uc_assert_err(
        UC_ERR_EXCEPTION,
        uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    r_cpsr = 0x60000013;
    OK(uc_reg_write(uc, UC_ARM_REG_CPSR, &r_cpsr));
    r_cpsr = 0x60000010;
    OK(uc_reg_write(uc, UC_ARM_REG_CPSR, &r_cpsr));
    r_cpsr = 0x60000013;
    OK(uc_reg_write(uc, UC_ARM_REG_CPSR, &r_cpsr));

    OK(uc_reg_read(uc, UC_ARM_REG_PC, &r_pc));

    OK(uc_emu_start(uc, r_pc, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_ARM_REG_CPSR, &r_cpsr));
    OK(uc_reg_read(uc, UC_ARM_REG_R13, &r_r13));
    OK(uc_reg_read(uc, UC_ARM_REG_R14, &r_r14));

    TEST_CHECK(r_cpsr == 0x60000010);
    TEST_CHECK(r_r13 == 0x00010000);
    TEST_CHECK(r_r14 == 0x00001234);

    OK(uc_close(uc));
}

static bool test_arm_mem_access_abort_hook_mem(uc_engine *uc, uc_mem_type type,
                                               uint64_t addr, int size,
                                               int64_t val, void *data)
{
    OK(uc_reg_read(uc, UC_ARM_REG_PC, data));
    return false;
}

static bool test_arm_mem_access_abort_hook_insn_invalid(uc_engine *uc,
                                                        void *data)
{
    OK(uc_reg_read(uc, UC_ARM_REG_PC, data));
    return false;
}

static void test_arm_mem_access_abort(void)
{
    // LDR     r0, [r0]
    // Undefined instruction
    char code[] = "\x00\x00\x90\xe5\x00\xa0\xf0\xf7";
    uc_engine *uc;
    uint32_t r_pc, r_r0, r_pc_in_hook;
    uc_hook hk, hkk;

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_ARM, code, sizeof(code) - 1,
                    UC_CPU_ARM_CORTEX_A9);

    r_r0 = 0x990000;
    OK(uc_reg_write(uc, UC_ARM_REG_R0, &r_r0));

    OK(uc_hook_add(uc, &hk, UC_HOOK_MEM_UNMAPPED,
                   test_arm_mem_access_abort_hook_mem, (void *)&r_pc_in_hook, 1,
                   0));
    OK(uc_hook_add(uc, &hkk, UC_HOOK_INSN_INVALID,
                   test_arm_mem_access_abort_hook_insn_invalid,
                   (void *)&r_pc_in_hook, 1, 0));

    uc_assert_err(UC_ERR_READ_UNMAPPED,
                  uc_emu_start(uc, code_start, code_start + 4, 0, 0));

    OK(uc_reg_read(uc, UC_ARM_REG_PC, &r_pc));

    TEST_CHECK(r_pc == r_pc_in_hook);

    uc_assert_err(UC_ERR_INSN_INVALID,
                  uc_emu_start(uc, code_start + 4, code_start + 8, 0, 0));

    OK(uc_reg_read(uc, UC_ARM_REG_PC, &r_pc));

    TEST_CHECK(r_pc == r_pc_in_hook);

    uc_assert_err(UC_ERR_FETCH_UNMAPPED,
                  uc_emu_start(uc, 0x900000, 0x900000 + 8, 0, 0));

    OK(uc_reg_read(uc, UC_ARM_REG_PC, &r_pc));

    TEST_CHECK(r_pc == r_pc_in_hook);

    OK(uc_close(uc));
}

static void test_arm_read_sctlr(void)
{
    uc_engine *uc;
    uc_arm_cp_reg reg;

    OK(uc_open(UC_ARCH_ARM, UC_MODE_ARM, &uc));

    // SCTLR. See arm reference.
    reg.cp = 15;
    reg.is64 = 0;
    reg.sec = 0;
    reg.crn = 1;
    reg.crm = 0;
    reg.opc1 = 0;
    reg.opc2 = 0;

    OK(uc_reg_read(uc, UC_ARM_REG_CP_REG, &reg));

    TEST_CHECK((uint32_t)((reg.val >> 31) & 1) == 0);

    OK(uc_close(uc));
}

static void test_arm_be_cpsr_sctlr(void)
{
    uc_engine *uc;
    uc_arm_cp_reg reg;
    uint32_t cpsr;

    OK(uc_open(UC_ARCH_ARM, UC_MODE_BIG_ENDIAN, &uc));
    OK(uc_ctl_set_cpu_model(
        uc, UC_CPU_ARM_1176)); // big endian code, big endian data

    // SCTLR. See arm reference.
    reg.cp = 15;
    reg.is64 = 0;
    reg.sec = 0;
    reg.crn = 1;
    reg.crm = 0;
    reg.opc1 = 0;
    reg.opc2 = 0;

    OK(uc_reg_read(uc, UC_ARM_REG_CP_REG, &reg));
    OK(uc_reg_read(uc, UC_ARM_REG_CPSR, &cpsr));

    TEST_CHECK((reg.val & (1 << 7)) != 0);
    TEST_CHECK((cpsr & (1 << 9)) != 0);

    OK(uc_close(uc));

    OK(uc_open(UC_ARCH_ARM, UC_MODE_ARMBE8, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_ARM_CORTEX_A15));

    // SCTLR. See arm reference.
    reg.cp = 15;
    reg.is64 = 0;
    reg.sec = 0;
    reg.crn = 1;
    reg.crm = 0;
    reg.opc1 = 0;
    reg.opc2 = 0;

    OK(uc_reg_read(uc, UC_ARM_REG_CP_REG, &reg));
    OK(uc_reg_read(uc, UC_ARM_REG_CPSR, &cpsr));

    // SCTLR.B == 0
    TEST_CHECK((reg.val & (1 << 7)) == 0);
    TEST_CHECK((cpsr & (1 << 9)) != 0);

    OK(uc_close(uc));
}

static void test_arm_switch_endian(void)
{
    uc_engine *uc;
    char code[] = "\x00\x00\x91\xe5"; // ldr r0, [r1]
    uint32_t r_r1 = (uint32_t)code_start;
    uint32_t r_r0, r_cpsr;

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_ARM, code, sizeof(code) - 1,
                    UC_CPU_ARM_CORTEX_A15);
    OK(uc_reg_write(uc, UC_ARM_REG_R1, &r_r1));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_ARM_REG_R0, &r_r0));

    // Little endian
    TEST_CHECK(r_r0 == 0xe5910000);

    OK(uc_reg_read(uc, UC_ARM_REG_CPSR, &r_cpsr));
    r_cpsr |= (1 << 9);
    OK(uc_reg_write(uc, UC_ARM_REG_CPSR, &r_cpsr));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_ARM_REG_R0, &r_r0));

    // Big endian
    TEST_CHECK(r_r0 == 0x000091e5);

    OK(uc_close(uc));
}

static void test_armeb_ldrb(void)
{
    uc_engine *uc;
    const char test_code[] = "\xe5\xd2\x10\x00"; // ldrb r1, [r2]
    uint64_t data_address = 0x800000;
    int r1 = 0x1234;
    int r2 = data_address;

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_ARM | UC_MODE_BIG_ENDIAN,
                    test_code, sizeof(test_code) - 1, UC_CPU_ARM_1176);

    OK(uc_mem_map(uc, data_address, 1024 * 1024, UC_PROT_ALL));
    OK(uc_mem_write(uc, data_address, "\x66\x67\x68\x69", 4));
    OK(uc_reg_write(uc, UC_ARM_REG_R2, &r2));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(test_code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_ARM_REG_R1, &r1));

    TEST_CHECK(r1 == 0x66);

    OK(uc_close(uc));
}

static void test_arm_context_save(void)
{
    uc_engine *uc;
    uc_engine *uc2;
    char code[] = "\x83\xb0"; // sub    sp, #0xc
    uc_context *ctx;
    uint32_t pc;

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_THUMB, code, sizeof(code) - 1,
                    UC_CPU_ARM_CORTEX_R5);

    OK(uc_context_alloc(uc, &ctx));
    OK(uc_context_save(uc, ctx));
    OK(uc_context_reg_read(ctx, UC_ARM_REG_PC, (void *)&pc));
    OK(uc_context_reg_write(ctx, UC_ARM_REG_PC, (void *)&pc));
    OK(uc_context_restore(uc, ctx));

    uc_common_setup(&uc2, UC_ARCH_ARM, UC_MODE_THUMB, code, sizeof(code) - 1,
                    UC_CPU_ARM_CORTEX_A7); // Note the different CPU model

    OK(uc_context_restore(uc2, ctx));

    OK(uc_context_free(ctx));
    OK(uc_close(uc));
    OK(uc_close(uc2));
}

static void test_arm_context_repeated_restore(void)
{
    uc_engine *uc;
    uc_context *ctx;
    char code[] = "\x01\x00\x80\xe2"; // add r0, r0, #1
    uint32_t value = 41;
    uint32_t pc = (uint32_t)code_start;

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_ARM, code, sizeof(code) - 1,
                    UC_CPU_ARM_CORTEX_R5F);
    OK(uc_reg_write(uc, UC_ARM_REG_R0, &value));
    OK(uc_reg_write(uc, UC_ARM_REG_PC, &pc));
    OK(uc_context_alloc(uc, &ctx));
    OK(uc_context_save(uc, ctx));
    for (int i = 0; i < 64; i++) {
        OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
        OK(uc_reg_read(uc, UC_ARM_REG_R0, &value));
        TEST_CHECK(value == 42);
        OK(uc_context_restore(uc, ctx));
        OK(uc_reg_read(uc, UC_ARM_REG_R0, &value));
        TEST_CHECK(value == 41);
        OK(uc_reg_read(uc, UC_ARM_REG_PC, &pc));
        TEST_CHECK(pc == code_start);
    }
    OK(uc_context_free(ctx));
    OK(uc_close(uc));
}

static void test_arm_thumb2(void)
{
    uc_engine *uc;
    // MOVS  R0, #0x24
    // AND.W R0, R0, #4
    char code[] = "\x24\x20\x00\xF0\x04\x00";
    uint32_t r_r0;

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_LITTLE_ENDIAN,
                    code, sizeof(code) - 1, UC_CPU_ARM_CORTEX_R5);

    OK(uc_emu_start(uc, code_start | 1, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_ARM_REG_R0, &r_r0));

    TEST_CHECK(r_r0 == 0x4);

    OK(uc_close(uc));
}

static void test_armeb_be32_thumb2(void)
{
    uc_engine *uc;
    // MOVS  R0, #0x24
    // AND.W R0, R0, #4
    char code[] = "\x20\x24\xF0\x00\x00\x04";
    uint32_t r_r0;

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_BIG_ENDIAN, code,
                    sizeof(code) - 1, UC_CPU_ARM_CORTEX_R5);

    OK(uc_emu_start(uc, code_start | 1, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_ARM_REG_R0, &r_r0));

    TEST_CHECK(r_r0 == 0x4);

    OK(uc_close(uc));
}

static bool test_arm_mem_read_write_cb(uc_engine *uc, int type,
                                       uint64_t address, int size,
                                       int64_t value, void *user_data)
{
    uint64_t *count = (uint64_t *)user_data;
    switch (type) {
    case UC_MEM_READ:
        count[0]++;
        break;
    case UC_MEM_WRITE:
        count[1]++;
        break;
    }

    return 0;
}
static void test_arm_mem_hook_read_write(void)
{
    uc_engine *uc;
    // ldr r1, [sp]
    // str r1, [sp, #4]
    // ldr r2, [sp, #4]
    // str r2, [sp]
    const char code[] =
        "\x00\x10\x9d\xe5\x04\x10\x8d\xe5\x04\x20\x9d\xe5\x00\x20\x8d\xe5";
    uint32_t r_sp;
    r_sp = 0x9000;
    uc_hook hk;
    uint64_t counter[2] = {0, 0};

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_ARM, code, sizeof(code) - 1,
                    UC_CPU_ARM_CORTEX_A15);

    uc_reg_write(uc, UC_ARM_REG_SP, &r_sp);
    uc_mem_map(uc, 0x8000, 1024 * 16, UC_PROT_ALL);

    OK(uc_hook_add(uc, &hk, UC_HOOK_MEM_READ, test_arm_mem_read_write_cb,
                   counter, 1, 0));
    OK(uc_hook_add(uc, &hk, UC_HOOK_MEM_WRITE, test_arm_mem_read_write_cb,
                   counter, 1, 0));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    TEST_CHECK(counter[0] == 2 && counter[1] == 2);
    OK(uc_close(uc));
}

typedef struct {
    uint64_t v0;
    uint64_t v1;
    uint64_t size;
    uint64_t pc;
} _last_cmp_info;

static void _uc_hook_sub_cmp(uc_engine *uc, uint64_t address, uint64_t arg1,
                             uint64_t arg2, uint32_t size,
                             _last_cmp_info *user_data)
{
    user_data->pc = address;
    user_data->size = size;
    user_data->v0 = arg1;
    user_data->v1 = arg2;
}

static void test_arm_tcg_opcode_cmp(void)
{
    uc_engine *uc;
    const char code[] = "\x04\x00\x9f\xe5" // ldr   r0, [pc, #4]
                        "\x04\x10\x9f\xe5" // ldr   r1, [pc, #4]
                        "\x01\x00\x50\xe1" // cmp   r0, r1
                        "\x05\x00\x00\x00" // (5)
                        "\x03\x00\x00\x00" // (3)
        ;

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_ARM, code, sizeof(code) - 1,
                    UC_CPU_ARM_CORTEX_A15);

    uc_hook hook;
    _last_cmp_info cmp_info = {0};

    OK(uc_hook_add(uc, &hook, UC_HOOK_TCG_OPCODE, (void *)_uc_hook_sub_cmp,
                   (void *)&cmp_info, 1, 0, UC_TCG_OP_SUB, UC_TCG_OP_FLAG_CMP));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 3));
    TEST_CHECK(cmp_info.v0 == 5 && cmp_info.v1 == 3);
    TEST_CHECK(cmp_info.pc == 0x1008);
    TEST_CHECK(cmp_info.size == 32);
    OK(uc_close(uc));
}

static void test_arm_thumb_tcg_opcode_cmn(void)
{
    uc_engine *uc;
    const char code[] = "\x01\x48"         // ldr  r0, [pc, #4]
                        "\x02\x49"         // ldr  r1, [pc, #8]
                        "\x00\xbf"         // nop
                        "\xc8\x42"         // cmn  r0, r1
                        "\x05\x00\x00\x00" // (5)
                        "\x03\x00\x00\x00" // (3)
        ;

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_THUMB, code, sizeof(code) - 1,
                    UC_CPU_ARM_CORTEX_A15);

    uc_hook hook;
    _last_cmp_info cmp_info = {0};

    OK(uc_hook_add(uc, &hook, UC_HOOK_TCG_OPCODE, (void *)_uc_hook_sub_cmp,
                   (void *)&cmp_info, 1, 0, UC_TCG_OP_SUB, UC_TCG_OP_FLAG_CMP));

    OK(uc_emu_start(uc, code_start | 1, code_start + sizeof(code) - 1, 0, 4));
    TEST_CHECK(cmp_info.v0 == 5 && cmp_info.v1 == 3);
    TEST_CHECK(cmp_info.pc == 0x1006);
    TEST_CHECK(cmp_info.size == 32);
    OK(uc_close(uc));
}

static void test_arm_cp15_c1_c0_2(void)
{
    uc_engine *uc;
    uint32_t val = 0x12345678;
    uint32_t read_val;

    // Initialize emulator in ARM mode
    OK(uc_open(UC_ARCH_ARM, UC_MODE_ARM, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_ARM_CORTEX_A15));

    // Write to CP15 C1_C0_2
    OK(uc_reg_write(uc, UC_ARM_REG_C1_C0_2, &val));

    // Read from CP15 C1_C0_2
    OK(uc_reg_read(uc, UC_ARM_REG_C1_C0_2, &read_val));

    TEST_CHECK(read_val == val);

    OK(uc_close(uc));
}

static bool test_arm_v7_lpae_hook_tlb(uc_engine *uc, uint64_t addr,
                                      uc_mem_type type, uc_tlb_entry *result,
                                      void *user_data)
{
    result->paddr = addr + 0x100000000;
    result->perms = UC_PROT_ALL;
    return 1;
}

static void test_arm_v7_lpae_hook_read(uc_engine *uc, uc_mem_type type,
                                       uint64_t address, int size,
                                       uint64_t value, void *user_data)
{
    TEST_CHECK(address == 0x100001000);
}

static void test_arm_v7_lpae(void)
{
    uc_engine *uc;
    uc_hook hook_read, hook_tlb;
    uint32_t reg;
    char code[] = "\x00\x10\x90\xe5"; // ldr r1, [r0]
    OK(uc_open(UC_ARCH_ARM, UC_MODE_ARM, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_ARM_CORTEX_A7));

    OK(uc_ctl_tlb_mode(uc, UC_TLB_VIRTUAL));
    OK(uc_hook_add(uc, &hook_tlb, UC_HOOK_TLB_FILL, test_arm_v7_lpae_hook_tlb,
                   NULL, 1, 0));
    OK(uc_hook_add(uc, &hook_read, UC_HOOK_MEM_READ, test_arm_v7_lpae_hook_read,
                   NULL, 1, 0));

    reg = 0x1000;
    OK(uc_reg_write(uc, UC_ARM_REG_R0, &reg));
    OK(uc_mem_map(uc, 0x100001000, 0x1000, UC_PROT_ALL));
    OK(uc_mem_write(uc, 0x100001000, code, sizeof(code)));
    OK(uc_emu_start(uc, 0x1000, 0x1000 + sizeof(code) - 1, 0, 0));
    OK(uc_reg_read(uc, UC_ARM_REG_R1, &reg));
    TEST_CHECK(reg == 0xe5901000);

    OK(uc_close(uc));
}

static void test_arm_svc_interrupt(uc_engine *uc, int intno, void *user_data)
{
    uint32_t esr;
    OK(uc_reg_read(uc, UC_ARM_REG_ESR, &esr));
    switch (intno) {
    // SVC
    case 2:
        TEST_CHECK((esr & 0xff) == 0x42);
        break;
    // HVC
    case 3:
        TEST_CHECK((esr & 0xff) == 0x33);
        break;
    }
}

static void test_arm_svc_hvc_syndrome(void)
{
    uc_engine *uc;
    uint8_t code[] = {
        0x42, 0x00, 0x00, 0xef, // svc #0x42
        0x73, 0x03, 0x40, 0xe1, // hvc #0x33
    };

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_ARM, (char *)code, sizeof(code),
                    UC_CPU_ARM_CORTEX_A15);

    uc_hook hook;
    OK(uc_hook_add(uc, &hook, UC_HOOK_INTR, test_arm_svc_interrupt, NULL, 1,
                   0));

    OK(uc_emu_start(uc, code_start, code_start + 4, 0, 0));

    OK(uc_close(uc));
}

static void test_arm_neon_single_lane_64_shift(void)
{
    uc_engine *uc;
    // vshl.u64 d0, d0, d1; vshl.s64 d2, d2, d3;
    // vshl.u64 q2, q2, q3. Exercise both one-lane and two-lane lowering.
    const char code[] = "\x00\x04\x31\xf3\x02\x24\x33\xf2\x44\x44\x36\xf3";
    const int shifts[] = {-65, -64, -63, -1, 0, 1, 63, 64, 65};
    const uint64_t source = UINT64_C(0x923456789abcdef0);
    uc_arm_cp_reg cpacr = {.cp = 15, .crn = 1, .opc2 = 2,
                          .val = 0xf00000};
    uint32_t fpexc = 1u << 30;

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_ARM, code, sizeof(code) - 1,
                    UC_CPU_ARM_CORTEX_A15);
    OK(uc_reg_write(uc, UC_ARM_REG_CP_REG, &cpacr));
    OK(uc_reg_write(uc, UC_ARM_REG_FPEXC, &fpexc));
    for (size_t i = 0; i < sizeof(shifts) / sizeof(shifts[0]); i++) {
        int shift = shifts[i];
        // Only the signed low byte is the shift count.
        uint64_t count = UINT64_C(0x123456789abc0000) | (uint8_t)shift;
        uint64_t actual, expected_unsigned, expected_signed;
        if (shift >= 64) {
            expected_unsigned = expected_signed = 0;
        } else if (shift >= 0) {
            expected_unsigned = expected_signed = source << shift;
        } else if (shift <= -64) {
            expected_unsigned = 0;
            expected_signed = UINT64_MAX;
        } else {
            expected_unsigned = source >> -shift;
            expected_signed = ~((~source) >> -shift);
        }
        OK(uc_reg_write(uc, UC_ARM_REG_D0, &source));
        OK(uc_reg_write(uc, UC_ARM_REG_D1, &count));
        OK(uc_reg_write(uc, UC_ARM_REG_D2, &source));
        OK(uc_reg_write(uc, UC_ARM_REG_D3, &count));
        OK(uc_reg_write(uc, UC_ARM_REG_D4, &source));
        OK(uc_reg_write(uc, UC_ARM_REG_D5, &source));
        OK(uc_reg_write(uc, UC_ARM_REG_D6, &count));
        OK(uc_reg_write(uc, UC_ARM_REG_D7, &count));
        OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
        OK(uc_reg_read(uc, UC_ARM_REG_D0, &actual));
        TEST_CHECK(actual == expected_unsigned);
        OK(uc_reg_read(uc, UC_ARM_REG_D2, &actual));
        TEST_CHECK(actual == expected_signed);
        OK(uc_reg_read(uc, UC_ARM_REG_D4, &actual));
        TEST_CHECK(actual == expected_unsigned);
        OK(uc_reg_read(uc, UC_ARM_REG_D5, &actual));
        TEST_CHECK(actual == expected_unsigned);
    }
    OK(uc_close(uc));
}

// An alignment qualifier on a VLDn/VSTn (multiple structures) faults on an
// unaligned address whatever SCTLR.A says (ARMv7-A). Contributed by Taylor
// Chapman (heytchap) and Claude (Anthropic).
static void test_arm_neon_align_qualifier_hook_interrupt(uc_engine *uc,
                                                          int intno, void *data)
{
    *(int *)data = intno;
    OK(uc_emu_stop(uc));
}

static int test_arm_neon_align_qualifier_run(const char *code, uint32_t address)
{
    uc_engine *uc;
    uc_hook hook;
    uc_arm_cp_reg cpacr = {.cp = 15, .crn = 1, .opc2 = 2, .val = 0xf00000};
    uint32_t fpexc = 1u << 30;
    int intno = -1;

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_ARM, code, 4,
                    UC_CPU_ARM_CORTEX_A15);
    OK(uc_reg_write(uc, UC_ARM_REG_CP_REG, &cpacr));
    OK(uc_reg_write(uc, UC_ARM_REG_FPEXC, &fpexc));
    OK(uc_mem_map(uc, 0x10000, 0x1000, UC_PROT_ALL));
    OK(uc_reg_write(uc, UC_ARM_REG_R3, &address));
    OK(uc_hook_add(uc, &hook, UC_HOOK_INTR,
                   test_arm_neon_align_qualifier_hook_interrupt, &intno, 1, 0));
    OK(uc_emu_start(uc, code_start, code_start + 4, 0, 1));
    OK(uc_hook_del(uc, hook));
    OK(uc_close(uc));
    return intno;
}

static void test_arm_neon_align_qualifier(void)
{
    // vst1.8 {d16-d17}, [r3 :64]
    const char qualified[] = "\x1f\x0a\x43\xf4";
    // vst1.8 {d16-d17}, [r3 :128]
    const char qualified128[] = "\x2f\x0a\x43\xf4";
    // vst1.8 {d16-d17}, [r3]
    const char plain[] = "\x0f\x0a\x43\xf4";

    TEST_CHECK(test_arm_neon_align_qualifier_run(qualified, 0x10008) == -1);
    // EXCP_DATA_ABORT
    TEST_CHECK(test_arm_neon_align_qualifier_run(qualified, 0x10004) == 4);
    // 8-byte aligned, so the plain access passes and the 128-bit qualifier
    // faults (the MMU is off, which already faults a plain access that is
    // not 8-byte aligned).
    TEST_CHECK(test_arm_neon_align_qualifier_run(plain, 0x10008) == -1);
    TEST_CHECK(test_arm_neon_align_qualifier_run(qualified128, 0x10008) == 4);
    TEST_CHECK(test_arm_neon_align_qualifier_run(qualified128, 0x10010) == -1);
}

static void test_it_hook_cb(uc_engine *uc, uc_mem_type type, uint64_t address,
                            int size, int64_t value, void *user_data)
{
    (*(int *)user_data)++;
}

// A memory hook that returns into its block must not leave the ITSTATE of
// the hooked instruction in the CPU state: the block goes on past the IT
// block, and the next block would begin as if inside it.
static void test_thumb_it_memory_hook(void)
{
    // movs r3,#0; cmp r0,r0; it eq; str/ldr eq r1,[r2]; movs r4,#1; b .+2;
    // adds r3,#5
    char store[] = "\x00\x23\x80\x42\x08\xbf\x11\x60\x01\x24\xff\xe7\x05\x33";
    char load[] = "\x00\x23\x80\x42\x08\xbf\x11\x68\x01\x24\xff\xe7\x05\x33";
    int variant;

    for (variant = 0; variant < 4; variant++) {
        uc_engine *uc;
        uc_hook hook;
        int calls = 0, r2 = 0x3000, r3 = -1;
        char *code = variant & 1 ? load : store;

        uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_THUMB, code, sizeof(store) - 1,
                        UC_CPU_ARM_CORTEX_A15);
        OK(uc_reg_write(uc, UC_ARM_REG_R2, &r2));
        if (variant & 2) {
            OK(uc_hook_add(uc, &hook,
                           variant & 1 ? UC_HOOK_MEM_READ : UC_HOOK_MEM_WRITE,
                           test_it_hook_cb, &calls, 0x3000, 0x3fff));
        }
        OK(uc_emu_start(uc, code_start | 1, code_start + sizeof(store) - 1, 0, 0));
        OK(uc_reg_read(uc, UC_ARM_REG_R3, &r3));
        TEST_CHECK(calls == (variant & 2 ? 1 : 0));
        TEST_CHECK(r3 == 5);
        OK(uc_close(uc));
    }
}

// A store is certain to reach watched write hooks when every target page it
// touches holds a watched 1 KiB page.
uc_err uc_embedder_watch(uc_engine *uc, uint64_t address, uint64_t size);
bool uc_embedder_store_watched(uc_engine *uc, uint64_t address, uint64_t size);
static void test_arm_embedder_store_watched(void)
{
    uc_engine *uc;
    char code[] = "\x00\xf0\x20\xe3"; // nop
    uint64_t page = 0x40000;

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_ARM, code, sizeof(code) - 1,
                    UC_CPU_ARM_CORTEX_A15);
    TEST_CHECK(!uc_embedder_store_watched(uc, page, 4));
    OK(uc_embedder_watch(uc, page + 0x400, 4));
    // The watched 1 KiB page.
    TEST_CHECK(uc_embedder_store_watched(uc, page + 0x400, 4));
    TEST_CHECK(uc_embedder_store_watched(uc, page + 0x7fc, 4));
    // A target page without a watch, a range that leaves the watched target
    // page, an empty range and a range past 4 GiB.
    TEST_CHECK(!uc_embedder_store_watched(uc, page + 0x1000, 4));
    TEST_CHECK(!uc_embedder_store_watched(uc, page - 4, 4));
    TEST_CHECK(!uc_embedder_store_watched(uc, page + 0x400, 0x2000));
    TEST_CHECK(!uc_embedder_store_watched(uc, page + 0x400, 0));
    TEST_CHECK(!uc_embedder_store_watched(uc, 0xfffffffcULL, 8));
    OK(uc_close(uc));
}

// Block chains (uc_priv.h, uc_embedder_chain). An admitted block passes its
// hook while its page guard holds its key and the budget covers its weight.
uc_err uc_embedder_set_block_chains(uc_engine *uc, bool enabled);
void *uc_embedder_block_chain(uc_engine *uc);
uc_err uc_embedder_admit_block(uc_engine *uc, uint32_t weight, uint32_t key,
                               uint64_t tag);
struct test_chain_record {
    int64_t budget;
    uint64_t last;
    uint32_t guards[64];
};
struct test_chain_state {
    struct test_chain_record *record;
    int calls[3];
    uint32_t admit;
    int64_t grant;
    int stop_at;
    bool flush_on_stop;
    int64_t left;
    uc_err admitted;
};

static void test_chain_block_cb(uc_engine *uc, uint64_t address, uint32_t size,
                                void *user_data)
{
    struct test_chain_state *state = user_data;

    if (address == code_start) {
        if (++state->calls[0] == state->stop_at) {
            OK(uc_emu_stop(uc));
        }
    } else if (address == code_start + 0x100) {
        state->calls[1]++;
        if (state->admit) {
            state->admitted = uc_embedder_admit_block(uc, state->admit, 7, 0xabcd);
        }
        if (state->grant) {
            state->record->budget = state->grant;
            state->grant = 0;
        }
    } else if (address == code_start + 0x200) {
        state->calls[2]++;
    }
}

// head: add r0, r0, #1; b inner. inner: add r1, r1, #1; b tail.
// tail: cmp r0, #40; bne head; (end)
static void test_chain_run(struct test_chain_state *state, bool chains,
                           uint32_t guard, int *r0, int *r1)
{
    uc_engine *uc;
    uc_hook hook;
    char head[] = "\x01\x00\x80\xe2\x3d\x00\x00\xea";
    char inner[] = "\x01\x10\x81\xe2\x3d\x00\x00\xea";
    char tail[] = "\x28\x00\x50\xe3\x7d\xff\xff\x1a";
    uint64_t pc = code_start;

    uc_common_setup(&uc, UC_ARCH_ARM, UC_MODE_ARM, head, sizeof(head) - 1,
                    UC_CPU_ARM_CORTEX_A15);
    OK(uc_mem_write(uc, code_start + 0x100, inner, sizeof(inner) - 1));
    OK(uc_mem_write(uc, code_start + 0x200, tail, sizeof(tail) - 1));
    OK(uc_embedder_set_block_chains(uc, chains));
    state->record = uc_embedder_block_chain(uc);
    state->record->guards[(code_start + 0x100) >> 12 & 63] = guard;
    // Outside a block hook nothing can be admitted.
    TEST_CHECK(uc_embedder_admit_block(uc, 2, 7, 0) == UC_ERR_ARG);
    OK(uc_hook_add(uc, &hook, UC_HOOK_BLOCK, test_chain_block_cb, state, 1, 0));
    // A stop from a hook is served before the next block; run on after it.
    for (;;) {
        OK(uc_emu_start(uc, pc, code_start + 0x208, 0, 0));
        OK(uc_reg_read(uc, UC_ARM_REG_PC, &pc));
        pc &= 0xffffffff;
        if (pc == code_start + 0x208) {
            break;
        }
        TEST_CHECK(pc == code_start);
        state->stop_at = 0;
        if (state->flush_on_stop) {
            OK(uc_ctl_flush_tb(uc));
        }
    }
    state->left = state->record->budget;
    OK(uc_reg_read(uc, UC_ARM_REG_R0, r0));
    OK(uc_reg_read(uc, UC_ARM_REG_R1, r1));
    OK(uc_close(uc));
}

static void test_arm_embedder_block_chains(void)
{
    struct test_chain_state state;
    int r0, r1;

    // Without chains, and with chains but nothing admitted, an open guard
    // without a budget, or a budget behind a closed guard: every block
    // calls its hook.
    memset(&state, 0, sizeof(state));
    test_chain_run(&state, false, 7, &r0, &r1);
    TEST_CHECK(state.calls[0] == 40 && state.calls[1] == 40 && state.calls[2] == 40);
    TEST_CHECK(r0 == 40 && r1 == 40);
    memset(&state, 0, sizeof(state));
    state.grant = 1000;
    test_chain_run(&state, true, 7, &r0, &r1);
    TEST_CHECK(state.calls[1] == 40 && r1 == 40);
    memset(&state, 0, sizeof(state));
    state.admit = 2;
    test_chain_run(&state, true, 7, &r0, &r1);
    TEST_CHECK(state.calls[1] == 40 && state.admitted == UC_ERR_OK);
    memset(&state, 0, sizeof(state));
    state.admit = 2;
    state.grant = 1000;
    test_chain_run(&state, true, 8, &r0, &r1);
    TEST_CHECK(state.calls[1] == 40 && r1 == 40);

    // Admitted, guard open, a budget for five entries and one unit over:
    // five entries pass the hook, and the guest runs the same instructions.
    memset(&state, 0, sizeof(state));
    state.admit = 2;
    state.grant = 11;
    test_chain_run(&state, true, 7, &r0, &r1);
    TEST_CHECK(state.calls[0] == 40 && state.calls[1] == 35 && state.calls[2] == 40);
    TEST_CHECK(r0 == 40 && r1 == 40);

    // A stop requested by another block's hook is served at once, whatever
    // the budget; resumed, the remaining entries are chained.
    memset(&state, 0, sizeof(state));
    state.admit = 2;
    state.grant = 1000;
    state.stop_at = 10;
    test_chain_run(&state, true, 7, &r0, &r1);
    TEST_CHECK(state.calls[0] == 41 && state.calls[1] == 1 && state.calls[2] == 40);
    TEST_CHECK(r0 == 40 && r1 == 40);

    // A translation flush ends every admission and leaves the budget alone:
    // the new translation calls its hook once, is admitted again and spends
    // what was left. 38 of the 40 entries are chained.
    memset(&state, 0, sizeof(state));
    state.admit = 2;
    state.grant = 1000;
    state.stop_at = 10;
    state.flush_on_stop = true;
    test_chain_run(&state, true, 7, &r0, &r1);
    TEST_CHECK(state.calls[0] == 41 && state.calls[1] == 2 && state.calls[2] == 40);
    TEST_CHECK(state.left == 1000 - 2 * 38);
    TEST_CHECK(r0 == 40 && r1 == 40);
}

static void test_arm_contextidr_short_descriptors(void)
{
    const uint32_t code[] = {
        LEINT32(0xe5914000), /* ldr r4, [r1]: fill the TLB */
        LEINT32(0xe5832000), /* str r2, [r3]: replace the descriptor */
        LEINT32(0xee0d0f30), /* mcr p15, 0, r0, c13, c0, 1: change ASID */
        LEINT32(0xe5915000), /* ldr r5, [r1]: only a global entry survives */
        LEINT32(0xee080f17), /* mcr p15, 0, r0, c8, c7, 0: invalidate all */
        LEINT32(0xe5916000), /* ldr r6, [r1]: use the new descriptor */
    };
    const uint32_t data_va = 0x100000;
    const uint32_t asid = 1;
    const uint32_t cpsr = 0xd3; /* Secure SVC, IRQ and FIQ masked. */
    const uint32_t direct = LEINT32(0x0c0e);
    const uint32_t old_word = LEINT32(0x11);
    const uint32_t new_word = LEINT32(0x22);
    for (unsigned nonglobal = 0; nonglobal <= 1; nonglobal++) {
        uc_engine *uc;
        uc_arm_cp_reg cp = { .cp = 15, .sec = 1, .crn = 1, .crm = 1 };
        uint32_t page_table_slot = 0x4004;
        uint32_t attr = 0x0c0e | (nonglobal << 17);
        uint32_t first = LEINT32(0x100000 | attr);
        uint32_t replacement = 0x200000 | attr;
        uint32_t r4, r5, r6;

        OK(uc_open(UC_ARCH_ARM, UC_MODE_ARM, &uc));
        OK(uc_ctl_set_cpu_model(uc, UC_CPU_ARM_CORTEX_A7));
        OK(uc_mem_map(uc, 0, 0x300000, UC_PROT_ALL));
        OK(uc_mem_write(uc, code_start, code, sizeof(code)));
        OK(uc_mem_write(uc, 0x4000, &direct, sizeof(direct)));
        OK(uc_mem_write(uc, page_table_slot, &first, sizeof(first)));
        OK(uc_mem_write(uc, 0x100000, &old_word, sizeof(old_word)));
        OK(uc_mem_write(uc, 0x200000, &new_word, sizeof(new_word)));
        OK(uc_reg_write(uc, UC_ARM_REG_CPSR, &cpsr));
        OK(uc_reg_write(uc, UC_ARM_REG_R0, &asid));
        OK(uc_reg_write(uc, UC_ARM_REG_R1, &data_va));
        OK(uc_reg_write(uc, UC_ARM_REG_R2, &replacement));
        OK(uc_reg_write(uc, UC_ARM_REG_R3, &page_table_slot));

        OK(uc_reg_write(uc, UC_ARM_REG_CP_REG, &cp)); /* SCR.NS = 0 */
        cp.crn = 2;
        cp.crm = 0;
        cp.val = 0x4000;
        OK(uc_reg_write(uc, UC_ARM_REG_CP_REG, &cp)); /* TTBR0 */
        cp.crn = 3;
        cp.val = 0xffffffff;
        OK(uc_reg_write(uc, UC_ARM_REG_CP_REG, &cp)); /* DACR */
        cp.crn = 1;
        OK(uc_reg_read(uc, UC_ARM_REG_CP_REG, &cp));
        cp.val |= (1u << 23) | 1; /* XP and M */
        OK(uc_reg_write(uc, UC_ARM_REG_CP_REG, &cp)); /* SCTLR */
        OK(uc_emu_start(uc, code_start, code_start + sizeof(code), 0, 0));
        OK(uc_reg_read(uc, UC_ARM_REG_R4, &r4));
        OK(uc_reg_read(uc, UC_ARM_REG_R5, &r5));
        OK(uc_reg_read(uc, UC_ARM_REG_R6, &r6));
        TEST_CHECK(r4 == 0x11);
        TEST_CHECK(r5 == (nonglobal ? 0x22 : 0x11));
        TEST_CHECK(r6 == 0x22);
        OK(uc_close(uc));
    }
}

static bool contextidr_tlb_fill(uc_engine *uc, uint64_t addr,
                               uc_mem_type type, uc_tlb_entry *result,
                               void *userdata)
{
    result->paddr = addr;
    if (addr == 0x100000) {
        uc_arm_cp_reg context = { .cp = 15, .crn = 13, .opc2 = 1 };
        OK(uc_reg_read(uc, UC_ARM_REG_CP_REG, &context));
        if (context.val & 0xff) {
            result->paddr = 0x200000;
        }
    }
    result->perms = UC_PROT_ALL;
    return true;
}

static void test_arm_contextidr_virtual_tlb(void)
{
    const uint32_t code[] = {
        LEINT32(0xe5914000), /* ldr r4, [r1] */
        LEINT32(0xee0d0f30), /* mcr p15, 0, r0, c13, c0, 1 */
        LEINT32(0xe5915000), /* ldr r5, [r1] */
    };
    const uint32_t asid = 1;
    const uint32_t data_va = 0x100000;
    const uint32_t old_word = LEINT32(0x11);
    const uint32_t new_word = LEINT32(0x22);
    uc_engine *uc;
    uc_hook hook;
    uint32_t r4, r5;

    OK(uc_open(UC_ARCH_ARM, UC_MODE_ARM, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_ARM_CORTEX_A7));
    OK(uc_ctl_tlb_mode(uc, UC_TLB_VIRTUAL));
    OK(uc_mem_map(uc, 0, 0x300000, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code)));
    OK(uc_mem_write(uc, 0x100000, &old_word, sizeof(old_word)));
    OK(uc_mem_write(uc, 0x200000, &new_word, sizeof(new_word)));
    OK(uc_reg_write(uc, UC_ARM_REG_R0, &asid));
    OK(uc_reg_write(uc, UC_ARM_REG_R1, &data_va));
    OK(uc_hook_add(uc, &hook, UC_HOOK_TLB_FILL, contextidr_tlb_fill, NULL, 1, 0));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code), 0, 0));
    OK(uc_reg_read(uc, UC_ARM_REG_R4, &r4));
    OK(uc_reg_read(uc, UC_ARM_REG_R5, &r5));
    TEST_CHECK(r4 == 0x11);
    TEST_CHECK(r5 == 0x22);
    OK(uc_close(uc));
}

TEST_LIST = {{"test_arm_nop", test_arm_nop},
             {"test_arm_neon_align_qualifier", test_arm_neon_align_qualifier},
             {"test_arm_embedder_store_watched", test_arm_embedder_store_watched},
             {"test_arm_embedder_block_chains", test_arm_embedder_block_chains},
             {"test_thumb_it_memory_hook", test_thumb_it_memory_hook},
             {"test_arm_neon_single_lane_64_shift", test_arm_neon_single_lane_64_shift},
             {"test_arm_thumb_sub", test_arm_thumb_sub},
             {"test_armeb_sub", test_armeb_sub},
             {"test_armeb_be8_sub", test_armeb_be8_sub},
             {"test_arm_thumbeb_sub", test_arm_thumbeb_sub},
             {"test_arm_thumb_ite", test_arm_thumb_ite},
             {"test_arm_thumb_it_stop_resume", test_arm_thumb_it_stop_resume},
             {"test_arm_m_thumb_mrs", test_arm_m_thumb_mrs},
             {"test_arm_m_control", test_arm_m_control},
             {"test_arm_m_exc_return", test_arm_m_exc_return},
             {"test_arm_und32_to_svc32", test_arm_und32_to_svc32},
             {"test_arm_usr32_to_svc32", test_arm_usr32_to_svc32},
             {"test_arm_v8", test_arm_v8},
             {"test_arm_thumb_smlabb", test_arm_thumb_smlabb},
             {"test_arm_not_allow_privilege_escalation",
              test_arm_not_allow_privilege_escalation},
             {"test_arm_mrc", test_arm_mrc},
             {"test_arm_hflags_rebuilt", test_arm_hflags_rebuilt},
             {"test_arm_mem_access_abort", test_arm_mem_access_abort},
             {"test_arm_read_sctlr", test_arm_read_sctlr},
             {"test_arm_be_cpsr_sctlr", test_arm_be_cpsr_sctlr},
             {"test_arm_switch_endian", test_arm_switch_endian},
             {"test_armeb_ldrb", test_armeb_ldrb},
             {"test_arm_context_save", test_arm_context_save},
             {"test_arm_context_repeated_restore", test_arm_context_repeated_restore},
             {"test_arm_thumb2", test_arm_thumb2},
             {"test_armeb_be32_thumb2", test_armeb_be32_thumb2},
             {"test_arm_mem_hook_read_write", test_arm_mem_hook_read_write},
             {"test_arm_tcg_opcode_cmp", test_arm_tcg_opcode_cmp},
             {"test_arm_thumb_tcg_opcode_cmn", test_arm_thumb_tcg_opcode_cmn},
             {"test_arm_cp15_c1_c0_2", test_arm_cp15_c1_c0_2},
             {"test_arm_v7_lpae", test_arm_v7_lpae},
             {"test_arm_contextidr_short_descriptors",
              test_arm_contextidr_short_descriptors},
             {"test_arm_contextidr_virtual_tlb", test_arm_contextidr_virtual_tlb},
             {"test_arm_svc_hvc_syndrome", test_arm_svc_hvc_syndrome},
             {NULL, NULL}};
