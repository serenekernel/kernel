#include <arch/cpu_local.h>
#include <common/arch.h>
#include <common/fs/fd_store.h>
#include <common/fs/vfs.h>
#include <common/sched/process.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/userspace/sys_proc.h>
#include <common/userspace/userspace.h>
#include <memory/memory.h>
#include <stdio.h>
#include <string.h>

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

syscall_ret_t syscall_sys_open(virt_addr_t pathname_str, size_t pathname_len, uint64_t flags, uint64_t mode) {
    if(pathname_len == 0 || pathname_len > 256) { return SYSCALL_RET_ERROR(ERROR_INVAL); }
    if(!validate_user_buffer(CPU_LOCAL_GET_CURRENT_THREAD()->common.process, pathname_str, pathname_len)) { return SYSCALL_RET_ERROR(ERROR_FAULT); }

    char pathname[256];
    bool __prev = arch_disable_uap();
    memcpy(pathname, (const void*) pathname_str, pathname_len);
    pathname[pathname_len] = '\0';
    arch_restore_uap(__prev);

    (void) flags; // @todo:
    (void) mode; // @todo:

    vfs_node_t* node;
    if(vfs_lookup(&VFS_MAKE_ABS_PATH(pathname), &node) != VFS_RESULT_OK) { return SYSCALL_RET_ERROR(ERROR_NOENT); }

    fd_store_t* store = CPU_LOCAL_GET_CURRENT_THREAD()->common.process->fd_store;
    fd_data_t* fd_data = (fd_data_t*) heap_alloc(sizeof(fd_data_t));
    fd_data->node = node;
    fd_data->cursor = 0;
    int fd = fd_store_allocate(store, fd_data);
    if(fd == -1) { return SYSCALL_RET_ERROR(ERROR_NOMEM); }

    return SYSCALL_RET_VALUE(fd);
}

syscall_ret_t syscall_sys_read(uint64_t fd, virt_addr_t buf, size_t count) {
    if(count == 0) { return SYSCALL_RET_VALUE(0); }
    if(!validate_user_buffer(CPU_LOCAL_GET_CURRENT_THREAD()->common.process, buf, count)) { return SYSCALL_RET_ERROR(ERROR_FAULT); }

    // @todo: vfs shit
    if(fd == 0 || fd == 1 || fd == 2) { return SYSCALL_RET_ERROR(ERROR_BADFD); }

    printf("syscall_sys_read: pid=%lu, fd=%d, count=%lu\n", CPU_LOCAL_GET_CURRENT_THREAD()->common.process->pid, fd, count);

    fd_store_t* store = CPU_LOCAL_GET_CURRENT_THREAD()->common.process->fd_store;
    fd_data_t* node = fd_store_get_fd(store, fd);
    if(!node) { return SYSCALL_RET_ERROR(ERROR_BADFD); }
    if(!node->node) { return SYSCALL_RET_ERROR(ERROR_BADFD); }
    if(node->node->type == VFS_NODE_TYPE_DIR) { return SYSCALL_RET_ERROR(ERROR_BADFD); }

    bool __prev = arch_disable_uap();
    size_t read_count = 0;
    vfs_result_t result = node->node->ops->read(node->node, (void*) buf, count, node->cursor, &read_count);
    arch_restore_uap(__prev);

    if(result != VFS_RESULT_OK) { return SYSCALL_RET_ERROR(ERROR_BADFD); }

    node->cursor += read_count;
    return SYSCALL_RET_VALUE(read_count);
}

