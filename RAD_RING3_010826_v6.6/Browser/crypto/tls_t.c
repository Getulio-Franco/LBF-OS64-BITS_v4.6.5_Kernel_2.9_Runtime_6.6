/*
====================================================================
Arquivo: tls_t.c (Teste Handshake TLS 1.2 — Etapa 2a)
Versão: 2.0
Data: 01/09/2026
Autor: LBF-OS Team + AI Assistant

Descrição:
    App de teste do handshake TLS 1.2 (TWebTLS Etapa 2a).
    v2.0: REFEITO sobre a base do crypto.c (loop de eventos completo):
      - Janela responde a clique/foco/fechamento no Explorer
      - Botão HANDSHAKE: rede + DNS + connect :443 +
        tls_handshake_flight() + diagnóstico no memo
Testado em:
    - LBF-OS Base_v4.6.5 / Kernel v2.9 / Runtime v6.3
====================================================================
*/
#include "Runtime_sdk/sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"
#include "../system/sysutils.h"
#include "Runtime_sdk/components/TOS_IPC.h"

// Pilha de Rede Ring 3
#include "net_user/net_utils.h"
#include "net_user/net_interface.h"
#include "net_user/net_poll.h"
#include "net_user/arp.h"
#include "net_user/ip.h"
#include "net_user/dhcp.h"
#include "net_user/dns.h"
#include "net_user/socket.h"

// TWebTLS Etapa 2a
#include "Browser/crypto/tls.h"
#include "Browser/crypto/x509.h"
#include "Browser/crypto/rng.h" 
#include "Browser/crypto/ec256.h"
#include "Browser/crypto/sha256.h"
#include "Browser/crypto/rsa_verify.h"

void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;
extern void GUI_Memo_AddStr(TGUIControl* memo, const char* str);
extern void GUI_Memo_Clear(TGUIControl* memo);

int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth = 520, winHeight = 430;
TGUIControl* BtnRun  = NULL;
TGUIControl* LogMemo = NULL;

static bool g_net_ready = false;
static uint32_t g_dns_server = 0x0302000A;

typedef struct {
    uint8_t dummy[sizeof(IPC_WINDOW_LIST[0])];
    volatile uint8_t fila_teclado_virtual;
    volatile uint8_t tem_evento_teclado;
} __attribute__((packed)) AppWindowInfoExtended;

/* ============================================================================
* FUNÇÕES DE JANELA / IPC (mesmo padrão crypto.c / image_t.c)
* ============================================================================ */
char Obter_Tecla_Entrada(void) {
    if (my_app_slot < 0) return 0;
    AppWindowInfoExtended* ext = (AppWindowInfoExtended*)&IPC_WINDOW_LIST[my_app_slot];
    if (ext->tem_evento_teclado == 1) {
        char key = (char)ext->fila_teclado_virtual;
        ext->tem_evento_teclado = 0;
        return key;
    }
    return 0;
}

void Flush_Grafico_Janela(void) {
    if (my_app_slot < 0 || !MyApp.MainWindow) return;
    gui_draw_form((TForm*)MyApp.MainWindow);
    gui_render_form((TForm*)MyApp.MainWindow);
    OS_IPC_FlipBuffers(my_app_slot, winWidth, winHeight);
}

void Tratar_Fechamento_Software(void) {
    if (my_app_slot < 0) return;
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_VISIBLE, 0);
    uint32_t* b0 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_0;
    uint32_t* b1 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_1;
    if (b0) memset(b0, 0, winWidth * winHeight * 4);
    if (b1) memset(b1, 0, winWidth * winHeight * 4);
    IPC_WINDOW_LIST[my_app_slot].is_active = 0;
    sys_sleep(50);
}

/* ============================================================================
* HELPERS DE LOG
* ============================================================================ */
static void hexbyte(uint8_t v, char* out) {
    static const char* hx = "0123456789abcdef";
    out[0] = hx[v >> 4]; out[1] = hx[v & 15]; out[2] = 0;
}

static void hexstr_local(const uint8_t* b, int n, char* out) {
    static const char* hx = "0123456789abcdef";
    for (int i = 0; i < n; i++) { out[i*2] = hx[b[i]>>4]; out[i*2+1] = hx[b[i]&15]; }
    out[n*2] = 0;
}

