#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "../vtfs_interface.h"

struct vtfs_ram_node {
  struct vtfs_node_meta meta;
  char name[NAME_MAX + 1];
  struct vtfs_ram_node* next;
};

struct vtfs_ram_storage {
  struct vtfs_ram_node* nodes_head;
  vtfs_ino_t next_ino;
};

static struct vtfs_ram_storage* get_storage(struct super_block* sb) {
  return (struct vtfs_ram_storage*)sb->s_fs_info;
}

static struct vtfs_ram_node* find_node_by_ino(struct vtfs_ram_storage* storage, vtfs_ino_t ino) {
  struct vtfs_ram_node* cur = storage->nodes_head;

  while (cur) {
    if (cur->meta.ino == ino)
      return cur;
    cur = cur->next;
  }

  return NULL;
}

static struct vtfs_ram_node* find_child(
    struct vtfs_ram_storage* storage, vtfs_ino_t parent, const char* name
) {
  struct vtfs_ram_node* cur = storage->nodes_head;

  while (cur) {
    if (cur->meta.parent_ino == parent && !strcmp(cur->name, name))
      return cur;
    cur = cur->next;
  }

  return NULL;
}

static struct vtfs_ram_node* alloc_node(struct vtfs_ram_storage* storage) {
  struct vtfs_ram_node* node = kmalloc(sizeof(*node), GFP_KERNEL);
  if (!node)
    return NULL;

  node->next = storage->nodes_head;
  storage->nodes_head = node;
  return node;
}

static void free_all_nodes(struct vtfs_ram_storage* storage) {
  struct vtfs_ram_node* cur = storage->nodes_head;
  while (cur) {
    struct vtfs_ram_node* next = cur->next;
    kfree(cur);
    cur = next;
  }
  storage->nodes_head = NULL;
  storage->next_ino = VTFS_ROOT_INO + 1;
}

int vtfs_ram_storage_init(struct super_block* sb, const char* token) {
  struct vtfs_ram_storage* storage = kzalloc(sizeof(*storage), GFP_KERNEL);
  if (!storage)
    return -ENOMEM;

  storage->nodes_head = NULL;
  storage->next_ino = VTFS_ROOT_INO + 1;

  struct vtfs_ram_node* root = alloc_node(storage);
  if (!root) {
    kfree(storage);
    return -ENOMEM;
  }

  root->meta.ino = VTFS_ROOT_INO;
  root->meta.parent_ino = 0;
  root->meta.type = VTFS_NODE_DIR;
  root->meta.mode = S_IFDIR | 0777;
  root->meta.size = 0;
  root->name[0] = '\0';

  sb->s_fs_info = storage;
  return 0;
}

void vtfs_ram_storage_shutdown(struct super_block* sb) {
  struct vtfs_ram_storage* storage = get_storage(sb);
  if (!storage)
    return;

  free_all_nodes(storage);
  kfree(storage);
  sb->s_fs_info = NULL;
}

int vtfs_ram_storage_get_root(struct super_block* sb, struct vtfs_node_meta* out) {
  struct vtfs_ram_storage* storage = get_storage(sb);
  if (!storage)
    return -EINVAL;

  struct vtfs_ram_node* root = find_node_by_ino(storage, VTFS_ROOT_INO);
  if (!root)
    return -ENOENT;

  *out = root->meta;
  return 0;
}

int vtfs_ram_storage_lookup(
    struct super_block* sb, vtfs_ino_t parent, const char* name, struct vtfs_node_meta* out
) {
  struct vtfs_ram_storage* storage = get_storage(sb);
  if (!storage)
    return -EINVAL;

  struct vtfs_ram_node* node = find_child(storage, parent, name);
  if (!node)
    return -ENOENT;

  *out = node->meta;
  return 0;
}

int vtfs_ram_storage_iterate_dir(
    struct super_block* sb, vtfs_ino_t dir_ino, unsigned long* offset, struct vtfs_dirent* out
) {
  struct vtfs_ram_storage* storage = get_storage(sb);
  if (!storage)
    return -EINVAL;

  unsigned long count = 0;
  struct vtfs_ram_node* cur = storage->nodes_head;

  while (cur) {
    if (cur->meta.parent_ino == dir_ino) {
      if (count == *offset) {
        strncpy(out->name, cur->name, NAME_MAX);
        out->name[NAME_MAX] = '\0';
        out->ino = cur->meta.ino;
        out->type = cur->meta.type;

        (*offset)++;
        return 0;
      }
      count++;
    }
    cur = cur->next;
  }

  return -ENOENT;
}

