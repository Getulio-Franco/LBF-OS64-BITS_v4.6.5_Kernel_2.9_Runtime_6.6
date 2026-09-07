/*
====================================================================
Arquivo: x509.c — parser DER + validadores (Etapa 3 do TWebTLS)
Versão: 2.0
Data: 02/09/2026
Autor: LBF-OS Team + AI Assistant

v2.0: parser robusto com suporte a:
  - SPKI: RSA, EC-P256, EC-P384
  - Assinaturas: RSA-SHA256, RSA-SHA384, RSA-SHA512, RSA-PSS,
                 ECDSA-SHA256, ECDSA-SHA384
  - Extensões: Subject Alternative Name (SAN), Key Usage, EKU
  - Validação de hostname (CN + SAN)
  - Validação de validade (UTC string → Unix ts)
====================================================================
*/
#include "x509.h"
#include "../system/string.h"

/* ================= DER PRIMITIVES ================= */

typedef struct { const uint8_t* p; const uint8_t* end; } der_t;

static int der_get(der_t* d, uint8_t* tag, const uint8_t** body, uint32_t* len) {
    if (d->p >= d->end) return -1;
    *tag = *d->p++;
    if (d->p >= d->end) return -1;
    uint8_t b = *d->p++;
    uint32_t L;
    if (b < 0x80) L = b;
    else {
        int n = b & 0x7F;
        if (n == 0 || n > 4) return -1;
        L = 0;
        while (n--) { if (d->p >= d->end) return -1; L = (L << 8) | *d->p++; }
    }
    if (d->p + L > d->end) return -1;
    *body = d->p; *len = L;
    d->p += L;
    return 0;
}

// peek sem consumir (p/ detectar tags contextuais)
static int der_peek_tag(const der_t* d, uint8_t* tag) {
    if (d->p >= d->end) return -1;
    *tag = *d->p;
    return 0;
}

static void copy_cstr(char* dst, size_t max, const uint8_t* s, uint32_t n) {
    if (n >= max) n = max - 1;
    for (uint32_t i = 0; i < n; i++) {
        char c = (char)s[i];
        // filtrar não-printable (DER pode ter UTF-8)
        if (c < 0x20 || c == 0x7F) c = '?';
        dst[i] = c;
    }
    dst[n] = 0;
}

static int oid_eq(const uint8_t* a, uint32_t al, const uint8_t* b, uint32_t bl) {
    if (al != bl) return 0;
    for (uint32_t i = 0; i < al; i++) if (a[i] != b[i]) return 0;
    return 1;
}

/* ================= OIDs ================= */

// atributos de nome
static const uint8_t OID_CN[3]   = {0x55,0x04,0x03};

// algoritmos de chave pública
static const uint8_t OID_RSA[9]  = {0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x01};
static const uint8_t OID_EC[7]   = {0x2A,0x86,0x48,0xCE,0x3D,0x02,0x01};
static const uint8_t OID_P256[8] = {0x2A,0x86,0x48,0xCE,0x3D,0x03,0x01,0x07};
static const uint8_t OID_P384[5] = {0x2B,0x81,0x04,0x00,0x22};

// algoritmos de assinatura
static const uint8_t OID_S256[9] = {0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x0B}; // sha256RSA
static const uint8_t OID_S384[9] = {0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x0C}; // sha384RSA
static const uint8_t OID_S512[9] = {0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x0D}; // sha512RSA
static const uint8_t OID_PSS[9]  = {0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x0A}; // rsa-pss
static const uint8_t OID_E256[8] = {0x2A,0x86,0x48,0xCE,0x3D,0x04,0x03,0x02};      // ecdsa-SHA256
static const uint8_t OID_E384[8] = {0x2A,0x86,0x48,0xCE,0x3D,0x04,0x03,0x03};      // ecdsa-SHA384

