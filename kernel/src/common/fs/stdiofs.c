#include <common/fs/vfs.h>
#include <memory/heap.h>
#include <string.h>

#include "stdio.h"

#define NODES(VFS) ((stdio_nodes_t*) (VFS)->private_data)

typedef struct {
    vfs_node_t* root;
    vfs_node_t* stdin;
    vfs_node_t* stdout;
    vfs_node_t* stderr;
} stdio_nodes_t;

static vfs_node_ops_t g_stdin_ops;
static vfs_node_ops_t g_stdout_ops;
static vfs_node_ops_t g_stderr_ops;

static vfs_result_t stdio_shared_node_attr(vfs_node_t* node, vfs_node_attr_t* attr) {
    (void) node;
    attr->size = 0;
    attr->type = VFS_NODE_TYPE_FILE;
    return VFS_RESULT_OK;
}

static vfs_result_t stdio_stdout_node_read(vfs_node_t* node, void* buf, size_t size, size_t offset, size_t* read_count) {
    (void) node;
    (void) buf;
    (void) size;
    (void) offset;
    (void) read_count;

    return VFS_RESULT_ERR_UNSUPPORTED;
}
static vfs_result_t stdio_stdin_node_read(vfs_node_t* node, void* buf, size_t size, size_t offset, size_t* read_count) {
    (void) node;
    (void) buf;
    (void) size;
    (void) offset;
    (void) read_count;

    return VFS_RESULT_ERR_UNSUPPORTED;
}
static vfs_result_t stdio_stderr_node_read(vfs_node_t* node, void* buf, size_t size, size_t offset, size_t* read_count) {
    (void) node;
    (void) buf;
    (void) size;
    (void) offset;
    (void) read_count;

    return VFS_RESULT_ERR_UNSUPPORTED;
}

static vfs_result_t stdio_stdin_node_write(vfs_node_t* node, const void* buf, size_t size, size_t offset, size_t* write_count) {
    (void) node;
    (void) buf;
    (void) size;
    (void) offset;
    (void) write_count;

    return VFS_RESULT_ERR_UNSUPPORTED;
}

static vfs_result_t stdio_stdout_node_write(vfs_node_t* node, const void* buf, size_t size, size_t offset, size_t* write_count) {
    (void) node;
    (void) offset;
    nl_printf("%.*s", (int) size, (char*) buf);
    *write_count = size;
    return VFS_RESULT_OK;
}
static vfs_result_t stdio_stderr_node_write(vfs_node_t* node, const void* buf, size_t size, size_t offset, size_t* write_count) {
    (void) node;
    (void) offset;

    nl_printf("%.*s", (int) size, (char*) buf);
    *write_count = size;
    return VFS_RESULT_OK;
}


static vfs_result_t stdio_shared_node_lookup(vfs_node_t* node, char* name, vfs_node_t** out) {
    (void) node;
    (void) name;
    (void) out;
    return VFS_RESULT_ERR_NOT_DIR;
}

static vfs_result_t stdio_shared_node_readdir(vfs_node_t* node, size_t* offset, char** out) {
    (void) node;
    (void) offset;
    (void) out;
    return VFS_RESULT_ERR_NOT_DIR;
}


static vfs_node_ops_t g_stdin_ops = {
    .attr = stdio_shared_node_attr,
    .lookup = stdio_shared_node_lookup,
    .read = stdio_stdin_node_read,
    .write = stdio_stdin_node_write,
    .readdir = stdio_shared_node_readdir,
};

static vfs_node_ops_t g_stdout_ops = {
    .attr = stdio_shared_node_attr,
    .lookup = stdio_shared_node_lookup,
    .read = stdio_stdout_node_read,
    .write = stdio_stdout_node_write,
    .readdir = stdio_shared_node_readdir,
};

static vfs_node_ops_t g_stderr_ops = {
    .attr = stdio_shared_node_attr,
    .lookup = stdio_shared_node_lookup,
    .read = stdio_stderr_node_read,
    .write = stdio_stderr_node_write,
    .readdir = stdio_shared_node_readdir,
};

static vfs_result_t stdio_root_node_attr(vfs_node_t* node, vfs_node_attr_t* attr) {
    (void) node;
    attr->size = 0;
    attr->type = VFS_NODE_TYPE_DIR;
    return VFS_RESULT_OK;
}

