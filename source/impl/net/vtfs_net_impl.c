#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/byteorder/generic.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <asm/byteorder.h>

#include "../../vtfs_interface.h"
#include "../../http.h"
#include "base64.h"
#include "decode.h"

#define MAX_TOKEN_LEN 256

struct vtfs_net_storage {
  char token[MAX_TOKEN_LEN];
};

static struct vtfs_net_storage* get_storage(struct super_block* sb) {
  return (struct vtfs_net_storage*)sb->s_fs_info;
}

static int vtfs_net_storage_init(struct super_block* sb, const char* token) {
  if (!token) {
    printk(KERN_WARNING "[vtfs_net] Token is NULL, using default: REMOUNT\n");
    token = "REMOUNT";
  }

  struct vtfs_net_storage* storage = kzalloc(sizeof(*storage), GFP_KERNEL);
  if (!storage) {
    printk(KERN_ERR "[vtfs_net] Failed to allocate storage\n");
    return -ENOMEM;
  }

  size_t token_len = strlen(token);
  if (token_len >= MAX_TOKEN_LEN) {
    printk(KERN_ERR "[vtfs_net] Token too long: %zu (max %d)\n", token_len, MAX_TOKEN_LEN - 1);
    kfree(storage);
    return -EINVAL;
  }

  strncpy(storage->token, token, MAX_TOKEN_LEN - 1);
  storage->token[MAX_TOKEN_LEN - 1] = '\0';

  sb->s_fs_info = storage;

  char response_buffer[1024];
  memset(response_buffer, 0, sizeof(response_buffer));
  
  int64_t result = vtfs_http_call(storage->token, "init", response_buffer, sizeof(response_buffer), NULL, 0);

  if (result != 0) {
    // File system alrady exists, but is's okay
    if (result == EEXIST) {
      printk(KERN_INFO "[vtfs_net] Filesystem already exists on server (token: %s), continuing\n", storage->token);
    } else {
      printk(KERN_ERR "[vtfs_net] Server init failed with code: %lld\n", (long long)result);
      kfree(storage);
      sb->s_fs_info = NULL;
      
      if (result > 0) {
        return (int)result;
      }
      return (int)result;
    }
  }

  printk(KERN_INFO "[vtfs_net] Storage initialized with token: %s\n", storage->token);
  return 0;
}

static void vtfs_net_storage_shutdown(struct super_block* sb) {
  struct vtfs_net_storage* storage = get_storage(sb);
  if (storage) {
    kfree(storage);
    sb->s_fs_info = NULL;
  }
}

static int vtfs_net_storage_get_root(struct super_block* sb, struct vtfs_node_meta* out) {
  struct vtfs_net_storage* storage = get_storage(sb);
  if (!storage) {
    printk(KERN_ERR "[vtfs_net] Storage not initialized\n");
    return -EINVAL;
  }

  char response_buffer[1024];
  memset(response_buffer, 0, sizeof(response_buffer));
  
  int64_t result = vtfs_http_call(storage->token, "get_root", response_buffer, sizeof(response_buffer), NULL, 0);

  if (result != 0) {
    printk(KERN_ERR "[vtfs_net] Server get_root failed with code: %lld\n", (long long)result);
    if (result > 0) {
      return (int)result;
    }
    return (int)result;
  }

  int parse_result = parse_node_meta(response_buffer, out);
  if (parse_result != 0) {
    printk(KERN_ERR "[vtfs_net] Failed to parse NodeMeta from response: %d\n", parse_result);
    return parse_result;
  }

  return 0;
}

static int vtfs_net_storage_lookup(
    struct super_block* sb, vtfs_ino_t parent, const char* name, struct vtfs_node_meta* out
) {
  struct vtfs_net_storage* storage = get_storage(sb);
  if (!storage) {
    printk(KERN_ERR "[vtfs_net] Storage not initialized\n");
    return -EINVAL;
  }

  if (!name) {
    printk(KERN_ERR "[vtfs_net] Name is NULL\n");
    return -EINVAL;
  }

  char encoded_name[NAME_MAX * 3 + 1];
  char parent_str[32];
  encode(name, encoded_name);
  snprintf(parent_str, sizeof(parent_str), "%llu", (unsigned long long)parent);

  char response_buffer[1024];
  memset(response_buffer, 0, sizeof(response_buffer));
  
  int64_t result = vtfs_http_call(
      storage->token, 
      "lookup", 
      response_buffer, 
      sizeof(response_buffer),
      NULL,
      2,  // 2 args
      "parent", parent_str,
      "name", encoded_name
  );

  if (result != 0) {
    if (result == ENOENT) {
      return -ENOENT;
    }
    printk(KERN_ERR "[vtfs_net] Server lookup failed with code: %lld\n", (long long)result);
    if (result > 0) {
      return (int)result;
    }
    return (int)result;
  }

  int parse_result = parse_node_meta(response_buffer, out);
  if (parse_result != 0) {
    printk(KERN_ERR "[vtfs_net] Failed to parse NodeMeta from lookup response: %d\n", parse_result);
    return parse_result;
  }

  return 0;
}

