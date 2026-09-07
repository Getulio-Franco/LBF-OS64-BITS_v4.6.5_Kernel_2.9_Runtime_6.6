/*
====================================================================
Arquivo: tls.c (TWebTLS — handshake TLS 1.2 + record layer AES-GCM)
Versão: 2.1
Data: 02/09/2026
Autor: LBF-OS Team + AI Assistant

v2.1: CONSOLIDADO (elimina erros de duplicidade):
  - UMA única read_record (retorna -10 p/ record > buffer)
  - read_enc_record e handshake_full usam read_record (read_record_raw removida)
  - Buffers de voo em 16KB static (cadeias > 8KB, ex: iana.org)
  - cert0/cert0_len expostos p/ Etapa 3 (X.509)
  - RNG real (DRBG) p/ chave ECDHE
Testado em:
    - LBF-OS Base_v4.6.5 / Kernel v2.9 / Runtime v6.3
====================================================================
*/
#include "tls.h"
#include "hmac.h"
#include "tls_prf.h"
#include "x25519.h"
#include "../system/string.h"
#include "../system/liblib.h"
#include "net_user/socket.h"
#include "Browser/crypto/aes_gcm.h"
#include "Browser/crypto/rng.h"

#define HS_NEW_SESSION_TICKET 4

extern void GUI_Memo_AddStr(void* memo, const char* s); // p/ log do app

static void put8(uint8_t** p, uint8_t v){ *(*p)++ = v; }
static void put16(uint8_t** p, uint16_t v){ *(*p)++=v>>8; *(*p)++=v&0xFF; }
static void putbuf(uint8_t** p, const uint8_t* s, int n){ for(int i=0;i<n;i++) *(*p)++=s[i]; }
static uint16_t get16(const uint8_t* p){ return (p[0]<<8)|p[1]; }

static void hs_feed(tls_ctx* c, const uint8_t* b, int n){ sha256_update(&c->hs, b, n); }

// buffer dedicado p/ o 1º cert (o body do flight é reutilizado
// pelos records seguintes antes de voltarmos ao app!)
static uint8_t cert0_buf[8192];

// envia um record de handshake
static int send_hs(tls_ctx* c, const uint8_t* hs_body, int hs_len){
    uint8_t rec[4096];
    rec[0]=RT_HS; rec[1]=0x03; rec[2]=0x01;         // record ver TLS1.0
    int total = hs_len;
    rec[3]=total>>8; rec[4]=total&0xFF;
    for(int i=0;i<hs_len;i++) rec[5+i]=hs_body[i];
    return send(c->sockfd, rec, 5+total, 0);
}

static int recv_exact(tls_ctx* c, uint8_t* b, int n){
    int got=0;
    while(got<n){
        int r = recv(c->sockfd, b+got, n-got, 0);
        if(r<=0) return -1;
        got+=r;
    }
    return 0;
}

/* ============================================================================
* ÚNICA read_record do arquivo (antes do flight, p/ C ver a declaração)
* retorna: len ok | -1 recv/fechou | -10 record maior que o buffer
* ============================================================================ */
static int read_record(tls_ctx* c, uint8_t* type, uint8_t* body, int max){
    uint8_t h[5];
    if (recv_exact(c, h, 5)) return -1;        // servidor fechou / erro de recv
    int len = (h[3]<<8)|h[4];
    if (len > max) return -10;                 // record maior que o buffer
    if (recv_exact(c, body, len)) return -1;
    *type = h[0];
    return len;
}

