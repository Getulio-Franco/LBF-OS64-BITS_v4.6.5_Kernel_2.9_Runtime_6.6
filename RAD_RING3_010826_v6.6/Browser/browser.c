/*
====================================================================
                    HISTÓRICO DE COLABORAÇÃO
====================================================================
Arquivo: browser.c
Versão: 3.0
Data: 31/08/2026
Autor: LBF-OS Team + AI Assistant

Base:
    - v2.0 (browser de texto Lynx-style com VOLTAR/ATUALIZAR/SALVAR)

Mudanças v3.0 (BROWSER VISUAL):
    - MemoPage substituído por TWebPage (componente aprovado)
    - html_to_webdoc(): HTML -> WebDoc (títulos/texto/links/imagens)
    - Links CLICÁVEIS via OnNavigate (sem digitar número)
    - <img src> baixado via HTTP: BMP renderiza inline; outros
      formatos viram placeholder "[IMG] url"
    - Label de status + memo fino de log
    - Mantém: redirect, dechunk, https-downgrade, histórico, SALVAR

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

#include "Browser/crypto/tls.h"
#include "Browser/crypto/rng.h"

// Protótipos RAD
void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;
extern void GUI_Memo_AddStr(TGUIControl* memo, const char* str);
extern void GUI_Memo_Clear(TGUIControl* memo);

// Protótipos web_html.c (construtor de WebDoc)
extern WebDoc* webdoc_create(void);
extern void    webdoc_destroy(WebDoc* doc);
extern void    webdoc_add_line(WebDoc* doc, const char* text, uint8_t style, int link_id, int img_slot);
extern int     webdoc_add_link(WebDoc* doc, const char* url);
extern int     webdoc_add_image(WebDoc* doc, const char* url);
extern void    webdoc_finalize(WebDoc* doc);

extern int webimg_decode(const uint8_t* data, int len, uint32_t** out_px, int* out_w, int* out_h);

extern void webimg_get_debug(int* w, int* h, int* ct, int* idat, int* rawlen, int* rl);

// Protótipos locais
void Flush_Grafico_Janela(void);
void Tratar_Fechamento_Software(void);
char Obter_Tecla_Entrada(void);

int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth = 620;
const int winHeight = 620;

TGUIControl* EditURL    = NULL;
TGUIControl* BtnGo      = NULL;
TGUIControl* BtnBack    = NULL;
TGUIControl* BtnRefresh = NULL;
TGUIControl* BtnSave    = NULL;
TGUIControl* LblStatus  = NULL;
TGUIControl* WebPage    = NULL;
TGUIControl* LogMemo    = NULL;

// Buffers estáticos grandes
static char g_rx[200 * 1024];    // resposta HTTP (páginas + imagens BMP)
static char g_chunk[512];
//static uint32_t g_img_px[256 * 256];   // decode temporário de imagem

// v2.0: corpo da última página (p/ SALVAR no FAT32)
static char* g_body = NULL;
static int   g_body_len = 0;

static char g_host[128];         // host atual (p/ resolver links relativos)
static WebDoc* g_doc = NULL;     // documento atual do TWebPage

// v2.0: histórico de navegação (botão VOLTAR)
#define MAX_HISTORY 10
static char g_history[MAX_HISTORY][192];
static int  g_history_top = 0;

static bool g_net_ready = false;
static uint32_t g_dns_server = 0x0302000A;

typedef struct {
    uint8_t dummy[sizeof(IPC_WINDOW_LIST[0])];
    volatile uint8_t fila_teclado_virtual;
    volatile uint8_t tem_evento_teclado;
} __attribute__((packed)) AppWindowInfoExtended;

// ============================================================================
// HELPERS BÁSICOS
// ============================================================================
static size_t safe_strcpy(char* dest, const char* src, size_t max_size) {
    if (!dest || max_size == 0) return 0;
    size_t i = 0;
    if (src) { while (src[i] != '\0' && i < (max_size - 1)) { dest[i] = src[i]; i++; } }
    dest[i] = '\0';
    return i;
}

static size_t safe_strcat(char* dest, const char* src, size_t max_size, size_t current_len) {
    if (!dest || !src || current_len >= max_size) return current_len;
    while (*src && current_len < (max_size - 1)) dest[current_len++] = *src++;
    dest[current_len] = '\0';
    return current_len;
}

static const char* local_strstr(const char* haystack, const char* needle) {
    if (!haystack || !needle || !*needle) return haystack;
    for (const char* h = haystack; *h; h++) {
        const char* hh = h;
        const char* n = needle;
        while (*n && *hh == *n) { hh++; n++; }
        if (!*n) return h;
    }
    return 0;
}

static bool starts_with_ci(const char* s, const char* pre) {
    while (*pre) {
        char a = *s, b = *pre;
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return false;
        s++; pre++;
    }
    return true;
}

static uint32_t parse_ip(const char* ip_str) {
    if (!ip_str) return 0;
    uint32_t bytes[4] = {0};
    int byte_idx = 0, current_val = 0;
    bool has_digit = false;
    while (*ip_str) {
        if (*ip_str >= '0' && *ip_str <= '9') {
            current_val = (current_val * 10) + (*ip_str - '0');
            if (current_val > 255) return 0;
            has_digit = true;
        } else if (*ip_str == '.') {
            if (!has_digit || byte_idx >= 3) return 0;
            bytes[byte_idx++] = current_val;
            current_val = 0; has_digit = false;
        } else return 0;
        ip_str++;
    }
    if (!has_digit || byte_idx != 3) return 0;
    bytes[3] = current_val;
    return (bytes[0]) | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
}

static bool parse_url(const char* url, char* host_out, size_t host_max,
                      char* path_out, size_t path_max) {
    if (!url || !host_out || !path_out) return false;
    const char* p = url;
    if (starts_with_ci(p, "http://")) p += 7;
    size_t hi = 0;
    while (*p && *p != '/' && hi < host_max - 1) host_out[hi++] = *p++;
    host_out[hi] = '\0';
    size_t pi = 0;
    if (*p == '/') { while (*p && pi < path_max - 1) path_out[pi++] = *p++; }
    else path_out[pi++] = '/';
    path_out[pi] = '\0';
    return (hi > 0);
}

static int http_status_of(const char* buf) {
    if (!buf || !(buf[0]=='H' && buf[1]=='T' && buf[2]=='T' && buf[3]=='P')) return -1;
    const char* p = buf;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    int st = 0;
    while (*p >= '0' && *p <= '9') { st = st * 10 + (*p - '0'); p++; }
    return st;
}

// Resolve link relativo contra o host atual
static void resolve_url(const char* href, char* out, size_t max) {
    if (starts_with_ci(href, "http://") || starts_with_ci(href, "https://")) {
        safe_strcpy(out, href, max);
    } else if (href[0] == '/') {
        size_t l = safe_strcpy(out, g_host, max);
        safe_strcat(out, href, max, l);
    } else {
        size_t l = safe_strcpy(out, g_host, max);
        l = safe_strcat(out, "/", max, l);
        safe_strcat(out, href, max, l);
    }
}

static void Label_SetText(TGUIControl* lbl, const char* text) {
    if (!lbl || !lbl->KernelHandle || !text) return;
    gui_set_prop((void*)lbl->KernelHandle, PROP_CAPTION, (uintptr_t)text);
}

// ============================================================================
// DECHUNK (Transfer-Encoding: chunked)
// ============================================================================
static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static int http_dechunk_inplace(char* body, int len) {
    if (!body || len <= 0) return 0;
    int r = 0, w = 0;
    while (r < len) {
        int chunk_size = 0;
        while (r < len) {
            char c = body[r++];
            if (c == '\r') { if (r < len && body[r] == '\n') r++; break; }
            if (c == '\n') break;
            if (c == ';') { while (r < len && body[r] != '\n') r++; if (r < len) r++; break; }
            int hv = hex_value(c);
            if (hv < 0) return w;
            chunk_size = (chunk_size << 4) | hv;
        }
        if (chunk_size == 0) break;
        if (r + chunk_size > len) chunk_size = len - r;
        for (int i = 0; i < chunk_size; i++) body[w++] = body[r++];
        if (r < len && body[r] == '\r') r++;
        if (r < len && body[r] == '\n') r++;
    }
    return w;
}

// ============================================================================
// DECODIFICADOR BMP (24/32-bit) p/ imagens inline
// ============================================================================
/*static int bmp_decode(const uint8_t* data, int len,
                      int* out_w, int* out_h, uint32_t* out_px, int max_px) {
    if (!data || len < 54 || !out_w || !out_h || !out_px) return -1;
    if (data[0] != 'B' || data[1] != 'M') return -2;

    uint32_t data_offset = *(const uint32_t*)(data + 10);
    int32_t  w    = *(const int32_t*)(data + 18);
    int32_t  h    = *(const int32_t*)(data + 22);
    uint16_t bpp  = *(const uint16_t*)(data + 28);
    uint32_t comp = *(const uint32_t*)(data + 30);

    if (bpp != 24 && bpp != 32) return -3;
    if (comp != 0 && comp != 3) return -3;
    if (w <= 0 || h == 0) return -4;

    int height = (h > 0) ? h : -h;
    if (w * height > max_px) return -5;

    int bytes_per_px = bpp / 8;
    int row_size = ((w * bytes_per_px + 3) / 4) * 4;

    for (int y = 0; y < height; y++) {
        int src_row = (h > 0) ? (height - 1 - y) : y;
        const uint8_t* row = data + data_offset + (uint32_t)src_row * row_size;
        if ((long)(row - data) + w * bytes_per_px > len) return -6;
        for (int x = 0; x < w; x++) {
            uint8_t b = row[x * bytes_per_px + 0];
            uint8_t g = row[x * bytes_per_px + 1];
            uint8_t r = row[x * bytes_per_px + 2];
            out_px[y * w + x] = 0xFF000000u | ((uint32_t)r << 16) |
                                ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
    *out_w = w; *out_h = height;
    return 0;
}*/