static void loghex(const char* tag, const uint8_t* b, int n) {
    char line[160];
    strcpy(line, tag);
    int l = strlen(line);
    for (int i = 0; i < n && l < 150; i++) {
        hexbyte(b[i], line + l);
        l += 2;
    }
    line[l] = 0;
    GUI_Memo_AddStr(LogMemo, line);
    GUI_Memo_AddStr(LogMemo, "\n");
}

/* ============================================================================
* BOOTSTRAP DE REDE (mesmo padrão do browser)
* ============================================================================ */
static bool net_bootstrap(void) {
    GUI_Memo_AddStr(LogMemo, "[NET] Bootstrap (ARP + DHCP)...\n");
    uint8_t mac[6] = {0};
    if (sys_net_get_mac(mac) != 0) return false;
    ip_set_config(MAKE_IP(10,0,2,15), MAKE_IP(255,255,255,0), MAKE_IP(10,0,2,2));
    arp_send_request(MAKE_IP(10,0,2,2));
    uint8_t gw_mac[6];
    for (int i = 0; i < 50; i++) {
        net_poll();
        if (arp_lookup(MAKE_IP(10,0,2,2), gw_mac)) break;
        sys_sleep(20);
    }
    dhcp_config_t cfg;
    if (dhcp_request_ip(mac, &cfg) == 0) {
        ip_set_config(cfg.ip, cfg.netmask, cfg.gateway);
        if (cfg.dns_server != 0) g_dns_server = cfg.dns_server;
        GUI_Memo_AddStr(LogMemo, "[NET] DHCP OK.\n");
    }
    // 🔧 FIX 1º-clique: pré-aquece o ARP do servidor DNS
    arp_send_request(g_dns_server);
    for (int i = 0; i < 50; i++) {
        net_poll();
        if (arp_lookup(g_dns_server, gw_mac)) break;
        sys_sleep(20);
    }
    return true;
}

