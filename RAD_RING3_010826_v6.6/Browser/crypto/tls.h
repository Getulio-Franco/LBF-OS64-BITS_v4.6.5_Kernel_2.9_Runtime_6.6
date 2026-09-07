#ifndef TLS_H
#define TLS_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "sha256.h"

#define TLS_VER 0x0303
// record types
#define RT_CCS 20
#define RT_ALERT 21
#define RT_HS 22
#define RT_APP 23
// handshake types
#define HS_CLIENT_HELLO 1
#define HS_SERVER_HELLO 2
#define HS_CERTIFICATE 11
#define HS_SERVER_KEY_EXCHANGE 12
#define HS_HELLO_DONE 14
#define HS_CLIENT_KEY_EXCHANGE 16
#define HS_FINISHED 20

#define TLS_MAX_CHAIN 3

typedef struct {
    int sockfd;
    char host[128];
    uint8_t cr[32], sr[32];       // client/server random
    uint8_t master[48];
    uint8_t cli_key[16], srv_key[16];
    uint8_t cli_iv[4],  srv_iv[4];
    uint16_t cipher;               // suíte escolhida
    uint8_t srv_pub[32];           // pubkey ECDHE do servidor
    uint8_t cli_priv[32], cli_pub[32];
    uint32_t cert_len;             // tamanho total da cadeia
    const uint8_t* cert0;          // ponteiro p/ o 1º cert DER
    uint32_t cert0_len;            // tamanho do 1º cert (Etapa 3 / X.509)
    const uint8_t* chain[TLS_MAX_CHAIN];   // leaf + intermediária (+ raiz se vier)  ← UMA vez só!
    uint32_t chain_len[TLS_MAX_CHAIN];
    int chain_n;
    sha256_ctx hs;                 // hash acumulado p/ Finished
    uint64_t wseq, rseq;           // sequências de escrita/leitura (records)
} tls_ctx;

int  tls_handshake_flight(tls_ctx* c);  // 2a: ClientHello + parse + chaves
int  tls_handshake_full(tls_ctx* c);    // 2b: CKE+CCS+Finished + verifica servidor
int  tls_send_app_data(tls_ctx* c, const uint8_t* data, int len);
int  tls_recv_app_data(tls_ctx* c, uint8_t* buf, int max, uint8_t* type);
#endif
