#include <common/fs/rdsk.h>
#include <common/fs/vfs.h>
#include <string.h>

#include "memory/heap.h"

#define INFO(VFS) ((info_t*) (VFS)->private_data)
#define FILE(NODE) ((rdsk_file_t*) (NODE)->private_data)
#define DIR(NODE) ((rdsk_dir_t*) (NODE)->private_data)

typedef uint64_t rdsk_index_t;


typedef struct [[gnu::packed]] {
    uint16_t entry_size;
    uint64_t entry_count;
    uint64_t offset;
} rdsk_table_t;

typedef struct [[gnu::packed]] {
    char signature[4];
    uint16_t revision;
    uint16_t header_size;
    uint64_t root_index;

    uint64_t nametable_offset;
    uint64_t nametable_size;

    rdsk_table_t dir_table;
    rdsk_table_t file_table;
} rdsk_header_t;

typedef struct [[gnu::packed]] {
    bool used;
    uint64_t nametable_offset;
    uint64_t data_offset;
    uint64_t size;
    uint64_t next_index;
    uint64_t parent_index;
} rdsk_file_t;

typedef struct [[gnu::packed]] {
    bool used;
    uint64_t nametable_offset;
    uint64_t filetable_index;
    uint64_t dirtable_index;
    uint64_t next_index;
    uint64_t parent_index;
} rdsk_dir_t;

typedef struct {
    rdsk_header_t* header;
    vfs_node_t** dir_cache;
    uint64_t dir_cache_size;
    vfs_node_t** file_cache;
    uint64_t file_cache_size;
} info_t;

#define MAKE_RDSK_VERSION(major, minor) ((major << 8) | minor)

static vfs_node_ops_t g_node_ops;

static const char* get_name(vfs_t* vfs, uint64_t offset) {
    info_t* info = (info_t*) vfs->private_data;
    return (const char*) ((uintptr_t) info->header + info->header->nametable_offset + offset);
}

static rdsk_dir_t* get_dir(vfs_t* vfs, rdsk_index_t index) {
    info_t* info = (info_t*) vfs->private_data;
    return (rdsk_dir_t*) ((uintptr_t) info->header + info->header->dir_table.offset + (index - 1) * info->header->dir_table.entry_size);
}

static rdsk_file_t* get_file(vfs_t* vfs, rdsk_index_t index) {
    info_t* info = (info_t*) vfs->private_data;
    return (rdsk_file_t*) ((uintptr_t) info->header + info->header->file_table.offset + (index - 1) * info->header->file_table.entry_size);
}

static rdsk_index_t get_dir_index(vfs_t* vfs, rdsk_dir_t* dir) {
    info_t* info = (info_t*) vfs->private_data;
    return ((uintptr_t) dir - ((uintptr_t) info->header + info->header->dir_table.offset)) / info->header->dir_table.entry_size + 1;
}

static rdsk_index_t get_file_index(vfs_t* vfs, rdsk_file_t* file) {
    info_t* info = (info_t*) vfs->private_data;
    return ((uintptr_t) file - ((uintptr_t) info->header + info->header->file_table.offset)) / info->header->file_table.entry_size + 1;
}

static vfs_node_t* create_vfs_dir_node(vfs_t* vfs, rdsk_index_t index) {
    info_t* info = INFO(vfs);
    if(info->dir_cache[index - 1]) return info->dir_cache[index - 1];
    vfs_node_t* node = heap_alloc(sizeof(vfs_node_t));
    memset(node, 0, sizeof(vfs_node_t));
    node->vfs = vfs;
    node->type = VFS_NODE_TYPE_DIR;
    node->private_data = get_dir(vfs, index);
    node->ops = &g_node_ops;
    info->dir_cache[index - 1] = node;
    return node;
}

static vfs_node_t* create_vfs_file_node(vfs_t* vfs, rdsk_index_t index) {
    info_t* info = INFO(vfs);
    if(info->file_cache[index - 1]) return info->file_cache[index - 1];
    vfs_node_t* node = heap_alloc(sizeof(vfs_node_t));
    memset(node, 0, sizeof(vfs_node_t));
    node->vfs = vfs;
    node->type = VFS_NODE_TYPE_FILE;
    node->private_data = get_file(vfs, index);
    node->ops = &g_node_ops;
    info->file_cache[index - 1] = node;
    return node;
}

static rdsk_dir_t* find_dir(vfs_t* vfs, rdsk_dir_t* dir, char* name) {
    rdsk_index_t curindex = dir->dirtable_index;
    while(curindex != 0) {
        rdsk_dir_t* curdir = get_dir(vfs, curindex);
        curindex = curdir->next_index;
        if(!strcmp(name, get_name(vfs, curdir->nametable_offset))) continue;
        return curdir;
    }
    return nullptr;
}

static rdsk_file_t* find_file(vfs_t* vfs, rdsk_dir_t* dir, char* name) {
    rdsk_index_t curindex = dir->filetable_index;
    while(curindex != 0) {
        rdsk_file_t* curfile = get_file(vfs, curindex);
        curindex = curfile->next_index;
        if(!strcmp(name, get_name(vfs, curfile->nametable_offset))) continue;
        return curfile;
    }
    return nullptr;
}


