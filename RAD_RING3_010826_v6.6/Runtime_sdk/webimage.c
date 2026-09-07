/*
====================================================================
                    HISTÓRICO DE COLABORAÇÃO
====================================================================
Arquivo: webimage.c (Teste TWebImage)
Versão: 1.4
Data: 31/08/2026
Autor: LBF-OS Team + AI Assistant

Descrição:
    App de teste do componente TWebImage (SDK Ring 3).
    v1.4: NOVO LAYOUT RAD (fundo cinza 0xC0C0C0 p/ Labels visíveis):
      - Coluna esquerda: 4 botões + Label + Edit de caminho
      - Label "Carregado:" mostra o arquivo que o Edit carregou
      - TWebImage grande à direita
      - MEMO de log na base, largura total
    O Edit controla o caminho do BMP (não é mais hardcoded).

    Modos de teste:
      1. GRADIENTE RGB  (blit por pixel, cores)
      2. XADREZ P&B     (contraste e alinhamento)
      3. LIMPAR         (apaga a imagem)
      4. CARREGAR BMP   (lê o caminho do Edit via SDK)

Testado em:
    - LBF-OS Base_v4.6.5 / Kernel v2.9 / Runtime v6.3
====================================================================
*/

#include "sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/sysutils.h"
#include "../system/liblib.h"
#include "components/TOS_IPC.h"

// Protótipos de renderização gráfica RAD
void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);

// Funções externas de controle de hardware e foco
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;

// Protótipos da SDK para Memo
extern void GUI_Memo_AddStr(TGUIControl* memo, const char* str);
extern void GUI_Memo_Clear(TGUIControl* memo);

// Protótipos do componente TWebImage (IMPLEMENTAÇÃO vive na SDK TWebImage.c)
extern TGUIControl* GUI_CreateWebImage(TGUIEnvironment* app, int x, int y, int w, int h);
extern void GUI_WebImage_SetPixels(TGUIControl* img, const uint32_t* px, int w, int h, bool copy);
// Protótipos (troque os antigos):
extern void GUI_WebImage_GetDebug(int* bytes, int* bpp, int* comp, int* rc);
extern void GUI_WebImage_Clear(TGUIControl* img);

// Variáveis de controle do ambiente da aplicação
int my_app_slot = -1;
TGUIEnvironment MyApp;

// Dimensões da janela (v1.4: mais alta p/ caber o memo largo)
const int winWidth = 620;
const int winHeight = 520;

// Ponteiros de Controle RAD
TGUIControl* WebImg     = NULL;
TGUIControl* BtnGradient = NULL;
TGUIControl* BtnChecker  = NULL;
TGUIControl* BtnClear    = NULL;
TGUIControl* BtnLoadBmp  = NULL;
TGUIControl* EditPath    = NULL;   // v1.4: caminho do BMP
TGUIControl* LblLoaded   = NULL;   // v1.4: mostra o arquivo carregado
TGUIControl* LogMemo     = NULL;

/* ============================================================================
* ESTRUTURA AUXILIAR IPC (Mapeamento de Eventos Estendidos)
* ============================================================================ */
typedef struct {
    uint8_t dummy[sizeof(IPC_WINDOW_LIST[0])];
    volatile uint8_t fila_teclado_virtual;
    volatile uint8_t tem_evento_teclado;
} __attribute__((packed)) AppWindowInfoExtended;

/* ============================================================================
* FUNÇÕES AUXILIARES
* ============================================================================ */
char Obter_Tecla_Entrada(void) {
    AppWindowInfoExtended* ext_slot = (AppWindowInfoExtended*)&IPC_WINDOW_LIST[my_app_slot];
    if (ext_slot->tem_evento_teclado == 1) {
        char key = (char)ext_slot->fila_teclado_virtual;
        ext_slot->tem_evento_teclado = 0;
        return key;
    }
    return 0;
}

