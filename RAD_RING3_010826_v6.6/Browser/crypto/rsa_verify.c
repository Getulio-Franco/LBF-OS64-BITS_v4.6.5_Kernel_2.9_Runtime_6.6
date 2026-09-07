#include "rsa_verify.h"
#include "../system/string.h"
#include "Browser/crypto/sha256.h"

#define RSA_MAX_WORDS 128                      // 4096 bits

typedef struct { uint32_t w[RSA_MAX_WORDS]; } bn;

static void bn_zero(bn* a){ for(int i=0;i<RSA_MAX_WORDS;i++) a->w[i]=0; }
static int bn_cmp(const bn* a, const bn* b, int n){
    for(int i=n-1;i>=0;i--){ if(a->w[i]<b->w[i]) return -1; if(a->w[i]>b->w[i]) return 1; }
    return 0;
}
static void bn_sub(bn* r, const bn* a, const bn* b, int n){
    uint64_t borrow=0;
    for(int i=0;i<n;i++){
        uint64_t d = (uint64_t)a->w[i] - b->w[i] - borrow;
        r->w[i] = (uint32_t)d;
        borrow = (d >> 63) & 1;
    }
}
static int bn_from_be(bn* a, const uint8_t* be, int len, int n){
    while (len > n*4 && *be == 0) { be++; len--; }
    if (len > n*4) return -1;
    bn_zero(a);
    for (int i = 0; i < len; i++) {
        int p = len - 1 - i;
        a->w[p/4] |= ((uint32_t)be[i]) << (8*(p%4));
    }
    return 0;
}

static int der_len(const uint8_t* p, int* i){
    int b = p[(*i)++];
    if (b < 0x80) return b;
    int n = b & 0x7F, v = 0;
    while (n--) v = (v<<8) | p[(*i)++];
    return v;
}

int rsa_parse_spki(const uint8_t* p, int len,
                   const uint8_t** n, int* nlen,
                   const uint8_t** e, int* elen){
    int i = 0;
    if (len < 8 || p[i++] != 0x30) return -1;
    der_len(p, &i);
    if (p[i++] != 0x02) return -2;
    int nl = der_len(p, &i);
    while (nl > 1 && p[i] == 0) { i++; nl--; }
    *n = p + i; *nlen = nl; i += nl;
    if (i >= len || p[i++] != 0x02) return -3;
    int el = der_len(p, &i);
    if (i + el > len) return -4;
    *e = p + i; *elen = el;
    return 0;
}

// mul mod m: produto escola + redução bit-a-bit (genérico p/ 2048/4096)
static void mod_mul(bn* r, const bn* a, const bn* b, const bn* m, int n){
    static uint32_t prod[2*RSA_MAX_WORDS];
    for(int i=0;i<2*n;i++) prod[i]=0;
    for(int i=0;i<n;i++){
        if(!a->w[i]) continue;
        __int128 carry=0;
        for(int j=0;j<n;j++){
            __int128 t = (__int128)a->w[i]*b->w[j] + prod[i+j] + carry;
            prod[i+j] = (uint32_t)t;
            carry = t >> 32;
        }
        int k = i+n;
        while (carry && k < 2*n) { __int128 t = (__int128)prod[k] + carry; prod[k]=(uint32_t)t; carry = t>>32; k++; }
    }
    static bn acc;
    bn_zero(&acc);
    for (int bit = 2*n*32 - 1; bit >= 0; bit--) {
        uint32_t carry = 0;
        for (int i = 0; i < n; i++) { uint32_t nc = acc.w[i] >> 31; acc.w[i] = (acc.w[i]<<1)|carry; carry = nc; }
        if ((prod[bit/32] >> (bit&31)) & 1) acc.w[0] |= 1;
        if (carry || bn_cmp(&acc, m, n) >= 0) { bn t; bn_sub(&t, &acc, m, n); acc = t; }
    }
    *r = acc;
}

static void mod_pow(bn* r, const bn* base, const uint8_t* e, int elen, const bn* m, int n){
    bn acc, b;
    bn_zero(&acc); acc.w[0]=1;
    b = *base;
    for (int i = 0; i < elen; i++)
        for (int bb = 7; bb >= 0; bb--) {
            mod_mul(&acc, &acc, &acc, m, n);
            if ((e[i] >> bb) & 1) mod_mul(&acc, &acc, &b, m, n);
        }
    *r = acc;
}

