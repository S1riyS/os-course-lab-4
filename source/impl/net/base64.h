#ifndef BASE64_H
#define BASE64_H

#include <linux/types.h>

#define BASE64_SIZE(len) ((len + 2) / 3) * 4 + 1

int base64_encode(const char* input, size_t input_len, char* output, size_t output_size);

#endif // BASE64_H

