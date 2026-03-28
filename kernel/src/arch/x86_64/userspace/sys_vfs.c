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
    printf("syscall_sys_stat: pid=%lu, pathname=%s\n", CPU_LOCAL_GET_CURRENT_THREAD()->common.process->pid, pathname);

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
    if(node->node->type == VFS_NODE_TYPE_CHARDEV) { return SYSCALL_RET_ERROR(ERROR_SPIPE); }
    int64_t signed_offset = (int64_t) offset;
    vfs_node_attr_t node_attr;
    if(node->node->ops->attr(node->node, &node_attr) != VFS_RESULT_OK) { return SYSCALL_RET_ERROR(ERROR_SPIPE); }

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

    printf("new_cursor=%ld, file size=%ld\n", new_cursor, signed_file_size);
    if(new_cursor < 0 || new_cursor > signed_file_size) { return SYSCALL_RET_ERROR(ERROR_RANGE); }
    node->cursor = new_cursor;

    return SYSCALL_RET_VALUE(0);
}

syscall_ret_t syscall_sys_is_a_tty(uint64_t fd) {
    // @todo: STUB
    fd_store_t* store = CPU_LOCAL_GET_CURRENT_THREAD()->common.process->fd_store;
    fd_data_t* node = fd_store_get_fd(store, fd);
    if(!node) { return SYSCALL_RET_ERROR(ERROR_BADFD); }
    if(!node->node) { return SYSCALL_RET_ERROR(ERROR_BADFD); }
    if(node->node->type == VFS_NODE_TYPE_DIR) { return SYSCALL_RET_ERROR(ERROR_BADFD); }
    // @todo: NOT ALL CHAR DEVS ARE TTYS
    if(node->node->type == VFS_NODE_TYPE_CHARDEV) { return SYSCALL_RET_VALUE(0); }

    return SYSCALL_RET_ERROR(ERROR_NOTTY);
}

#define MODE_TYPE_MASK 0x0F000
#define MODE_TYPE_CHAR 0x02000
#define MODE_TYPE_FILE 0x08000
#define MODE_TYPE_DIR 0x04000

#define MODE_PERM_USER_RWX 0700
#define MODE_PERM_USER_R 0400
#define MODE_PERM_USER_W 0200
#define MODE_PERM_USER_X 0100
#define MODE_PERM_GROUP_RWX 070
#define MODE_PERM_GROUP_R 040
#define MODE_PERM_GROUP_W 020
#define MODE_PERM_GROUP_X 010
#define MODE_PERM_OTHER_RWX 07
#define MODE_PERM_OTHER_R 04
#define MODE_PERM_OTHER_W 02
#define MODE_PERM_OTHER_X 01

typedef struct {
    int64_t st_size;
    uint64_t st_nlink;
    uint32_t st_mode;
} serene_stat_t;

syscall_ret_t stat_internal(serene_stat_t* statbuf, vfs_node_t* vfs_node) {
    vfs_node_attr_t attr;
    vfs_result_t res = vfs_node->ops->attr(vfs_node, &attr);
    if(res != VFS_RESULT_OK) {
        assert(false);
        return SYSCALL_RET_ERROR(ERROR_FAULT);
    }

    bool __prev = arch_disable_uap();
    statbuf->st_size = attr.size;
    statbuf->st_nlink = 1; // @todo:
    statbuf->st_mode = 0;
    switch(attr.type) {
        case VFS_NODE_TYPE_FILE:    statbuf->st_mode |= MODE_TYPE_FILE; break;
        case VFS_NODE_TYPE_DIR:     statbuf->st_mode |= MODE_TYPE_DIR; break;
        case VFS_NODE_TYPE_CHARDEV: statbuf->st_mode |= MODE_TYPE_CHAR; break;
        default:                    assert(false); return SYSCALL_RET_ERROR(ERROR_FAULT);
    }

    // @todo: horrifc
    statbuf->st_mode |= MODE_PERM_USER_R | MODE_PERM_USER_X;
    arch_restore_uap(__prev);
    return SYSCALL_RET_VALUE(0);
}