static vfs_result_t stdio_root_node_lookup(vfs_node_t* node, char* name, vfs_node_t** out) {
    if(strcmp(name, "..") == 0) {
        *out = NULL;
        return 0;
    }
    if(strcmp(name, ".") == 0) {
        *out = node;
        return 0;
    }
    if(strcmp(name, "stdin") == 0) {
        *out = NODES(node->vfs)->stdin;
        return 0;
    }
    if(strcmp(name, "stdout") == 0) {
        *out = NODES(node->vfs)->stdout;
        return 0;
    }
    if(strcmp(name, "stderr") == 0) {
        *out = NODES(node->vfs)->stderr;
        return 0;
    }
    return VFS_RESULT_ERR_NOT_FOUND;
}

static vfs_result_t stdio_root_node_read(vfs_node_t* node, void* buf, size_t size, size_t offset, size_t* read_count) {
    (void) node;
    (void) buf;
    (void) size;
    (void) offset;
    (void) read_count;
    return VFS_RESULT_ERR_NOT_FILE;
}

static vfs_result_t stdio_root_node_write(vfs_node_t* node, const void* buf, size_t size, size_t offset, size_t* written_count) {
    (void) node;
    (void) buf;
    (void) size;
    (void) offset;
    (void) written_count;
    return VFS_RESULT_ERR_NOT_FILE;
}

static vfs_result_t stdio_root_node_readdir(vfs_node_t* node, size_t* offset, char** out) {
    (void) node;
    switch(*offset) {
        case 0:  *out = "stdin"; break;
        case 1:  *out = "stdout"; break;
        case 2:  *out = "stderr"; break;
        default: *out = NULL; return 0;
    }
    (*offset)++;
    return 0;
}

static vfs_node_ops_t g_root_ops = {
    .attr = stdio_root_node_attr,
    .lookup = stdio_root_node_lookup,
    .read = stdio_root_node_read,
    .write = stdio_root_node_write,
    .readdir = stdio_root_node_readdir,
};

static vfs_result_t stdio_mount(vfs_t* vfs) {
    stdio_nodes_t* nodes = heap_alloc(sizeof(stdio_nodes_t));

    nodes->root = heap_alloc(sizeof(vfs_node_t));
    memset(nodes->root, 0, sizeof(vfs_node_t));
    nodes->root->vfs = vfs;
    nodes->root->type = VFS_NODE_TYPE_DIR;
    nodes->root->ops = &g_root_ops;

    nodes->stdin = heap_alloc(sizeof(vfs_node_t));
    memset(nodes->stdin, 0, sizeof(vfs_node_t));
    nodes->stdin->vfs = vfs;
    nodes->stdin->type = VFS_NODE_TYPE_FILE;
    nodes->stdin->ops = &g_stdin_ops;

    nodes->stdout = heap_alloc(sizeof(vfs_node_t));
    memset(nodes->stdout, 0, sizeof(vfs_node_t));
    nodes->stdout->vfs = vfs;
    nodes->stdout->type = VFS_NODE_TYPE_FILE;
    nodes->stdout->ops = &g_stdout_ops;

    nodes->stderr = heap_alloc(sizeof(vfs_node_t));
    memset(nodes->stderr, 0, sizeof(vfs_node_t));
    nodes->stderr->vfs = vfs;
    nodes->stderr->type = VFS_NODE_TYPE_FILE;
    nodes->stderr->ops = &g_stderr_ops;

    vfs->private_data = (void*) nodes;
    return 0;
}

static vfs_result_t stdio_unmount(vfs_t* vfs) {
    stdio_nodes_t* nodes = NODES(vfs);
    heap_free(nodes->root, sizeof(vfs_node_t));
    heap_free(nodes->stdin, sizeof(vfs_node_t));
    heap_free(nodes->stdout, sizeof(vfs_node_t));
    heap_free(nodes->stderr, sizeof(vfs_node_t));
    heap_free(nodes, sizeof(stdio_nodes_t));
    return 0;
}

static vfs_result_t stdio_get_root_node(vfs_t* vfs, vfs_node_t** out) {
    *out = NODES(vfs)->root;
    return 0;
}

vfs_ops_t fs_stdio_ops = { .mount = stdio_mount, .unmount = stdio_unmount, .get_root_node = stdio_get_root_node };
