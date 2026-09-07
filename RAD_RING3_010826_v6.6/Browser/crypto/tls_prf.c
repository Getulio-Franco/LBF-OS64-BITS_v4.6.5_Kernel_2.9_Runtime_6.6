#include "tls_prf.h"
#include "hmac.h"

// P_sha256 (RFC 5246 §5): A(0)=seed; A(i)=HMAC(secret,A(i-1));
// out = HMAC(secret, A(i)+seed) concatenados
static void p_sha256(const uint8_t* sec, size_t slen,
                     const uint8_t* seed, size_t seedlen,
                     uint8_t* out, size_t olen) {
    uint8_t A[32];
    hmac_sha256(sec, slen, seed, seedlen, A);      // A(1)
    size_t pos = 0;
    while (pos < olen) {
        hmac_ctx h;
        uint8_t tmp[32];
        hmac_init(&h, sec, slen);
        hmac_update(&h, A, 32);
        hmac_update(&h, seed, seedlen);
        hmac_final(&h, tmp);
        size_t n = olen - pos; if (n > 32) n = 32;
        for (size_t i = 0; i < n; i++) out[pos + i] = tmp[i];
        pos += n;
        if (pos < olen) hmac_sha256(sec, slen, A, 32, A);
    }
}

void tls12_prf(const uint8_t* sec, size_t slen, const char* label,
               const uint8_t* seed1, size_t s1len,
               const uint8_t* seed2, size_t s2len,
               uint8_t* out, size_t olen) {
    uint8_t seed[256];
    size_t l = 0;
    for (const char* p = label; *p && l < 256; p++) seed[l++] = (uint8_t)*p;
    for (size_t i = 0; i < s1len && l < 256; i++) seed[l++] = seed1[i];
    for (size_t i = 0; i < s2len && l < 256; i++) seed[l++] = seed2[i];
    p_sha256(sec, slen, seed, l, out, olen);
}
