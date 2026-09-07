/*
====================================================================
                    HISTÓRICO DE COLABORAÇÃO
====================================================================
Arquivo: mini_wget.c
Versão: 1.1
Data: 31/08/2026
Autor: LBF-OS Team + AI Assistant

Base:
    - v1.0 (download HTTP + gravação FAT32 + redirect)

Mudanças v1.1:
    - NOVO LAYOUT RAD:
      * Edit da URL ocupa toda a largura do memo (10..610)
      * Botão "BAIXAR E SALVAR" (150px, largura do texto) abaixo
      * Edit do arquivo alinhado ao memo e ao edit da URL (170..610)
    - HTTP: envia "Accept-Encoding: identity" (sem compressão)
    - HTTP: decodifica Transfer-Encoding: chunked antes de salvar
    - HTTP: avisa se o corpo vier comprimido (gzip/br)
    - Corrigidos helpers (local_strstr / parse_url / ip_to_str)

Testado em:
    - LBF-OS Base_v4.6.5 / Kernel v2.9 / Runtime v6.3
====================================================================
*/

#include "sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"
#include "../system/sysutils.h"
#include "components/TOS_IPC.h"

// Pilha de Rede Ring 3
#include "net_user/net_utils.h"
#include "net_user/net_interface.h"
#include "net_user/net_poll.h"
#include "net_user/arp.h"
#include "net_user/ip.h"
#include "net_user/dhcp.h"
#include "net_user/dns.h"
#include "net_user/socket.h"

// Protótipos RAD
void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;
extern void GUI_Memo_AddStr(TGUIControl* memo, const char* str);
extern void GUI_Memo_Clear(TGUIControl* memo);

// Protótipos locais (evita implicit declaration nos callbacks)
void Flush_Grafico_Janela(void);
void Tratar_Fechamento_Software(void);
char Obter_Tecla_Entrada(void);

int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth = 620;
const int winHeight = 460;

TGUIControl* EditURL  = NULL;
TGUIControl* EditFile = NULL;
TGUIControl* BtnWget  = NULL;
TGUIControl* MemoLog  = NULL;

// Buffers grandes ESTÁTICOS (evita estourar a pilha)
static char g_rx[131072];        // 128KB p/ resposta HTTP
static char g_chunk[512];        // pedaço temporário do recv

static bool g_net_ready = false;
static uint32_t g_dns_server = 0x0302000A; // 10.0.2.3 (fallback NAT)

typedef struct {
    uint8_t dummy[sizeof(IPC_WINDOW_LIST[0])];
    volatile uint8_t fila_teclado_virtual;
    volatile uint8_t tem_evento_teclado;
} __attribute__((packed)) AppWindowInfoExtended;

// ============================================================================
// HELPERS
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

static void ip_to_str(uint32_t ip, char* out_buf) {
    if (!out_buf) return;
    uint8_t b1 = ip & 0xFF;
    uint8_t b2 = (ip >> 8) & 0xFF;
    uint8_t b3 = (ip >> 16) & 0xFF;
    uint8_t b4 = (ip >> 24) & 0xFF;
    char temp[16];
    IntToStr(b1, temp); strcpy(out_buf, temp); strcat(out_buf, ".");
    IntToStr(b2, temp); strcat(out_buf, temp); strcat(out_buf, ".");
    IntToStr(b3, temp); strcat(out_buf, temp); strcat(out_buf, ".");
    IntToStr(b4, temp); strcat(out_buf, temp);
}

// Parseia "http://host/caminho" (scheme opcional)
static bool parse_url(const char* url, char* host_out, size_t host_max,
                      char* path_out, size_t path_max) {
    if (!url || !host_out || !path_out) return false;
    const char* p = url;
    if (p[0]=='h' && p[1]=='t' && p[2]=='t' && p[3]=='p' &&
        p[4]==':' && p[5]=='/' && p[6]=='/') p += 7;
    size_t hi = 0;
    while (*p && *p != '/' && hi < host_max - 1) host_out[hi++] = *p++;
    host_out[hi] = '\0';
    size_t pi = 0;
    if (*p == '/') { while (*p && pi < path_max - 1) path_out[pi++] = *p++; }
    else path_out[pi++] = '/';
    path_out[pi] = '\0';
    return (hi > 0);
}

// Extrai o código de status da 1ª linha ("HTTP/1.1 200 OK")
static int http_status_of(const char* buf) {
    if (!buf || !(buf[0]=='H' && buf[1]=='T' && buf[2]=='T' && buf[3]=='P')) return -1;
    const char* p = buf;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    int st = 0;
    while (*p >= '0' && *p <= '9') { st = st * 10 + (*p - '0'); p++; }
    return st;
}