vfs_result_t rdsk_node_name(vfs_node_t* node, char* buffer, size_t length, size_t* out_size) {
    const char* name;
    switch(node->type) {
        case VFS_NODE_TYPE_DIR:  name = get_name(node->vfs, DIR(node)->nametable_offset); break;
        case VFS_NODE_TYPE_FILE: name = get_name(node->vfs, FILE(node)->nametable_offset); break;
    }

    size_t namelen = strlen(name);
    if(namelen > length) {
        assert(out_size != nullptr);
        *out_size = namelen;
        return VFS_RESULT_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(buffer, name, namelen);
    if(out_size != nullptr) *out_size = namelen;
    return VFS_RESULT_OK;
}

vfs_result_t rdsk_node_attr(vfs_node_t* node, vfs_node_attr_t* attr) {
    attr->size = node->type == VFS_NODE_TYPE_FILE ? FILE(node)->size : 0;
    return VFS_RESULT_OK;
}

vfs_result_t rdsk_node_lookup(vfs_node_t* node, char* name, vfs_node_t** out_node) {
    if(node->type != VFS_NODE_TYPE_DIR) return VFS_RESULT_ERR_NOT_DIR;

    if(strcmp(name, ".") == 0) {
        *out_node = node;
        return VFS_RESULT_OK;
    }

    if(strcmp(name, "..") == 0) {
        if(DIR(node)->parent_index != 0) {
            *out_node = create_vfs_dir_node(node->vfs, DIR(node)->parent_index);
        } else {
            *out_node = nullptr;
        }
        return VFS_RESULT_OK;
    }

    rdsk_file_t* found_file = find_file(node->vfs, DIR(node), name);
    if(found_file != nullptr) {
        *out_node = create_vfs_file_node(node->vfs, get_file_index(node->vfs, found_file));
        return VFS_RESULT_OK;
    }

    rdsk_dir_t* found_dir = find_dir(node->vfs, DIR(node), name);
    if(found_dir != nullptr) {
        *out_node = create_vfs_dir_node(node->vfs, get_dir_index(node->vfs, found_dir));
        return VFS_RESULT_OK;
    }

    return VFS_RESULT_ERR_NOT_FOUND;
}

vfs_result_t rdsk_node_readdir(vfs_node_t* node, size_t* offset, vfs_node_t** dirent) {
    if(node->type != VFS_NODE_TYPE_DIR) return VFS_RESULT_ERR_NOT_DIR;

    int local_offset = *offset;
    rdsk_header_t* header = INFO(node->vfs)->header;
    if(local_offset < (int) header->file_table.entry_count) {
        rdsk_index_t index = DIR(node)->filetable_index;
        for(int i = 0; i < local_offset && index != 0; i++) index = get_file(node->vfs, index)->next_index;
        if(index == 0) {
            local_offset = header->file_table.entry_count;
        } else {
            *dirent = create_vfs_file_node(node->vfs, index);
            *offset = local_offset + 1;
            return VFS_RESULT_OK;
        }
    }

    rdsk_index_t index = DIR(node)->dirtable_index;
    for(int i = 0; i < local_offset - (int) header->file_table.entry_count && index != 0; i++) index = get_dir(node->vfs, index)->next_index;
    if(index == 0) {
        *dirent = nullptr;
        return VFS_RESULT_OK;
    }

    *dirent = create_vfs_dir_node(node->vfs, index);
    *offset = local_offset + 1;
    return VFS_RESULT_OK;
}

vfs_result_t rdsk_node_read(vfs_node_t* node, void* buffer, size_t size, size_t offset, size_t* read_count) {
    if(node->type != VFS_NODE_TYPE_FILE) return VFS_RESULT_ERR_NOT_FILE;

    if(offset >= FILE(node)->size) {
        if(read_count) *read_count = 0;
        return VFS_RESULT_OK;
    }
    size_t count = FILE(node)->size - offset;
    if(count > size) count = size;
    memcpy(buffer, (void*) (((uintptr_t) INFO(node->vfs)->header + FILE(node)->data_offset) + offset), count);
    if(read_count) *read_count = count;
    return VFS_RESULT_OK;
}

vfs_result_t rdsk_node_write(vfs_node_t* node, const void* buffer, size_t size, size_t offset, size_t* written_count) {
    (void) node;
    (void) buffer;
    (void) size;
    (void) offset;
    (void) written_count;
    return VFS_RESULT_ERR_READ_ONLY;
}

vfs_result_t rdsk_umount(vfs_t* vfs) {
    (void) vfs;
    return VFS_RESULT_ERR_UNSUPPORTED;
}

vfs_result_t rdsk_mount(vfs_t* vfs) {
    rdsk_header_t* header = (rdsk_header_t*) vfs->private_data;
    if(header->revision != MAKE_RDSK_VERSION(1, 1)) { return VFS_RESULT_ERR_UNSUPPORTED; }

    info_t* info = heap_alloc(sizeof(info_t));
    info->header = header;
    info->dir_cache_size = header->dir_table.entry_count;
    info->file_cache_size = header->file_table.entry_count;

    info->dir_cache = heap_alloc(sizeof(vfs_node_t*) * info->dir_cache_size);
    info->file_cache = heap_alloc(sizeof(vfs_node_t*) * info->file_cache_size);

    memset(info->dir_cache, 0, sizeof(vfs_node_t*) * info->dir_cache_size);
    memset(info->file_cache, 0, sizeof(vfs_node_t*) * info->file_cache_size);

    vfs->private_data = info;
    return VFS_RESULT_OK;
}

vfs_result_t rdsk_root(vfs_t* vfs, vfs_node_t** root_node) {
    *root_node = create_vfs_dir_node(vfs, INFO(vfs)->header->root_index);
    return VFS_RESULT_OK;
}

static vfs_node_ops_t g_node_ops = { .lookup = rdsk_node_lookup, .read = rdsk_node_read, .write = rdsk_node_write, .name = rdsk_node_name, .attr = rdsk_node_attr, .readdir = rdsk_node_readdir };

const vfs_ops_t fs_rdsk_ops = { .mount = rdsk_mount, .unmount = rdsk_umount, .get_root_node = rdsk_root };
