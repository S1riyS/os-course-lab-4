#include "decode.h"

#include <linux/errno.h>
#include <linux/string.h>
#include <linux/byteorder/generic.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <asm/byteorder.h>

int parse_node_meta(const char* data, struct vtfs_node_meta* out) {
  // Парсим ino (int64, little-endian)
  __le64 ino_le;
  memcpy(&ino_le, data, sizeof(ino_le));
  out->ino = le64_to_cpu(ino_le);
  data += 8;

  // Парсим parent_ino (int64, little-endian)
  __le64 parent_ino_le;
  memcpy(&parent_ino_le, data, sizeof(parent_ino_le));
  out->parent_ino = le64_to_cpu(parent_ino_le);
  data += 8;

  // Парсим type (int16, little-endian)
  __le16 type_le;
  memcpy(&type_le, data, sizeof(type_le));
  out->type = (enum vtfs_node_type)le16_to_cpu(type_le);
  data += 2;

  // Парсим mode (uint32, little-endian)
  __le32 mode_le;
  memcpy(&mode_le, data, sizeof(mode_le));
  out->mode = le32_to_cpu(mode_le);
  data += 4;

  // Парсим size (int64, little-endian)
  __le64 size_le;
  memcpy(&size_le, data, sizeof(size_le));
  out->size = le64_to_cpu(size_le);

  return 0;
}

// Парсинг Dirent из бинарного ответа сервера
// Формат: name (char[256]) + ino (int64, 8) + type (int16, 2) = 266 байт
int parse_dirent(const char* data, struct vtfs_dirent* out) {
  // Парсим name (char[256] от сервера, но наш массив имеет размер NAME_MAX+1 = 256)
  // Копируем максимум 255 байт, чтобы оставить место для null-terminator
  memcpy(out->name, data, 255);
  out->name[255] = '\0'; // Обеспечиваем null-termination (последний элемент массива)
  data += 256; // Пропускаем все 256 байт из формата сервера

  // Парсим ino (int64, little-endian)
  __le64 ino_le;
  memcpy(&ino_le, data, sizeof(ino_le));
  out->ino = le64_to_cpu(ino_le);
  data += 8;

  // Парсим type (int16, little-endian)
  __le16 type_le;
  memcpy(&type_le, data, sizeof(type_le));
  out->type = (enum vtfs_node_type)le16_to_cpu(type_le);

  return 0;
}