// ============================================================================
// v1.1: DECODIFICADOR DE TRANSFER-ENCODING: CHUNKED (in-place)
// ============================================================================
static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static int http_dechunk_inplace(char* body, int len) {
    if (!body || len <= 0) return 0;
    int read_pos = 0;
    int write_pos = 0;

    while (read_pos < len) {
        int chunk_size = 0;

        // Lê o tamanho do chunk em hexadecimal até o CRLF
        while (read_pos < len) {
            char c = body[read_pos++];
            if (c == '\r') {
                if (read_pos < len && body[read_pos] == '\n') read_pos++;
                break;
            }
            if (c == '\n') break;
            if (c == ';') { // extensão de chunk: pula até o fim da linha
                while (read_pos < len && body[read_pos] != '\n') read_pos++;
                if (read_pos < len) read_pos++;
                break;
            }
            int hv = hex_value(c);
            if (hv < 0) return write_pos;
            chunk_size = (chunk_size << 4) | hv;
        }

        if (chunk_size == 0) break; // chunk final

        if (read_pos + chunk_size > len) chunk_size = len - read_pos;
        for (int i = 0; i < chunk_size; i++) body[write_pos++] = body[read_pos++];

        // Pula o CRLF após os dados do chunk
        if (read_pos < len && body[read_pos] == '\r') read_pos++;
        if (read_pos < len && body[read_pos] == '\n') read_pos++;
    }
    return write_pos;
}

// ============================================================================
// BOOTSTRAP DE REDE (a pilha vive no Ring 3: cada app faz o seu)
// ============================================================================
static bool net_bootstrap(void) {
    GUI_Memo_AddStr(MemoLog, "[WGET] Bootstrap de rede (ARP + DHCP)...\n");
    uint8_t mac[6] = {0};
    if (sys_net_get_mac(mac) != 0) {
        GUI_Memo_AddStr(MemoLog, "[ERRO] Falha ao obter MAC.\n");
        return false;
    }
    // IP estático temporário + ARP do gateway
    ip_set_config(MAKE_IP(10,0,2,15), MAKE_IP(255,255,255,0), MAKE_IP(10,0,2,2));
    arp_send_request(MAKE_IP(10,0,2,2));
    uint8_t gw_mac[6];
    for (int i = 0; i < 50; i++) {
        net_poll();
        if (arp_lookup(MAKE_IP(10,0,2,2), gw_mac)) break;
        sys_sleep(20);
    }
    // DHCP (DORA)
    dhcp_config_t cfg;
    if (dhcp_request_ip(mac, &cfg) == 0) {
        ip_set_config(cfg.ip, cfg.netmask, cfg.gateway);
        if (cfg.dns_server != 0) g_dns_server = cfg.dns_server;
        char s[32];
        GUI_Memo_AddStr(MemoLog, "[OK] DHCP: IP ");
        ip_to_str(cfg.ip, s); GUI_Memo_AddStr(MemoLog, s);
        GUI_Memo_AddStr(MemoLog, " / DNS ");
        ip_to_str(g_dns_server, s); GUI_Memo_AddStr(MemoLog, s);
        GUI_Memo_AddStr(MemoLog, "\n");
    } else {
        GUI_Memo_AddStr(MemoLog, "[AVISO] DHCP falhou, usando estatico 10.0.2.15.\n");
    }
    return true;
}