static void build_client_hello(tls_ctx* c, uint8_t* out, int* olen){
    uint8_t* p = out+4;                 // reserva header do handshake
    put16(&p, TLS_VER);
    putbuf(&p, c->cr, 32);
    put8(&p, 0);                        // session_id_len
    // cipher suites: ECDHE_RSA / ECDHE_ECDSA com AES_128_GCM_SHA256
    uint8_t* cslen = p; put16(&p,0);
    uint8_t* cs = p;
    put16(&p,0xC02F); put16(&p,0xC02B);
    cslen[0]=(p-cs)>>8; cslen[1]=(p-cs)&0xFF;
    put8(&p,1); put8(&p,0);             // compression null
    // extensões
    uint8_t* extlen = p; put16(&p,0);
    uint8_t* ext = p;
    // SNI
    int hl = strlen(c->host);
    put16(&p,0x0000);
    put16(&p, hl+5);
    put16(&p, hl+3);
    put8(&p,0);
    put16(&p, hl);
    for(int i=0;i<hl;i++) put8(&p, c->host[i]);
    // supported_groups: x25519
    put16(&p,0x000A); put16(&p,4); put16(&p,2); put16(&p,0x001D);
    // ec_point_formats
    put16(&p,0x000B); put16(&p,2); put8(&p,1); put8(&p,0);
    // signature_algorithms
    put16(&p,0x000D); put16(&p,8); put16(&p,6);
    put16(&p,0x0401); put16(&p,0x0403); put16(&p,0x0201);
    extlen[0]=(p-ext)>>8; extlen[1]=(p-ext)&0xFF;

    int hs_len = p-(out+4);
    out[0]=HS_CLIENT_HELLO;
    out[1]=hs_len>>16; out[2]=(hs_len>>8)&0xFF; out[3]=hs_len&0xFF;
    *olen = 4+hs_len;
}

/* ============================ ETAPA 2a ============================ */
int tls_handshake_flight(tls_ctx* c){
    sha256_init(&c->hs);

    // 🔒 MELHORIA #1 (defensiva): se caller esqueceu c->cr, preenche aqui
    // (evita handshake com client_random = zeros)
    int cr_zero = 1;
    for (int i = 0; i < 32; i++) if (c->cr[i]) { cr_zero = 0; break; }
    if (cr_zero) rng_bytes(c->cr, 32);

    // chave ECDHE REAL via DRBG (RDRAND + jitter RDTSC)
    rng_bytes(c->cli_priv, 32);
    int allz = 1;
    for (int i = 0; i < 32; i++) if (c->cli_priv[i]) { allz = 0; break; }
    if (allz) rng_bytes(c->cli_priv, 32);            // paranoia: nunca zero
    x25519_base(c->cli_pub, c->cli_priv);

    uint8_t ch[512]; int ch_len;
    build_client_hello(c, ch, &ch_len);
    hs_feed(c, ch, ch_len);
    send_hs(c, ch, ch_len);

    // lê o voo do servidor (16KB static: cadeias grandes como iana.org)
    static uint8_t body[16384 + 2048];
    uint8_t type;
    bool done=false;
    while(!done){
        int len = read_record(c, &type, body, sizeof(body));
        if (len == -10) return -10;                  // nem 16KB coube (raríssimo)
        if (len < 0)    return -1;                   // servidor FECHOU (política)
        if (type == RT_ALERT) return -(1000 + body[1]); // codifica o alert desc
        if (type != RT_HS)    return -2;
        int off=0;
        while(off<len){
            uint8_t hst = body[off];
            int hsl = (body[off+1]<<16)|(body[off+2]<<8)|body[off+3];
            uint8_t* m = body+off;
            hs_feed(c, m, 4+hsl);
            uint8_t* b = body+off+4;
            if(hst==HS_SERVER_HELLO){
                // b: ver(2) rand(32) sidlen(1).. cipher(2) comp(1)
                for(int i=0;i<32;i++) c->sr[i]=b[2+i];
                int sid = b[34];
                c->cipher = get16(b+35+sid);
                } else if(hst==HS_CERTIFICATE){
                    c->cert_len = (b[0]<<16)|(b[1]<<8)|b[2];
                    static uint8_t cbuf[3][8192];          // cópia imediata (body é reutilizado!)
                    uint32_t o = 3;
                    c->chain_n = 0;
                    while (o + 3 <= (uint32_t)hsl && c->chain_n < 3) {
                        uint32_t cl = (b[o]<<16)|(b[o+1]<<8)|b[o+2];
                        if (cl == 0 || cl > 8192 || o + 3 + cl > (uint32_t)hsl) break;
                        for (uint32_t i = 0; i < cl; i++) cbuf[c->chain_n][i] = b[o+3+i];
                        c->chain[c->chain_n]     = cbuf[c->chain_n];
                        c->chain_len[c->chain_n] = cl;
                        c->chain_n++;
                        o += 3 + cl;
                    }
                    if (c->chain_n > 0) { c->cert0 = c->chain[0]; c->cert0_len = c->chain_len[0]; }
                    else                { c->cert0 = NULL;       c->cert0_len = 0; }
            } else if(hst==HS_SERVER_KEY_EXCHANGE){
                // curve_type(1) named_curve(2) pubkey_len(1) pubkey(32)
                int pklen = b[3];
                for(int i=0;i<pklen && i<32;i++) c->srv_pub[i] = b[4+i];
            } else if(hst==HS_HELLO_DONE){
                done=true;
            }
            off += 4+hsl;
        }
    }

    // 🔒 MELHORIA #2 (defensiva): pubkey do servidor inválida = falha cedo
    int srv_zero = 1;
    for (int i = 0; i < 32; i++) if (c->srv_pub[i]) { srv_zero = 0; break; }
    if (srv_zero) return -20;   // servidor não mandou pubkey ECDHE

    // premaster = x25519(cli_priv, srv_pub)
    uint8_t pre[32];
    x25519(pre, c->cli_priv, c->srv_pub);
    // master = PRF(pre, "master secret", cr+sr)
    tls12_prf(pre,32,"master secret", c->cr,32, c->sr,32, c->master,48);
    // key_block = PRF(master, "key expansion", sr+cr)
    uint8_t kb[40];
    tls12_prf(c->master,48,"key expansion", c->sr,32, c->cr,32, kb,40);
    for(int i=0;i<16;i++){ c->cli_key[i]=kb[i]; c->srv_key[i]=kb[16+i]; }
    for(int i=0;i<4;i++){ c->cli_iv[i]=kb[32+i]; c->srv_iv[i]=kb[36+i]; }
    return 0;
}

