#include "x25519.h"

#define M51 0x7ffffffffffffULL
typedef struct { uint64_t v[5]; } fe;

static void fe_frombytes(fe* f, const uint8_t* b) {
    uint64_t x0=0,x1=0,x2=0,x3=0;
    for (int i = 7; i >= 0; i--)  x0 = (x0 << 8) | b[i];
    for (int i = 15; i >= 8; i--) x1 = (x1 << 8) | b[i];
    for (int i = 23; i >= 16; i--) x2 = (x2 << 8) | b[i];
    for (int i = 31; i >= 24; i--) x3 = (x3 << 8) | b[i];
    f->v[0] = x0 & M51;
    f->v[1] = ((x0 >> 51) | (x1 << 13)) & M51;
    f->v[2] = ((x1 >> 38) | (x2 << 26)) & M51;
    f->v[3] = ((x2 >> 25) | (x3 << 39)) & M51;
    f->v[4] = (x3 >> 12) & M51;          // máscara do bit 255 (RFC 7748)
}

static void fe_tobytes(uint8_t* b, const fe* f) {
    uint64_t t[5];
    for (int i = 0; i < 5; i++) t[i] = f->v[i];
    for (int j = 0; j < 2; j++) {                       // carry chain
        for (int i = 0; i < 4; i++) { t[i+1] += t[i] >> 51; t[i] &= M51; }
        t[0] += (t[4] >> 51) * 19; t[4] &= M51;
    }
    uint64_t p[5] = { M51-18, M51, M51, M51, M51 };     // p = 2^255-19
    uint64_t r[5]; int borrow = 0;
    for (int i = 0; i < 5; i++) {
        int64_t d = (int64_t)t[i] - (int64_t)p[i] - borrow;
        if (d < 0) { d += (int64_t)M51 + 1; borrow = 1; } else borrow = 0;
        r[i] = (uint64_t)d;
    }
    for (int i = 0; i < 5; i++) if (!borrow) t[i] = r[i];   // t>=p ? usa r
    uint64_t x0 = t[0] | (t[1] << 51);
    uint64_t x1 = (t[1] >> 13) | (t[2] << 38);
    uint64_t x2 = (t[2] >> 26) | (t[3] << 25);
    uint64_t x3 = (t[3] >> 39) | (t[4] << 12);
    for (int i = 0; i < 8; i++) b[i]    = (uint8_t)(x0 >> (8*i));
    for (int i = 0; i < 8; i++) b[8+i]  = (uint8_t)(x1 >> (8*i));
    for (int i = 0; i < 8; i++) b[16+i] = (uint8_t)(x2 >> (8*i));
    for (int i = 0; i < 8; i++) b[24+i] = (uint8_t)(x3 >> (8*i));
}

