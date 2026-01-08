#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "../vtfs_interface.h"

static int vtfs_net_storage_init(struct super_block* sb, const char* token) {
  // TODO: implement
  return -ENOSYS;
}

static void vtfs_net_storage_shutdown(struct super_block* sb) {
  // TODO: implement
}

static int vtfs_net_storage_get_root(struct super_block* sb, struct vtfs_node_meta* out) {
  // TODO: implement
  return -ENOSYS;
}

static int vtfs_net_storage_lookup(
    struct super_block* sb, vtfs_ino_t parent, const char* name, struct vtfs_node_meta* out
) {
  // TODO: implement
  return -ENOSYS;
}

static int vtfs_net_storage_iterate_dir(
    struct super_block* sb, vtfs_ino_t dir_ino, unsigned long* offset, struct vtfs_dirent* out
) {
  // TODO: implement
  return -ENOSYS;
}

static int vtfs_net_storage_create_file(
    struct super_block* sb,
    vtfs_ino_t parent,
    const char* name,
    umode_t mode,
    struct vtfs_node_meta* out
) {
  // TODO: implement
  return -ENOSYS;
}

static int vtfs_net_storage_unlink(struct super_block* sb, vtfs_ino_t parent, const char* name) {
  // TODO: implement
  return -ENOSYS;
}

static int vtfs_net_storage_create_dir(
    struct super_block* sb,
    vtfs_ino_t parent,
    const char* name,
    umode_t mode,
    struct vtfs_node_meta* out
) {
  // TODO: implement
  return -ENOSYS;
}

static int vtfs_net_storage_rmdir(struct super_block* sb, vtfs_ino_t parent, const char* name) {
  // TODO: implement
  return -ENOSYS;
}

static ssize_t vtfs_net_storage_read(
    struct super_block* sb, vtfs_ino_t ino, char* buffer, size_t len, loff_t* offset
) {
  // TODO: implement
  return -ENOSYS;
}

static ssize_t vtfs_net_storage_write(
    struct super_block* sb, vtfs_ino_t ino, const char* buffer, size_t len, loff_t* offset
) {
  // TODO: implement
  return -ENOSYS;
}

static int vtfs_net_storage_link(
    struct super_block* sb, vtfs_ino_t target_ino, vtfs_ino_t parent, const char* name
) {
  // TODO: implement
  return -ENOSYS;
}

static unsigned int vtfs_net_storage_count_links(struct super_block* sb, vtfs_ino_t ino) {
  // TODO: implement
  return 1;
}

// Ops struct
static const struct vtfs_storage_ops net_storage_ops = {
    .init = vtfs_net_storage_init,
    .shutdown = vtfs_net_storage_shutdown,
    .get_root = vtfs_net_storage_get_root,
    .lookup = vtfs_net_storage_lookup,
    .iterate_dir = vtfs_net_storage_iterate_dir,
    .create_file = vtfs_net_storage_create_file,
    .unlink = vtfs_net_storage_unlink,
    .mkdir = vtfs_net_storage_create_dir,
    .rmdir = vtfs_net_storage_rmdir,
    .read = vtfs_net_storage_read,
    .write = vtfs_net_storage_write,
    .link = vtfs_net_storage_link,
    ._count_links = vtfs_net_storage_count_links,
};

const struct vtfs_storage_ops* vtfs_get_net_storage_ops(void) {
  return &net_storage_ops;
}
