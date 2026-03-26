#pragma once
#include <common/sched/process.h>
#include <stdint.h>

typedef enum : uint64_t {
    SYS_EXIT = 0,
    SYS_OPEN = 1,
    SYS_READ = 2,
    SYS_WRITE = 3,
    SYS_CLOSE = 4,
    SYS_SEEK = 5,
    SYS_DEBUG_LOG = 6,
    SYS_TCB_SET = 7,
    SYS_MEM_VM_MAP = 8,
    SYS_MEM_VM_UNMAP = 9,
    SYS_MEM_VM_PROTECT = 10
} syscall_nr_t;

typedef enum : int64_t {
    ERROR_NOENT = 2, // No such file or directory
    ERROR_FAULT = 14, // Bad address
    ERROR_NOMEM = 12, // Out of memory
    ERROR_INVAL = 22, // Invalid argument
    ERROR_ROFS = 30, // Read-only file system
    ERROR_BADFD = 81, // Bad file descriptor
    ERROR_ENOSYS = 88 // Function not implemented
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

#define SYSCALL_ASSERT_PARAM(cond)                           \
    do {                                                     \
        if(!(cond)) {                                        \
            printf("syscall assertion failed: %s\n", #cond); \
            return SYSCALL_RET_ERROR(ERROR_INVAL);           \
        }                                                    \
    } while(0)


bool validate_user_buffer(process_t* process, virt_addr_t buf, size_t count);
