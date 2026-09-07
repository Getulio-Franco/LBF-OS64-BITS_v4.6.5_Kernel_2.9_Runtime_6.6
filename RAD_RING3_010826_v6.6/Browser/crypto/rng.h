#ifndef RNG_H
#define RNG_H
#include <stdint.h>

// Coleta entropia (RDRAND + RDTSC jitter + estado) e instancia o DRBG
void rng_init(void);
// Gera n bytes criptograficamente utilizáveis (chaves, nonces, randoms)
void rng_bytes(uint8_t* out, int n);
int  rng_ready(void);
#endif
