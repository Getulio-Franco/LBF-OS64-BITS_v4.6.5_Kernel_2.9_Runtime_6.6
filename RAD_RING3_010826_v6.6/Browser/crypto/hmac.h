#ifndef HMAC_H
#define HMAC_H
#include "sha256.h"
typedef struct { sha256_ctx i; uint8_t kpad[64]; } hmac_ctx;
void hmac_init(hmac_ctx* h, const uint8_t* key, size_t klen);
void hmac_update(hmac_ctx* h, const uint8_t* d, size_t l);
void hmac_final(hmac_ctx* h, uint8_t out[32]);
void hmac_sha256(const uint8_t* k, size_t kl, const uint8_t* m, size_t ml, uint8_t out[32]);
#endif