// ============================================================================
// BOOTSTRAP DE REDE
// ============================================================================
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
    return true;
}

// ============================================================================
// HTTP GET (com 1 redirect)
// ============================================================================
static int http_fetch(const char* host, const char* path) {
    uint32_t target_ip = parse_ip(host);
    if (target_ip == 0) {
        target_ip = dns_resolve(host, g_dns_server);
        if (target_ip == 0) return -1;
    }
    int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) return -2;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(80);
    sa.sin_addr.s_addr = target_ip;
    if (connect(sockfd, (struct sockaddr*)&sa, sizeof(sa)) < 0) { close(sockfd); return -3; }

    char req[256];
    size_t l = safe_strcpy(req, "GET ", sizeof(req));
    l = safe_strcat(req, path, sizeof(req), l);
    l = safe_strcat(req, " HTTP/1.1\r\nHost: ", sizeof(req), l);
    l = safe_strcat(req, host, sizeof(req), l);
    l = safe_strcat(req, "\r\nUser-Agent: LBF-Browser/3.0\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n", sizeof(req), l);
    send(sockfd, req, l, 0);

    int total = 0, timeout = 300;
    while (timeout > 0 && total < (int)sizeof(g_rx) - 1) {
        int n = recv(sockfd, g_chunk, sizeof(g_chunk) - 1, 0);
        if (n > 0) {
            if (total + n > (int)sizeof(g_rx) - 1) n = sizeof(g_rx) - 1 - total;
            for (int i = 0; i < n; i++) g_rx[total + i] = g_chunk[i];
            total += n;
            timeout = 150;
        } else { sys_sleep(10); timeout--; }
    }
    close(sockfd);
    g_rx[total] = '\0';
    return total;
}

