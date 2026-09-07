#ifndef X25519_H
#define X25519_H
#include <stdint.h>
// out = scalar * u (Curve25519, RFC 7748) — 32 bytes cada
void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t u[32]);
// out = scalar * 9 (chave pública)
void x25519_base(uint8_t out[32], const uint8_t scalar[32]);
#endif
