#ifndef RSA_VERIFY_H
#define RSA_VERIFY_H
#include <stdint.h>
                         
// RSA-PSS (SHA-256, sLen=32): retorna 0 = válido
int rsa_verify_pss_sha256(const uint8_t hash[32],
                          const uint8_t* sig, int siglen,
                          const uint8_t* nb, int nlen,
                          const uint8_t* eb, int elen);

// parse do SPKI RSA (conteúdo do BIT STRING): modulus/exp big-endian
int rsa_parse_spki(const uint8_t* p, int len,
                   const uint8_t** n, int* nlen,
                   const uint8_t** e, int* elen);

// RSA PKCS#1 v1.5 SHA-256: retorna 0 = assinatura válida
int rsa_verify_pkcs1_sha256(const uint8_t hash[32],
                            const uint8_t* sig, int siglen,
                            const uint8_t* nb, int nlen,
                            const uint8_t* eb, int elen);
#endif