/* ============================================================================
* v4.1: HTTPS NATIVO — handshake TLS 1.2 + GET dentro do túnel
* v4.1 FIX: client_random REAL via DRBG + buffer 16KB static p/ records grandes
* ============================================================================ */
static int https_fetch(const char* host, const char* path) {
    uint32_t target_ip = parse_ip(host);
    if (target_ip == 0) {
        target_ip = dns_resolve(host, g_dns_server);
        if (target_ip == 0) return -1;
    }
    int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) return -2;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(443);
    sa.sin_addr.s_addr = target_ip;
    if (connect(sockfd, (struct sockaddr*)&sa, sizeof(sa)) < 0) { close(sockfd); return -3; }

    tls_ctx c;
    memset(&c, 0, sizeof(c));
    c.sockfd = sockfd;
    safe_strcpy(c.host, host, sizeof(c.host));

    // v4.1: client_random REAL (imprevisível) — substitui o MAC+endereço
    rng_bytes(c.cr, 32);

    GUI_Memo_AddStr(LogMemo, "[TLS] handshake...\n");
    Flush_Grafico_Janela();
    int rcF = tls_handshake_flight(&c);
    if (rcF != 0) {
        close(sockfd);
        char m[80];
        if (rcF <= -1000) {
            strcpy(m, "[ERRO] servidor recusou (ALERT desc=");
            IntToStr(-rcF - 1000, m + strlen(m));
            strcat(m, ")\n");
        } else {
            strcpy(m, "[ERRO] flight rc=");
            IntToStr(rcF, m + strlen(m));
            strcat(m, "\n");
        }
        GUI_Memo_AddStr(LogMemo, m);
        return -4;
    }
    int rcL = tls_handshake_full(&c);
    if (rcL != 0) {
        close(sockfd);
        char m[64];
        strcpy(m, "[ERRO] full rc=");
        IntToStr(rcL, m + strlen(m));
        strcat(m, "\n");
        GUI_Memo_AddStr(LogMemo, m);
        return -5;
    }
    GUI_Memo_AddStr(LogMemo, "[TLS] OK! Tunel estabelecido.\n");
    Flush_Grafico_Janela();

    char req[256];
    size_t l = safe_strcpy(req, "GET ", sizeof(req));
    l = safe_strcat(req, path, sizeof(req), l);
    l = safe_strcat(req, " HTTP/1.1\r\nHost: ", sizeof(req), l);
    l = safe_strcat(req, host, sizeof(req), l);
    l = safe_strcat(req, "\r\nUser-Agent: LBF-Browser/4.0\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n", sizeof(req), l);
    tls_send_app_data(&c, (const uint8_t*)req, (int)l);

    // acumula plaintext dos records em g_rx
    // CORREÇÃO: buffer static 16KB+256 (fora da pilha, escopo local)
    int total = 0, timeout = 300;
    static uint8_t buf[16384 + 256];   // ← era 4200 na pilha; agora 16KB static
    uint8_t rt = 0;
    while (timeout > 0 && total < (int)sizeof(g_rx) - 1) {
        int n = tls_recv_app_data(&c, buf, sizeof(buf), &rt);
        if (n > 0 && rt == RT_APP) {
            if (total + n > (int)sizeof(g_rx) - 1) n = (int)sizeof(g_rx) - 1 - total;
            for (int i = 0; i < n; i++) g_rx[total + i] = (char)buf[i];
            total += n;
            timeout = 150;
        } else if (n > 0 && rt == RT_ALERT) break;   // close_notify
        else if (n < 0) break;                        // conexão fechou
        else { sys_sleep(10); timeout--; }
    }
    close(sockfd);
    g_rx[total] = '\0';
    return total;
}

