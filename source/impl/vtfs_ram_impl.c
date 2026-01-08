#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "../vtfs.h"
#include "../vtfs_interface.h"

struct vtfs_ram_inode_payload {
  struct vtfs_node_meta meta;
  char* data;
  size_t capacity;
  unsigned int ref_count;
};

struct vtfs_ram_node {
  char name[NAME_MAX + 1];
  struct vtfs_ram_node* next;
  vtfs_ino_t parent_ino;
  struct vtfs_ram_inode_payload* payload;
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
    if (cur->payload && cur->payload->meta.ino == ino)
      return cur;
    cur = cur->next;
  }

  return NULL;
}

static struct vtfs_ram_inode_payload* find_payload_by_ino(
    struct vtfs_ram_storage* storage, vtfs_ino_t ino
) {
  struct vtfs_ram_node* node = find_node_by_ino(storage, ino);
  if (node && node->payload)
    return node->payload;
  return NULL;
}

static struct vtfs_ram_node* find_child(
    struct vtfs_ram_storage* storage, vtfs_ino_t parent, const char* name
) {
  struct vtfs_ram_node* cur = storage->nodes_head;

  while (cur) {
    if (cur->parent_ino == parent && !strcmp(cur->name, name))
      return cur;
    cur = cur->next;
  }

  return NULL;
}

static unsigned int count_links_to_ino(struct vtfs_ram_storage* storage, vtfs_ino_t ino) {
  struct vtfs_ram_inode_payload* payload = find_payload_by_ino(storage, ino);
  if (payload)
    return payload->ref_count;
  return 0;
}

static struct vtfs_ram_inode_payload* alloc_payload(void) {
  struct vtfs_ram_inode_payload* payload = kzalloc(sizeof(*payload), GFP_KERNEL);
  if (payload) {
    payload->ref_count = 1;
    payload->data = NULL;
    payload->capacity = 0;
  }
  return payload;
}

// Increment reference count for payload
static void payload_get(struct vtfs_ram_inode_payload* payload) {
  if (payload)
    payload->ref_count++;
}

// Decrement reference count and free if zero
static void payload_put(struct vtfs_ram_inode_payload* payload) {
  if (!payload)
    return;

  payload->ref_count--;
  if (payload->ref_count == 0) {
    if (payload->data) {
      kfree(payload->data);
    }
    kfree(payload);
  }
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
    if (cur->payload) {
      payload_put(cur->payload);
    }
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

  struct vtfs_ram_inode_payload* root_payload = alloc_payload();
  if (!root_payload) {
    kfree(root);
    kfree(storage);
    return -ENOMEM;
  }

  root_payload->meta.ino = VTFS_ROOT_INO;
  root_payload->meta.parent_ino = 0;
  root_payload->meta.type = VTFS_NODE_DIR;
  root_payload->meta.mode = S_IFDIR | 0777;
  root_payload->meta.size = 0;

  root->parent_ino = 0;
  root->name[0] = '\0';
  root->payload = root_payload;

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
  if (!root || !root->payload)
    return -ENOENT;

  *out = root->payload->meta;
  return 0;
}

