#pragma once

#include <stdint.h>


int utf8_decode(const char *text, int text_len, uint32_t *out);
int utf8_encode(char *text, int text_len, uint32_t cp);