static int vtfs_net_storage_iterate_dir(
    struct super_block* sb, vtfs_ino_t dir_ino, unsigned long* offset, struct vtfs_dirent* out
) {
  struct vtfs_net_storage* storage = get_storage(sb);
  if (!storage) {
    printk(KERN_ERR "[vtfs_net] Storage not initialized\n");
    return -EINVAL;
  }

  if (!offset || !out) {
    printk(KERN_ERR "[vtfs_net] Invalid arguments: offset or out is NULL\n");
    return -EINVAL;
  }

  char dir_ino_str[32];
  char offset_str[32];
  snprintf(dir_ino_str, sizeof(dir_ino_str), "%llu", (unsigned long long)dir_ino);
  snprintf(offset_str, sizeof(offset_str), "%lu", *offset);

  char response_buffer[1024];
  memset(response_buffer, 0, sizeof(response_buffer));
  
  int64_t result = vtfs_http_call(
      storage->token,
      "iterate_dir",
      response_buffer,
      sizeof(response_buffer),
      NULL,
      2,  // 2 args
      "dir_ino", dir_ino_str,
      "offset", offset_str
  );

  if (result != 0) {
    if (result == ENOENT) {
      return -ENOENT;
    }
    printk(KERN_ERR "[vtfs_net] Server iterate_dir failed with code: %lld\n", (long long)result);
    if (result > 0) {
      return (int)result;
    }
    return (int)result;
  }

  int parse_result = parse_dirent(response_buffer, out);
  if (parse_result != 0) {
    printk(KERN_ERR "[vtfs_net] Failed to parse Dirent from iterate_dir response: %d\n", parse_result);
    return parse_result;
  }

  (*offset)++;

  return 0;
}

static int vtfs_net_storage_create_file(
    struct super_block* sb,
    vtfs_ino_t parent,
    const char* name,
    umode_t mode,
    struct vtfs_node_meta* out
) {
  struct vtfs_net_storage* storage = get_storage(sb);
  if (!storage) {
    printk(KERN_ERR "[vtfs_net] Storage not initialized\n");
    return -EINVAL;
  }

  if (!name) {
    printk(KERN_ERR "[vtfs_net] Name is NULL\n");
    return -EINVAL;
  }

  umode_t permissions = mode & 0777;

  char encoded_name[NAME_MAX * 3 + 1];
  char parent_str[32];
  char mode_str[32];
  encode(name, encoded_name);
  snprintf(parent_str, sizeof(parent_str), "%llu", (unsigned long long)parent);
  snprintf(mode_str, sizeof(mode_str), "%u", (unsigned int)permissions);

  char response_buffer[1024];
  memset(response_buffer, 0, sizeof(response_buffer));
  
  int64_t result = vtfs_http_call(
      storage->token,
      "create_file",
      response_buffer,
      sizeof(response_buffer),
      NULL,
      3,  // 3 args
      "parent", parent_str,
      "name", encoded_name,
      "mode", mode_str
  );

  if (result != 0) {
    printk(KERN_ERR "[vtfs_net] Server create_file failed with code: %lld\n", (long long)result);
    if (result > 0) {
      return (int)result;
    }
    return (int)result;
  }

  int parse_result = parse_node_meta(response_buffer, out);
  if (parse_result != 0) {
    printk(KERN_ERR "[vtfs_net] Failed to parse NodeMeta from create_file response: %d\n", parse_result);
    return parse_result;
  }

  return 0;
}