void Flush_Grafico_Janela(void) {
    gui_draw_form((TForm*)MyApp.MainWindow);
    gui_render_form((TForm*)MyApp.MainWindow);
    OS_IPC_FlipBuffers(my_app_slot, winWidth, winHeight);
}

void Tratar_Fechamento_Software(void) {
    if (MyApp.MainWindow) {
        gui_set_prop(MyApp.MainWindow, PROP_VISIBLE, 0);
    }
    uint32_t* b0 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_0;
    uint32_t* b1 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_1;
    if (b0) memset(b0, 0, winWidth * winHeight * 4);
    if (b1) memset(b1, 0, winWidth * winHeight * 4);
    IPC_WINDOW_LIST[my_app_slot].is_active = 0;
    sys_sleep(50);
}

// v1.4: atualiza o texto de um Label em runtime (igual gfile.c usa PROP_CAPTION)
static void Label_SetText(TGUIControl* lbl, const char* text) {
    if (!lbl || !lbl->KernelHandle || !text) return;
    gui_set_prop((void*)lbl->KernelHandle, PROP_CAPTION, (uintptr_t)text);
}

/* ============================================================================
* BUFFER DE PIXELS E GERADORES DE PADRÕES DE TESTE
* ============================================================================ */
#define IMG_W 256
#define IMG_H 192
static uint32_t g_pixels[IMG_W * IMG_H];

