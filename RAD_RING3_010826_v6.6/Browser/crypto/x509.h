#ifndef X509_H
#define X509_H
#include <stdint.h>

// tipos de chave pública
#define SPKI_UNKNOWN  0
#define SPKI_RSA      1
#define SPKI_EC_P256  2
#define SPKI_EC_P384  3

// tipos de algoritmo de assinatura
#define SIG_UNKNOWN     0
#define SIG_RSA_SHA256  1
#define SIG_ECDSA_SHA256 2
#define SIG_RSA_PSS     3
#define SIG_ECDSA_SHA384 4
#define SIG_RSA_SHA384  5
#define SIG_RSA_SHA512  6

// limites
#define X509_MAX_CN      80
#define X509_MAX_SAN     8
#define X509_MAX_SAN_LEN 128

typedef struct {
    // identificação
    char subject_cn[X509_MAX_CN];
    char issuer_cn[X509_MAX_CN];
    char not_before[24];
    char not_after[24];

    // SAN (Subject Alternative Name) — dNSName
    char san_dns[X509_MAX_SAN][X509_MAX_SAN_LEN];
    int  san_n;

    // algoritmos
    uint8_t spki_type;      // SPKI_*
    uint8_t sig_type;       // SIG_*

    // ranges brutos p/ verify criptográfico
    const uint8_t* tbs;          uint32_t tbs_len;     // TBS (o que foi assinado)
    const uint8_t* sig;          uint32_t sig_len;     // assinatura DER
    const uint8_t* pubkey;       uint32_t pubkey_len;  // conteúdo do BIT STRING

    // flags de extensão
    uint8_t has_san;
    uint8_t has_server_auth;    // EKU id-kp-serverAuth
    uint8_t ku_digital_sig;     // keyUsage bit 0
} x509_info;

// parser DER completo
int x509_parse(const uint8_t* der, uint32_t len, x509_info* out);

// valida hostname contra CN + SAN (retorna 0 = confere)
int x509_check_hostname(const x509_info* xi, const char* host);

// valida validade contra timestamp Unix (0 = dentro da janela)
// ts_unix = 0 → pula a checagem
int x509_check_validity(const x509_info* xi, uint64_t ts_unix);

// nomes legíveis p/ log
const char* x509_spki_name(uint8_t t);
const char* x509_sig_name(uint8_t t);
#endif
