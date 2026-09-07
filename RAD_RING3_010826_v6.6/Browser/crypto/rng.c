/*
====================================================================
Arquivo: rng.c (TWebTLS)
Versão: 1.0
Data: 02/09/2026
Autor: LBF-OS Team + AI Assistant

Descrição:
    Gerador de números aleatórios do LBF-OS (Ring 3).
    - Entropia: RDRAND (hardware, se disponível) + jitter de RDTSC
      + endereço de pilha + contador — tudo comprimido por SHA-256.
    - DRBG: HMAC-SHA256 DRBG (estrutura do NIST SP 800-90A:
      update/generate), usando o hmac_sha256 já aprovado.
    Sem dependência da pilha de rede (pode rodar no crypto_t).

Testado em:
    - LBF-OS Base_v4.6.5 / Kernel v2.9 / Runtime v6.3
====================================================================
*/
#include "rng.h"
#include "hmac.h"
#include "sha256.h"
#include "../system/string.h"

/* ---------------- fontes de entropia ---------------- */
static uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static int rdrand64(uint64_t* v) {
    unsigned char ok;
    __asm__ volatile ("rdrand %0; setc %1" : "=r"(*v), "=qm"(ok));
    return (int)ok;
}

static void collect_entropy(uint8_t* out32) {
    uint8_t pool[96];
    int o = 0;

    // 1) RDRAND: 4 x 64 bits de entropia de hardware (com retry)
    for (int i = 0; i < 4; i++) {
        uint64_t v = 0;
        int tries = 0;
        while (tries < 16 && !rdrand64(&v)) tries++;
        for (int j = 0; j < 8; j++) pool[o++] = (uint8_t)(v >> (8 * j));
    }
    // 2) Jitter de RDTSC: 4 amostras com queima de ciclos entre elas
    for (int i = 0; i < 4; i++) {
        volatile int burn = 0;
        for (int k = 0; k < 300; k++) burn += k * i;
        uint64_t t = rdtsc() ^ (rdtsc() << (i & 7));
        for (int j = 0; j < 8; j++) pool[o++] = (uint8_t)(t >> (8 * j));
    }
    // 3) Endereço de pilha + contador (estado do processo)
    uintptr_t sp = (uintptr_t)&sp;
    static uint32_t ctr = 0x9E3779B9u;
    ctr += 0x9E3779B9u;
    for (int j = 0; j < 8 && o < 96; j++) pool[o++] = (uint8_t)(sp >> (8 * j));
    for (int j = 0; j < 4 && o < 96; j++) pool[o++] = (uint8_t)(ctr >> (8 * j));

    // Comprime tudo em 32 bytes
    sha256(pool, o, out32);
}

/* ---------------- HMAC-SHA256 DRBG (SP 800-90A) ---------------- */
static uint8_t K[32], V[32];
static int ready = 0;
static uint32_t gen_count = 0;

static void drbg_update(const uint8_t* provided, int plen) {
    uint8_t tmp[32 + 1 + 64];
    int n = 0;
    for (int i = 0; i < 32; i++) tmp[n++] = V[i];
    tmp[n++] = 0x00;
    for (int i = 0; i < plen && n < (int)sizeof(tmp); i++) tmp[n++] = provided[i];
    hmac_sha256(K, 32, tmp, n, K);
    hmac_sha256(K, 32, V, 32, V);
    if (plen > 0) {
        n = 0;
        for (int i = 0; i < 32; i++) tmp[n++] = V[i];
        tmp[n++] = 0x01;
        for (int i = 0; i < plen && n < (int)sizeof(tmp); i++) tmp[n++] = provided[i];
        hmac_sha256(K, 32, tmp, n, K);
        hmac_sha256(K, 32, V, 32, V);
    }
}

void rng_init(void) {
    for (int i = 0; i < 32; i++) { K[i] = 0x00; V[i] = 0x01; }
    uint8_t seed[32];
    collect_entropy(seed);
    drbg_update(seed, 32);
    gen_count = 0;
    ready = 1;
}

int rng_ready(void) { return ready; }

void rng_bytes(uint8_t* out, int n) {
    if (!ready) rng_init();
    if (gen_count > 64) {                 // reseed periódico
        uint8_t seed[32];
        collect_entropy(seed);
        drbg_update(seed, 32);
        gen_count = 0;
    }
    int pos = 0;
    while (pos < n) {
        hmac_sha256(K, 32, V, 32, V);
        int m = n - pos; if (m > 32) m = 32;
        for (int i = 0; i < m; i++) out[pos++] = V[i];
    }
    drbg_update(NULL, 0);
    gen_count++;
}