/* ============================================================================
* v4.0: fetch unificado (http/https) + 1 redirect, seguindo o scheme
* ============================================================================ */
static int fetch_any(const char* url, char* out_host, size_t host_max, bool* out_tls) {
    char cur[256];
    safe_strcpy(cur, url, sizeof(cur));
    for (int r = 0; r < 2; r++) {
        bool tls = starts_with_ci(cur, "https://");
        char purl[256];
        if (tls) { safe_strcpy(purl, "http://", sizeof(purl)); safe_strcat(purl, cur + 8, sizeof(purl), 7); }
        else     { safe_strcpy(purl, cur, sizeof(purl)); }
        char host[128], path[128];
        if (!parse_url(purl, host, sizeof(host), path, sizeof(path))) return -1;
        int total = tls ? https_fetch(host, path) : http_fetch(host, path);
        if (total <= 0) return total;
        int st = http_status_of(g_rx);
        if (st == 301 || st == 302) {
            const char* loc = local_strstr(g_rx, "Location:");
            if (loc) {
                loc += 9; while (*loc == ' ') loc++;
                char nu[192]; size_t k = 0;
                while (*loc && *loc != '\r' && *loc != '\n' && k < sizeof(nu) - 1) nu[k++] = *loc++;
                nu[k] = '\0';
                if (!starts_with_ci(nu, "http://") && !starts_with_ci(nu, "https://")) {
                    char tmp[256];
                    safe_strcpy(tmp, tls ? "https://" : "http://", sizeof(tmp));
                    size_t tl = safe_strcat(tmp, host, sizeof(tmp), strlen(tmp));
                    if (nu[0] != '/') tl = safe_strcat(tmp, "/", sizeof(tmp), tl);
                    safe_strcat(tmp, nu, sizeof(tmp), tl);
                    safe_strcpy(nu, tmp, sizeof(nu));
                }
                safe_strcpy(cur, nu, sizeof(cur));
                continue;
            }
        }
        if (out_host) safe_strcpy(out_host, host, host_max);
        if (out_tls) *out_tls = tls;
        return total;
    }
    return -1;
}

// ============================================================================
// v3.0: PARSER HTML -> WebDoc (títulos / texto / links / imagens)
// ============================================================================
static void flush_cur(WebDoc* doc, char* cur, int* len, uint8_t style, int link_id) {
    while (*len > 0 && cur[*len - 1] == ' ') (*len)--;
    if (*len > 0) {
        cur[*len] = '\0';
        webdoc_add_line(doc, cur, style, link_id, -1);
    }
    *len = 0;
    cur[0] = '\0';
}

