/*
====================================================================
                    HISTÓRICO DE COLABORAÇÃO
====================================================================
Arquivo: ntp.c
Versão: 1.0
Data: 31/08/2026
Autor: LBF-OS Team + AI Assistant

Descrição:
    Cliente NTP do LBF-OS: consulta servidores de tempo via UDP/123
    e exibe a data/hora atual sincronizada com a internet.
    - Bootstrap de rede próprio (ARP + DHCP)
    - Envia NTP request via UDP
    - Converte timestamp Unix para data/hora legível
    - (Opcional) Chama sys_set_time para ajustar o relógio do kernel

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

// Protótipos locais
void Flush_Grafico_Janela(void);
void Tratar_Fechamento_Software(void);
char Obter_Tecla_Entrada(void);

int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth = 620;
const int winHeight = 460;

TGUIControl* EditServer = NULL;
TGUIControl* BtnSync    = NULL;
TGUIControl* MemoLog    = NULL;

static bool g_net_ready = false;
static uint32_t g_dns_server = 0x0302000A; // 10.0.2.3 (fallback NAT)

typedef struct {
    uint8_t dummy[sizeof(IPC_WINDOW_LIST[0])];
    volatile uint8_t fila_teclado_virtual;
    volatile uint8_t tem_evento_teclado;
} __attribute__((packed)) AppWindowInfoExtended;

// ============================================================================
// ESTRUTURA NTP PACKET (48 bytes)
// ============================================================================
typedef struct {
    uint8_t  li_vn_mode;      // Leap indicator (2 bits) | Version (3 bits) | Mode (3 bits)
    uint8_t  stratum;         // Stratum level
    uint8_t  poll;            // Poll interval
    uint8_t  precision;       // Precision
    uint32_t root_delay;      // Root delay
    uint32_t root_dispersion; // Root dispersion
    uint32_t ref_id;          // Reference ID
    uint32_t ref_timestamp_s; // Reference timestamp (seconds)
    uint32_t ref_timestamp_f; // Reference timestamp (fraction)
    uint32_t orig_timestamp_s;// Originate timestamp (seconds)
    uint32_t orig_timestamp_f;// Originate timestamp (fraction)
    uint32_t recv_timestamp_s;// Receive timestamp (seconds)
    uint32_t recv_timestamp_f;// Receive timestamp (fraction)
    uint32_t tx_timestamp_s;  // Transmit timestamp (seconds)
    uint32_t tx_timestamp_f;  // Transmit timestamp (fraction)
} __attribute__((packed)) ntp_packet_t;

// ============================================================================
// HELPERS
// ============================================================================
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

// ============================================================================
// HELPERS DE STRING (seguros contra overflow)
// ============================================================================
static size_t safe_strcpy(char* dest, const char* src, size_t max_size) {
    if (!dest || max_size == 0) return 0;
    size_t i = 0;
    if (src) {
        while (src[i] != '\0' && i < (max_size - 1)) {
            dest[i] = src[i];
            i++;
        }
    }
    dest[i] = '\0';
    return i;
}

static size_t safe_strcat(char* dest, const char* src, size_t max_size, size_t current_len) {
    if (!dest || !src || current_len >= max_size) return current_len;
    while (*src && current_len < (max_size - 1)) dest[current_len++] = *src++;
    dest[current_len] = '\0';
    return current_len;
}

// Converte timestamp Unix (segundos desde 1970) para data/hora legível
static void unix_to_datetime(uint32_t timestamp, char* out_buf, size_t max_len) {
    if (!out_buf || max_len < 32) return;
    
    // NTP usa epoch de 1900, Unix usa 1970. Diferença: 70 anos + 17 dias bissextos
    // Offset: 2208988800 segundos
    if (timestamp > 2208988800) {
        timestamp -= 2208988800;
    }
    
    // Algoritmo simplificado de conversão Unix timestamp -> data/hora
    uint32_t days = timestamp / 86400;
    uint32_t hours = (timestamp % 86400) / 3600;
    uint32_t minutes = (timestamp % 3600) / 60;
    uint32_t seconds = timestamp % 60;
    
    // Cálculo de ano/mês/dia (simplificado, assume 1970+)
    int year = 1970;
    int month = 1;
    int day = 1;
    
    // Dias por mês (não bissexto por padrão)
    int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    
    while (days >= 365) {
        // Verifica se é ano bissexto
        bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        int days_in_year = leap ? 366 : 365;
        if (days >= days_in_year) {
            days -= days_in_year;
            year++;
        } else {
            break;
        }
    }
    
    // Ajusta fevereiro para ano bissexto
    bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    if (leap) days_in_month[1] = 29;
    
    while (month <= 12 && days >= days_in_month[month - 1]) {
        days -= days_in_month[month - 1];
        month++;
    }
    day = days + 1;
    
    // Formata: YYYY-MM-DD HH:MM:SS
    char temp[16];
    strcpy(out_buf, "");
    
    IntToStr(year, temp);
    strcat(out_buf, temp);
    strcat(out_buf, "-");
    
    if (month < 10) strcat(out_buf, "0");
    IntToStr(month, temp);
    strcat(out_buf, temp);
    strcat(out_buf, "-");
    
    if (day < 10) strcat(out_buf, "0");
    IntToStr(day, temp);
    strcat(out_buf, temp);
    strcat(out_buf, " ");
    
    if (hours < 10) strcat(out_buf, "0");
    IntToStr(hours, temp);
    strcat(out_buf, temp);
    strcat(out_buf, ":");
    
    if (minutes < 10) strcat(out_buf, "0");
    IntToStr(minutes, temp);
    strcat(out_buf, temp);
    strcat(out_buf, ":");
    
    if (seconds < 10) strcat(out_buf, "0");
    IntToStr(seconds, temp);
    strcat(out_buf, temp);
    strcat(out_buf, " UTC");
}

// ============================================================================
// BOOTSTRAP DE REDE
// ============================================================================
static bool net_bootstrap(void) {
    GUI_Memo_AddStr(MemoLog, "[NTP] Bootstrap de rede (ARP + DHCP)...\n");
    uint8_t mac[6] = {0};
    if (sys_net_get_mac(mac) != 0) {
        GUI_Memo_AddStr(MemoLog, "[ERRO] Falha ao obter MAC.\n");
        return false;
    }
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
// NTP REQUEST
// ============================================================================
static bool ntp_query(const char* server) {
    GUI_Memo_AddStr(MemoLog, "[NTP] Resolvendo DNS de ");
    GUI_Memo_AddStr(MemoLog, server);
    GUI_Memo_AddStr(MemoLog, "...\n");
    Flush_Grafico_Janela();
    
    uint32_t server_ip = dns_resolve(server, g_dns_server);
    if (server_ip == 0) {
        GUI_Memo_AddStr(MemoLog, "[ERRO] DNS falhou.\n");
        return false;
    }
    
    char s[32];
    GUI_Memo_AddStr(MemoLog, "[OK] DNS -> ");
    ip_to_str(server_ip, s);
    GUI_Memo_AddStr(MemoLog, s);
    GUI_Memo_AddStr(MemoLog, "\n");
    Flush_Grafico_Janela();
    
    // Cria socket UDP
    int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) {
        GUI_Memo_AddStr(MemoLog, "[ERRO] Falha ao criar socket UDP.\n");
        return false;
    }
    
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(123); // Porta NTP
    dest_addr.sin_addr.s_addr = server_ip;
    
    if (connect(sockfd, (struct sockaddr*)&dest_addr, sizeof(dest_addr)) < 0) {
        GUI_Memo_AddStr(MemoLog, "[ERRO] Falha no connect UDP.\n");
        close(sockfd);
        return false;
    }
    
    GUI_Memo_AddStr(MemoLog, "[NTP] Enviando request...\n");
    Flush_Grafico_Janela();
    
    // Monta NTP packet
    ntp_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.li_vn_mode = 0x1B; // LI=0, VN=3 (NTPv3), Mode=3 (Client)
    
    if (send(sockfd, &pkt, sizeof(pkt), 0) < 0) {
        GUI_Memo_AddStr(MemoLog, "[ERRO] Falha ao enviar NTP request.\n");
        close(sockfd);
        return false;
    }
    
    // Aguarda resposta (timeout ~2 segundos)
    ntp_packet_t response;
    int attempts = 200;
    int bytes_received = 0;
    while (attempts-- > 0) {
        net_poll();
        bytes_received = recv(sockfd, &response, sizeof(response), 0);
        if (bytes_received == sizeof(response)) {
            break;
        }
        sys_sleep(10);
    }
    
    close(sockfd);
    
    if (bytes_received != sizeof(response)) {
        GUI_Memo_AddStr(MemoLog, "[ERRO] Timeout aguardando resposta NTP.\n");
        return false;
    }
    
    GUI_Memo_AddStr(MemoLog, "[OK] Resposta NTP recebida!\n");
    
    // Extrai o timestamp de transmissão (campo tx_timestamp_s)
    uint32_t ntp_time = ntohl(response.tx_timestamp_s);
    
    char datetime[64];
    unix_to_datetime(ntp_time, datetime, sizeof(datetime));
    
    GUI_Memo_AddStr(MemoLog, "\n========================================\n");
    GUI_Memo_AddStr(MemoLog, "   DATA/HORA SINCRONIZADA:\n");
    GUI_Memo_AddStr(MemoLog, "   ");
    GUI_Memo_AddStr(MemoLog, datetime);
    GUI_Memo_AddStr(MemoLog, "\n========================================\n\n");
    
    // (Opcional) Ajusta o relógio do kernel via syscall
    // sys_set_time(ntp_time); // Descomente se existir no kernel
    
    return true;
}

// ============================================================================
// CALLBACK
// ============================================================================
void OnBtnSyncClick(void* sender) {
    (void)sender;
    GUI_Memo_Clear(MemoLog);
    GUI_Memo_AddStr(MemoLog, "========================================\n");
    GUI_Memo_AddStr(MemoLog, "   Cliente NTP - LBF-OS v1.0\n");
    GUI_Memo_AddStr(MemoLog, "========================================\n");
    Flush_Grafico_Janela();
    
    if (!g_net_ready) {
        g_net_ready = net_bootstrap();
        if (!g_net_ready) {
            Flush_Grafico_Janela();
            return;
        }
    }
    
    char server[128];
    safe_strcpy(server, GUI_Edit_GetText(EditServer), sizeof(server));
    if (server[0] == '\0') safe_strcpy(server, "pool.ntp.org", sizeof(server));
    
    if (ntp_query(server)) {
        GUI_Memo_AddStr(MemoLog, "[OK] Sincronizacao concluida!\n");
    } else {
        GUI_Memo_AddStr(MemoLog, "[ERRO] Falha ao sincronizar.\n");
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
    
    my_app_slot = OS_IPC_RegisterApp("Cliente NTP", winWidth, winHeight);
    if (my_app_slot == -1) return -1;
    
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Cliente NTP - LBF-OS v1.0", winWidth, winHeight);
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0x000000);
    
    GUI_CreateLabel(&MyApp, 10, 40, "Servidor NTP:");
    EditServer = GUI_CreateEdit(&MyApp, 110, 36, 350, 28, "pool.ntp.org", NULL);
    BtnSync    = GUI_CreateButton(&MyApp, 470, 36, 140, 28, "SINCRONIZAR", OnBtnSyncClick);
    
    GUI_CreateLabel(&MyApp, 10, 78, "Log do NTP:");
    MemoLog = GUI_CreateMemo(&MyApp, 10, 96, 600, 354);
    gui_set_prop(MemoLog, PROP_COLOR, 0x000000);
    
    g_focused_control = (void*)EditServer;
    ultimo_controle_focado = (void*)EditServer;
    gui_set_prop(EditServer, PROP_SET_FOCUS, 1);
    
    GUI_Memo_AddStr(MemoLog, "Cliente NTP pronto!\n");
    GUI_Memo_AddStr(MemoLog, "Clique em 'SINCRONIZAR' para consultar a hora atual\n");
    GUI_Memo_AddStr(MemoLog, "em pool.ntp.org (ou outro servidor de sua escolha).\n");
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
                if (BtnSync && rel_x >= BtnSync->Left && rel_x < (BtnSync->Left + BtnSync->Width) &&
                    rel_y >= BtnSync->Top && rel_y < (BtnSync->Top + BtnSync->Height)) {
                    gui_set_prop(BtnSync, PROP_STATE, 2);
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
                if (BtnSync) gui_set_prop(BtnSync, PROP_STATE, 0);
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
