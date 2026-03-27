#pragma once

#include <linked_list.h>

typedef struct vfs vfs_t;
typedef struct vfs_node vfs_node_t;
typedef struct vfs_ops vfs_ops_t;
typedef struct vfs_node_ops vfs_node_ops_t;

typedef enum {
    VFS_RESULT_OK = 0,
    VFS_RESULT_ERR_UNSUPPORTED,
    VFS_RESULT_ERR_NOT_FOUND,
    VFS_RESULT_ERR_NOT_FILE,
    VFS_RESULT_ERR_NOT_DIR,
    VFS_RESULT_ERR_EXISTS,
    VFS_RESULT_ERR_READ_ONLY,
    VFS_RESULT_ERR_BUFFER_TOO_SMALL
} vfs_result_t;

typedef enum {
    VFS_NODE_TYPE_FILE,
    VFS_NODE_TYPE_DIR
} vfs_node_type_t;

typedef struct {
    size_t size;
    vfs_node_type_t type;
} vfs_node_attr_t;

struct vfs_ops {
    vfs_result_t (*mount)(vfs_t* vfs);
    vfs_result_t (*unmount)(vfs_t* vfs);

    vfs_result_t (*get_root_node)(vfs_t* vfs, vfs_node_t** root_node);
};

struct vfs_node_ops {
    // @param: name - the name of the node to look up
    // @param: res_node - the resulting node, if found
    vfs_result_t (*lookup)(vfs_node_t* node, char* name, vfs_node_t** out_node);

    // @param: buf - the buffer to read into
    // @param: size - the size of the buffer
    // @param: offset - the offset to read from
    // @param: read_count - set to the number of bytes read
    // @note: if buffer is null or size is 0, read_count is set to the size of the file and the return value is VFS_RESULT_OK
    vfs_result_t (*read)(vfs_node_t* node, void* buf, size_t size, size_t offset, size_t* read_count);

    // @param: buf - the buffer to write from
    // @param: size - the size of the buffer
    // @param: offset - the offset to write to
    // @param: written_count - set to the number of bytes written
    vfs_result_t (*write)(vfs_node_t* node, const void* buf, size_t size, size_t offset, size_t* written_count);

    // @param: offset - the offset to read from (set to the next offset to read from)
    // @param: name - set to the name of the directory entry, or nullptr if there are no more entries
    vfs_result_t (*readdir)(vfs_node_t* node, size_t* offset, char** name);

    // @param: attr - the attribute struct to fill in
    vfs_result_t (*attr)(vfs_node_t* node, vfs_node_attr_t* attr);
};

struct vfs {
    const vfs_ops_t* ops;
    void* private_data;
    vfs_node_t* mount_point;
    list_node_t list_node;
};

struct vfs_node {
    vfs_t* vfs;
    vfs_t* mounted_vfs;
    vfs_node_t* parent;
    vfs_node_ops_t* ops;
    vfs_node_type_t type;
    void* private_data;
};

typedef struct {
    vfs_node_t* node;
    const char* rel_path;
} vfs_path_t;

#define VFS_MAKE_ABS_PATH(__path)               \
    (vfs_path_t) {                              \
        .node = (nullptr), .rel_path = (__path) \
    }
#define VFS_MAKE_REL_PATH(__node, __path)      \
    (vfs_path_t) {                             \
        .node = (__node), .rel_path = (__path) \
    }

extern list_t g_vfs_list;

vfs_result_t vfs_mount(const vfs_ops_t* opts, const char* path, void* private_data);
vfs_result_t vfs_unmount(const char* path);

vfs_result_t vfs_root(vfs_node_t** root_node);
vfs_result_t vfs_lookup(vfs_path_t* path, vfs_node_t** result_node);

vfs_result_t vfs_read(vfs_path_t* path, void* buf, size_t size, size_t offset, size_t* read_count);
vfs_result_t vfs_write(vfs_path_t* path, const void* buf, size_t size, size_t offset, size_t* written_count);
vfs_result_t vfs_attr(vfs_path_t* path, vfs_node_attr_t* attr);
vfs_result_t vfs_path_to(vfs_node_t* node, char** out_buf, size_t* out_size);
