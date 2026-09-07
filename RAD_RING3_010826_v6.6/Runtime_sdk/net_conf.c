/*
====================================================================
                    HISTÓRICO DE COLABORAÇÃO
====================================================================
Arquivo: net_config.c
Versão: 1.1
Data: 31/08/2026
Autor: LBF-OS Team + AI Assistant

Base:
    - v1.0 (Configurador de Rede com bootstrap DHCP)

Mudanças v1.1:
    - CORREÇÃO: botões descidos para BAIXO da barra de títulos (y=35)
    - ADICIONADO botão "3. LIMPAR LOGS" (limpa o memo de status)
    - Janela alargada na horizontal: 500 -> 620 px
    - Textos sem acentos (a fonte RAD nao renderiza c/a til)
    - Memo ampliado para aproveitar a nova largura

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

// Módulos da Pilha de Rede Ring 3
#include "net_user/net_interface.h"
#include "net_user/net_poll.h"
#include "net_user/arp.h"
#include "net_user/ip.h"
#include "net_user/dhcp.h"
#include "net_user/dns.h"
#include "net_user/icmp.h"

// Protótipos de renderização gráfica RAD
void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void GUI_Memo_AddStr(TGUIControl* memo, const char* str);
extern void GUI_Memo_Clear(TGUIControl* memo);

// Variáveis Globais de Janela e Estado
int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth = 620;    // v1.1: alargado (era 500)
const int winHeight = 400;

// Ponteiros de Controle RAD
TGUIControl* MemoStatus = NULL;
TGUIControl* BtnConnect = NULL;
TGUIControl* BtnTest = NULL;
TGUIControl* BtnClear = NULL;   // v1.1: novo botão

// Estado da conexão
static bool g_network_ready = false;
static uint32_t g_my_ip = 0;
static uint32_t g_gateway = 0;
static uint32_t g_dns = 0;

typedef struct {
    uint8_t dummy[sizeof(IPC_WINDOW_LIST[0])];
    volatile uint8_t fila_teclado_virtual;
    volatile uint8_t tem_evento_teclado;
} __attribute__((packed)) AppWindowInfoExtended;

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================
static void ip_to_str(uint32_t ip, char* out_buf) {
    if (!out_buf) return;
    uint8_t b1 = ip & 0xFF;
    uint8_t b2 = (ip >> 8) & 0xFF;
    uint8_t b3 = (ip >> 16) & 0xFF;
    uint8_t b4 = (ip >> 24) & 0xFF;
    char temp[16];
    IntToStr(b1, temp);   strcpy(out_buf, temp); strcat(out_buf, ".");
    IntToStr(b2, temp);   strcat(out_buf, temp); strcat(out_buf, ".");
    IntToStr(b3, temp);   strcat(out_buf, temp); strcat(out_buf, ".");
    IntToStr(b4, temp);   strcat(out_buf, temp);
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
// SEQUÊNCIA DE BOOTSTRAP DA REDE (DHCP AUTOMÁTICO)
// ============================================================================
static bool net_bootstrap(void) {
    GUI_Memo_AddStr(MemoStatus, "[BOOT] Iniciando configuracao de rede...\n");
    Flush_Grafico_Janela();

    // 1. Obtém o MAC da placa de rede
    uint8_t mac[6] = {0};
    if (sys_net_get_mac(mac) != 0) {
        GUI_Memo_AddStr(MemoStatus, "[ERRO] Falha ao obter MAC da placa de rede.\n");
        return false;
    }

    char mac_str[32];
    char hex[4];
    strcpy(mac_str, "[OK] MAC: ");
    for (int i = 0; i < 6; i++) {
        IntToHex(mac[i], hex, 2);
        strcat(mac_str, hex + 2);
        if (i < 5) strcat(mac_str, ":");
    }
    strcat(mac_str, "\n");
    GUI_Memo_AddStr(MemoStatus, mac_str);
    Flush_Grafico_Janela();

    // 2. Configura IP estático temporário para o ARP funcionar
    uint32_t temp_ip = MAKE_IP(10, 0, 2, 15);
    uint32_t temp_gw = MAKE_IP(10, 0, 2, 2);
    uint32_t temp_mask = MAKE_IP(255, 255, 255, 0);
    ip_set_config(temp_ip, temp_mask, temp_gw);

    // 3. Envia ARP Request para o gateway
    GUI_Memo_AddStr(MemoStatus, "[ARP] Resolvendo MAC do gateway...\n");
    Flush_Grafico_Janela();

    arp_send_request(temp_gw);

    uint8_t gw_mac[6] = {0};
    bool arp_ok = false;
    for (int i = 0; i < 100; i++) {
        net_poll();
        if (arp_lookup(temp_gw, gw_mac)) {
            arp_ok = true;
            break;
        }
        sys_sleep(20);
    }

    if (!arp_ok) {
        GUI_Memo_AddStr(MemoStatus, "[ERRO] Timeout ARP: gateway nao respondeu.\n");
        return false;
    }

    GUI_Memo_AddStr(MemoStatus, "[OK] Gateway resolvido via ARP.\n");
    Flush_Grafico_Janela();

    // 4. Solicita IP via DHCP (DORA)
    GUI_Memo_AddStr(MemoStatus, "[DHCP] Solicitando IP dinamico...\n");
    Flush_Grafico_Janela();

    dhcp_config_t dhcp_cfg;
    if (dhcp_request_ip(mac, &dhcp_cfg) != 0) {
        GUI_Memo_AddStr(MemoStatus, "[AVISO] DHCP falhou. Usando IP estatico.\n");
        g_my_ip = temp_ip;
        g_gateway = temp_gw;
        g_dns = MAKE_IP(10, 0, 2, 3); // DNS padrão do VirtualBox NAT
    } else {
        g_my_ip = dhcp_cfg.ip;
        g_gateway = dhcp_cfg.gateway;
        g_dns = dhcp_cfg.dns_server;
        ip_set_config(g_my_ip, dhcp_cfg.netmask, g_gateway);

        char ip_str[32], gw_str[32], dns_str[32];
        ip_to_str(g_my_ip, ip_str);
        ip_to_str(g_gateway, gw_str);
        ip_to_str(g_dns, dns_str);

        GUI_Memo_AddStr(MemoStatus, "[OK] DHCP Sucesso!\n");
        GUI_Memo_AddStr(MemoStatus, "     IP: ");
        GUI_Memo_AddStr(MemoStatus, ip_str);
        GUI_Memo_AddStr(MemoStatus, "\n     Gateway: ");
        GUI_Memo_AddStr(MemoStatus, gw_str);
        GUI_Memo_AddStr(MemoStatus, "\n     DNS: ");
        GUI_Memo_AddStr(MemoStatus, dns_str);
        GUI_Memo_AddStr(MemoStatus, "\n");
    }

    Flush_Grafico_Janela();
    return true;
}

// ============================================================================
// TESTE DE CONEXÃO (PING + DNS)
// ============================================================================
static void test_connection(void) {
    GUI_Memo_AddStr(MemoStatus, "\n[TESTE] Validando conexao...\n");
    Flush_Grafico_Janela();

    // 1. Ping no gateway
    GUI_Memo_AddStr(MemoStatus, "[PING] Enviando Echo Request para o gateway...\n");
    Flush_Grafico_Janela();

    icmp_send_echo_request(g_gateway, 1, 1, "TEST", 4);

    for (int i = 0; i < 50; i++) {
        net_poll();
        sys_sleep(20);
    }

    GUI_Memo_AddStr(MemoStatus, "[OK] Ciclo de ping finalizado.\n");
    Flush_Grafico_Janela();

    // 2. Resolve DNS (google.com)
    GUI_Memo_AddStr(MemoStatus, "[DNS] Resolvendo google.com...\n");
    Flush_Grafico_Janela();

    uint32_t resolved = dns_resolve("google.com", g_dns);
    if (resolved != 0) {
        char ip_str[32];
        ip_to_str(resolved, ip_str);
        GUI_Memo_AddStr(MemoStatus, "[OK] DNS Resolvido: ");
        GUI_Memo_AddStr(MemoStatus, ip_str);
        GUI_Memo_AddStr(MemoStatus, "\n");
    } else {
        GUI_Memo_AddStr(MemoStatus, "[ERRO] Falha ao resolver DNS.\n");
    }

    Flush_Grafico_Janela();
}

// ============================================================================
// CALLBACKS DOS BOTÕES
// ============================================================================
void OnBtnConnectClick(void* sender) {
    (void)sender;
    GUI_Memo_Clear(MemoStatus);
    GUI_Memo_AddStr(MemoStatus, "========================================\n");
    GUI_Memo_AddStr(MemoStatus, "   Configurador de Rede - LBF-OS\n");
    GUI_Memo_AddStr(MemoStatus, "========================================\n\n");
    Flush_Grafico_Janela();

    if (net_bootstrap()) {
        g_network_ready = true;
        GUI_Memo_AddStr(MemoStatus, "\n[READY] Rede configurada e pronta para uso!\n");
    } else {
        g_network_ready = false;
        GUI_Memo_AddStr(MemoStatus, "\n[FALHA] Nao foi possivel configurar a rede.\n");
    }
    Flush_Grafico_Janela();
}

void OnBtnTestClick(void* sender) {
    (void)sender;
    if (!g_network_ready) {
        GUI_Memo_AddStr(MemoStatus, "\n[AVISO] Rede nao configurada. Clique em 'CONECTAR' primeiro.\n");
        Flush_Grafico_Janela();
        return;
    }
    test_connection();
}

// v1.1: novo callback para limpar o memo
void OnBtnClearClick(void* sender) {
    (void)sender;
    GUI_Memo_Clear(MemoStatus);
    GUI_Memo_AddStr(MemoStatus, "[Sistema] Logs limpos.\n");
    Flush_Grafico_Janela();
}

// ============================================================================
// FUNÇÃO PRINCIPAL (MAIN)
// ============================================================================
int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    static int ultimo_x = 0, ultimo_y = 0;
    static int mouse_hold_timer = 0;
    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;

    graphics_init_app(winWidth, winHeight);
    wm_init();

    my_app_slot = OS_IPC_RegisterApp("Configurador de Rede", winWidth, winHeight);
    if (my_app_slot == -1) return -1;

    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Configurador de Rede - LBF-OS v1.1", winWidth, winHeight);
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0x000000);

    // ========================================================================
    // v1.1: LAYOUT CORRIGIDO — botões ABAIXO da barra de títulos (y=35)
    // e janela alargada para 620px
    // ========================================================================
    int btnY = 35;
    int btnH = 30;
    BtnConnect = GUI_CreateButton(&MyApp, 10,  btnY, 230, btnH, "1. CONECTAR INTERNET", OnBtnConnectClick);
    BtnTest    = GUI_CreateButton(&MyApp, 250, btnY, 200, btnH, "2. TESTAR CONEXAO",  OnBtnTestClick);
    BtnClear   = GUI_CreateButton(&MyApp, 460, btnY, 150, btnH, "3. LIMPAR LOGS",     OnBtnClearClick);

    // Memo de Status (aproveita a nova largura: 600px)
    GUI_CreateLabel(&MyApp, 10, btnY + btnH + 10, "Status da Conexao:");
    MemoStatus = GUI_CreateMemo(&MyApp, 10, btnY + btnH + 30, 600, 295);
    gui_set_prop(MemoStatus, PROP_COLOR, 0x000000);

    GUI_Memo_AddStr(MemoStatus, "Bem-vindo ao Configurador de Rede!\n\n");
    GUI_Memo_AddStr(MemoStatus, "Clique em 'CONECTAR INTERNET' para iniciar\n");
    GUI_Memo_AddStr(MemoStatus, "a configuracao automatica via DHCP.\n\n");
    GUI_Memo_AddStr(MemoStatus, "Depois clique em 'TESTAR CONEXAO' para validar.\n");
    Flush_Grafico_Janela();

    while (1) {
        if (IPC_WINDOW_LIST[my_app_slot].is_active == 0) {
            Tratar_Fechamento_Software();
            break;
        }

        bool precisa_redesenhar = false;

        if (primeiro_desenho) {
            primeiro_desenho = false;
            precisa_redesenhar = true;
        }

        bool euTenhoFocoJanelaReal = (IPC_CONTROL->active_focus_slot == my_app_slot);
        if (euTenhoFocoJanelaReal != ultimo_estado_foco) {
            ultimo_estado_foco = euTenhoFocoJanelaReal;
            if (MyApp.MainWindow) ((TForm*)MyApp.MainWindow)->ActiveFocus = euTenhoFocoJanelaReal;
            precisa_redesenhar = true;
        }

        char key = Obter_Tecla_Entrada();
        if (key != 0) {
            GUI_ProcessKeyboard(&MyApp, key);
            precisa_redesenhar = true;
        }

        if (IPC_WINDOW_LIST[my_app_slot].has_click_event == 1) {
            if (mouse_hold_timer == 0) {
                int rel_x = IPC_WINDOW_LIST[my_app_slot].local_click_x;
                int rel_y = IPC_WINDOW_LIST[my_app_slot].local_click_y;
                ultimo_x = rel_x;
                ultimo_y = rel_y;
                mouse_hold_timer = 2;

                if (BtnConnect && rel_x >= BtnConnect->Left && rel_x < (BtnConnect->Left + BtnConnect->Width) &&
                    rel_y >= BtnConnect->Top && rel_y < (BtnConnect->Top + BtnConnect->Height)) {
                    gui_set_prop(BtnConnect, PROP_STATE, 2);
                }
                else if (BtnTest && rel_x >= BtnTest->Left && rel_x < (BtnTest->Left + BtnTest->Width) &&
                         rel_y >= BtnTest->Top && rel_y < (BtnTest->Top + BtnTest->Height)) {
                    gui_set_prop(BtnTest, PROP_STATE, 2);
                }
                else if (BtnClear && rel_x >= BtnClear->Left && rel_x < (BtnClear->Left + BtnClear->Width) &&
                         rel_y >= BtnClear->Top && rel_y < (BtnClear->Top + BtnClear->Height)) {
                    gui_set_prop(BtnClear, PROP_STATE, 2);
                }

                events_process_mouse(rel_x, rel_y, 1, 0);
                if (GUI_ProcessMouseClick(&MyApp, rel_x, rel_y)) precisa_redesenhar = true;
            }
            IPC_WINDOW_LIST[my_app_slot].has_click_event = 0;
        }

        if (mouse_hold_timer > 0) {
            mouse_hold_timer--;
            if (mouse_hold_timer == 0) {
                if (BtnConnect) gui_set_prop(BtnConnect, PROP_STATE, 0);
                if (BtnTest)    gui_set_prop(BtnTest, PROP_STATE, 0);
                if (BtnClear)   gui_set_prop(BtnClear, PROP_STATE, 0);
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