// Gradiente RGB: vermelho varia com X, verde com Y, azul fixo
static void gen_gradient(void) {
    for (int y = 0; y < IMG_H; y++) {
        for (int x = 0; x < IMG_W; x++) {
            uint8_t r = (uint8_t)((x * 255) / (IMG_W - 1));
            uint8_t g = (uint8_t)((y * 255) / (IMG_H - 1));
            uint8_t b = 128;
            g_pixels[y * IMG_W + x] = 0xFF000000u | ((uint32_t)r << 16) |
                                      ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
}

// Xadrez 16x16 (preto e branco)
static void gen_checkerboard(void) {
    for (int y = 0; y < IMG_H; y++) {
        for (int x = 0; x < IMG_W; x++) {
            bool white = ((x / 16) + (y / 16)) & 1;
            g_pixels[y * IMG_W + x] = white ? 0xFFFFFFFFu : 0xFF000000u;
        }
    }
}

/* ============================================================================
* CALLBACKS DE EVENTOS RAD
* ============================================================================ */
void OnBtnGradientClick(void* sender) {
    (void)sender;
    GUI_Memo_AddStr(LogMemo, "[TESTE] Gerando gradiente RGB 256x192...\n");
    Flush_Grafico_Janela();
    gen_gradient();
    GUI_WebImage_SetPixels(WebImg, g_pixels, IMG_W, IMG_H, true);
    GUI_Memo_AddStr(LogMemo, "[OK] Gradiente renderizado!\n\n");
    Flush_Grafico_Janela();
}

void OnBtnCheckerClick(void* sender) {
    (void)sender;
    GUI_Memo_AddStr(LogMemo, "[TESTE] Gerando xadrez P&B 16x16...\n");
    Flush_Grafico_Janela();
    gen_checkerboard();
    GUI_WebImage_SetPixels(WebImg, g_pixels, IMG_W, IMG_H, true);
    GUI_Memo_AddStr(LogMemo, "[OK] Xadrez renderizado!\n\n");
    Flush_Grafico_Janela();
}

void OnBtnClearClick(void* sender) {
    (void)sender;
    GUI_WebImage_Clear(WebImg);
    Label_SetText(LblLoaded, "Carregado: (nenhum)");
    GUI_Memo_AddStr(LogMemo, "[OK] Imagem limpa.\n\n");
    Flush_Grafico_Janela();
}

// v1.4: lê o caminho do Edit e atualiza o Label "Carregado:"
// Callback com diagnóstico completo:
void OnBtnLoadBmpClick(void* sender) {
    (void)sender;
    char* input = GUI_Edit_GetText(EditPath);
    const char* path = (input && input[0] != '\0') ? input : "0:/image.bmp";

    char caption[160];
    strcpy(caption, "Carregado: ");
    strcat(caption, path);
    Label_SetText(LblLoaded, caption);

    GUI_Memo_AddStr(LogMemo, "[TESTE] Carregando ");
    GUI_Memo_AddStr(LogMemo, path);
    GUI_Memo_AddStr(LogMemo, "...\n");
    Flush_Grafico_Janela();

    int st = GUI_WebImage_SetFromFile(WebImg, path);

    if (st == 0) {
        GUI_Memo_AddStr(LogMemo, "[OK] Imagem carregada e renderizada!\n\n");
    } else {
        int bytes = 0, bpp = 0, comp = 0, rc = 0;
        GUI_WebImage_GetDebug(&bytes, &bpp, &comp, &rc);
        char msg[128];
        strcpy(msg, "[ERRO] codigo ");
        IntToStr(st, msg + strlen(msg));
        strcat(msg, " | bytes lidos: ");
        IntToStr(bytes, msg + strlen(msg));
        strcat(msg, "\n        bpp: ");
        IntToStr(bpp, msg + strlen(msg));
        strcat(msg, " | compressao: ");
        IntToStr(comp, msg + strlen(msg));
        strcat(msg, "\n");
        GUI_Memo_AddStr(LogMemo, msg);

        if (st == -1)      GUI_Memo_AddStr(LogMemo, "  -> arquivo nao encontrado\n");
        else if (st == -3) GUI_Memo_AddStr(LogMemo, "  -> formato nao suportado\n");
        else if (st == -6) GUI_Memo_AddStr(LogMemo, "  -> leitura truncada (driver FAT)\n");
        GUI_Memo_AddStr(LogMemo, "\n");
    }
    Flush_Grafico_Janela();
}
/* ============================================================================
* FUNÇÃO PRINCIPAL (MAIN)
* ============================================================================ */
int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    static int ultimo_x = 0, ultimo_y = 0, mouse_hold_timer = 0;
    static bool primeiro_desenho = true, ultimo_estado_foco = false;
    void* ultimo_controle_focado = NULL;

    graphics_init_app(winWidth, winHeight);
    wm_init();

    my_app_slot = OS_IPC_RegisterApp("Teste TWebImage", winWidth, winHeight);
    if (my_app_slot == -1) return -1;

    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Teste TWebImage v1.4", winWidth, winHeight);

    // v1.4: FUNDO CINZA (0xC0C0C0) para os Labels pretos aparecerem (padrão gfile.c)
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0xC0C0C0);

    // =========================================================================
    // LAYOUT v1.4
    // =========================================================================
    // Coluna esquerda: botões 1..4
    BtnGradient = GUI_CreateButton(&MyApp, 10, 40,  180, 30, "1. GRADIENTE RGB", OnBtnGradientClick);
    BtnChecker  = GUI_CreateButton(&MyApp, 10, 78,  180, 30, "2. XADREZ P&B",    OnBtnCheckerClick);
    BtnClear    = GUI_CreateButton(&MyApp, 10, 116, 180, 30, "3. LIMPAR",        OnBtnClearClick);
    BtnLoadBmp  = GUI_CreateButton(&MyApp, 10, 154, 180, 30, "4. CARREGAR BMP",  OnBtnLoadBmpClick);

    // Label capitulando o Edit + Edit de caminho
    GUI_CreateLabel(&MyApp, 10, 198, "Arquivo BMP:");
    EditPath = GUI_CreateEdit(&MyApp, 10, 216, 180, 26, "", NULL);
    GUI_Edit_SetText(EditPath, "0:/image.bmp");

    // Label que mostra o arquivo que o Edit carregou
    LblLoaded = GUI_CreateLabel(&MyApp, 10, 252, "Carregado: (nenhum)");

    // TWebImage grande à direita
    GUI_CreateLabel(&MyApp, 200, 44, "TWebImage:");
    WebImg = GUI_CreateWebImage(&MyApp, 200, 62, 410, 320);

    // MEMO de log na base, largura total
    GUI_CreateLabel(&MyApp, 10, 390, "Log de Execucao:");
    LogMemo = GUI_CreateMemo(&MyApp, 10, 408, 600, 102);
    gui_set_prop(LogMemo, PROP_COLOR, 0x000000);

    // Foco inicial no Edit de caminho
    g_focused_control = (void*)EditPath;
    ultimo_controle_focado = (void*)EditPath;
    gui_set_prop(EditPath, PROP_SET_FOCUS, 1);

    GUI_Memo_AddStr(LogMemo, "Teste do componente TWebImage v1.4\n");
    GUI_Memo_AddStr(LogMemo, "=================================\n");
    GUI_Memo_AddStr(LogMemo, "1. GRADIENTE RGB: cores suaves.\n");
    GUI_Memo_AddStr(LogMemo, "2. XADREZ P&B: alinhamento.\n");
    GUI_Memo_AddStr(LogMemo, "3. LIMPAR apaga a imagem.\n");
    GUI_Memo_AddStr(LogMemo, "4. CARREGAR: le o caminho do Edit\n");
    GUI_Memo_AddStr(LogMemo, "   e mostra no Label 'Carregado:'.\n\n");
    Flush_Grafico_Janela();

    // =========================================================================
    // LOOP DE EVENTOS CONTINUO
    // =========================================================================
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

        if (g_focused_control != NULL) ultimo_controle_focado = g_focused_control;
        else if (ultimo_controle_focado != NULL) {
            g_focused_control = ultimo_controle_focado;
            gui_set_prop((TGUIControl*)ultimo_controle_focado, PROP_SET_FOCUS, 1);
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
                ultimo_x = rel_x; ultimo_y = rel_y;
                mouse_hold_timer = 2;

                if (BtnGradient && rel_x >= BtnGradient->Left && rel_x < (BtnGradient->Left + BtnGradient->Width) &&
                    rel_y >= BtnGradient->Top && rel_y < (BtnGradient->Top + BtnGradient->Height)) {
                    gui_set_prop(BtnGradient, PROP_STATE, 2);
                }
                else if (BtnChecker && rel_x >= BtnChecker->Left && rel_x < (BtnChecker->Left + BtnChecker->Width) &&
                         rel_y >= BtnChecker->Top && rel_y < (BtnChecker->Top + BtnChecker->Height)) {
                    gui_set_prop(BtnChecker, PROP_STATE, 2);
                }
                else if (BtnClear && rel_x >= BtnClear->Left && rel_x < (BtnClear->Left + BtnClear->Width) &&
                         rel_y >= BtnClear->Top && rel_y < (BtnClear->Top + BtnClear->Height)) {
                    gui_set_prop(BtnClear, PROP_STATE, 2);
                }
                else if (BtnLoadBmp && rel_x >= BtnLoadBmp->Left && rel_x < (BtnLoadBmp->Left + BtnLoadBmp->Width) &&
                         rel_y >= BtnLoadBmp->Top && rel_y < (BtnLoadBmp->Top + BtnLoadBmp->Height)) {
                    gui_set_prop(BtnLoadBmp, PROP_STATE, 2);
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
                if (BtnGradient) gui_set_prop(BtnGradient, PROP_STATE, 0);
                if (BtnChecker)  gui_set_prop(BtnChecker, PROP_STATE, 0);
                if (BtnClear)    gui_set_prop(BtnClear, PROP_STATE, 0);
                if (BtnLoadBmp)  gui_set_prop(BtnLoadBmp, PROP_STATE, 0);
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