static int send_record_raw(tls_ctx* c, uint8_t type, uint8_t v0, uint8_t v1,
                           const uint8_t* body, int len) {
    uint8_t hdr[5];
    hdr[0]=type; hdr[1]=v0; hdr[2]=v1; hdr[3]=len>>8; hdr[4]=len&0xFF;
    if (send(c->sockfd, hdr, 5, 0) < 0) return -1;
    if (len > 0 && send(c->sockfd, body, len, 0) < 0) return -1;
    return 0;
}

/* Record criptografado (AES-128-GCM, RFC 5288):
   nonce = iv_implicit(4) + explicit(8=seq) ; AAD = seq(8)+type+ver+len(plaintext) */
static int write_enc_record(tls_ctx* c, uint8_t type, const uint8_t* pt, int ptlen) {
    static uint8_t body[4300];
    uint8_t nonce[12], aad[13], tag[16];
    for (int i = 0; i < 8;  i++) { nonce[4+i] = (uint8_t)(c->wseq >> (56-8*i)); body[i] = nonce[4+i]; }
    for (int i = 0; i < 4;  i++) nonce[i] = c->cli_iv[i];
    for (int i = 0; i < 8;  i++) aad[i] = body[i];
    aad[8]=type; aad[9]=0x03; aad[10]=0x03; aad[11]=ptlen>>8; aad[12]=ptlen&0xFF;
    aes_gcm_encrypt(c->cli_key, nonce, aad, 13, pt, ptlen, body+8, tag);
    for (int i = 0; i < 16; i++) body[8+ptlen+i] = tag[i];
    c->wseq++;
    return send_record_raw(c, type, 0x03, 0x03, body, 8 + ptlen + 16);
}