static void html_to_webdoc(const char* html, WebDoc* doc) {
    static char cur[160];
    int len = 0;
    const char* p = html;
    bool in_script = false, in_style = false, in_title = false;
    bool in_link = false, in_heading = false;
    int link_id = -1;

    while (*p) {
        if (*p == '<') {
            // comentários
            if (p[1]=='!' && p[2]=='-' && p[3]=='-') {
                const char* e = local_strstr(p, "-->");
                p = e ? (e + 3) : (p + 4);
                continue;
            }
            if (starts_with_ci(p, "<script")) in_script = true;
            else if (starts_with_ci(p, "</script")) in_script = false;
            else if (starts_with_ci(p, "<style")) in_style = true;
            else if (starts_with_ci(p, "</style")) in_style = false;
            else if (starts_with_ci(p, "<title")) in_title = true;
            else if (starts_with_ci(p, "</title")) in_title = false;
            else if (!in_script && !in_style && !in_title) {
                uint8_t cur_style = in_link ? WEB_STYLE_LINK :
                                    (in_heading ? WEB_STYLE_TITLE : WEB_STYLE_TEXT);

                // quebras de bloco
                if (starts_with_ci(p, "<br") || starts_with_ci(p, "<p") ||
                    starts_with_ci(p, "</p") || starts_with_ci(p, "</div") ||
                    starts_with_ci(p, "<li") || starts_with_ci(p, "<tr") ||
                    starts_with_ci(p, "</tr")) {
                    flush_cur(doc, cur, &len, cur_style, link_id);
                }
                // títulos
                else if (starts_with_ci(p, "<h1") || starts_with_ci(p, "<h2") ||
                         starts_with_ci(p, "<h3")) {
                    flush_cur(doc, cur, &len, cur_style, link_id);
                    in_heading = true;
                }
                else if (starts_with_ci(p, "</h1") || starts_with_ci(p, "</h2") ||
                         starts_with_ci(p, "</h3")) {
                    flush_cur(doc, cur, &len, WEB_STYLE_TITLE, -1);
                    in_heading = false;
                }
                // links
                else if (starts_with_ci(p, "<a")) {
                    flush_cur(doc, cur, &len, cur_style, link_id);
                    const char* gt = p;
                    while (*gt && *gt != '>') gt++;
                    const char* h = local_strstr(p, "href=");
                    if (h && h < gt && doc->kc < 64) {
                        h += 5;
                        char q = 0;
                        if (*h == '"' || *h == '\'') { q = *h; h++; }
                        char href[160]; int i = 0;
                        while (*h && *h != '>' && i < 159) {
                            if (q && *h == q) break;
                            if (!q && (*h == ' ' || *h == '\t')) break;
                            href[i++] = *h++;
                        }
                        href[i] = '\0';
                        if (i > 0) {
                            link_id = webdoc_add_link(doc, href);
                            in_link = (link_id >= 0);
                        }
                    }
                }
                else if (starts_with_ci(p, "</a")) {
                    flush_cur(doc, cur, &len, WEB_STYLE_LINK, link_id);
                    in_link = false;
                    link_id = -1;
                }
                // imagens
                else if (starts_with_ci(p, "<img")) {
                    flush_cur(doc, cur, &len, cur_style, link_id);
                    const char* gt = p;
                    while (*gt && *gt != '>') gt++;
                    const char* s = local_strstr(p, "src=");
                    if (s && s < gt && doc->ic < 8) {
                        s += 4;
                        char q = 0;
                        if (*s == '"' || *s == '\'') { q = *s; s++; }
                        char src[160]; int i = 0;
                        while (*s && *s != '>' && i < 159) {
                            if (q && *s == q) break;
                            src[i++] = *s++;
                        }
                        src[i] = '\0';
                        if (i > 0) {
                            int slot = webdoc_add_image(doc, src);
                            if (slot >= 0) {
                                char tmp[180];
                                safe_strcpy(tmp, "[IMG] ", sizeof(tmp));
                                safe_strcat(tmp, src, sizeof(tmp), strlen(tmp));
                                webdoc_add_line(doc, tmp, WEB_STYLE_IMG, -1, slot);
                            }
                        }
                    }
                }
            }
            while (*p && *p != '>') p++;
            if (*p == '>') p++;
            continue;
        }

        if (in_script || in_style || in_title) { p++; continue; }

        // entidades + caractere
        char c = 0;
        if (*p == '&') {
            if (starts_with_ci(p, "&amp;"))       { c = '&';  p += 5; }
            else if (starts_with_ci(p, "&lt;"))   { c = '<';  p += 4; }
            else if (starts_with_ci(p, "&gt;"))   { c = '>';  p += 4; }
            else if (starts_with_ci(p, "&quot;")) { c = '"';  p += 6; }
            else if (starts_with_ci(p, "&#39;"))  { c = '\''; p += 5; }
            else if (starts_with_ci(p, "&nbsp;")) { c = ' ';  p += 6; }
            else if (p[1] == '#') { c = ' '; while (*p && *p != ';') p++; if (*p) p++; }
            else { c = '&'; p++; }
        } else {
            c = *p++;
        }

        // quebras/whitespace viram espaço simples
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if (c == ' ' && (len == 0 || cur[len - 1] == ' ')) continue;

        cur[len++] = c;
        if (len >= 140) {   // segurança: linha cheia
            uint8_t st = in_link ? WEB_STYLE_LINK :
                         (in_heading ? WEB_STYLE_TITLE : WEB_STYLE_TEXT);
            flush_cur(doc, cur, &len, st, link_id);
        }
    }

    // flush final
    flush_cur(doc, cur, &len,
              in_link ? WEB_STYLE_LINK : (in_heading ? WEB_STYLE_TITLE : WEB_STYLE_TEXT),
              link_id);
    webdoc_finalize(doc);
}

