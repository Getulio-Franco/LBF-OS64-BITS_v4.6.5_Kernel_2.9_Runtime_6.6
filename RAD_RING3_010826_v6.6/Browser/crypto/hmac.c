#include "hmac.h"

void hmac_init(hmac_ctx* h, const uint8_t* key, size_t klen) {
    uint8_t k[64] = {0};
    if (klen > 64) sha256(key, klen, k);
    else for (size_t i = 0; i < klen; i++) k[i] = key[i];
    uint8_t ipad[64];
    for (int i = 0; i < 64; i++) { ipad[i] = k[i] ^ 0x36; h->kpad[i] = k[i] ^ 0x5c; }
    sha256_init(&h->i);
    sha256_update(&h->i, ipad, 64);
}
void hmac_update(hmac_ctx* h, const uint8_t* d, size_t l) { sha256_update(&h->i, d, l); }
void hmac_final(hmac_ctx* h, uint8_t out[32]) {
    uint8_t inner[32];
    sha256_final(&h->i, inner);
    sha256_ctx o;
    sha256_init(&o);
    sha256_update(&o, h->kpad, 64);
    sha256_update(&o, inner, 32);
    sha256_final(&o, out);
}
void hmac_sha256(const uint8_t* k, size_t kl, const uint8_t* m, size_t ml, uint8_t out[32]) {
    hmac_ctx h;
    hmac_init(&h, k, kl);
    hmac_update(&h, m, ml);
    hmac_final(&h, out);
}