static int read_enc_record(tls_ctx* c, uint8_t* type, uint8_t* pt, int max) {
    static uint8_t body[16384 + 2048];   // records TLS chegam a ~16KB
    uint8_t t;
    int len = read_record(c, &t, body, sizeof(body));   // ← read_record (única)
    if (len < 0) return -1;
    if (t == RT_CCS) { *type = t; return 0; }
    if (len < 8 + 16) return -2;
    int ctlen = len - 8 - 16;
    if (ctlen > max) return -3;
    uint8_t nonce[12], aad[13], tag[16];
    for (int i = 0; i < 4; i++) nonce[i] = c->srv_iv[i];
    for (int i = 0; i < 8; i++) nonce[4+i] = body[i];
    for (int i = 0; i < 8; i++) aad[i] = (uint8_t)(c->rseq >> (56-8*i));
    aad[8]=t; aad[9]=0x03; aad[10]=0x03; aad[11]=ctlen>>8; aad[12]=ctlen&0xFF;
    for (int i = 0; i < 16; i++) tag[i] = body[8+ctlen+i];
    if (aes_gcm_decrypt(c->srv_key, nonce, aad, 13, body+8, ctlen, pt, tag) != 0) return -4;
    c->rseq++;
    *type = t;
    return ctlen;
}

/* ============================ ETAPA 2b ============================ */
int tls_handshake_full(tls_ctx* c) {
    // 1) ClientKeyExchange (ECDHE: nossa pubkey de 32 bytes)
    uint8_t cke[4+33];
    cke[0]=HS_CLIENT_KEY_EXCHANGE; cke[1]=0; cke[2]=0; cke[3]=33; cke[4]=32;
    for (int i = 0; i < 32; i++) cke[5+i] = c->cli_pub[i];
    hs_feed(c, cke, sizeof(cke));
    if (send_record_raw(c, RT_HS, 0x03, 0x01, cke, sizeof(cke)) < 0) return -1;

    // 2) verify_data do CLIENTE = PRF(master, "client finished", hash(até CKE))
    sha256_ctx snap = c->hs;
    uint8_t h1[32]; sha256_final(&snap, h1);
    uint8_t fin[4+12];
    fin[0]=HS_FINISHED; fin[1]=0; fin[2]=0; fin[3]=12;
    tls12_prf(c->master, 48, "client finished", h1, 32, NULL, 0, fin+4, 12);

    // 3) ChangeCipherSpec + Finished criptografado
    uint8_t ccs[1] = {1};
    if (send_record_raw(c, RT_CCS, 0x03, 0x03, ccs, 1) < 0) return -2;
    c->wseq = 0;
    if (write_enc_record(c, RT_HS, fin, sizeof(fin)) < 0) return -3;
    hs_feed(c, fin, sizeof(fin));   // entra no hash p/ o verify do servidor

    // 4) CCS do servidor (sem cifra) + Finished dele (com cifra)
    uint8_t type; uint8_t pt[1500];
    int n = read_record(c, &type, pt, sizeof(pt));      // ← read_record (única)
    if (n < 0 || type != RT_CCS) return -4;
    c->rseq = 0;

    for (;;) {
        n = read_enc_record(c, &type, pt, sizeof(pt));
        if (n < 0) return -5;
        if (type == RT_ALERT) return -6;
        if (type != RT_HS) continue;
        if (pt[0] == HS_NEW_SESSION_TICKET) { hs_feed(c, pt, n); continue; } // entra no hash!
        if (pt[0] == HS_FINISHED) {
            sha256_ctx snap2 = c->hs;
            uint8_t h2[32]; sha256_final(&snap2, h2);
            uint8_t exp[12];
            tls12_prf(c->master, 48, "server finished", h2, 32, NULL, 0, exp, 12);
            int diff = 0;
            for (int i = 0; i < 12; i++) diff |= pt[4+i] ^ exp[i];
            return diff == 0 ? 0 : -7;
        }
    }
}

int tls_send_app_data(tls_ctx* c, const uint8_t* data, int len) {
    return write_enc_record(c, RT_APP, data, len);
}
int tls_recv_app_data(tls_ctx* c, uint8_t* buf, int max, uint8_t* type) {
    return read_enc_record(c, type, buf, max);
}