int rsa_verify_pkcs1_sha256(const uint8_t hash[32],
                            const uint8_t* sig, int siglen,
                            const uint8_t* nb, int nlen,
                            const uint8_t* eb, int elen){
    if (nlen > RSA_MAX_WORDS*4 || siglen != nlen) return -1;
    int n = (nlen + 3) / 4;
    bn m, s, r;
    if (bn_from_be(&m, nb, nlen, n) != 0) return -2;
    if (bn_from_be(&s, sig, siglen, n) != 0) return -3;
    if (bn_cmp(&s, &m, n) >= 0) return -4;
    mod_pow(&r, &s, eb, elen, &m, n);
    // EM = r em bytes big-endian (nlen bytes)
    uint8_t em[512];
    for (int i = 0; i < nlen; i++) {
        int p = nlen - 1 - i;
        em[i] = (uint8_t)(r.w[p/4] >> (8*(p%4)));
    }
    // PKCS#1 v1.5: 00 01 FF..FF 00 || DigestInfo(SHA-256) || hash
    static const uint8_t DI[19] = {0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,
                                   0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20};
    int ps = nlen - 3 - 51;
    if (ps < 8) return -5;
    if (em[0] != 0x00 || em[1] != 0x01) return -6;
    for (int i = 0; i < ps; i++) if (em[2+i] != 0xFF) return -7;
    if (em[2+ps] != 0x00) return -8;
    const uint8_t* t = em + 3 + ps;
    for (int i = 0; i < 19; i++) if (t[i] != DI[i]) return -9;
    for (int i = 0; i < 32; i++) if (t[19+i] != hash[i]) return -10;
    return 0;
}

#include "sha256.h"   // ← garanta no topo do rsa_verify.c

// MGF1-SHA256 (máscara do PSS)
static void mgf1_sha256(uint8_t* out, int outlen, const uint8_t* seed, int seedlen) {
    int pos = 0;
    for (uint32_t c = 0; pos < outlen; c++) {
        uint8_t cnt[4] = { (uint8_t)(c>>24), (uint8_t)(c>>16), (uint8_t)(c>>8), (uint8_t)c };
        sha256_ctx sc;
        sha256_init(&sc);
        sha256_update(&sc, seed, seedlen);
        sha256_update(&sc, cnt, 4);
        uint8_t h[32];
        sha256_final(&sc, h);
        int m = outlen - pos; if (m > 32) m = 32;
        for (int i = 0; i < m; i++) out[pos+i] = h[i];
        pos += m;
    }
}

int rsa_verify_pss_sha256(const uint8_t hash[32], const uint8_t* sig, int siglen,
                          const uint8_t* nb, int nlen,
                          const uint8_t* eb, int elen) {
    if (nlen > RSA_MAX_WORDS*4 || siglen != nlen) return -1;
    int n = (nlen + 3) / 4;
    bn m, s, r;
    if (bn_from_be(&m, nb, nlen, n) != 0) return -2;
    if (bn_from_be(&s, sig, siglen, n) != 0) return -3;
    if (bn_cmp(&s, &m, n) >= 0) return -4;
    mod_pow(&r, &s, eb, elen, &m, n);
    uint8_t em[512];
    for (int i = 0; i < nlen; i++) {
        int p = nlen - 1 - i;
        em[i] = (uint8_t)(r.w[p/4] >> (8*(p%4)));
    }
    const int hLen = 32;                              // sLen = hLen (padrão)
    if (nlen < hLen + 33) return -5;
    if (em[nlen-1] != 0xBC) return -6;
    int dbLen = nlen - hLen - 1;
    const uint8_t* maskedDB = em;
    const uint8_t* H = em + dbLen;
    uint8_t dbMask[512], DB[512];
    mgf1_sha256(dbMask, dbLen, H, hLen);
    for (int i = 0; i < dbLen; i++) DB[i] = maskedDB[i] ^ dbMask[i];
    // zera bits extras à esquerda (emBits = bit-length do módulo)
    int bits = n * 32;
    while (bits > 0 && !((m.w[(bits-1)/32] >> ((bits-1)&31)) & 1)) bits--;
    int zb = nlen*8 - bits;
    if (zb > 0 && zb < 8) DB[0] &= (uint8_t)(0xFF >> zb);
    int psLen = dbLen - hLen - 1;
    for (int i = 0; i < psLen; i++) if (DB[i] != 0) return -7;
    if (DB[psLen] != 0x01) return -8;
    const uint8_t* salt = DB + psLen + 1;
    uint8_t mp[8 + 32 + 64];
    for (int i = 0; i < 8; i++) mp[i] = 0;
    for (int i = 0; i < 32; i++) mp[8+i] = hash[i];
    for (int i = 0; i < hLen; i++) mp[40+i] = salt[i];
    uint8_t h2[32];
    sha256(mp, 40 + hLen, h2);
    int diff = 0;
    for (int i = 0; i < 32; i++) diff |= h2[i] ^ H[i];
    return diff == 0 ? 0 : -9;
}
