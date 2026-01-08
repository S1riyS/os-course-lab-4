#ifndef _VTFS_INTERFACE_H
#define _VTFS_INTERFACE_H

#include <linux/fs.h>
#include <linux/limits.h>

#define VTFS_ROOT_INO 1000

typedef ino_t vtfs_ino_t;

enum vtfs_node_type {
  VTFS_NODE_DIR,
  VTFS_NODE_FILE,
};

struct vtfs_node_meta {
  vtfs_ino_t ino;
  vtfs_ino_t parent_ino;
  enum vtfs_node_type type;
  umode_t mode;
  loff_t size;
};

struct vtfs_dirent {
  char name[NAME_MAX + 1];
  vtfs_ino_t ino;
  enum vtfs_node_type type;
};

struct vtfs_storage_ops {
  int (*init)(struct super_block* sb, const char* token);
  void (*shutdown)(struct super_block* sb);
  int (*get_root)(struct super_block* sb, struct vtfs_node_meta* out);
  int (*lookup)(
      struct super_block* sb, vtfs_ino_t parent, const char* name, struct vtfs_node_meta* out
  );
  int (*iterate_dir)(
      struct super_block* sb, vtfs_ino_t dir_ino, unsigned long* offset, struct vtfs_dirent* out
  );
  int (*create_file)(
      struct super_block* sb,
      vtfs_ino_t parent,
      const char* name,
      umode_t mode,
      struct vtfs_node_meta* out
  );
  int (*unlink)(struct super_block* sb, vtfs_ino_t parent, const char* name);
};

// Implementation getters
extern const struct vtfs_storage_ops* vtfs_get_ram_storage_ops(void);
extern const struct vtfs_storage_ops* vtfs_get_net_storage_ops(void);

#endif  // _VTFS_INTERFACE_H