// ============================================================================
// v3.1: baixa <img> via HTTP (1 redirect) + diagnóstico completo no log
// ============================================================================
static int try_load_images(WebDoc* doc) {
    int loaded = 0;
    for (int i = 0; i < doc->ic && loaded < 3; i++) {
        WebImgSlot* sl = &doc->imgs[i];
        char full[256];
        resolve_url(sl->url, full, sizeof(full));
        // .gif liberado (web_img v1.1); vetoriais/JPEG ainda não
        if (local_strstr(full, ".svg") || local_strstr(full, ".webp") ||
            local_strstr(full, ".ico") || local_strstr(full, ".jpg") ||
            local_strstr(full, ".jpeg")) continue;
        GUI_Memo_AddStr(LogMemo, "[IMG] ");
        GUI_Memo_AddStr(LogMemo, full);
        GUI_Memo_AddStr(LogMemo, "\n");
        Flush_Grafico_Janela();

        char ihost[128]; bool itls = false;
        int total = fetch_any(full, ihost, sizeof(ihost), &itls);
        if (total <= 0) continue;

        const char* hdr = local_strstr(g_rx, "\r\n\r\n");
        char* body = hdr ? (char*)(hdr + 4) : g_rx;
        int blen = total - (int)(body - g_rx);
        if (blen < 0) blen = 0;
        int hlen = hdr ? (int)(hdr - g_rx) : total;
        char sv = g_rx[hlen]; g_rx[hlen] = '\0';
        bool chk = (local_strstr(g_rx, "chunked") != NULL);
        g_rx[hlen] = sv;
        if (chk) blen = http_dechunk_inplace(body, blen);
        if (blen < 8) continue;

        uint32_t* px = NULL; int w = 0, h = 0;
        int rc = webimg_decode((uint8_t*)body, blen, &px, &w, &h);
        char msg[110];
        strcpy(msg, "      -> decode rc ");
        IntToStr(rc, msg + strlen(msg));
        if (rc == 0) {
            strcat(msg, " ("); IntToStr(w, msg + strlen(msg));
            strcat(msg, "x"); IntToStr(h, msg + strlen(msg)); strcat(msg, ")");
        }
        strcat(msg, "\n");
        GUI_Memo_AddStr(LogMemo, msg);

        if (rc != 0 || !px || w <= 0 || h <= 0 || w * h > 256 * 256) {
            if (px) free(px);
            continue;
        }
        sl->px = px; sl->w = w; sl->h = h; sl->loaded = true;
        loaded++;
    }
    for (int i = 0; i < doc->lc; i++) {
        WebLine* ln = &doc->lines[i];
        if (ln->style == WEB_STYLE_IMG &&
            ln->img_slot >= 0 && ln->img_slot < doc->ic &&
            !doc->imgs[ln->img_slot].loaded) {
            ln->style = WEB_STYLE_TEXT;
        }
    }
    return loaded;
}

// ============================================================================
// v2.0: HISTÓRICO (botão VOLTAR)
// ============================================================================
static void history_push(const char* url) {
    if (g_history_top < MAX_HISTORY) {
        safe_strcpy(g_history[g_history_top], url, 192);
        g_history_top++;
    } else {
        for (int i = 0; i < MAX_HISTORY - 1; i++)
            safe_strcpy(g_history[i], g_history[i + 1], 192);
        safe_strcpy(g_history[MAX_HISTORY - 1], url, 192);
    }
}

static void free_doc(void) {
    if (!g_doc) return;
    for (int i = 0; i < g_doc->ic; i++) {
        if (g_doc->imgs[i].px) free(g_doc->imgs[i].px);
    }
    webdoc_destroy(g_doc);
    g_doc = NULL;
}