int vtfs_ram_storage_lookup(
    struct super_block* sb, vtfs_ino_t parent, const char* name, struct vtfs_node_meta* out
) {
  struct vtfs_ram_storage* storage = get_storage(sb);
  if (!storage)
    return -EINVAL;

  struct vtfs_ram_node* node = find_child(storage, parent, name);
  if (!node || !node->payload)
    return -ENOENT;

  *out = node->payload->meta;
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
    if (cur->parent_ino == dir_ino && cur->payload) {
      if (count == *offset) {
        strncpy(out->name, cur->name, NAME_MAX);
        out->name[NAME_MAX] = '\0';
        out->ino = cur->payload->meta.ino;
        out->type = cur->payload->meta.type;

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
  if (!parent_node || !parent_node->payload || parent_node->payload->meta.type != VTFS_NODE_DIR)
    return -ENOTDIR;

  struct vtfs_ram_inode_payload* payload = alloc_payload();
  if (!payload)
    return -ENOMEM;

  struct vtfs_ram_node* node = alloc_node(storage);
  if (!node) {
    payload_put(payload);
    return -ENOMEM;
  }

  payload->meta.ino = storage->next_ino++;
  payload->meta.parent_ino = parent;
  payload->meta.type = VTFS_NODE_FILE;
  payload->meta.mode = S_IFREG | (mode & 0777);
  payload->meta.size = 0;

  node->parent_ino = parent;
  strncpy(node->name, name, NAME_MAX);
  node->name[NAME_MAX] = '\0';
  node->payload = payload;

  *out = payload->meta;
  return 0;
}

int vtfs_ram_storage_unlink(struct super_block* sb, vtfs_ino_t parent, const char* name) {
  struct vtfs_ram_storage* storage = get_storage(sb);
  if (!storage)
    return -EINVAL;

  struct vtfs_ram_node* prev = NULL;
  struct vtfs_ram_node* cur = storage->nodes_head;

  while (cur) {
    if (cur->parent_ino == parent && !strcmp(cur->name, name)) {
      if (!cur->payload || cur->payload->meta.type != VTFS_NODE_FILE)
        return -EPERM;

      struct vtfs_ram_inode_payload* payload = cur->payload;

      // Remove the node from the list
      if (prev)
        prev->next = cur->next;
      else
        storage->nodes_head = cur->next;

      // Decrement reference count
      payload_put(payload);

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
  if (!parent_node || !parent_node->payload || parent_node->payload->meta.type != VTFS_NODE_DIR)
    return -ENOTDIR;

  struct vtfs_ram_inode_payload* payload = alloc_payload();
  if (!payload)
    return -ENOMEM;

  struct vtfs_ram_node* node = alloc_node(storage);
  if (!node) {
    payload_put(payload);
    return -ENOMEM;
  }

  payload->meta.ino = storage->next_ino++;
  payload->meta.parent_ino = parent;
  payload->meta.type = VTFS_NODE_DIR;
  payload->meta.mode = S_IFDIR | (mode & 0777);
  payload->meta.size = 0;

  node->parent_ino = parent;
  strncpy(node->name, name, NAME_MAX);
  node->name[NAME_MAX] = '\0';
  node->payload = payload;

  *out = payload->meta;
  return 0;
}

int vtfs_ram_storage_rmdir(struct super_block* sb, vtfs_ino_t parent, const char* name) {
  struct vtfs_ram_storage* storage = get_storage(sb);
  if (!storage)
    return -EINVAL;

  struct vtfs_ram_node* dir_node = find_child(storage, parent, name);
  if (!dir_node || !dir_node->payload)
    return -ENOENT;

  if (dir_node->payload->meta.type != VTFS_NODE_DIR)
    return -ENOTDIR;

  // Check that directory is empty
  struct vtfs_ram_node* cur = storage->nodes_head;
  while (cur) {
    if (cur->parent_ino == dir_node->payload->meta.ino)
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
      payload_put(cur->payload);
      kfree(cur);
      return 0;
    }
    prev = cur;
    cur = cur->next;
  }

  return -ENOENT;
}

ssize_t vtfs_ram_storage_read(
    struct super_block* sb, vtfs_ino_t ino, char* buffer, size_t len, loff_t* offset
) {
  struct vtfs_ram_storage* storage = get_storage(sb);
  if (!storage)
    return -EINVAL;

  struct vtfs_ram_inode_payload* payload = find_payload_by_ino(storage, ino);
  if (!payload)
    return -ENOENT;

  if (payload->meta.type != VTFS_NODE_FILE)
    return -EISDIR;

  int ret = vtfs_validate_io_params(*offset, len, NULL);
  if (ret)
    return ret;

  if (*offset >= payload->meta.size)
    return 0;  // EOF

  // Calculate how much we can read
  size_t available = payload->meta.size - *offset;
  size_t to_read = (len < available) ? len : available;

  if (to_read == 0)
    return 0;

  // Copy data from kernel space to user space
  if (copy_to_user(buffer, payload->data + *offset, to_read))
    return -EFAULT;

  *offset += to_read;
  return to_read;
}

ssize_t vtfs_ram_storage_write(
    struct super_block* sb, vtfs_ino_t ino, const char* buffer, size_t len, loff_t* offset
) {
  struct vtfs_ram_storage* storage = get_storage(sb);
  if (!storage)
    return -EINVAL;

  struct vtfs_ram_inode_payload* payload = find_payload_by_ino(storage, ino);
  if (!payload)
    return -ENOENT;

  if (payload->meta.type != VTFS_NODE_FILE)
    return -EISDIR;

  loff_t new_size;
  int ret = vtfs_validate_io_params(*offset, len, &new_size);
  if (ret)
    return ret;

  // Allocate or reallocate data buffer if needed
  if (new_size > payload->capacity) {
    size_t new_capacity = new_size * 2;
    if (new_capacity < 1024)
      new_capacity = 1024;  // Minimum 1KB

    char* new_data = krealloc(payload->data, new_capacity, GFP_KERNEL);
    if (!new_data)
      return -ENOMEM;

    if (payload->capacity > 0) {
      memset(new_data + payload->capacity, 0, new_capacity - payload->capacity);
    } else {
      memset(new_data, 0, new_capacity);
    }

    payload->data = new_data;
    payload->capacity = new_capacity;
  }

  // Copy data from user space to kernel space
  if (copy_from_user(payload->data + *offset, buffer, len))
    return -EFAULT;

  if (new_size > payload->meta.size) {
    payload->meta.size = new_size;
  }

  *offset += len;
  return len;
}

int vtfs_ram_storage_link(
    struct super_block* sb, vtfs_ino_t target_ino, vtfs_ino_t parent, const char* name
) {
  struct vtfs_ram_storage* storage = get_storage(sb);
  if (!storage)
    return -EINVAL;

  struct vtfs_ram_node* target_node = find_node_by_ino(storage, target_ino);
  if (!target_node || !target_node->payload)
    return -ENOENT;

  if (target_node->payload->meta.type != VTFS_NODE_FILE)
    return -EPERM;  // Hard links only for files

  struct vtfs_ram_node* parent_node = find_node_by_ino(storage, parent);
  if (!parent_node || !parent_node->payload || parent_node->payload->meta.type != VTFS_NODE_DIR)
    return -ENOTDIR;

  if (find_child(storage, parent, name))
    return -EEXIST;

  struct vtfs_ram_node* node = alloc_node(storage);
  if (!node)
    return -ENOMEM;

  // Increment reference count
  payload_get(target_node->payload);

  node->parent_ino = parent;
  strncpy(node->name, name, NAME_MAX);
  node->name[NAME_MAX] = '\0';
  node->payload = target_node->payload;  // Share the same payload

  return 0;
}

unsigned int vtfs_ram_storage_count_links(struct super_block* sb, vtfs_ino_t ino) {
  struct vtfs_ram_storage* storage = get_storage(sb);
  if (!storage)
    return 0;

  return count_links_to_ino(storage, ino);
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
    .read = vtfs_ram_storage_read,
    .write = vtfs_ram_storage_write,
    .link = vtfs_ram_storage_link,
    ._count_links = vtfs_ram_storage_count_links,
};

const struct vtfs_storage_ops* vtfs_get_ram_storage_ops(void) {
  return &ram_storage_ops;
}
