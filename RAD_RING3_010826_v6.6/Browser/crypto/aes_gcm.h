#ifndef AES_GCM_H
#define AES_GCM_H
#include <stdint.h>
#include <stddef.h>

void aes128_encrypt_block(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);

// AES-128-GCM (IV de 96 bits, como o TLS usa)
void aes_gcm_encrypt(const uint8_t key[16], const uint8_t iv[12],
                     const uint8_t* aad, uint32_t aad_len,
                     const uint8_t* pt, uint32_t pt_len,
                     uint8_t* ct, uint8_t tag[16]);
int  aes_gcm_decrypt(const uint8_t key[16], const uint8_t iv[12],
                     const uint8_t* aad, uint32_t aad_len,
                     const uint8_t* ct, uint32_t ct_len,
                     uint8_t* pt, const uint8_t tag[16]); // 0 = tag OK
#endif