syscall_ret_t syscall_sys_stat(uint64_t fd, uint64_t statbuf) {
    if(!validate_user_buffer(CPU_LOCAL_GET_CURRENT_THREAD()->common.process, statbuf, sizeof(serene_stat_t))) { return SYSCALL_RET_ERROR(ERROR_FAULT); }
    printf("syscall_sys_stat: pid=%lu, fd=%d\n", CPU_LOCAL_GET_CURRENT_THREAD()->common.process->pid, fd);

    fd_store_t* store = CPU_LOCAL_GET_CURRENT_THREAD()->common.process->fd_store;
    fd_data_t* node = fd_store_get_fd(store, fd);
    if(!node) { return SYSCALL_RET_ERROR(ERROR_BADFD); }
    if(!node->node) { return SYSCALL_RET_ERROR(ERROR_BADFD); }

    return stat_internal((serene_stat_t*) statbuf, node->node);
}
#define AT_FDCWD -100

syscall_ret_t syscall_sys_stat_at(uint64_t fd, uint64_t path, size_t path_len, uint64_t statbuf, size_t flag) {
    (void) flag;
    if(!validate_user_buffer(CPU_LOCAL_GET_CURRENT_THREAD()->common.process, statbuf, sizeof(serene_stat_t))) { return SYSCALL_RET_ERROR(ERROR_FAULT); }

    if(path_len == 0 || path_len > 256) { return SYSCALL_RET_ERROR(ERROR_INVAL); }
    if(!validate_user_buffer(CPU_LOCAL_GET_CURRENT_THREAD()->common.process, path, path_len)) { return SYSCALL_RET_ERROR(ERROR_FAULT); }

    char pathname[256];
    bool __prev = arch_disable_uap();
    memcpy(pathname, (const void*) path, path_len);
    pathname[path_len] = '\0';
    arch_restore_uap(__prev);

    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    vfs_path_t vfs_path;

    printf("syscall_sys_stat_at: pid=%lu, fd=%d, path=%s\n", process->pid, fd, pathname);
    if(pathname[0] == '/') {
        vfs_path.node = nullptr;
        vfs_path.rel_path = pathname;
    } else {
        if(((int32_t) fd) == AT_FDCWD) {
            vfs_path.node = process->cwd;
        } else {
            fd_store_t* store = process->fd_store;
            fd_data_t* node = fd_store_get_fd(store, fd);
            if(!node) { return SYSCALL_RET_ERROR(ERROR_BADFD); }
            if(!node->node) { return SYSCALL_RET_ERROR(ERROR_BADFD); }
            vfs_path.node = node->node;
        }
        vfs_path.rel_path = pathname;
    }

    vfs_node_t* result_node;
    vfs_result_t res = vfs_lookup(&vfs_path, &result_node);
    if(res != VFS_RESULT_OK) { return SYSCALL_RET_ERROR(ERROR_NOENT); }
    return stat_internal((serene_stat_t*) statbuf, result_node);
}

syscall_ret_t syscall_sys_getcwd(virt_addr_t buf, size_t size) {
    printf("get_cwd: buf=%p, size=%lu\n", buf, size);
    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    char* kernel_buf;
    size_t kernel_buf_size;
    vfs_result_t res = vfs_path_to(process->cwd, &kernel_buf, &kernel_buf_size);
    if(res != VFS_RESULT_OK) { return SYSCALL_RET_ERROR(ERROR_FAULT); }

    size_t cwd_len = strlen(kernel_buf);
    if(cwd_len > size) { return SYSCALL_RET_ERROR(ERROR_RANGE); }

    arch_disable_uap();
    memcpy((void*) buf, kernel_buf, cwd_len);
    arch_restore_uap(false);

    heap_free(kernel_buf, kernel_buf_size);
    return SYSCALL_RET_VALUE(0);
}
