#ifndef DECODE_H
#define DECODE_H

#include <linux/types.h>
#include "../../vtfs_interface.h"

// Парсинг NodeMeta из бинарного ответа сервера
// Формат: ino (int64, 8) + parent_ino (int64, 8) + type (int16, 2) + mode (uint32, 4) + size (int64, 8) = 30 байт
int parse_node_meta(const char* data, struct vtfs_node_meta* out);

// Парсинг Dirent из бинарного ответа сервера
// Формат: name (char[256]) + ino (int64, 8) + type (int16, 2) = 266 байт
int parse_dirent(const char* data, struct vtfs_dirent* out);

#endif // DECODE_H