// extensões
static const uint8_t OID_SAN[3]  = {0x55,0x1D,0x11};                        // subjectAltName
static const uint8_t OID_KU[3]   = {0x55,0x1D,0x0F};                        // keyUsage
static const uint8_t OID_EKU[3]  = {0x55,0x1D,0x25};                       // extKeyUsage
static const uint8_t OID_SRV[8]  = {0x2B,0x06,0x01,0x05,0x05,0x07,0x03,0x01}; // id-kp-serverAuth

/* ================= PARSERS AUXILIARES ================= */

// lê CN de um Name (SEQUENCE OF SET OF AttributeTypeAndValue)
static void parse_name(const uint8_t* body, uint32_t len, char* cn, size_t max) {
    der_t d = { body, body + len };
    uint8_t t; const uint8_t* b; uint32_t l;
    cn[0] = 0;
    while (der_get(&d, &t, &b, &l) == 0 && t == 0x31) {
        der_t s = { b, b + l };
        uint8_t t2; const uint8_t* b2; uint32_t l2;
        if (der_get(&s, &t2, &b2, &l2) != 0 || t2 != 0x30) continue;
        der_t av = { b2, b2 + l2 };
        uint8_t t3; const uint8_t* oid; uint32_t ol;
        if (der_get(&av, &t3, &oid, &ol) != 0 || t3 != 0x06) continue;
        uint8_t t4; const uint8_t* val; uint32_t vl;
        if (der_get(&av, &t4, &val, &vl) != 0) continue;
        if (oid_eq(oid, ol, OID_CN, 3)) copy_cstr(cn, max, val, vl);
    }
}

// identifica algoritmo de assinatura a partir do OID
static uint8_t sig_alg_of(const uint8_t* oid, uint32_t len) {
    if (oid_eq(oid, len, OID_S256, 9)) return SIG_RSA_SHA256;
    if (oid_eq(oid, len, OID_S384, 9)) return SIG_RSA_SHA384;
    if (oid_eq(oid, len, OID_S512, 9)) return SIG_RSA_SHA512;
    if (oid_eq(oid, len, OID_PSS,  9)) return SIG_RSA_PSS;
    if (oid_eq(oid, len, OID_E256, 8)) return SIG_ECDSA_SHA256;
    if (oid_eq(oid, len, OID_E384, 8)) return SIG_ECDSA_SHA384;
    return SIG_UNKNOWN;
}

// identifica curva EC a partir do OID (parâmetro do SPKI)
static uint8_t ec_curve_of(const uint8_t* oid, uint32_t len) {
    if (oid_eq(oid, len, OID_P256, 8)) return SPKI_EC_P256;
    if (oid_eq(oid, len, OID_P384, 5)) return SPKI_EC_P384;
    return SPKI_UNKNOWN;
}