int vtfs_ram_storage_create_file(
    struct super_block* sb,
    vtfs_ino_t parent,
    const char* name,
    umode_t mode,
    struct vtfs_node_meta* out
) {
  struct vtfs_ram_storage* storage = get_storage(sb);
  if (!storage)
    return -EINVAL;

  if (find_child(storage, parent, name))
    return -EEXIST;

  struct vtfs_ram_node* parent_node = find_node_by_ino(storage, parent);
  if (!parent_node || parent_node->meta.type != VTFS_NODE_DIR)
    return -ENOTDIR;

  struct vtfs_ram_node* node = alloc_node(storage);
  if (!node)
    return -ENOMEM;

  node->meta.ino = storage->next_ino++;
  node->meta.parent_ino = parent;
  node->meta.type = VTFS_NODE_FILE;
  node->meta.mode = S_IFREG | (mode & 0777);
  node->meta.size = 0;

  strncpy(node->name, name, NAME_MAX);
  node->name[NAME_MAX] = '\0';

  *out = node->meta;
  return 0;
}

int vtfs_ram_storage_unlink(struct super_block* sb, vtfs_ino_t parent, const char* name) {
  struct vtfs_ram_storage* storage = get_storage(sb);
  if (!storage)
    return -EINVAL;

  struct vtfs_ram_node* prev = NULL;
  struct vtfs_ram_node* cur = storage->nodes_head;

  while (cur) {
    if (cur->meta.parent_ino == parent && !strcmp(cur->name, name)) {
      if (cur->meta.type != VTFS_NODE_FILE)
        return -EPERM;

      if (prev)
        prev->next = cur->next;
      else
        storage->nodes_head = cur->next;

      kfree(cur);
      return 0;
    }
    prev = cur;
    cur = cur->next;
  }

  return -ENOENT;
}

int vtfs_ram_storage_mkdir(
    struct super_block* sb,
    vtfs_ino_t parent,
    const char* name,
    umode_t mode,
    struct vtfs_node_meta* out
) {
  struct vtfs_ram_storage* storage = get_storage(sb);
  if (!storage)
    return -EINVAL;

  if (find_child(storage, parent, name))
    return -EEXIST;

  struct vtfs_ram_node* parent_node = find_node_by_ino(storage, parent);
  if (!parent_node || parent_node->meta.type != VTFS_NODE_DIR)
    return -ENOTDIR;

  struct vtfs_ram_node* node = alloc_node(storage);
  if (!node)
    return -ENOMEM;

  node->meta.ino = storage->next_ino++;
  node->meta.parent_ino = parent;
  node->meta.type = VTFS_NODE_DIR;
  node->meta.mode = S_IFDIR | (mode & 0777);
  node->meta.size = 0;

  strncpy(node->name, name, NAME_MAX);
  node->name[NAME_MAX] = '\0';

  *out = node->meta;
  return 0;
}

int vtfs_ram_storage_rmdir(struct super_block* sb, vtfs_ino_t parent, const char* name) {
  struct vtfs_ram_storage* storage = get_storage(sb);
  if (!storage)
    return -EINVAL;

  struct vtfs_ram_node* dir_node = find_child(storage, parent, name);
  if (!dir_node)
    return -ENOENT;

  if (dir_node->meta.type != VTFS_NODE_DIR)
    return -ENOTDIR;

  // Check that directory is empty
  struct vtfs_ram_node* cur = storage->nodes_head;
  while (cur) {
    if (cur->meta.parent_ino == dir_node->meta.ino)
      return -ENOTEMPTY;
    cur = cur->next;
  }

  // Delete directory
  struct vtfs_ram_node* prev = NULL;
  cur = storage->nodes_head;
  while (cur) {
    if (cur == dir_node) {
      if (prev)
        prev->next = cur->next;
      else
        storage->nodes_head = cur->next;
      kfree(cur);
      return 0;
    }
    prev = cur;
    cur = cur->next;
  }

  return -ENOENT;
}

// Ops struct
static const struct vtfs_storage_ops ram_storage_ops = {
    .init = vtfs_ram_storage_init,
    .shutdown = vtfs_ram_storage_shutdown,
    .get_root = vtfs_ram_storage_get_root,
    .lookup = vtfs_ram_storage_lookup,
    .iterate_dir = vtfs_ram_storage_iterate_dir,
    .create_file = vtfs_ram_storage_create_file,
    .unlink = vtfs_ram_storage_unlink,
    .mkdir = vtfs_ram_storage_mkdir,
    .rmdir = vtfs_ram_storage_rmdir,
};

const struct vtfs_storage_ops* vtfs_get_ram_storage_ops(void) {
  return &ram_storage_ops;
}
