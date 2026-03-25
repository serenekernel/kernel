#include <common/fs/vfs.h>
#include <linked_list.h>
#include <memory/heap.h>
#include <string.h>

list_t g_vfs_list = LIST_INIT;

vfs_result_t vfs_mount(const vfs_ops_t* vfs_ops, const char* path, void* private_data) {
    vfs_t* vfs = heap_alloc(sizeof(vfs_t));
    vfs->ops = vfs_ops;
    vfs->private_data = private_data;
    vfs->ops->mount(vfs);

    if(g_vfs_list.count == 0) {
        if(path != nullptr) {
            heap_free(vfs, sizeof(vfs_t));
            return VFS_RESULT_ERR_NOT_FOUND;
        }
        vfs->mount_point = nullptr;
        list_push_back(&g_vfs_list, &vfs->list_node);
        return VFS_RESULT_OK;
    }

    vfs_node_t* node;
    vfs_result_t res = vfs_lookup(&VFS_MAKE_ABS_PATH(path), &node);
    if(res != VFS_RESULT_OK) {
        heap_free(vfs, sizeof(vfs_t));
        return res;
    }
    if(node->type != VFS_NODE_TYPE_DIR) {
        heap_free(vfs, sizeof(vfs_t));
        return VFS_RESULT_ERR_NOT_DIR;
    }
    if(node->mounted_vfs != nullptr) {
        heap_free(vfs, sizeof(vfs_t));
        return VFS_RESULT_ERR_EXISTS;
    }
    node->mounted_vfs = vfs;
    vfs->mount_point = node;

    list_push_back(&g_vfs_list, &vfs->list_node);
    return VFS_RESULT_OK;
}

vfs_result_t vfs_unmount(const char* path) {
    (void) path;
    return VFS_RESULT_ERR_UNSUPPORTED;
}

vfs_result_t vfs_root(vfs_node_t** root_node) {
    if(g_vfs_list.count == 0) return VFS_RESULT_ERR_NOT_FOUND;
    vfs_t* vfs = CONTAINER_OF(g_vfs_list.head, vfs_t, list_node);
    return vfs->ops->get_root_node(vfs, root_node);
}

vfs_result_t vfs_lookup(vfs_path_t* path, vfs_node_t** result_node) {
    int comp_start = 0, comp_end = 0;

    vfs_node_t* current_node = path->node;
    if(path->rel_path[comp_end] == '/' || current_node == nullptr) {
        vfs_result_t res = vfs_root(&current_node);
        if(res != VFS_RESULT_OK) return res;
        if(path->rel_path[comp_end] == '/') { comp_start++, comp_end++; }
    }
    if(current_node == nullptr) return VFS_RESULT_ERR_NOT_FOUND;

    do {
        switch(path->rel_path[comp_end]) {
            case '\0': [[fallthrough]];
            case '/':
                if(comp_start == comp_end) {
                    comp_start++;
                    break;
                }
                int comp_length = comp_end - comp_start;
                char* component = heap_alloc(comp_length + 1);
                memcpy(component, path->rel_path + comp_start, comp_length);
                component[comp_length] = 0;
                comp_start = comp_end + 1;

                vfs_node_t* next_node;
                vfs_result_t res = current_node->ops->lookup(current_node, component, &next_node);
                if(res == VFS_RESULT_OK) {
                    if(next_node == nullptr) {
                        if(current_node->vfs->mount_point != nullptr) {
                            // @todo
                            res = current_node->ops->lookup(current_node, "..", &next_node);
                        }
                    } else {
                        current_node = next_node;
                    }
                }

                heap_free(component, comp_length + 1);

                if(res != VFS_RESULT_OK) return res;
                break;
        }

        if(current_node->mounted_vfs == nullptr) continue;
        vfs_result_t res = current_node->mounted_vfs->ops->get_root_node(current_node->mounted_vfs, &current_node);
        if(res != VFS_RESULT_OK) return res;
    } while(path->rel_path[comp_end++]);

    *result_node = current_node;
    return VFS_RESULT_OK;
}

vfs_result_t vfs_read(vfs_path_t* path, void* buff, size_t size, size_t offset, size_t* read_count) {
    vfs_node_t* node;
    vfs_result_t res = vfs_lookup(path, &node);
    if(res != VFS_RESULT_OK) return res;
    return node->ops->read(node, buff, size, offset, read_count);
}

vfs_result_t vfs_write(vfs_path_t* path, const void* buff, size_t size, size_t offset, size_t* written_count) {
    vfs_node_t* node;
    vfs_result_t res = vfs_lookup(path, &node);
    if(res != VFS_RESULT_OK) return res;
    return node->ops->write(node, buff, size, offset, written_count);
}