/* ============================================================================
* HANDSHAKE TLS 1.2 (Etapa 2a)
* ============================================================================ */
static void run_handshake(void) {
    GUI_Memo_Clear(LogMemo);
    GUI_Memo_AddStr(LogMemo, "== TWebTLS Etapa 2a+2b: handshake ==\n");
    Flush_Grafico_Janela();

    // ============================================================
    // INÍCIO BLOCO 1: REDE (bootstrap ARP + DHCP)
    // ============================================================
    if (!g_net_ready) {
        g_net_ready = net_bootstrap();
        if (!g_net_ready) {
            GUI_Memo_AddStr(LogMemo, "[ERRO] rede\n");
            Flush_Grafico_Janela();
            return;
        }
    }
    // ============================================================
    // FIM BLOCO 1
    // ============================================================

    // ============================================================
    // INÍCIO BLOCO 2: DNS com 3 tentativas (FIX 1º-clique)
    // ============================================================
    const char* host = "example.com";
    GUI_Memo_AddStr(LogMemo, "[DNS] ");
    GUI_Memo_AddStr(LogMemo, host);
    GUI_Memo_AddStr(LogMemo, "...\n");
    Flush_Grafico_Janela();
    uint32_t ip = 0;
    for (int t = 0; t < 3 && ip == 0; t++) {
        ip = dns_resolve(host, g_dns_server);
        if (ip == 0) {
            GUI_Memo_AddStr(LogMemo, "[DNS] retry...\n");
            Flush_Grafico_Janela();
            sys_sleep(50);
        }
    }
    if (ip == 0) {
        GUI_Memo_AddStr(LogMemo, "[ERRO] DNS\n");
        Flush_Grafico_Janela();
        return;
    }
    // ============================================================
    // FIM BLOCO 2
    // ============================================================

    // ============================================================
    // INÍCIO BLOCO 3: TCP connect :443
    // ============================================================
    int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) {
        GUI_Memo_AddStr(LogMemo, "[ERRO] socket\n");
        Flush_Grafico_Janela();
        return;
    }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(443);
    sa.sin_addr.s_addr = ip;
    GUI_Memo_AddStr(LogMemo, "[TCP] connect :443...\n");
    Flush_Grafico_Janela();
    if (connect(s, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        close(s);
        GUI_Memo_AddStr(LogMemo, "[ERRO] connect\n");
        Flush_Grafico_Janela();
        return;
    }
    // ============================================================
    // FIM BLOCO 3
    // ============================================================

    // ============================================================
    // INÍCIO BLOCO 4: 'tls_ctx c' NASCE AQUI + client_random REAL
    // ============================================================
    tls_ctx c;
    memset(&c, 0, sizeof(c));
    c.sockfd = s;
    strcpy(c.host, host);
    rng_bytes(c.cr, 32);                    // client_random REAL (DRBG)
    // ============================================================
    // FIM BLOCO 4
    // ============================================================

    // ============================================================
    // INÍCIO BLOCO 5: Etapa 2a — ClientHello + parse voo
    // ============================================================
    GUI_Memo_AddStr(LogMemo, "[TLS] ClientHello...\n");
    Flush_Grafico_Janela();
    int rc = tls_handshake_flight(&c);
    if (rc != 0) {
        char m[48];
        strcpy(m, "[ERRO] flight rc=");
        IntToStr(rc, m + strlen(m));
        strcat(m, "\n");
        GUI_Memo_AddStr(LogMemo, m);
        close(s);
        Flush_Grafico_Janela();
        return;
    }

    char m[80];
    strcpy(m, "cipher=0x");
    hexbyte((c.cipher >> 8) & 0xFF, m + strlen(m));
    hexbyte(c.cipher & 0xFF, m + strlen(m));
    strcat(m, "\n");
    GUI_Memo_AddStr(LogMemo, m);

    char m2[64];
    strcpy(m2, "cert_len=");
    IntToStr(c.cert_len, m2 + strlen(m2));
    strcat(m2, "\n");
    GUI_Memo_AddStr(LogMemo, m2);

    loghex("sr=", c.sr, 16);
    loghex("master=", c.master, 16);
    GUI_Memo_AddStr(LogMemo, "[OK] voo parseado + chaves derivadas!\n");
    // ============================================================
    // FIM BLOCO 5
    // ============================================================

    // ============================================================
    // INÍCIO BLOCO 6: Etapa 3a — dump X509 do LEAF + SAN + hostname
    // ============================================================
    if (c.cert0 && c.cert0_len) {
        x509_info xi;
        int xrc = x509_parse(c.cert0, c.cert0_len, &xi);
        if (xrc == 0) {
            // identificação
            GUI_Memo_AddStr(LogMemo, "[X509] subject: "); GUI_Memo_AddStr(LogMemo, xi.subject_cn); GUI_Memo_AddStr(LogMemo, "\n");
            GUI_Memo_AddStr(LogMemo, "[X509] issuer:  "); GUI_Memo_AddStr(LogMemo, xi.issuer_cn); GUI_Memo_AddStr(LogMemo, "\n");
            GUI_Memo_AddStr(LogMemo, "[X509] validade: "); GUI_Memo_AddStr(LogMemo, xi.not_before);
            GUI_Memo_AddStr(LogMemo, " .. "); GUI_Memo_AddStr(LogMemo, xi.not_after); GUI_Memo_AddStr(LogMemo, "\n");

            // algoritmos (nome legível via helper do x509.c)
            char ma[96];
            strcpy(ma, "[X509] chave=");
            strcat(ma, x509_spki_name(xi.spki_type));
            strcat(ma, " sig=");
            strcat(ma, x509_sig_name(xi.sig_type));
            strcat(ma, "\n");
            GUI_Memo_AddStr(LogMemo, ma);

            // SAN (Subject Alternative Name — dNSName)
            if (xi.san_n > 0) {
                for (int i = 0; i < xi.san_n; i++) {
                    char ms[160];
                    strcpy(ms, "[X509] SAN[");
                    IntToStr(i, ms + strlen(ms));
                    strcat(ms, "]: ");
                    strcat(ms, xi.san_dns[i]);
                    strcat(ms, "\n");
                    GUI_Memo_AddStr(LogMemo, ms);
                }
            } else {
                GUI_Memo_AddStr(LogMemo, "[X509] SAN: ausente (usando CN)\n");
            }

            // validação de hostname (SAN > CN, com wildcard)
            int hv = x509_check_hostname(&xi, host);
            GUI_Memo_AddStr(LogMemo,
                hv == 0 ? "[X509] hostname CONFERE!\n"
                        : "[X509] hostname NAO confere\n");

            // validação de validade (passa 0 = skip, sem RTC ainda)
            int vv = x509_check_validity(&xi, 0);
            GUI_Memo_AddStr(LogMemo,
                vv == 0 ? "[X509] validade OK (RTC ausente)\n"
                        : "[X509] validade INVALIDA\n");

            // flags de extensão
            char mf[96];
            strcpy(mf, "[X509] flags: serverAuth=");
            strcat(mf, xi.has_server_auth ? "1" : "0");
            strcat(mf, " digitalSig=");
            strcat(mf, xi.ku_digital_sig ? "1" : "0");
            strcat(mf, "\n");
            GUI_Memo_AddStr(LogMemo, mf);
        } else {
            char mx[96];
            strcpy(mx, "[X509] parse rc=");
            IntToStr(xrc, mx + strlen(mx));
            strcat(mx, " cert0_len=");
            IntToStr((int)c.cert0_len, mx + strlen(mx));
            strcat(mx, "\n");
            GUI_Memo_AddStr(LogMemo, mx);
            loghex("[X509] der[0..15]=", c.cert0, 16);
        }
    } else {
        GUI_Memo_AddStr(LogMemo, "[X509] cert0 NAO preenchido pelo flight!\n");
    }
    // ============================================================
    // FIM BLOCO 6
    // ============================================================
    // ============================================================
    // INÍCIO BLOCO 6b: Etapa 3b — cadeia (nomes) + hostname
    // ============================================================
    for (int i = 0; i < c.chain_n; i++) {
        x509_info xc;
        if (x509_parse(c.chain[i], c.chain_len[i], &xc) == 0) {
            char mc[160];
            strcpy(mc, "[X509] cert");
            IntToStr(i, mc + strlen(mc));
            strcat(mc, ": ");
            strcat(mc, xc.subject_cn);
            strcat(mc, " <- ");
            strcat(mc, xc.issuer_cn);
            strcat(mc, "\n");
            GUI_Memo_AddStr(LogMemo, mc);
        }
    }
    if (c.chain_n >= 2) {
        x509_info a, bb;
        if (x509_parse(c.chain[0], c.chain_len[0], &a) == 0 &&
            x509_parse(c.chain[1], c.chain_len[1], &bb) == 0) {
            GUI_Memo_AddStr(LogMemo,
                (strcmp(a.issuer_cn, bb.subject_cn) == 0)
                ? "[X509] cadeia CONFERE (leaf->inter)\n"
                : "[X509] cadeia QUEBRADA (leaf->inter)\n");
        }
    }
    {
        x509_info x0;
        if (c.cert0 && x509_parse(c.cert0, c.cert0_len, &x0) == 0) {
            GUI_Memo_AddStr(LogMemo,
                (strcmp(x0.subject_cn, host) == 0)
                ? "[X509] hostname CONFERE!\n"
                : "[X509] hostname NAO confere (CN difere)\n");
        }
    }
    // ============================================================
    // FIM BLOCO 6b
    // ============================================================

    // ============================================================
    // INÍCIO BLOCO 6c: Etapa 3c — ASSINATURAS (ECDSA + RSA v1.5 + RSA-PSS)
    // ============================================================
    for (int i = 0; i + 1 < c.chain_n; i++) {
        x509_info child, issuer;
        if (x509_parse(c.chain[i],   c.chain_len[i],   &child)  != 0) continue;
        if (x509_parse(c.chain[i+1], c.chain_len[i+1], &issuer) != 0) continue;
        char mc[96];
        strcpy(mc, "[X509] sig cert");
        IntToStr(i, mc + strlen(mc));

        uint8_t ehash[32];
        sha256(child.tbs, child.tbs_len, ehash);
        int vr = -99;

        // caso ECDSA-P256
        if (child.sig_type == 2 && issuer.spki_type == 2 &&
            issuer.pubkey_len >= 65 && issuer.pubkey[0] == 0x04) {
            uint8_t rr[32], ss[32];
            if (ecdsa_parse_der_sig(child.sig, child.sig_len, rr, ss) == 0)
                vr = ecdsa_p256_verify(ehash, rr, ss,
                                       issuer.pubkey + 1, issuer.pubkey + 33);
        }
        // caso RSA PKCS#1 v1.5 SHA-256
        else if (child.sig_type == 1 && issuer.spki_type == 1) {
            const uint8_t *nb, *eb; int nl, el;
            if (rsa_parse_spki(issuer.pubkey, issuer.pubkey_len, &nb, &nl, &eb, &el) == 0)
                vr = rsa_verify_pkcs1_sha256(ehash, child.sig, child.sig_len, nb, nl, eb, el);
        }
        // caso RSA-PSS (sig_type == 3, chave RSA)
        else if (child.sig_type == 3 && issuer.spki_type == 1) {
            const uint8_t *nb, *eb; int nl, el;
            if (rsa_parse_spki(issuer.pubkey, issuer.pubkey_len, &nb, &nl, &eb, &el) == 0)
                vr = rsa_verify_pss_sha256(ehash, child.sig, child.sig_len, nb, nl, eb, el);
        }
        else {
            // algoritmo não suportado (PSS com EC, ou outro)
            strcat(mc, ": nao suportado (sig="); IntToStr(child.sig_type, mc + strlen(mc));
            strcat(mc, " spki=");                IntToStr(issuer.spki_type, mc + strlen(mc));
            strcat(mc, ")\n");
            GUI_Memo_AddStr(LogMemo, mc);
            continue;
        }

        // imprime resultado
        if (vr == 0) {
            if      (child.sig_type == 2) strcat(mc, ": ECDSA OK!\n");
            else if (child.sig_type == 3) strcat(mc, ": RSA-PSS OK!\n");
            else                          strcat(mc, ": RSA OK!\n");
        } else {
            strcat(mc, ": FALHOU rc ");
            IntToStr(vr, mc + strlen(mc));
            strcat(mc, "\n");
        }
        GUI_Memo_AddStr(LogMemo, mc);
    }
    // ============================================================
    // FIM BLOCO 6c (assinaturas)
    // ============================================================

    // ============================================================
    // INÍCIO BLOCO 6d: ÂNCORA — 3 modos (TOFU / PIN confere / PIN diverge)
    // ============================================================
    {
        x509_info last;
        if (c.chain_n >= 1 &&
            x509_parse(c.chain[c.chain_n-1], c.chain_len[c.chain_n-1], &last) == 0 &&
            last.pubkey_len > 0) {
            sha256_ctx sc; uint8_t pin[32];
            sha256_init(&sc);
            sha256_update(&sc, last.pubkey, last.pubkey_len);
            sha256_final(&sc, pin);
            char got[80];
            hexstr_local(pin, 32, got);

            // PIN opcional: tudo zero = modo TOFU (sem vermelho, sem paste)
            static const uint8_t PIN_SPKI[32] = { 0 };
            int configured = 0;
            for (int i = 0; i < 32; i++) if (PIN_SPKI[i]) { configured = 1; break; }

            char mg[160];
            strcpy(mg, "[X509] anchor: "); strcat(mg, got); strcat(mg, "\n");
            GUI_Memo_AddStr(LogMemo, mg);

            if (!configured) {
                GUI_Memo_AddStr(LogMemo,
                    "[X509] anchor modo TOFU (pin opcional: cole o hash em PIN_SPKI)\n");
            } else {
                int d = 0;
                for (int i = 0; i < 32; i++) d |= pin[i] ^ PIN_SPKI[i];
                GUI_Memo_AddStr(LogMemo,
                    d == 0 ? "[X509] anchor PIN confere!\n"
                           : "[X509] anchor PIN DIVERGE! (MITM?)\n");
            }
        }
    }
    // ============================================================
    // FIM BLOCO 6d
    // ============================================================

    // ============================================================
    // INÍCIO BLOCO 7: Etapa 2b — CKE/CCS/Finished + GET / HTTP
    // ============================================================
    GUI_Memo_AddStr(LogMemo, "[TLS] Completando handshake (CKE/CCS/Finished)...\n");
    Flush_Grafico_Janela();
    int rc2 = tls_handshake_full(&c);
    if (rc2 == 0) {
        GUI_Memo_AddStr(LogMemo, "[TLS] HANDSHAKE COMPLETO! Server Finished OK.\n");
        Flush_Grafico_Janela();
        const char* req = "GET / HTTP/1.1\r\nHost: example.com\r\n"
                          "User-Agent: LBF-TLS/1.0\r\nConnection: close\r\n\r\n";
        tls_send_app_data(&c, (const uint8_t*)req, strlen(req));
        static uint8_t buf[4200];
        uint8_t rt = 0;
        int n = -1;
        for (int tries = 0; tries < 8; tries++) {
            n = tls_recv_app_data(&c, buf, sizeof(buf) - 1, &rt);
            if (n > 0 && rt == RT_APP) break;
            if (n < 0) break;
        }
        if (n > 0 && rt == RT_APP) {
            if (n > 300) n = 300;
            buf[n] = 0;
            GUI_Memo_AddStr(LogMemo, "[HTTPS] ");
            GUI_Memo_AddStr(LogMemo, (char*)buf);
            GUI_Memo_AddStr(LogMemo, "\n");
        } else {
            char m3[48];
            strcpy(m3, "[ERRO] recv rc=");
            IntToStr(n, m3 + strlen(m3));
            strcat(m3, "\n");
            GUI_Memo_AddStr(LogMemo, m3);
        }
    } else {
        char m3[48];
        strcpy(m3, "[ERRO] handshake_full rc=");
        IntToStr(rc2, m3 + strlen(m3));
        strcat(m3, "\n");
        GUI_Memo_AddStr(LogMemo, m3);
    }
    // ============================================================
    // FIM BLOCO 7
    // ============================================================

    // ============================================================
    // INÍCIO BLOCO 8: close do socket
    // ============================================================
    close(s);
    Flush_Grafico_Janela();
    // ============================================================
    // FIM BLOCO 8
    // ============================================================
}

