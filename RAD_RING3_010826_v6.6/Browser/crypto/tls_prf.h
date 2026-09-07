#ifndef TLS_PRF_H
#define TLS_PRF_H
#include <stdint.h>
#include <stddef.h>
void tls12_prf(const uint8_t* sec, size_t slen, const char* label,
               const uint8_t* seed1, size_t s1len,
               const uint8_t* seed2, size_t s2len,
               uint8_t* out, size_t olen);
#endif
