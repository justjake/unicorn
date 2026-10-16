/* Typed helper dispatch for TCI. Every generated function has
 * the same C signature and makes a statically typed call to its helper. */
#ifndef EXEC_HELPER_TCI_H
#define EXEC_HELPER_TCI_H

#include "exec/helper-head.h"

#define tci_arg_i32(type, value) ((dh_ctype(type))(value))
#define tci_arg_i64(type, value) ((dh_ctype(type))(value))
#define tci_arg_ptr(type, value) ((dh_ctype(type))(uintptr_t)(value))
#define tci_arg(type, n) glue(tci_arg_, dh_alias(type))(type, args[n])
#define tci_return_i32(call) return (uint32_t)(call)
#define tci_return_i64(call) return (uint64_t)(call)
#define tci_return_ptr(call) return (uintptr_t)(call)
#define tci_return_void(call) do { call; return 0; } while (0)
#define tci_return_noreturn(call) tci_return_void(call)
#define tci_return(type, call) glue(tci_return_, dh_alias(type))(call)

#define DEF_HELPER_FLAGS_0(name, flags, ret) \
static uint64_t glue(tci_helper_, name)(const uint64_t *args) \
{ \
    (void)args; \
    tci_return(ret, HELPER(name)()); \
}

#define DEF_HELPER_FLAGS_1(name, flags, ret, t1) \
static uint64_t glue(tci_helper_, name)(const uint64_t *args) \
{ \
    tci_return(ret, HELPER(name)(tci_arg(t1, 0))); \
}

#define DEF_HELPER_FLAGS_2(name, flags, ret, t1, t2) \
static uint64_t glue(tci_helper_, name)(const uint64_t *args) \
{ \
    tci_return(ret, HELPER(name)(tci_arg(t1, 0), tci_arg(t2, 1))); \
}

#define DEF_HELPER_FLAGS_3(name, flags, ret, t1, t2, t3) \
static uint64_t glue(tci_helper_, name)(const uint64_t *args) \
{ \
    tci_return(ret, HELPER(name)(tci_arg(t1, 0), tci_arg(t2, 1), tci_arg(t3, 2))); \
}

#define DEF_HELPER_FLAGS_4(name, flags, ret, t1, t2, t3, t4) \
static uint64_t glue(tci_helper_, name)(const uint64_t *args) \
{ \
    tci_return(ret, HELPER(name)(tci_arg(t1, 0), tci_arg(t2, 1), tci_arg(t3, 2), tci_arg(t4, 3))); \
}

#define DEF_HELPER_FLAGS_5(name, flags, ret, t1, t2, t3, t4, t5) \
static uint64_t glue(tci_helper_, name)(const uint64_t *args) \
{ \
    tci_return(ret, HELPER(name)(tci_arg(t1, 0), tci_arg(t2, 1), tci_arg(t3, 2), tci_arg(t4, 3), tci_arg(t5, 4))); \
}

#define DEF_HELPER_FLAGS_6(name, flags, ret, t1, t2, t3, t4, t5, t6) \
static uint64_t glue(tci_helper_, name)(const uint64_t *args) \
{ \
    tci_return(ret, HELPER(name)(tci_arg(t1, 0), tci_arg(t2, 1), tci_arg(t3, 2), tci_arg(t4, 3), tci_arg(t5, 4), tci_arg(t6, 5))); \
}

#define DEF_HELPER_FLAGS_7(name, flags, ret, t1, t2, t3, t4, t5, t6, t7) \
static uint64_t glue(tci_helper_, name)(const uint64_t *args) \
{ \
    tci_return(ret, HELPER(name)(tci_arg(t1, 0), tci_arg(t2, 1), tci_arg(t3, 2), tci_arg(t4, 3), tci_arg(t5, 4), tci_arg(t6, 5), tci_arg(t7, 6))); \
}

#include "helper.h"
#include "accel/tcg/tcg-runtime.h"

#undef DEF_HELPER_FLAGS_0
#undef DEF_HELPER_FLAGS_1
#undef DEF_HELPER_FLAGS_2
#undef DEF_HELPER_FLAGS_3
#undef DEF_HELPER_FLAGS_4
#undef DEF_HELPER_FLAGS_5
#undef DEF_HELPER_FLAGS_6
#undef DEF_HELPER_FLAGS_7
#undef tci_arg_i32
#undef tci_arg_i64
#undef tci_arg_ptr
#undef tci_arg
#undef tci_return_i32
#undef tci_return_i64
#undef tci_return_ptr
#undef tci_return_void
#undef tci_return_noreturn
#undef tci_return

#endif /* EXEC_HELPER_TCI_H */