// ============================================================================
// CARREGA E RENDERIZA UMA URL (v3.0: TWebPage)
// ============================================================================
static void load_url(const char* url_in) {
    GUI_WebPage_Clear(WebPage);
    free_doc();
    bool era_https = starts_with_ci(url_in, "https://");
    Label_SetText(LblStatus, era_https ? "TLS... carregando" : "Carregando...");
    GUI_Memo_Clear(LogMemo);
    if (era_https) GUI_Memo_AddStr(LogMemo, "[TLS] HTTPS nativo v4.0.\n");
    Flush_Grafico_Janela();

    if (!g_net_ready) {
        g_net_ready = net_bootstrap();
        if (!g_net_ready) { Label_SetText(LblStatus, "Erro de rede"); return; }
    }

    GUI_Memo_AddStr(LogMemo, "[GET] ");
    GUI_Memo_AddStr(LogMemo, url_in);
    GUI_Memo_AddStr(LogMemo, "\n");
    Flush_Grafico_Janela();

    char host[128];
    bool final_tls = false;
    int total = fetch_any(url_in, host, sizeof(host), &final_tls);
    if (total <= 0) {
        Label_SetText(LblStatus, "Erro no download");
        GUI_Memo_AddStr(LogMemo, "[ERRO] Falha no download.\n");
        Flush_Grafico_Janela();
        return;
    }
    int status = http_status_of(g_rx);

    // g_host COM scheme: links relativos herdam o TLS da página
    safe_strcpy(g_host, final_tls ? "https://" : "http://", sizeof(g_host));
    safe_strcat(g_host, host, sizeof(g_host), strlen(g_host));

    const char* hdr_end = local_strstr(g_rx, "\r\n\r\n");
    char* body = hdr_end ? (char*)(hdr_end + 4) : g_rx;
    int body_len = total - (int)(body - g_rx);
    if (body_len < 0) body_len = 0;
    int hdr_len = hdr_end ? (int)(hdr_end - g_rx) : total;
    char saved = g_rx[hdr_len];
    g_rx[hdr_len] = '\0';
    bool is_chunked = (local_strstr(g_rx, "chunked") != NULL);
    g_rx[hdr_len] = saved;
    if (is_chunked) body_len = http_dechunk_inplace(body, body_len);

    static char* g_body_copy = NULL;
    if (g_body_copy) { free(g_body_copy); g_body_copy = NULL; }
    g_body_copy = (char*)malloc(body_len > 0 ? body_len : 1);
    if (g_body_copy) { for (int i = 0; i < body_len; i++) g_body_copy[i] = body[i]; g_body = g_body_copy; }
    else g_body = NULL;
    g_body_len = body_len;

    g_doc = webdoc_create();
    if (g_doc) {
        html_to_webdoc(body, g_doc);
        int imgs = try_load_images(g_doc);
        GUI_WebPage_SetDoc(WebPage, g_doc);
        char st[110]; char n1[8], n2[8];
        IntToStr(status, st);
        strcat(st, final_tls ? " OK (TLS) | " : " OK | ");
        IntToStr(g_doc->kc, n1); strcat(st, n1); strcat(st, " links | ");
        IntToStr(imgs, n2); strcat(st, n2); strcat(st, " imgs");
        Label_SetText(LblStatus, st);
        GUI_Memo_AddStr(LogMemo, "[OK] Pagina renderizada no TWebPage.\n");
    }
    Flush_Grafico_Janela();
}

// ============================================================================
// v3.0: clique em link do TWebPage
// ============================================================================
void OnPageNavigate(void* sender, const char* url) {
    (void)sender;
    if (!url || url[0] == '\0') return;

    // ignora schemes não-http (mailto:, ftp:, ...)
    if (!starts_with_ci(url, "http://") && !starts_with_ci(url, "https://") &&
        local_strstr(url, "://")) {
        Label_SetText(LblStatus, "Scheme nao suportado");
        return;
    }

    char full[256];
    resolve_url(url, full, sizeof(full));
    GUI_Edit_SetText(EditURL, full);
    history_push(full);
    load_url(full);
}

// ============================================================================
// CALLBACKS
// ============================================================================
void OnBtnGoClick(void* sender) {
    (void)sender;
    char url[192];
    safe_strcpy(url, GUI_Edit_GetText(EditURL), sizeof(url));
    if (url[0] == '\0') safe_strcpy(url, "info.cern.ch/", sizeof(url));
    GUI_Edit_SetText(EditURL, url);
    history_push(url);
    load_url(url);
}

void OnBtnBackClick(void* sender) {
    (void)sender;
    if (g_history_top > 1) {
        g_history_top--;
        char prev[192];
        safe_strcpy(prev, g_history[g_history_top - 1], 192);
        GUI_Edit_SetText(EditURL, prev);
        load_url(prev);
    } else {
        Label_SetText(LblStatus, "Sem historico");
    }
}

void OnBtnRefreshClick(void* sender) {
    (void)sender;
    char url[192];
    safe_strcpy(url, GUI_Edit_GetText(EditURL), sizeof(url));
    if (url[0] != '\0') load_url(url);
}

void OnBtnSaveClick(void* sender) {
    (void)sender;
    if (!g_body || g_body_len <= 0) {
        Label_SetText(LblStatus, "Nada p/ salvar");
        return;
    }
    const char* filename = "0:/pagina.html";
    int st = sys_fat_write(filename, (void*)g_body, (uint32_t)g_body_len);
    Label_SetText(LblStatus, (st == 0) ? "Salvo em 0:/pagina.html" : "Erro ao salvar");
}

// ============================================================================
// FUNÇÕES DE JANELA
// ============================================================================
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

char Obter_Tecla_Entrada(void) {
    if (my_app_slot < 0) return 0;
    AppWindowInfoExtended* ext_slot = (AppWindowInfoExtended*)&IPC_WINDOW_LIST[my_app_slot];
    if (ext_slot->tem_evento_teclado == 1) {
        char key = (char)ext_slot->fila_teclado_virtual;
        ext_slot->tem_evento_teclado = 0;
        return key;
    }
    return 0;
}