// parse das extensões (SEQUENCE OF Extension)
static void parse_extensions(const uint8_t* body, uint32_t len, x509_info* out) {
    der_t d = { body, body + len };
    uint8_t t; const uint8_t* b; uint32_t l;
    while (der_get(&d, &t, &b, &l) == 0 && t == 0x30) {   // Extension ::= SEQUENCE
        der_t ext = { b, b + l };
        uint8_t te; const uint8_t* be; uint32_t le;

        // extnID (OID)
        if (der_get(&ext, &te, &be, &le) != 0 || te != 0x06) continue;
        const uint8_t* oid = be; uint32_t olen = le;

        // critical BOOLEAN OPTIONAL (pula se presente)
        uint8_t tp; const uint8_t* bp; uint32_t lp;
        if (der_peek_tag(&ext, &tp) == 0 && tp == 0x01) {
            if (der_get(&ext, &tp, &bp, &lp) != 0) continue;
        }

        // extnValue OCTET STRING (conteúdo DER encapsulado)
        if (der_get(&ext, &te, &be, &le) != 0 || te != 0x04) continue;
        const uint8_t* val = be; uint32_t vlen = le;

        /* ----- SAN: dNSName (tag 0x82 = context [2]) ----- */
        if (oid_eq(oid, olen, OID_SAN, 3)) {
            der_t sv = { val, val + vlen };
            uint8_t ts; const uint8_t* bs; uint32_t ls;
            if (der_get(&sv, &ts, &bs, &ls) != 0 || ts != 0x30) continue;
            der_t seq = { bs, bs + ls };
            uint8_t tg; const uint8_t* bg; uint32_t lg;
            while (der_get(&seq, &tg, &bg, &lg) == 0 && out->san_n < X509_MAX_SAN) {
                if (tg == 0x82) {  // dNSName [2]
                    copy_cstr(out->san_dns[out->san_n], X509_MAX_SAN_LEN, bg, lg);
                    out->san_n++;
                }
            }
            out->has_san = 1;
        }
        /* ----- Key Usage: BIT STRING com flags ----- */
        else if (oid_eq(oid, olen, OID_KU, 3)) {
            der_t kv = { val, val + vlen };
            uint8_t tk; const uint8_t* bk; uint32_t lk;
            if (der_get(&kv, &tk, &bk, &lk) == 0 && tk == 0x03 && lk >= 2) {
                uint8_t bits = bk[1];
                if (bits & 0x80) out->ku_digital_sig = 1;  // bit 0
            }
        }
        /* ----- Extended Key Usage: id-kp-serverAuth ----- */
        else if (oid_eq(oid, olen, OID_EKU, 3)) {
            der_t ev = { val, val + vlen };
            uint8_t te2; const uint8_t* be2; uint32_t le2;
            if (der_get(&ev, &te2, &be2, &le2) == 0 && te2 == 0x30) {
                der_t eseq = { be2, be2 + le2 };
                uint8_t to; const uint8_t* bo; uint32_t lo;
                while (der_get(&eseq, &to, &bo, &lo) == 0) {
                    if (to == 0x06 && oid_eq(bo, lo, OID_SRV, 8)) {
                        out->has_server_auth = 1;
                    }
                }
            }
        }
    }
}

/* ================= API PÚBLICA ================= */

