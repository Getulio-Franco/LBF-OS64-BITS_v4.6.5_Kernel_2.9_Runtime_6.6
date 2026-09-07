#ifndef EC256_H
#define EC256_H
#include <stdint.h>

// ECDSA P-256: verifica assinatura (r,s) sobre hash SHA-256 com pubkey (qx,qy)
// tudo big-endian 32 bytes. retorna 0 = válida.
int ecdsa_p256_verify(const uint8_t hash[32],
                      const uint8_t r[32], const uint8_t s[32],
                      const uint8_t qx[32], const uint8_t qy[32]);

// parse da assinatura DER (SEQUENCE{INTEGER r, INTEGER s}) -> r/s 32B BE
int ecdsa_parse_der_sig(const uint8_t* der, int len, uint8_t r[32], uint8_t s[32]);
#endif
