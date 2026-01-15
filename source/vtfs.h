#ifndef _VTFS_H
#define _VTFS_H

#include <linux/fs.h>

#define MODULE_NAME "vtfs"
#define LOG(fmt, ...) pr_info("[" MODULE_NAME "]: " fmt, ##__VA_ARGS__)

extern struct file_system_type vtfs_fs_type;
extern struct inode_operations vtfs_inode_ops;
extern struct file_operations vtfs_dir_ops;

// Inode ops
struct dentry* vtfs_lookup(
    struct inode* parent_inode, struct dentry* child_dentry, unsigned int flag
);
int vtfs_create(
    struct mnt_idmap* idmap,
    struct inode* parent_inode,
    struct dentry* child_dentry,
    umode_t mode,
    bool b
);
int vtfs_unlink(struct inode* parent_inode, struct dentry* child_dentry);
struct dentry* vtfs_mkdir(
    struct mnt_idmap* idmap, struct inode* parent_inode, struct dentry* child_dentry, umode_t mode
);
int vtfs_rmdir(struct inode* parent_inode, struct dentry* child_dentry);
int vtfs_link(
    struct dentry* old_dentry, struct inode* parent_dir, struct dentry* new_dentry
);

// Dir ops
int vtfs_iterate(struct file* filp, struct dir_context* ctx);

// File ops
ssize_t vtfs_read(struct file* filp, char __user* buffer, size_t len, loff_t* offset);
ssize_t vtfs_write(struct file* filp, const char __user* buffer, size_t len, loff_t* offset);
loff_t vtfs_llseek(struct file *filp, loff_t offset, int whence);

// Mount
struct dentry* vtfs_mount(
    struct file_system_type* fs_type, int flags, const char* token, void* data
);
int vtfs_fill_super(struct super_block* sb, void* data, int silent);
void vtfs_kill_sb(struct super_block* sb);

// Utility
struct inode* vtfs_get_inode(
    struct super_block* sb, const struct inode* dir, umode_t mode, int i_ino
);

// Helper functions for I/O operations
int vtfs_validate_io_params(loff_t offset, size_t len, loff_t* new_size_out);
void vtfs_update_inode_size(struct inode* inode, loff_t new_size);

#endif