int x509_parse(const uint8_t* der, uint32_t len, x509_info* out) {
    if (!der || len < 4 || !out) return -1;
    memset(out, 0, sizeof(*out));

    uint8_t t; const uint8_t* b; uint32_t l;
    der_t root = { der, der + len };
    if (der_get(&root, &t, &b, &l) != 0 || t != 0x30) return -2;
    der_t cert = { b, b + l };

    // tbsCertificate (guarda range p/ verify)
    const uint8_t* tbs_start = cert.p;
    if (der_get(&cert, &t, &b, &l) != 0 || t != 0x30) return -3;
    out->tbs = tbs_start; out->tbs_len = (uint32_t)(b + l - tbs_start);
    der_t tbs = { b, b + l };

    // version [0] EXPLICIT INTEGER DEFAULT v1
    if (tbs.p < tbs.end && tbs.p[0] == 0xA0) {
        uint8_t t2; const uint8_t* b2; uint32_t l2;
        if (der_get(&tbs, &t2, &b2, &l2) != 0) return -4;
    }
    // serialNumber
    if (der_get(&tbs, &t, &b, &l) != 0 || t != 0x02) return -5;
    // signature AlgorithmIdentifier
    if (der_get(&tbs, &t, &b, &l) != 0 || t != 0x30) return -6;
    // issuer
    if (der_get(&tbs, &t, &b, &l) != 0 || t != 0x30) return -7;
    parse_name(b, l, out->issuer_cn, sizeof(out->issuer_cn));
    // validity
    if (der_get(&tbs, &t, &b, &l) != 0 || t != 0x30) return -8;
    der_t val = { b, b + l };
    uint8_t tv; const uint8_t* bv; uint32_t lv;
    if (der_get(&val, &tv, &bv, &lv) == 0) copy_cstr(out->not_before, sizeof(out->not_before), bv, lv);
    if (der_get(&val, &tv, &bv, &lv) == 0) copy_cstr(out->not_after,  sizeof(out->not_after),  bv, lv);
    // subject
    if (der_get(&tbs, &t, &b, &l) != 0 || t != 0x30) return -9;
    parse_name(b, l, out->subject_cn, sizeof(out->subject_cn));

    // subjectPublicKeyInfo
    if (der_get(&tbs, &t, &b, &l) != 0 || t != 0x30) return -10;
    der_t spki = { b, b + l };
    uint8_t ta; const uint8_t* ba; uint32_t la;
    if (der_get(&spki, &ta, &ba, &la) != 0 || ta != 0x30) return -11;
    der_t alg = { ba, ba + la };
    uint8_t to; const uint8_t* bo; uint32_t lo;
    if (der_get(&alg, &to, &bo, &lo) != 0 || to != 0x06) return -12;

    if (oid_eq(bo, lo, OID_RSA, 9)) {
        out->spki_type = SPKI_RSA;
    } else if (oid_eq(bo, lo, OID_EC, 7)) {
        out->spki_type = SPKI_EC_P256;   // default; tenta refinar pela curva
        uint8_t tc; const uint8_t* bc; uint32_t lc;
        if (der_get(&alg, &tc, &bc, &lc) == 0 && tc == 0x06) {
            uint8_t curve = ec_curve_of(bc, lc);
            if (curve != SPKI_UNKNOWN) out->spki_type = curve;
        }
    }

    uint8_t tb; const uint8_t* bb; uint32_t lb;
    if (der_get(&spki, &tb, &bb, &lb) != 0 || tb != 0x03) return -13;
    if (lb > 1) { out->pubkey = bb + 1; out->pubkey_len = lb - 1; }

    // extensions [3] EXPLICIT (v3)
    while (tbs.p < tbs.end) {
        uint8_t tp; const uint8_t* bp; uint32_t lp;
        if (der_get(&tbs, &tp, &bp, &lp) != 0) break;
        if (tp == 0xA3) {   // extensions
            der_t ext_wrap = { bp, bp + lp };
            uint8_t tw; const uint8_t* bw; uint32_t lw;
            if (der_get(&ext_wrap, &tw, &bw, &lw) == 0 && tw == 0x30) {
                parse_extensions(bw, lw, out);
            }
        }
        // outros campos opcionais do TBS (issuerUniqueID, subjectUniqueID): ignoramos
    }

    // signatureAlgorithm (cert level)
    if (der_get(&cert, &t, &b, &l) != 0 || t != 0x30) return -14;
    der_t sa = { b, b + l };
    uint8_t ts; const uint8_t* bs; uint32_t ls;
    if (der_get(&sa, &ts, &bs, &ls) != 0 || ts != 0x06) return -15;
    out->sig_type = sig_alg_of(bs, ls);

    // signatureValue BIT STRING
    if (der_get(&cert, &t, &b, &l) != 0 || t != 0x03) return -16;
    if (l > 1) { out->sig = b + 1; out->sig_len = l - 1; }
    return 0;
}

/* ================= VALIDAÇÃO DE HOSTNAME ================= */

// comparação case-insensitive + wildcard simples (*.domain)
static int host_match(const char* pat, const char* host) {
    if (pat[0] == '*' && pat[1] == '.') {
        // wildcard: *.foo.com aceita bar.foo.com mas não foo.com
        const char* p = pat + 2;
        const char* dot = strchr(host, '.');
        if (!dot) return 0;
        // comparação case-insensitive do sufixo
        const char* a = dot + 1, *b = p;
        while (*a && *b) {
            char ca = *a, cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) return 0;
            a++; b++;
        }
        return (*a == 0 && *b == 0);
    }
    // literal: comparação case-insensitive
    const char* a = pat, *b = host;
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        a++; b++;
    }
    return (*a == 0 && *b == 0);
}