static int vtfs_net_storage_unlink(struct super_block* sb, vtfs_ino_t parent, const char* name) {
  struct vtfs_net_storage* storage = get_storage(sb);
  if (!storage) {
    printk(KERN_ERR "[vtfs_net] Storage not initialized\n");
    return -EINVAL;
  }

  if (!name) {
    printk(KERN_ERR "[vtfs_net] Name is NULL\n");
    return -EINVAL;
  }

  char encoded_name[NAME_MAX * 3 + 1];
  char parent_str[32];
  encode(name, encoded_name);
  snprintf(parent_str, sizeof(parent_str), "%llu", (unsigned long long)parent);

  char response_buffer[1024];
  memset(response_buffer, 0, sizeof(response_buffer));
  
  int64_t result = vtfs_http_call(
      storage->token,
      "unlink",
      response_buffer,
      sizeof(response_buffer),
      NULL,
      2,  // 2 args
      "parent", parent_str,
      "name", encoded_name
  );

  if (result != 0) {
    printk(KERN_ERR "[vtfs_net] Server unlink failed with code: %lld\n", (long long)result);
    if (result > 0) {
      return (int)result;
    }
    return (int)result;
  }

  return 0;
}

static int vtfs_net_storage_create_dir(
    struct super_block* sb,
    vtfs_ino_t parent,
    const char* name,
    umode_t mode,
    struct vtfs_node_meta* out
) {
  struct vtfs_net_storage* storage = get_storage(sb);
  if (!storage) {
    printk(KERN_ERR "[vtfs_net] Storage not initialized\n");
    return -EINVAL;
  }

  if (!name) {
    printk(KERN_ERR "[vtfs_net] Name is NULL\n");
    return -EINVAL;
  }

  umode_t permissions = mode & 0777;

  char encoded_name[NAME_MAX * 3 + 1];
  char parent_str[32];
  char mode_str[32];
  encode(name, encoded_name);
  snprintf(parent_str, sizeof(parent_str), "%llu", (unsigned long long)parent);
  snprintf(mode_str, sizeof(mode_str), "%u", (unsigned int)permissions); // Десятичный формат, так как сервер парсит как base 10

  char response_buffer[1024];
  memset(response_buffer, 0, sizeof(response_buffer));
  
  int64_t result = vtfs_http_call(
      storage->token,
      "mkdir",
      response_buffer,
      sizeof(response_buffer),
      NULL,
      3,  // 3 args
      "parent", parent_str,
      "name", encoded_name,
      "mode", mode_str
  );

  if (result != 0) {
    printk(KERN_ERR "[vtfs_net] Server mkdir failed with code: %lld\n", (long long)result);
    if (result > 0) {
      return (int)result;
    }
    return (int)result;
  }

  int parse_result = parse_node_meta(response_buffer, out);
  if (parse_result != 0) {
    printk(KERN_ERR "[vtfs_net] Failed to parse NodeMeta from mkdir response: %d\n", parse_result);
    return parse_result;
  }

  return 0;
}

static int vtfs_net_storage_rmdir(struct super_block* sb, vtfs_ino_t parent, const char* name) {
  struct vtfs_net_storage* storage = get_storage(sb);
  if (!storage) {
    printk(KERN_ERR "[vtfs_net] Storage not initialized\n");
    return -EINVAL;
  }

  if (!name) {
    printk(KERN_ERR "[vtfs_net] Name is NULL\n");
    return -EINVAL;
  }

  char encoded_name[NAME_MAX * 3 + 1];
  char parent_str[32];
  encode(name, encoded_name);
  snprintf(parent_str, sizeof(parent_str), "%llu", (unsigned long long)parent);

  char response_buffer[1024];
  memset(response_buffer, 0, sizeof(response_buffer));
  
  int64_t result = vtfs_http_call(
      storage->token,
      "rmdir",
      response_buffer,
      sizeof(response_buffer),
      NULL,
      2,  // 2 args
      "parent", parent_str,
      "name", encoded_name
  );

  if (result != 0) {
    printk(KERN_ERR "[vtfs_net] Server rmdir failed with code: %lld\n", (long long)result);
    if (result > 0) {
      return (int)result;
    }
    return (int)result;
  }

  return 0;
}

