#include "aes_gcm.h"
#include "../system/string.h"

static const uint8_t sbox[256] = {
0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};

static uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ ((x >> 7) * 0x1b)); }

static void aes128_expand(const uint8_t key[16], uint8_t rk[176]) {
    for (int i = 0; i < 16; i++) rk[i] = key[i];
    uint8_t rcon = 1;
    for (int i = 16; i < 176; i += 4) {
        uint8_t t0 = rk[i-4], t1 = rk[i-3], t2 = rk[i-2], t3 = rk[i-1];
        if (i % 16 == 0) {
            uint8_t tmp = t0;
            t0 = (uint8_t)(sbox[t1] ^ rcon);
            t1 = sbox[t2]; t2 = sbox[t3]; t3 = sbox[tmp];
            rcon = xtime(rcon);
        }
        rk[i]   = rk[i-16] ^ t0;
        rk[i+1] = rk[i-15] ^ t1;
        rk[i+2] = rk[i-14] ^ t2;
        rk[i+3] = rk[i-13] ^ t3;
    }
}

void aes128_encrypt_block(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
    uint8_t rk[176];
    aes128_expand(key, rk);
    uint8_t s[16];
    for (int i = 0; i < 16; i++) s[i] = in[i] ^ rk[i];
    for (int r = 1; r <= 10; r++) {
        for (int i = 0; i < 16; i++) s[i] = sbox[s[i]];
        uint8_t t;
        t=s[1]; s[1]=s[5]; s[5]=s[9]; s[9]=s[13]; s[13]=t;
        t=s[2]; s[2]=s[10]; s[10]=t; t=s[6]; s[6]=s[14]; s[14]=t;
        t=s[15]; s[15]=s[11]; s[11]=s[7]; s[7]=s[3]; s[3]=t;
        if (r < 10) {
            for (int c = 0; c < 4; c++) {
                uint8_t* a = s + c*4;
                uint8_t a0=a[0],a1=a[1],a2=a[2],a3=a[3];
                a[0] = (uint8_t)(xtime(a0) ^ xtime(a1) ^ a1 ^ a2 ^ a3);
                a[1] = (uint8_t)(a0 ^ xtime(a1) ^ xtime(a2) ^ a2 ^ a3);
                a[2] = (uint8_t)(a0 ^ a1 ^ xtime(a2) ^ xtime(a3) ^ a3);
                a[3] = (uint8_t)(xtime(a0) ^ a0 ^ a1 ^ a2 ^ xtime(a3));
            }
        }
        for (int i = 0; i < 16; i++) s[i] ^= rk[r*16 + i];
    }
    for (int i = 0; i < 16; i++) out[i] = s[i];
}

/* ---- GF(2^128): Z ^= X·H via shift-right + redução 0xE1 ---- */
static void gf128_mul(uint8_t* x, const uint8_t* h) {
    uint8_t z[16] = {0}, v[16];
    for (int i = 0; i < 16; i++) v[i] = h[i];
    for (int i = 0; i < 128; i++) {
        if (x[i >> 3] & (1 << (7 - (i & 7))))
            for (int j = 0; j < 16; j++) z[j] ^= v[j];
        uint8_t carry = v[15] & 1;
        for (int j = 15; j > 0; j--) v[j] = (uint8_t)((v[j] >> 1) | (v[j-1] << 7));
        v[0] >>= 1;
        if (carry) v[0] ^= 0xe1;
    }
    for (int i = 0; i < 16; i++) x[i] = z[i];
}

static void ghash_feed(uint8_t* y, const uint8_t* h, const uint8_t* data, uint32_t len) {
    while (len > 0) {
        uint8_t tmp[16] = {0};
        uint32_t n = len > 16 ? 16 : len;
        for (uint32_t i = 0; i < n; i++) tmp[i] = data[i];
        for (int i = 0; i < 16; i++) y[i] ^= tmp[i];
        gf128_mul(y, h);
        data += n; len -= n;
    }
}