syscall_ret_t syscall_sys_write(uint64_t fd, virt_addr_t buf, size_t count) {
    if(count == 0) { return SYSCALL_RET_VALUE(0); }
    if(!validate_user_buffer(CPU_LOCAL_GET_CURRENT_THREAD()->common.process, buf, count)) { return SYSCALL_RET_ERROR(ERROR_FAULT); }

    // @todo: vfs shit
    if(fd == 0) { return SYSCALL_RET_ERROR(ERROR_BADFD); }
    if(fd == 1 || fd == 2) {
        bool __prev = arch_disable_uap();
        printf("%.*s", (int) count, (const char*) buf);
        arch_restore_uap(__prev);
        return SYSCALL_RET_VALUE(count);
    }

    printf("syscall_sys_write: pid=%lu, fd=%d, count=%lu\n", CPU_LOCAL_GET_CURRENT_THREAD()->common.process->pid, fd, count);

    fd_store_t* store = CPU_LOCAL_GET_CURRENT_THREAD()->common.process->fd_store;
    fd_data_t* node = fd_store_get_fd(store, fd);
    if(!node) { return SYSCALL_RET_ERROR(ERROR_BADFD); }
    if(!node->node) { return SYSCALL_RET_ERROR(ERROR_BADFD); }
    if(node->node->type == VFS_NODE_TYPE_DIR) { return SYSCALL_RET_ERROR(ERROR_BADFD); }
    bool __prev = arch_disable_uap();
    size_t write_count = 0;
    vfs_result_t result = node->node->ops->write(node->node, (void*) buf, count, node->cursor, &write_count);
    arch_restore_uap(__prev);

    if(result == VFS_RESULT_ERR_READ_ONLY) return SYSCALL_RET_ERROR(ERROR_ROFS);
    if(result != VFS_RESULT_OK) return SYSCALL_RET_ERROR(ERROR_BADFD);

    node->cursor += write_count;
    return SYSCALL_RET_VALUE(write_count);
}

syscall_ret_t syscall_sys_close(uint64_t fd) {
    printf("syscall_sys_close: pid=%lu, fd=%d\n", CPU_LOCAL_GET_CURRENT_THREAD()->common.process->pid, fd);
    fd_store_t* store = CPU_LOCAL_GET_CURRENT_THREAD()->common.process->fd_store;
    if(!fd_store_close(store, fd)) { return SYSCALL_RET_ERROR(ERROR_BADFD); }

    return SYSCALL_RET_VALUE(0);
}

syscall_ret_t syscall_sys_seek(uint64_t fd, size_t offset, size_t whence) {
    printf("syscall_sys_seek: pid=%lu, fd=%d, offset=%ld, whence=%d\n", CPU_LOCAL_GET_CURRENT_THREAD()->common.process->pid, fd, offset, whence);
    fd_store_t* store = CPU_LOCAL_GET_CURRENT_THREAD()->common.process->fd_store;
    fd_data_t* node = fd_store_get_fd(store, fd);
    if(!node) { return SYSCALL_RET_ERROR(ERROR_BADFD); }
    if(!node->node) { return SYSCALL_RET_ERROR(ERROR_BADFD); }
    if(node->node->type == VFS_NODE_TYPE_DIR) { return SYSCALL_RET_ERROR(ERROR_BADFD); }

    int64_t signed_offset = (int64_t) offset;
    vfs_node_attr_t node_attr;
    if(node->node->ops->attr(node->node, &node_attr) != VFS_RESULT_OK) { return SYSCALL_RET_ERROR(ERROR_FAULT); }

    int64_t signed_file_size = (int64_t) node_attr.size;

    int64_t new_cursor = 0;
    if(whence == SEEK_SET) {
        new_cursor = signed_offset;
    } else if(whence == SEEK_CUR) {
        new_cursor = node->cursor + signed_offset;
    } else if(whence == SEEK_END) {
        new_cursor = signed_file_size + signed_offset;
    } else {
        return SYSCALL_RET_ERROR(ERROR_INVAL);
    }


    if(new_cursor < 0 || new_cursor > signed_file_size) { return SYSCALL_RET_ERROR(ERROR_FAULT); }
    node->cursor = new_cursor;

    return SYSCALL_RET_VALUE(0);
}