static ssize_t vtfs_net_storage_read(
    struct super_block* sb, vtfs_ino_t ino, char* buffer, size_t len, loff_t* offset
) {
  struct vtfs_net_storage* storage = get_storage(sb);
  if (!storage) {
    printk(KERN_ERR "[vtfs_net] Storage not initialized\n");
    return -EINVAL;
  }

  if (!buffer || !offset) {
    printk(KERN_ERR "[vtfs_net] Invalid arguments: buffer or offset is NULL\n");
    return -EINVAL;
  }

  char ino_str[32];
  char len_str[32];
  char offset_str[32];
  snprintf(ino_str, sizeof(ino_str), "%llu", (unsigned long long)ino);
  snprintf(len_str, sizeof(len_str), "%zu", len);
  snprintf(offset_str, sizeof(offset_str), "%lld", (long long)*offset);

  size_t response_buffer_size = len + 1024;
  char* response_buffer = kmalloc(response_buffer_size, GFP_KERNEL);
  if (!response_buffer) {
    return -ENOMEM;
  }
  memset(response_buffer, 0, response_buffer_size);
  
  size_t data_length = 0;
  int64_t result = vtfs_http_call(
      storage->token,
      "read",
      response_buffer,
      response_buffer_size,
      &data_length,
      3,  // 3 args
      "ino", ino_str,
      "len", len_str,
      "offset", offset_str
  );

  if (result != 0) {
    printk(KERN_ERR "[vtfs_net] Server read failed with code: %lld\n", (long long)result);
    kfree(response_buffer);
    if (result > 0) {
      return (int)result;
    }
    return (int)result;
  }

  size_t bytes_to_copy = data_length;
  if (bytes_to_copy > len) {
    bytes_to_copy = len;
  }
  if (bytes_to_copy > response_buffer_size) {
    bytes_to_copy = response_buffer_size;
  }

  if (copy_to_user((char __user*)buffer, response_buffer, bytes_to_copy)) {
    kfree(response_buffer);
    return -EFAULT;
  }
  
  *offset += bytes_to_copy;

  kfree(response_buffer);
  return (ssize_t)bytes_to_copy;
}

static ssize_t vtfs_net_storage_write(
  struct super_block* sb, vtfs_ino_t ino, const char* buffer, size_t len, loff_t* offset
) {
struct vtfs_net_storage* storage = get_storage(sb);
if (!storage) {
  printk(KERN_ERR "[vtfs_net] Storage not initialized\n");
  return -EINVAL;
}

if (!buffer || !offset) {
  printk(KERN_ERR "[vtfs_net] Invalid arguments: buffer or offset is NULL\n");
  return -EINVAL;
}

const size_t MAX_CHUNK_SIZE = 4 * 1024; // 4KB

loff_t current_offset = *offset;
size_t total_written = 0;
size_t remaining = len;
const char* current_ptr = buffer;

while (remaining > 0) {
  size_t chunk_size = (remaining > MAX_CHUNK_SIZE) ? MAX_CHUNK_SIZE : remaining;
  
  char* kernel_buffer = kmalloc(chunk_size, GFP_KERNEL);
  if (!kernel_buffer) {
    return total_written > 0 ? (ssize_t)total_written : -ENOMEM;
  }
  
  if (copy_from_user(kernel_buffer, current_ptr, chunk_size)) {
    kfree(kernel_buffer);
    return total_written > 0 ? (ssize_t)total_written : -EFAULT;
  }

  size_t base64_size = BASE64_SIZE(chunk_size);
  char* base64_buffer = kmalloc(base64_size, GFP_KERNEL);
  if (!base64_buffer) {
    kfree(kernel_buffer);
    return total_written > 0 ? (ssize_t)total_written : -ENOMEM;
  }

  int base64_len = base64_encode(kernel_buffer, chunk_size, base64_buffer, base64_size);
  kfree(kernel_buffer);
  if (base64_len < 0) {
    printk(KERN_ERR "[vtfs_net] Base64 encoding failed\n");
    kfree(base64_buffer);
    return total_written > 0 ? (ssize_t)total_written : -EINVAL;
  }

  // URL encoding
  char* encoded_data = kmalloc(base64_size * 3, GFP_KERNEL);
  if (!encoded_data) {
    kfree(base64_buffer);
    return total_written > 0 ? (ssize_t)total_written : -ENOMEM;
  }
  encode(base64_buffer, encoded_data);
  kfree(base64_buffer);

  char ino_str[32];
  char len_str[32];
  char offset_str[32];
  snprintf(ino_str, sizeof(ino_str), "%llu", (unsigned long long)ino);
  snprintf(len_str, sizeof(len_str), "%zu", chunk_size);
  snprintf(offset_str, sizeof(offset_str), "%lld", (long long)current_offset);

  char response_buffer[256];
  memset(response_buffer, 0, sizeof(response_buffer));
  
  size_t write_data_length = 0;
  int64_t result = vtfs_http_call(
      storage->token,
      "write",
      response_buffer,
      sizeof(response_buffer),
      &write_data_length,
      4,  // 4 args
      "ino", ino_str,
      "len", len_str,
      "offset", offset_str,
      "data", encoded_data
  );

  kfree(encoded_data);

  if (result != 0) {
    printk(KERN_ERR "[vtfs_net] Server write failed with code: %lld at offset %lld\n", 
           (long long)result, (long long)current_offset);
    if (total_written > 0) {
      *offset = current_offset;
      return (ssize_t)total_written;
    }
    if (result > 0) {
      return (int)result;
    }
    return (int)result;
  }

  if (sizeof(response_buffer) < sizeof(int64_t)) {
    printk(KERN_ERR "[vtfs_net] Response buffer too small\n");
    if (total_written > 0) {
      *offset = current_offset;
      return (ssize_t)total_written;
    }
    return -EINVAL;
  }

  __le64 written_le;
  memcpy(&written_le, response_buffer, sizeof(written_le));
  int64_t written = le64_to_cpu(written_le);

  current_offset += written;
  total_written += written;
  current_ptr += written;
  remaining -= written;

  if (written < (ssize_t)chunk_size) {
    break;
  }
}

*offset = current_offset;
return (ssize_t)total_written;
}

