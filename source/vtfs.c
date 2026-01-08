#include "vtfs.h"

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/mnt_idmapping.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/string.h>

#include "vtfs_interface.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("S1riyS");
MODULE_DESCRIPTION("A simple FS kernel module");

// Module params
static char* storage_type = "ram";
module_param(storage_type, charp, 0644);
MODULE_PARM_DESC(storage_type, "Storage type: 'ram' or 'net' (default: ram)");

static const struct vtfs_storage_ops* storage_ops = NULL;

struct inode_operations vtfs_inode_ops = {
    .lookup = vtfs_lookup,
    .create = vtfs_create,
    .unlink = vtfs_unlink,
};

struct file_operations vtfs_dir_ops = {
    .iterate_shared = vtfs_iterate,
};

struct file_operations vtfs_file_ops = {
    // read and write ops will be added im part 8
};

static int __init vtfs_init(void) {
  // Select implementation
  if (strcmp(storage_type, "net") == 0) {
    storage_ops = vtfs_get_net_storage_ops();
    LOG("VTFS joined the kernel (using NET storage)\n");
  } else {
    storage_ops = vtfs_get_ram_storage_ops();
    LOG("VTFS joined the kernel (using RAM storage)\n");
  }

  if (!storage_ops) {
    LOG("Failed to get storage operations\n");
    return -EINVAL;
  }

  int ret = register_filesystem(&vtfs_fs_type);
  if (ret) {
    LOG("Failed to register filesystem: %d\n", ret);
  }
  return ret;
}

static void __exit vtfs_exit(void) {
  unregister_filesystem(&vtfs_fs_type);
  LOG("VTFS left the kernel\n");
}

struct file_system_type vtfs_fs_type = {
    .name = "vtfs",
    .mount = vtfs_mount,
    .kill_sb = vtfs_kill_sb,
};

struct dentry* vtfs_mount(
    struct file_system_type* fs_type, int flags, const char* token, void* data
) {
  struct dentry* ret = mount_nodev(fs_type, flags, data, vtfs_fill_super);
  if (ret == NULL) {
    printk(KERN_ERR "[vtfs] Can't mount file system\n");
  } else {
    printk(KERN_INFO "[vtfs] Mounted successfully\n");
  }
  return ret;
}

int vtfs_fill_super(struct super_block* sb, void* data, int silent) {
  const char* token = (const char*)data;

  int ret = storage_ops->init(sb, token);
  if (ret) {
    printk(KERN_ERR "[vtfs] Failed to init storage: %d\n", ret);
    return ret;
  }

  struct vtfs_node_meta root_meta;
  ret = storage_ops->get_root(sb, &root_meta);
  if (ret) {
    storage_ops->shutdown(sb);
    printk(KERN_ERR "[vtfs] Failed to get root: %d\n", ret);
    return ret;
  }

  struct inode* inode = vtfs_get_inode(sb, NULL, root_meta.mode, root_meta.ino);
  if (!inode) {
    storage_ops->shutdown(sb);
    return -ENOMEM;
  }

  inode->i_op = &vtfs_inode_ops;
  inode->i_fop = &vtfs_dir_ops;

  sb->s_root = d_make_root(inode);
  if (sb->s_root == NULL) {
    iput(inode);
    storage_ops->shutdown(sb);
    return -ENOMEM;
  }

  printk(KERN_INFO "[vtfs] Super block filled successfully\n");
  return 0;
}

struct inode* vtfs_get_inode(
    struct super_block* sb, const struct inode* dir, umode_t mode, int i_ino
) {
  struct inode* inode = new_inode(sb);
  if (inode != NULL) {
    inode_init_owner(&nop_mnt_idmap, inode, dir, mode);
    inode->i_mode = mode | 0777;
  }
  inode->i_ino = i_ino;
  return inode;
}

void vtfs_kill_sb(struct super_block* sb) {
  storage_ops->shutdown(sb);
  printk(KERN_INFO "[vtfs] Super block destroyed. Unmount successfully.\n");
}

struct dentry* vtfs_lookup(
    struct inode* parent_inode, struct dentry* child_dentry, unsigned int flag
) {
  struct vtfs_node_meta meta;
  int ret = storage_ops->lookup(
      parent_inode->i_sb, parent_inode->i_ino, child_dentry->d_name.name, &meta
  );

  if (ret == 0) {
    umode_t mode = meta.mode;
    if (meta.type == VTFS_NODE_DIR) {
      mode |= S_IFDIR;
    } else {
      mode |= S_IFREG;
    }

    struct inode* inode = vtfs_get_inode(parent_inode->i_sb, NULL, mode, meta.ino);
    if (inode) {
      if (meta.type == VTFS_NODE_DIR) {
        inode->i_op = &vtfs_inode_ops;
        inode->i_fop = &vtfs_dir_ops;
      } else {
        inode->i_op = &vtfs_inode_ops;
        inode->i_fop = &vtfs_file_ops;
      }
      d_add(child_dentry, inode);
    }
  }

  return NULL;
}

int vtfs_iterate(struct file* filp, struct dir_context* ctx) {
  struct dentry* dentry = filp->f_path.dentry;
  struct inode* inode = dentry->d_inode;
  unsigned long offset = filp->f_pos;
  ino_t dir_ino = inode->i_ino;

  // Handle "." and ".."
  if (offset == 0) {
    if (!dir_emit(ctx, ".", 1, dir_ino, DT_DIR))
      return 0;
    ctx->pos++;
    filp->f_pos = ctx->pos;
    return 0;
  }
  if (offset == 1) {
    ino_t parent_ino = dentry->d_parent->d_inode->i_ino;
    if (!dir_emit(ctx, "..", 2, parent_ino, DT_DIR))
      return 0;
    ctx->pos++;
    filp->f_pos = ctx->pos;
    return 0;
  }

  // Handle real files
  unsigned long storage_offset = offset - 2;
  struct vtfs_dirent dirent;

  int ret = storage_ops->iterate_dir(inode->i_sb, dir_ino, &storage_offset, &dirent);

  if (ret == 0) {
    unsigned char d_type = (dirent.type == VTFS_NODE_DIR) ? DT_DIR : DT_REG;
    if (!dir_emit(ctx, dirent.name, strlen(dirent.name), dirent.ino, d_type))
      return 0;

    ctx->pos++;
    filp->f_pos = ctx->pos;
    return 0;
  }

  return 0;
}

int vtfs_create(
    struct mnt_idmap* idmap,
    struct inode* parent_inode,
    struct dentry* child_dentry,
    umode_t mode,
    bool b
) {
  struct vtfs_node_meta meta;
  int ret = storage_ops->create_file(
      parent_inode->i_sb, parent_inode->i_ino, child_dentry->d_name.name, mode, &meta
  );

  if (ret)
    return ret;

  struct inode* inode = vtfs_get_inode(parent_inode->i_sb, NULL, meta.mode, meta.ino);
  if (!inode)
    return -ENOMEM;

  inode->i_op = &vtfs_inode_ops;
  inode->i_fop = &vtfs_file_ops;

  d_add(child_dentry, inode);
  return 0;
}

int vtfs_unlink(struct inode* parent_inode, struct dentry* child_dentry) {
  return storage_ops->unlink(parent_inode->i_sb, parent_inode->i_ino, child_dentry->d_name.name);
}

module_init(vtfs_init);
module_exit(vtfs_exit);