static void fe_add(fe* o, const fe* a, const fe* b) {
    for (int i = 0; i < 5; i++) o->v[i] = a->v[i] + b->v[i];
}
static void fe_sub(fe* o, const fe* a, const fe* b) {
    // padding = 2^256-38 ≡ 0 (mod p): limb0 += 2*M51-36, demais += 2*M51
    o->v[0] = a->v[0] + (2*M51 - 36) - b->v[0];
    for (int i = 1; i < 5; i++) o->v[i] = a->v[i] + 2*M51 - b->v[i];
}
static void fe_mul(fe* o, const fe* a, const fe* b) {
    uint64_t a0=a->v[0],a1=a->v[1],a2=a->v[2],a3=a->v[3],a4=a->v[4];
    uint64_t b0=b->v[0],b1=b->v[1],b2=b->v[2],b3=b->v[3],b4=b->v[4];
    uint64_t b1_19=b1*19,b2_19=b2*19,b3_19=b3*19,b4_19=b4*19;
    __int128 t0 = (__int128)a0*b0 + (__int128)a1*b4_19 + (__int128)a2*b3_19 + (__int128)a3*b2_19 + (__int128)a4*b1_19;
    __int128 t1 = (__int128)a0*b1 + (__int128)a1*b0 + (__int128)a2*b4_19 + (__int128)a3*b3_19 + (__int128)a4*b2_19;
    __int128 t2 = (__int128)a0*b2 + (__int128)a1*b1 + (__int128)a2*b0 + (__int128)a3*b4_19 + (__int128)a4*b3_19;
    __int128 t3 = (__int128)a0*b3 + (__int128)a1*b2 + (__int128)a2*b1 + (__int128)a3*b0 + (__int128)a4*b4_19;
    __int128 t4 = (__int128)a0*b4 + (__int128)a1*b3 + (__int128)a2*b2 + (__int128)a3*b1 + (__int128)a4*b0;
    uint64_t c, r0,r1,r2,r3,r4;
    c = (uint64_t)(t0 >> 51); t1 += c; r0 = (uint64_t)t0 & M51;
    c = (uint64_t)(t1 >> 51); t2 += c; r1 = (uint64_t)t1 & M51;
    c = (uint64_t)(t2 >> 51); t3 += c; r2 = (uint64_t)t2 & M51;
    c = (uint64_t)(t3 >> 51); t4 += c; r3 = (uint64_t)t3 & M51;
    c = (uint64_t)(t4 >> 51);            r4 = (uint64_t)t4 & M51;
    r0 += c * 19;
    c = r0 >> 51; r0 &= M51; r1 += c;
    o->v[0]=r0; o->v[1]=r1; o->v[2]=r2; o->v[3]=r3; o->v[4]=r4;
}
static void fe_sqr(fe* o, const fe* a) { fe_mul(o, a, a); }
static void fe_mul121665(fe* o, const fe* a) {
    uint64_t r[5]; uint64_t carry = 0;
    for (int i = 0; i < 5; i++) {
        __int128 t = (__int128)a->v[i] * 121665 + carry;
        r[i] = (uint64_t)t & M51;
        carry = (uint64_t)(t >> 51);
    }
    r[0] += carry * 19;
    carry = r[0] >> 51; r[0] &= M51; r[1] += carry;
    for (int i = 0; i < 5; i++) o->v[i] = r[i];
}
static void fe_cswap(fe* a, fe* b, uint64_t swap) {
    uint64_t m = 0ULL - (swap & 1);
    for (int i = 0; i < 5; i++) {
        uint64_t t = m & (a->v[i] ^ b->v[i]);
        a->v[i] ^= t; b->v[i] ^= t;
    }
}
static void fe_invert(fe* o, const fe* a) {          // a^(p-2), p-2 = 2^255-21
    fe r, base;
    r.v[0] = 1; r.v[1] = r.v[2] = r.v[3] = r.v[4] = 0;
    base = *a;
    uint8_t e[32];
    for (int i = 0; i < 32; i++) e[i] = 0xff;
    e[0] = 0xeb; e[31] = 0x7f;                       // LE de 2^255-21
    for (int i = 0; i < 255; i++) {
        if (e[i >> 3] & (1 << (i & 7))) fe_mul(&r, &r, &base);
        fe_sqr(&base, &base);
    }
    *o = r;
}

void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t u[32]) {
    uint8_t k[32];
    for (int i = 0; i < 32; i++) k[i] = scalar[i];
    k[0] &= 248; k[31] &= 127; k[31] |= 64;          // clamp RFC 7748

    fe x1, x2, z2, x3, z3;
    fe_frombytes(&x1, u);
    for (int i = 0; i < 5; i++) {
        x2.v[i] = (i == 0) ? 1 : 0;
        z2.v[i] = 0;
        x3.v[i] = x1.v[i];
        z3.v[i] = (i == 0) ? 1 : 0;
    }
    uint64_t swap = 0;
    for (int t = 254; t >= 0; t--) {
        uint64_t kt = (k[t >> 3] >> (t & 7)) & 1;
        swap ^= kt;
        fe_cswap(&x2, &x3, swap); fe_cswap(&z2, &z3, swap);
        swap = kt;
        fe A, AA, B, BB, E, C, D, DA, CB, T1, T2, T3;
        fe_add(&A, &x2, &z2); fe_sqr(&AA, &A);
        fe_sub(&B, &x2, &z2); fe_sqr(&BB, &B);
        fe_sub(&E, &AA, &BB);
        fe_add(&C, &x3, &z3); fe_sub(&D, &x3, &z3);
        fe_mul(&DA, &D, &A);  fe_mul(&CB, &C, &B);
        fe_add(&T1, &DA, &CB); fe_sqr(&x3, &T1);
        fe_sub(&T2, &DA, &CB); fe_sqr(&T2, &T2); fe_mul(&z3, &x1, &T2);
        fe_mul(&x2, &AA, &BB);
        fe_mul121665(&T3, &E);
        fe_add(&T3, &AA, &T3);
        fe_mul(&z2, &E, &T3);
    }
    fe_cswap(&x2, &x3, swap); fe_cswap(&z2, &z3, swap);
    fe zi, r;
    fe_invert(&zi, &z2);
    fe_mul(&r, &x2, &zi);
    fe_tobytes(out, &r);
}

void x25519_base(uint8_t out[32], const uint8_t scalar[32]) {
    uint8_t nine[32] = {0};
    nine[0] = 9;
    x25519(out, scalar, nine);
}