static int vtfs_net_storage_link(
    struct super_block* sb, vtfs_ino_t target_ino, vtfs_ino_t parent, const char* name
) {
  struct vtfs_net_storage* storage = get_storage(sb);
  if (!storage) {
    printk(KERN_ERR "[vtfs_net] Storage not initialized\n");
    return -EINVAL;
  }

  if (!name) {
    printk(KERN_ERR "[vtfs_net] Name is NULL\n");
    return -EINVAL;
  }


  char encoded_name[NAME_MAX * 3 + 1];
  char target_ino_str[32];
  char parent_str[32];
  encode(name, encoded_name);
  snprintf(target_ino_str, sizeof(target_ino_str), "%llu", (unsigned long long)target_ino);
  snprintf(parent_str, sizeof(parent_str), "%llu", (unsigned long long)parent);

  char response_buffer[1024];
  memset(response_buffer, 0, sizeof(response_buffer));
  
  int64_t result = vtfs_http_call(
      storage->token,
      "link",
      response_buffer,
      sizeof(response_buffer),
      NULL,
      3,  // 3 args
      "target_ino", target_ino_str,
      "parent", parent_str,
      "name", encoded_name
  );

  if (result != 0) {
    printk(KERN_ERR "[vtfs_net] Server link failed with code: %lld\n", (long long)result);
    if (result > 0) {
      return (int)result;
    }
    return (int)result;
  }

  return 0;
}

static unsigned int vtfs_net_storage_count_links(struct super_block* sb, vtfs_ino_t ino) {
  struct vtfs_net_storage* storage = get_storage(sb);
  if (!storage) {
    printk(KERN_ERR "[vtfs_net] Storage not initialized\n");
    return 0;
  }

  char ino_str[32];
  snprintf(ino_str, sizeof(ino_str), "%llu", (unsigned long long)ino);

  char response_buffer[256];
  memset(response_buffer, 0, sizeof(response_buffer));
  
  size_t data_length = 0;
  int64_t result = vtfs_http_call(
      storage->token,
      "count_links",
      response_buffer,
      sizeof(response_buffer),
      &data_length,
      1,  // 1 arg
      "ino", ino_str
  );

  if (result != 0) {
    printk(KERN_ERR "[vtfs_net] Server count_links failed with code: %lld\n", (long long)result);
    return 0;
  }

  if (data_length < sizeof(uint32_t)) {
    printk(KERN_ERR "[vtfs_net] Response buffer too small for count_links: %zu (expected %zu)\n", 
           data_length, sizeof(uint32_t));
    return 0;
  }

  __le32 count_le;
  memcpy(&count_le, response_buffer, sizeof(count_le));
  unsigned int count = le32_to_cpu(count_le);

  return count;
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
