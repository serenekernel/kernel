#pragma once
#include <common/sched/process.h>
#include <stdint.h>

typedef enum : uint64_t {
    SYS_EXIT = 0,
    SYS_WRITE = 1,
    SYS_DEBUG_LOG = 2,
    SYS_TCB_SET = 3,
    SYS_MEM_ANON_ALLOC = 4,
    SYS_MEM_ANON_FREE = 5,
} syscall_nr_t;

typedef enum : int64_t {
    SYSCALL_ERR_INVALID_ARGUMENT = -1,
    SYSCALL_ERR_INVALID_ADDRESS = -2,
    SYSCALL_ERR_INVALID_SYSCALL = -3,
    SYSCALL_ERR_OUT_OF_MEMORY = -4,
} syscall_err_t;

typedef struct {
    union {
        syscall_err_t err;
        uint64_t value;
    };
    uint64_t is_error;
} __attribute__((packed)) syscall_ret_t;

static_assert(sizeof(syscall_ret_t) == 16, "syscall_ret_t must be 16 bytes");

#define SYSCALL_RET_ERROR(err_code) ((syscall_ret_t) { .is_error = true, .err = (err_code) })

#define SYSCALL_RET_VALUE(val) ((syscall_ret_t) { .is_error = false, .value = (val) })

void userspace_init();

const char* convert_syscall_number(syscall_nr_t nr);
const char* convert_syscall_error(syscall_err_t err);

#define SYSCALL_ASSERT_PARAM(cond)                                  \
    do {                                                            \
        if(!(cond)) {                                               \
            printf("syscall assertion failed: %s\n", #cond);        \
            return SYSCALL_RET_ERROR(SYSCALL_ERR_INVALID_ARGUMENT); \
        }                                                           \
    } while(0)


bool validate_user_buffer(process_t* process, virt_addr_t buf, size_t count);