int x509_check_hostname(const x509_info* xi, const char* host) {
    if (!xi || !host) return -1;
    // prioridade: SAN dNSName > CN
    if (xi->san_n > 0) {
        for (int i = 0; i < xi->san_n; i++) {
            if (host_match(xi->san_dns[i], host)) return 0;
        }
        return -2;   // SAN presente mas nenhum confere
    }
    // fallback CN
    if (xi->subject_cn[0] && host_match(xi->subject_cn, host)) return 0;
    return -3;
}

/* ================= VALIDAÇÃO DE VALIDADE ================= */

// parse de UTC time "YYMMDDHHMMSSZ" ou "YYYYMMDDHHMMSSZ" -> componentes
static int parse_utc(const char* s, int* y, int* m, int* d, int* H, int* M, int* S) {
    int n = 0; while (s[n] && s[n] != 'Z') n++;
    int off = 0;
    if (n == 13) {       // YYMMDDHHMMSSZ
        *y = (s[0]-'0')*10 + (s[1]-'0');
        if (*y >= 50) *y += 1900; else *y += 2000;   // RFC 5280 §4.1.2.5
        off = 2;
    } else if (n == 15) { // YYYYMMDDHHMMSSZ
        *y = (s[0]-'0')*1000 + (s[1]-'0')*100 + (s[2]-'0')*10 + (s[3]-'0');
        off = 4;
    } else return -1;
    *m = (s[off+0]-'0')*10 + (s[off+1]-'0');
    *d = (s[off+2]-'0')*10 + (s[off+3]-'0');
    *H = (s[off+4]-'0')*10 + (s[off+5]-'0');
    *M = (s[off+6]-'0')*10 + (s[off+7]-'0');
    *S = (s[off+8]-'0')*10 + (s[off+9]-'0');
    return 0;
}

// UTC components -> Unix timestamp (simplificado, sem tz)
static uint64_t utc_to_unix(int y, int m, int d, int H, int M, int S) {
    static const int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint64_t days = 0;
    for (int i = 1970; i < y; i++) {
        days += 365;
        if ((i % 4 == 0 && i % 100 != 0) || i % 400 == 0) days++;
    }
    int leap = ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0);
    for (int i = 0; i < m - 1; i++) {
        days += mdays[i];
        if (i == 1 && leap) days++;
    }
    days += (d - 1);
    return days * 86400ULL + H * 3600ULL + M * 60ULL + S;
}

int x509_check_validity(const x509_info* xi, uint64_t ts_unix) {
    if (!xi) return -1;
    if (ts_unix == 0) return 0;  // sem RTC → skip

    int y, m, d, H, M, S;
    if (parse_utc(xi->not_before, &y, &m, &d, &H, &M, &S) != 0) return -2;
    uint64_t nb = utc_to_unix(y, m, d, H, M, S);
    if (parse_utc(xi->not_after, &y, &m, &d, &H, &M, &S) != 0) return -3;
    uint64_t na = utc_to_unix(y, m, d, H, M, S);

    if (ts_unix < nb) return -4;   // ainda não válido
    if (ts_unix > na) return -5;   // expirado
    return 0;
}

/* ================= NOMES LEGÍVEIS P/ LOG ================= */

const char* x509_spki_name(uint8_t t) {
    switch (t) {
        case SPKI_RSA:     return "RSA";
        case SPKI_EC_P256: return "EC-P256";
        case SPKI_EC_P384: return "EC-P384";
        default:           return "unknown";
    }
}

const char* x509_sig_name(uint8_t t) {
    switch (t) {
        case SIG_RSA_SHA256:   return "RSA-SHA256";
        case SIG_RSA_SHA384:   return "RSA-SHA384";
        case SIG_RSA_SHA512:   return "RSA-SHA512";
        case SIG_RSA_PSS:      return "RSA-PSS";
        case SIG_ECDSA_SHA256: return "ECDSA-SHA256";
        case SIG_ECDSA_SHA384: return "ECDSA-SHA384";
        default:               return "unknown";
    }
}