// ============================================================================
// HTTP GET (uma tentativa) — retorna total de bytes recebidos
// ============================================================================
static int http_fetch(const char* host, const char* path) {
    uint32_t target_ip = parse_ip(host);
    if (target_ip == 0) {
        GUI_Memo_AddStr(MemoLog, "[WGET] Resolvendo DNS de ");
        GUI_Memo_AddStr(MemoLog, host);
        GUI_Memo_AddStr(MemoLog, "...\n");
        target_ip = dns_resolve(host, g_dns_server);
        if (target_ip == 0) {
            GUI_Memo_AddStr(MemoLog, "[ERRO] DNS falhou.\n");
            return -1;
        }
        char s[32];
        GUI_Memo_AddStr(MemoLog, "[OK] DNS -> ");
        ip_to_str(target_ip, s); GUI_Memo_AddStr(MemoLog, s);
        GUI_Memo_AddStr(MemoLog, "\n");
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) return -2;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(80);
    sa.sin_addr.s_addr = target_ip;
    if (connect(sockfd, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        close(sockfd);
        GUI_Memo_AddStr(MemoLog, "[ERRO] Falha no connect TCP.\n");
        return -3;
    }
    GUI_Memo_AddStr(MemoLog, "[WGET] Conectado! Enviando GET...\n");

    char req[256];
    size_t l = safe_strcpy(req, "GET ", sizeof(req));
    l = safe_strcat(req, path, sizeof(req), l);
    l = safe_strcat(req, " HTTP/1.1\r\nHost: ", sizeof(req), l);
    l = safe_strcat(req, host, sizeof(req), l);
    // v1.1: pede conteúdo SEM compressão para salvar texto legível
    l = safe_strcat(req,
        "\r\nUser-Agent: LBF-Wget/1.1"
        "\r\nAccept: text/html,*/*"
        "\r\nAccept-Encoding: identity"
        "\r\nConnection: close\r\n\r\n", sizeof(req), l);
    send(sockfd, req, l, 0);

    int total = 0;
    int timeout = 300;
    while (timeout > 0 && total < (int)sizeof(g_rx) - 1) {
        int n = recv(sockfd, g_chunk, sizeof(g_chunk) - 1, 0);
        if (n > 0) {
            if (total + n > (int)sizeof(g_rx) - 1) n = sizeof(g_rx) - 1 - total;
            for (int i = 0; i < n; i++) g_rx[total + i] = g_chunk[i];
            total += n;
            timeout = 150;
        } else {
            sys_sleep(10);
            timeout--;
        }
    }
    close(sockfd);
    g_rx[total] = '\0';
    return total;
}

// ============================================================================
// CALLBACK: BAIXAR E SALVAR
// ============================================================================
void OnBtnWgetClick(void* sender) {
    (void)sender;
    GUI_Memo_Clear(MemoLog);
    GUI_Memo_AddStr(MemoLog, "========================================\n");
    GUI_Memo_AddStr(MemoLog, "   Mini-WGET LBF-OS v1.1\n");
    GUI_Memo_AddStr(MemoLog, "========================================\n");

    if (!g_net_ready) {
        g_net_ready = net_bootstrap();
        if (!g_net_ready) { Flush_Grafico_Janela(); return; }
    }

    char url[192], filename[128];
    safe_strcpy(url, GUI_Edit_GetText(EditURL), sizeof(url));
    safe_strcpy(filename, GUI_Edit_GetText(EditFile), sizeof(filename));
    if (url[0] == '\0') safe_strcpy(url, "www.google.com/", sizeof(url));
    if (filename[0] == '\0') safe_strcpy(filename, "0:/download.html", sizeof(filename));

    char host[128], path[128];
    if (!parse_url(url, host, sizeof(host), path, sizeof(path))) {
        GUI_Memo_AddStr(MemoLog, "[ERRO] URL invalida.\n");
        Flush_Grafico_Janela();
        return;
    }

    int total = -1;
    for (int r = 0; r < 2; r++) {   // no máx. 1 redirect
        GUI_Memo_AddStr(MemoLog, "\n[WGET] GET ");
        GUI_Memo_AddStr(MemoLog, path);
        GUI_Memo_AddStr(MemoLog, " em ");
        GUI_Memo_AddStr(MemoLog, host);
        GUI_Memo_AddStr(MemoLog, "...\n");
        Flush_Grafico_Janela();

        total = http_fetch(host, path);
        if (total <= 0) {
            GUI_Memo_AddStr(MemoLog, "[ERRO] Download falhou.\n");
            Flush_Grafico_Janela();
            return;
        }

        int status = http_status_of(g_rx);
        char msg[64];
        strcpy(msg, "[WGET] Status HTTP: ");
        IntToStr(status, msg + strlen(msg));
        strcat(msg, " | ");
        IntToStr(total, msg + strlen(msg));
        strcat(msg, " bytes recebidos\n");
        GUI_Memo_AddStr(MemoLog, msg);

        if ((status == 301 || status == 302)) {
            const char* loc = local_strstr(g_rx, "Location:");
            if (loc) {
                loc += 9;
                while (*loc == ' ') loc++;
                char new_url[192];
                size_t i = 0;
                while (*loc && *loc != '\r' && *loc != '\n' && i < sizeof(new_url) - 1)
                    new_url[i++] = *loc++;
                new_url[i] = '\0';
                char nh[128], np[128];
                if (parse_url(new_url, nh, sizeof(nh), np, sizeof(np))) {
                    GUI_Memo_AddStr(MemoLog, "[WGET] Redirect -> seguindo ");
                    GUI_Memo_AddStr(MemoLog, nh);
                    GUI_Memo_AddStr(MemoLog, "\n");
                    safe_strcpy(host, nh, sizeof(host));
                    safe_strcpy(path, np, sizeof(path));
                    continue;
                }
            }
        }
        break;   // 200 ou sem redirect: termina
    }

    // Separa cabeçalho do corpo
    const char* hdr_end = local_strstr(g_rx, "\r\n\r\n");
    const char* body = hdr_end ? (hdr_end + 4) : g_rx;
    int body_len = total - (int)(body - g_rx);
    if (body_len < 0) body_len = 0;

    // v1.1: decodifica chunked, se o servidor tiver usado
    if (local_strstr(g_rx, "Transfer-Encoding: chunked") ||
        local_strstr(g_rx, "transfer-encoding: chunked")) {
        GUI_Memo_AddStr(MemoLog, "[WGET] Chunked detectado: decodificando...\n");
        body_len = http_dechunk_inplace((char*)body, body_len);
    }

    // v1.1: avisa se veio comprimido (não suportado ainda)
    if (local_strstr(g_rx, "Content-Encoding: gzip") ||
        local_strstr(g_rx, "content-encoding: gzip") ||
        local_strstr(g_rx, "Content-Encoding: br") ||
        local_strstr(g_rx, "content-encoding: br")) {
        GUI_Memo_AddStr(MemoLog, "[AVISO] Conteudo comprimido (gzip/br): texto pode ficar ilegivel.\n");
    }

    char msg[96];
    strcpy(msg, "[WGET] Corpo da pagina: ");
    IntToStr(body_len, msg + strlen(msg));
    strcat(msg, " bytes\n");
    GUI_Memo_AddStr(MemoLog, msg);

    // Grava no FAT32 (raiz)
    GUI_Memo_AddStr(MemoLog, "[WGET] Gravando em ");
    GUI_Memo_AddStr(MemoLog, filename);
    GUI_Memo_AddStr(MemoLog, "...\n");
    Flush_Grafico_Janela();

    int st = sys_fat_write(filename, (void*)body, (uint32_t)body_len);
    if (st == 0) {
        GUI_Memo_AddStr(MemoLog, "[OK] ARQUIVO SALVO NO DISCO!\n");
        GUI_Memo_AddStr(MemoLog, "[DICA] Abra o Bloco de Notas e carregue ");
        GUI_Memo_AddStr(MemoLog, filename);
        GUI_Memo_AddStr(MemoLog, " para ver o HTML!\n\n");
    } else {
        GUI_Memo_AddStr(MemoLog, "[ERRO] sys_fat_write falhou.\n\n");
    }
    Flush_Grafico_Janela();
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

    my_app_slot = OS_IPC_RegisterApp("Mini WGET", winWidth, winHeight);
    if (my_app_slot == -1) return -1;

    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Mini-WGET - LBF-OS v1.1", winWidth, winHeight);
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0x000000);

    // ========================================================================
    // v1.1: NOVO LAYOUT RAD (alinhado ao memo 10..610)
    // ========================================================================
    // Linha 1: Edit da URL ocupa TODA a largura do memo
    EditURL  = GUI_CreateEdit(&MyApp, 10, 36, 600, 28, "www.google.com/", NULL);

    // Linha 2: Botão (largura do texto) + Edit do arquivo alinhado ao memo
    BtnWget  = GUI_CreateButton(&MyApp, 10, 72, 150, 28, "BAIXAR E SALVAR", OnBtnWgetClick);
    EditFile = GUI_CreateEdit(&MyApp, 170, 72, 440, 28, "0:/google.html", NULL);

    // Log do Download (memo alinhado 10..610)
    GUI_CreateLabel(&MyApp, 10, 108, "Log do Download:");
    MemoLog = GUI_CreateMemo(&MyApp, 10, 126, 600, 324);
    gui_set_prop(MemoLog, PROP_COLOR, 0x000000);

    g_focused_control = (void*)EditURL;
    ultimo_controle_focado = (void*)EditURL;
    gui_set_prop(EditURL, PROP_SET_FOCUS, 1);

    GUI_Memo_AddStr(MemoLog, "Mini-WGET v1.1 pronto!\n");
    GUI_Memo_AddStr(MemoLog, "Clique em 'BAIXAR E SALVAR' para baixar a URL\n");
    GUI_Memo_AddStr(MemoLog, "e gravar o corpo da pagina no FAT32 (raiz).\n");
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
                if (BtnWget && rel_x >= BtnWget->Left && rel_x < (BtnWget->Left + BtnWget->Width) &&
                    rel_y >= BtnWget->Top && rel_y < (BtnWget->Top + BtnWget->Height)) {
                    gui_set_prop(BtnWget, PROP_STATE, 2);
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
                if (BtnWget) gui_set_prop(BtnWget, PROP_STATE, 0);
                events_process_mouse(ultimo_x, ultimo_y, 0, 0);
                precisa_redesenhar = true;
            }
        }

        if (precisa_redesenhar) Flush_Grafico_Janela();
        sys_sleep(euTenhoFocoJanelaReal ? 16 : 32);
    }
    sys_exit();
    return 0;
}