void OnBtnRunClick(void* s) { (void)s; run_handshake(); }

/* ============================================================================
* MAIN — loop de eventos COMPLETO (base crypto.c)
* ============================================================================ */
int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    static int ultimo_x = 0, ultimo_y = 0, mouse_hold_timer = 0;
    static bool primeiro_desenho = true, ultimo_estado_foco = false;
    void* ultimo_controle_focado = NULL;

    graphics_init_app(winWidth, winHeight);
    wm_init();
    my_app_slot = OS_IPC_RegisterApp("TLS Handshake", winWidth, winHeight);
    if (my_app_slot == -1) return -1;
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "TWebTLS - Handshake 2a", winWidth, winHeight);
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0xC0C0C0);

    BtnRun  = GUI_CreateButton(&MyApp, 10, 40, 180, 30, "HANDSHAKE", OnBtnRunClick);
    LogMemo = GUI_CreateMemo(&MyApp, 10, 80, 490, 330);
    gui_set_prop(LogMemo, PROP_COLOR, 0x000000);

    Flush_Grafico_Janela();

    while (1) {
        if (IPC_WINDOW_LIST[my_app_slot].is_active == 0) {
            Tratar_Fechamento_Software();
            break;
        }
        bool precisa_redesenhar = false;
        if (primeiro_desenho) { primeiro_desenho = false; precisa_redesenhar = true; }

        bool foco = (IPC_CONTROL->active_focus_slot == my_app_slot);
        if (foco != ultimo_estado_foco) {
            ultimo_estado_foco = foco;
            if (MyApp.MainWindow) ((TForm*)MyApp.MainWindow)->ActiveFocus = foco;
            precisa_redesenhar = true;
        }
        if (g_focused_control != NULL) ultimo_controle_focado = g_focused_control;
        else if (ultimo_controle_focado != NULL) {
            g_focused_control = ultimo_controle_focado;
            gui_set_prop((TGUIControl*)ultimo_controle_focado, PROP_SET_FOCUS, 1);
        }

        char key = Obter_Tecla_Entrada();
        if (key != 0) { GUI_ProcessKeyboard(&MyApp, key); precisa_redesenhar = true; }

        if (IPC_WINDOW_LIST[my_app_slot].has_click_event == 1) {
            if (mouse_hold_timer == 0) {
                int rel_x = IPC_WINDOW_LIST[my_app_slot].local_click_x;
                int rel_y = IPC_WINDOW_LIST[my_app_slot].local_click_y;
                ultimo_x = rel_x; ultimo_y = rel_y;
                mouse_hold_timer = 2;
                if (BtnRun && rel_x >= BtnRun->Left && rel_x < (BtnRun->Left + BtnRun->Width) &&
                    rel_y >= BtnRun->Top && rel_y < (BtnRun->Top + BtnRun->Height))
                    gui_set_prop(BtnRun, PROP_STATE, 2);
                events_process_mouse(rel_x, rel_y, 1, 0);
                if (GUI_ProcessMouseClick(&MyApp, rel_x, rel_y)) precisa_redesenhar = true;
            }
            IPC_WINDOW_LIST[my_app_slot].has_click_event = 0;   // SEMPRE limpar!
        }
        if (mouse_hold_timer > 0) {
            mouse_hold_timer--;
            if (mouse_hold_timer == 0) {
                if (BtnRun) gui_set_prop(BtnRun, PROP_STATE, 0);
                events_process_mouse(ultimo_x, ultimo_y, 0, 0);
                precisa_redesenhar = true;
            }
        }
        if (precisa_redesenhar) Flush_Grafico_Janela();
        sys_sleep(foco ? 16 : 32);
    }
    sys_exit();
    return 0;
}