static void gcm_core(const uint8_t key[16], const uint8_t iv[12],
                     const uint8_t* aad, uint32_t aad_len,
                     const uint8_t* in, uint32_t in_len,
                     uint8_t* out, uint8_t tag[16]) {
    uint8_t H[16] = {0};
    aes128_encrypt_block(key, H, H);                 // H = E_K(0^128)
    uint8_t J0[16];
    for (int i = 0; i < 12; i++) J0[i] = iv[i];
    J0[12] = J0[13] = J0[14] = 0; J0[15] = 1;        // J0 = IV || 0^31 || 1
    uint8_t ctr[16];
    for (int i = 0; i < 16; i++) ctr[i] = J0[i];
    for (uint32_t i = 0; i < in_len; i += 16) {
        for (int j = 15; j >= 12; j--) { if (++ctr[j]) break; }   // inc32
        uint8_t ks[16];
        aes128_encrypt_block(key, ctr, ks);
        uint32_t n = in_len - i; if (n > 16) n = 16;
        for (uint32_t j = 0; j < n; j++) out[i+j] = in[i+j] ^ ks[j];
    }
    uint8_t y[16] = {0};
    ghash_feed(y, H, aad, aad_len);
    ghash_feed(y, H, out, in_len);
    uint8_t lenb[16] = {0};
    uint64_t la = (uint64_t)aad_len * 8, lc = (uint64_t)in_len * 8;
    for (int i = 0; i < 8; i++) { lenb[i] = (uint8_t)(la >> (56-8*i)); lenb[8+i] = (uint8_t)(lc >> (56-8*i)); }
    for (int i = 0; i < 16; i++) y[i] ^= lenb[i];
    gf128_mul(y, H);
    uint8_t ej[16];
    aes128_encrypt_block(key, J0, ej);               // tag = E_K(J0) ^ GHASH
    for (int i = 0; i < 16; i++) tag[i] = y[i] ^ ej[i];
}

void aes_gcm_encrypt(const uint8_t key[16], const uint8_t iv[12],
                     const uint8_t* aad, uint32_t aad_len,
                     const uint8_t* pt, uint32_t pt_len,
                     uint8_t* ct, uint8_t tag[16]) {
    gcm_core(key, iv, aad, aad_len, pt, pt_len, ct, tag);
}

int aes_gcm_decrypt(const uint8_t key[16], const uint8_t iv[12],
                    const uint8_t* aad, uint32_t aad_len,
                    const uint8_t* ct, uint32_t ct_len,
                    uint8_t* pt, const uint8_t tag[16]) {
    uint8_t calc[16];
    uint8_t H[16] = {0};
    aes128_encrypt_block(key, H, H);
    uint8_t J0[16];
    for (int i = 0; i < 12; i++) J0[i] = iv[i];
    J0[12] = J0[13] = J0[14] = 0; J0[15] = 1;
    uint8_t ctr[16];
    for (int i = 0; i < 16; i++) ctr[i] = J0[i];
    for (uint32_t i = 0; i < ct_len; i += 16) {
        for (int j = 15; j >= 12; j--) { if (++ctr[j]) break; }
        uint8_t ks[16];
        aes128_encrypt_block(key, ctr, ks);
        uint32_t n = ct_len - i; if (n > 16) n = 16;
        for (uint32_t j = 0; j < n; j++) pt[i+j] = ct[i+j] ^ ks[j];
    }
    uint8_t y[16] = {0};
    ghash_feed(y, H, aad, aad_len);
    ghash_feed(y, H, ct, ct_len);
    uint8_t lenb[16] = {0};
    uint64_t la = (uint64_t)aad_len * 8, lc = (uint64_t)ct_len * 8;
    for (int i = 0; i < 8; i++) { lenb[i] = (uint8_t)(la >> (56-8*i)); lenb[8+i] = (uint8_t)(lc >> (56-8*i)); }
    for (int i = 0; i < 16; i++) y[i] ^= lenb[i];
    gf128_mul(y, H);
    uint8_t ej[16];
    aes128_encrypt_block(key, J0, ej);
    for (int i = 0; i < 16; i++) calc[i] = y[i] ^ ej[i];
    int diff = 0;
    for (int i = 0; i < 16; i++) diff |= calc[i] ^ tag[i];
    return diff == 0 ? 0 : -1;
}