// ============================================================================
// MAIN
// ============================================================================
int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    static int ultimo_x = 0, ultimo_y = 0, mouse_hold_timer = 0;
    static bool primeiro_desenho = true, ultimo_estado_foco = false;
    void* ultimo_controle_focado = NULL;

    graphics_init_app(winWidth, winHeight);
    wm_init();

    my_app_slot = OS_IPC_RegisterApp("LBF Browser", winWidth, winHeight);
    if (my_app_slot == -1) return -1;

    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "LBF Browser v4.0", winWidth, winHeight);
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0xC0C0C0);

    // Linha 1: barra de endereço + IR
    EditURL = GUI_CreateEdit(&MyApp, 10, 36, 440, 28, "https://example.com/", NULL);
    BtnGo   = GUI_CreateButton(&MyApp, 460, 36, 70, 28, "IR", OnBtnGoClick);

    // Linha 2: navegação + status
    BtnBack    = GUI_CreateButton(&MyApp, 10, 74, 90, 28, "VOLTAR",    OnBtnBackClick);
    BtnRefresh = GUI_CreateButton(&MyApp, 110, 74, 110, 28, "ATUALIZAR", OnBtnRefreshClick);
    BtnSave    = GUI_CreateButton(&MyApp, 230, 74, 90, 28, "SALVAR",    OnBtnSaveClick);
    LblStatus  = GUI_CreateLabel(&MyApp, 330, 78, "Pronto.");

    // Página visual (TWebPage) + log fino
    WebPage = GUI_CreateWebPage(&MyApp, 10, 110, 600, 360, OnPageNavigate);
    LogMemo = GUI_CreateMemo(&MyApp, 10, 478, 600, 130);
    gui_set_prop(LogMemo, PROP_COLOR, 0x000000);

    g_focused_control = (void*)EditURL;
    ultimo_controle_focado = (void*)EditURL;
    gui_set_prop(EditURL, PROP_SET_FOCUS, 1);

    GUI_Memo_AddStr(LogMemo, "LBF Browser v3.0: clique nos links AZUIS!\n");
    Flush_Grafico_Janela();

    while (1) {
        if (IPC_WINDOW_LIST[my_app_slot].is_active == 0) {
            Tratar_Fechamento_Software();
            break;
        }
        bool precisa_redesenhar = false;
        if (primeiro_desenho) { primeiro_desenho = false; precisa_redesenhar = true; }

        bool euTenhoFocoJanelaReal = (IPC_CONTROL->active_focus_slot == my_app_slot);
        if (euTenhoFocoJanelaReal != ultimo_estado_foco) {
            ultimo_estado_foco = euTenhoFocoJanelaReal;
            if (MyApp.MainWindow) ((TForm*)MyApp.MainWindow)->ActiveFocus = euTenhoFocoJanelaReal;
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

                if (BtnGo && rel_x >= BtnGo->Left && rel_x < (BtnGo->Left + BtnGo->Width) &&
                    rel_y >= BtnGo->Top && rel_y < (BtnGo->Top + BtnGo->Height)) {
                    gui_set_prop(BtnGo, PROP_STATE, 2);
                }
                else if (BtnBack && rel_x >= BtnBack->Left && rel_x < (BtnBack->Left + BtnBack->Width) &&
                         rel_y >= BtnBack->Top && rel_y < (BtnBack->Top + BtnBack->Height)) {
                    gui_set_prop(BtnBack, PROP_STATE, 2);
                }
                else if (BtnRefresh && rel_x >= BtnRefresh->Left && rel_x < (BtnRefresh->Left + BtnRefresh->Width) &&
                         rel_y >= BtnRefresh->Top && rel_y < (BtnRefresh->Top + BtnRefresh->Height)) {
                    gui_set_prop(BtnRefresh, PROP_STATE, 2);
                }
                else if (BtnSave && rel_x >= BtnSave->Left && rel_x < (BtnSave->Left + BtnSave->Width) &&
                         rel_y >= BtnSave->Top && rel_y < (BtnSave->Top + BtnSave->Height)) {
                    gui_set_prop(BtnSave, PROP_STATE, 2);
                }

                events_process_mouse(rel_x, rel_y, 1, 0);
                if (GUI_ProcessMouseClick(&MyApp, rel_x, rel_y)) {
                    precisa_redesenhar = true;
                    if (g_focused_control != NULL) ultimo_controle_focado = g_focused_control;
                }
            }
            IPC_WINDOW_LIST[my_app_slot].has_click_event = 0;
        }
        if (mouse_hold_timer > 0) {
            mouse_hold_timer--;
            if (mouse_hold_timer == 0) {
                if (BtnGo)      gui_set_prop(BtnGo, PROP_STATE, 0);
                if (BtnBack)    gui_set_prop(BtnBack, PROP_STATE, 0);
                if (BtnRefresh) gui_set_prop(BtnRefresh, PROP_STATE, 0);
                if (BtnSave)    gui_set_prop(BtnSave, PROP_STATE, 0);
                events_process_mouse(ultimo_x, ultimo_y, 0, 0);
                precisa_redesenhar = true;
            }
        }

        if (precisa_redesenhar) Flush_Grafico_Janela();
        sys_sleep(euTenhoFocoJanelaReal ? 16 : 32);
    }

    free_doc();
    sys_exit();
    return 0;
}
