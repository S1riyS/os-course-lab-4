#include "base64.h"

#include <linux/types.h>
#include <linux/string.h>

int base64_encode(const char* input, size_t input_len, char* output, size_t output_size) {
  static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t output_len = 0;
  size_t i = 0;

  // Base64 увеличивает размер на ~33%, округляем вверх
  if (output_size < BASE64_SIZE(input_len)) {
    return -1;
  }

  while (i < input_len) {
    unsigned char byte1 = (unsigned char)input[i++];
    unsigned char byte2 = (i < input_len) ? (unsigned char)input[i++] : 0;
    unsigned char byte3 = (i < input_len) ? (unsigned char)input[i++] : 0;

    output[output_len++] = base64_chars[byte1 >> 2];
    output[output_len++] = base64_chars[((byte1 & 0x03) << 4) | (byte2 >> 4)];
    
    if (i - 2 < input_len) {
      output[output_len++] = base64_chars[((byte2 & 0x0F) << 2) | (byte3 >> 6)];
    } else {
      output[output_len++] = '=';
    }
    
    if (i - 1 < input_len) {
      output[output_len++] = base64_chars[byte3 & 0x3F];
    } else {
      output[output_len++] = '=';
    }
  }

  output[output_len] = '\0';
  return (int)output_len;
}

