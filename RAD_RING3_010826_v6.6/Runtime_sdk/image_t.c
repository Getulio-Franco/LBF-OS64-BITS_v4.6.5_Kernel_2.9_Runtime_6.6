/*
====================================================================
Arquivo: image_t.c
Versão: 1.0
Data: 01/09/2026
Autor: LBF-OS Team + AI Assistant

Descrição:
    App de teste do componente TImage.

    Permite:
      - gerar gradiente
      - gerar xadrez
      - limpar imagem
      - digitar caminho no Edit e abrir BMP do FAT32

====================================================================
*/

#include "sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/sysutils.h"
#include "../system/liblib.h"
#include "components/TOS_IPC.h"

/* ============================================================================
* Protótipos externos do ambiente gráfico
* ============================================================================ */
void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);

extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;

/* ============================================================================
* Variáveis globais
* ============================================================================ */
int my_app_slot = -1;
TGUIEnvironment MyApp;

const int winWidth  = 620;
const int winHeight = 520;

TGUIControl* ImgBox      = NULL;
TGUIControl* BtnGradient = NULL;
TGUIControl* BtnChecker  = NULL;
TGUIControl* BtnClear    = NULL;
TGUIControl* BtnLoadBmp  = NULL;
TGUIControl* EditPath    = NULL;
TGUIControl* LblLoaded   = NULL;
TGUIControl* LogMemo     = NULL;

/* ============================================================================
* IPC teclado
* ============================================================================ */
typedef struct {
    uint8_t dummy[sizeof(IPC_WINDOW_LIST[0])];
    volatile uint8_t fila_teclado_virtual;
    volatile uint8_t tem_evento_teclado;
} __attribute__((packed)) AppWindowInfoExtended;

/* ============================================================================
* Helpers
* ============================================================================ */
char Obter_Tecla_Entrada(void) {
    if (my_app_slot < 0) return 0;

    AppWindowInfoExtended* ext_slot =
        (AppWindowInfoExtended*)&IPC_WINDOW_LIST[my_app_slot];

    if (ext_slot->tem_evento_teclado == 1) {
        char key = (char)ext_slot->fila_teclado_virtual;
        ext_slot->tem_evento_teclado = 0;
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

    if (MyApp.MainWindow) {
        gui_set_prop(MyApp.MainWindow, PROP_VISIBLE, 0);
    }

    uint32_t* b0 =
        (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_0;

    uint32_t* b1 =
        (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_1;

    if (b0) memset(b0, 0, winWidth * winHeight * 4);
    if (b1) memset(b1, 0, winWidth * winHeight * 4);

    IPC_WINDOW_LIST[my_app_slot].is_active = 0;
    sys_sleep(50);
}

static void Label_SetText(TGUIControl* lbl, const char* text) {
    if (!lbl || !lbl->KernelHandle || !text) return;
    gui_set_prop((void*)lbl->KernelHandle, PROP_CAPTION, (uintptr_t)text);
}

/* ============================================================================
* Buffers de teste
* ============================================================================ */
#define TEST_W 256
#define TEST_H 192

static uint32_t g_pixels[TEST_W * TEST_H];

static void gen_gradient(void) {
    for (int y = 0; y < TEST_H; y++) {
        for (int x = 0; x < TEST_W; x++) {
            uint8_t r = (uint8_t)((x * 255) / (TEST_W - 1));
            uint8_t g = (uint8_t)((y * 255) / (TEST_H - 1));
            uint8_t b = 128;

            g_pixels[y * TEST_W + x] =
                0xFF000000u |
                ((uint32_t)r << 16) |
                ((uint32_t)g << 8) |
                (uint32_t)b;
        }
    }
}

static void gen_checkerboard(void) {
    for (int y = 0; y < TEST_H; y++) {
        for (int x = 0; x < TEST_W; x++) {
            bool white = (((x / 16) + (y / 16)) & 1) != 0;
            g_pixels[y * TEST_W + x] =
                white ? 0xFFFFFFFFu : 0xFF000000u;
        }
    }
}

/* ============================================================================
* Callbacks
* ============================================================================ */
void OnBtnGradientClick(void* sender) {
    (void)sender;

    GUI_Memo_AddStr(LogMemo, "[TESTE] Gerando gradiente RGB...\n");

    gen_gradient();
    GUI_Image_SetPixels(ImgBox, g_pixels, TEST_W, TEST_H, true);

    GUI_Memo_AddStr(LogMemo, "[OK] Gradiente renderizado.\n\n");
    Flush_Grafico_Janela();
}

void OnBtnCheckerClick(void* sender) {
    (void)sender;

    GUI_Memo_AddStr(LogMemo, "[TESTE] Gerando xadrez P&B...\n");

    gen_checkerboard();
    GUI_Image_SetPixels(ImgBox, g_pixels, TEST_W, TEST_H, true);

    GUI_Memo_AddStr(LogMemo, "[OK] Xadrez renderizado.\n\n");
    Flush_Grafico_Janela();
}

void OnBtnClearClick(void* sender) {
    (void)sender;

    GUI_Image_Clear(ImgBox);
    Label_SetText(LblLoaded, "Carregado: (nenhum)");

    GUI_Memo_AddStr(LogMemo, "[OK] Imagem limpa.\n\n");
    Flush_Grafico_Janela();
}

void OnBtnLoadBmpClick(void* sender) {
    (void)sender;

    char* path = GUI_Edit_GetText(EditPath);

    if (!path || path[0] == '\0') {
        path = "0:/image.bmp";
        GUI_Edit_SetText(EditPath, path);
    }

    GUI_Memo_AddStr(LogMemo, "[TESTE] Abrindo BMP: ");
    GUI_Memo_AddStr(LogMemo, path);
    GUI_Memo_AddStr(LogMemo, "\n");

    int st = GUI_Image_SetFromFile(ImgBox, path);

    if (st == 0) {
        char cap[180];
        strcpy(cap, "Carregado: ");
        strcat(cap, path);
        Label_SetText(LblLoaded, cap);

        GUI_Memo_AddStr(LogMemo, "[OK] BMP carregado e renderizado.\n\n");
    } else {
        int bytes = 0;
        int bpp   = 0;
        int comp  = 0;
        int rc    = 0;

        GUI_Image_GetDebug(&bytes, &bpp, &comp, &rc);

        char msg[180];

        strcpy(msg, "[ERRO] codigo ");
        IntToStr(st, msg + strlen(msg));

        strcat(msg, " | bytes=");
        IntToStr(bytes, msg + strlen(msg));

        strcat(msg, " | bpp=");
        IntToStr(bpp, msg + strlen(msg));

        strcat(msg, " | comp=");
        IntToStr(comp, msg + strlen(msg));

        strcat(msg, " | rc=");
        IntToStr(rc, msg + strlen(msg));

        strcat(msg, "\n");

        GUI_Memo_AddStr(LogMemo, msg);

        if (st == -1) {
            GUI_Memo_AddStr(LogMemo, "  -> arquivo nao encontrado ou leitura falhou.\n\n");
        } else if (st == -2) {
            GUI_Memo_AddStr(LogMemo, "  -> arquivo nao e BMP.\n\n");
        } else if (st == -3) {
            GUI_Memo_AddStr(LogMemo, "  -> BMP com formato nao suportado.\n\n");
        } else if (st == -5) {
            GUI_Memo_AddStr(LogMemo, "  -> BMP maior que 512x512.\n\n");
        } else if (st == -6) {
            GUI_Memo_AddStr(LogMemo, "  -> arquivo BMP truncado.\n\n");
        } else {
            GUI_Memo_AddStr(LogMemo, "  -> erro desconhecido.\n\n");
        }
    }

    Flush_Grafico_Janela();
}

/* ============================================================================
* Main
* ============================================================================ */
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    static int ultimo_x = 0;
    static int ultimo_y = 0;
    static int mouse_hold_timer = 0;

    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;

    void* ultimo_controle_focado = NULL;

    graphics_init_app(winWidth, winHeight);
    wm_init();

    my_app_slot = OS_IPC_RegisterApp("Teste TImage", winWidth, winHeight);
    if (my_app_slot == -1) return -1;

    graphics_set_slot(my_app_slot);

    GUI_InitApplication(&MyApp,
                        my_app_slot,
                        "Teste TImage v1.0",
                        winWidth,
                        winHeight);

    if (MyApp.MainWindow) {
        gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0xC0C0C0);
    }

    /* ------------------------------------------------------------------------
    * Layout
    * ------------------------------------------------------------------------ */
    BtnGradient =
        GUI_CreateButton(&MyApp, 10, 40, 180, 30,
                         "1. GRADIENTE RGB",
                         OnBtnGradientClick);

    BtnChecker =
        GUI_CreateButton(&MyApp, 10, 78, 180, 30,
                         "2. XADREZ P&B",
                         OnBtnCheckerClick);

    BtnClear =
        GUI_CreateButton(&MyApp, 10, 116, 180, 30,
                         "3. LIMPAR",
                         OnBtnClearClick);

    BtnLoadBmp =
        GUI_CreateButton(&MyApp, 10, 154, 180, 30,
                         "4. ABRIR BMP",
                         OnBtnLoadBmpClick);

    GUI_CreateLabel(&MyApp, 10, 198, "Arquivo BMP:");

    EditPath =
        GUI_CreateEdit(&MyApp, 10, 216, 180, 26,
                       "0:/image.bmp",
                       NULL);

    LblLoaded =
        GUI_CreateLabel(&MyApp, 10, 252, "Carregado: (nenhum)");

    GUI_CreateLabel(&MyApp, 200, 44, "TImage:");

    ImgBox = GUI_CreateImage(&MyApp, 200, 62, 410, 320, NULL);

    GUI_CreateLabel(&MyApp, 10, 390, "Log de Execucao:");

    LogMemo =
        GUI_CreateMemo(&MyApp, 10, 408, 600, 102);

    gui_set_prop(LogMemo, PROP_COLOR, 0x000000);

    /* ------------------------------------------------------------------------
    * Foco inicial
    * ------------------------------------------------------------------------ */
    g_focused_control = (void*)EditPath;
    ultimo_controle_focado = (void*)EditPath;
    gui_set_prop(EditPath, PROP_SET_FOCUS, 1);

    GUI_Memo_AddStr(LogMemo, "Teste do componente TImage v1.0\n");
    GUI_Memo_AddStr(LogMemo, "================================\n");
    GUI_Memo_AddStr(LogMemo, "Digite o caminho no Edit.\n");
    GUI_Memo_AddStr(LogMemo, "Exemplo: 0:/image.bmp\n");
    GUI_Memo_AddStr(LogMemo, "Depois clique em 4. ABRIR BMP.\n\n");

    Flush_Grafico_Janela();

    /* ------------------------------------------------------------------------
    * Loop
    * ------------------------------------------------------------------------ */
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

        bool euTenhoFocoJanelaReal =
            (IPC_CONTROL->active_focus_slot == my_app_slot);

        if (euTenhoFocoJanelaReal != ultimo_estado_foco) {
            ultimo_estado_foco = euTenhoFocoJanelaReal;

            if (MyApp.MainWindow) {
                ((TForm*)MyApp.MainWindow)->ActiveFocus =
                    euTenhoFocoJanelaReal;
            }

            precisa_redesenhar = true;
        }

        if (g_focused_control != NULL) {
            ultimo_controle_focado = g_focused_control;
        } else if (ultimo_controle_focado != NULL) {
            g_focused_control = ultimo_controle_focado;
            gui_set_prop((TGUIControl*)ultimo_controle_focado,
                         PROP_SET_FOCUS,
                         1);
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

                if (BtnGradient &&
                    rel_x >= BtnGradient->Left &&
                    rel_x <  BtnGradient->Left + BtnGradient->Width &&
                    rel_y >= BtnGradient->Top &&
                    rel_y <  BtnGradient->Top + BtnGradient->Height) {
                    gui_set_prop(BtnGradient, PROP_STATE, 2);
                }
                else if (BtnChecker &&
                         rel_x >= BtnChecker->Left &&
                         rel_x <  BtnChecker->Left + BtnChecker->Width &&
                         rel_y >= BtnChecker->Top &&
                         rel_y <  BtnChecker->Top + BtnChecker->Height) {
                    gui_set_prop(BtnChecker, PROP_STATE, 2);
                }
                else if (BtnClear &&
                         rel_x >= BtnClear->Left &&
                         rel_x <  BtnClear->Left + BtnClear->Width &&
                         rel_y >= BtnClear->Top &&
                         rel_y <  BtnClear->Top + BtnClear->Height) {
                    gui_set_prop(BtnClear, PROP_STATE, 2);
                }
                else if (BtnLoadBmp &&
                         rel_x >= BtnLoadBmp->Left &&
                         rel_x <  BtnLoadBmp->Left + BtnLoadBmp->Width &&
                         rel_y >= BtnLoadBmp->Top &&
                         rel_y <  BtnLoadBmp->Top + BtnLoadBmp->Height) {
                    gui_set_prop(BtnLoadBmp, PROP_STATE, 2);
                }

                events_process_mouse(rel_x, rel_y, 1, 0);

                if (GUI_ProcessMouseClick(&MyApp, rel_x, rel_y)) {
                    precisa_redesenhar = true;

                    if (g_focused_control != NULL) {
                        ultimo_controle_focado = g_focused_control;
                    }
                }
            }

            IPC_WINDOW_LIST[my_app_slot].has_click_event = 0;
        }

        if (mouse_hold_timer > 0) {
            mouse_hold_timer--;

            if (mouse_hold_timer == 0) {
                if (BtnGradient) gui_set_prop(BtnGradient, PROP_STATE, 0);
                if (BtnChecker)  gui_set_prop(BtnChecker,  PROP_STATE, 0);
                if (BtnClear)    gui_set_prop(BtnClear,    PROP_STATE, 0);
                if (BtnLoadBmp)  gui_set_prop(BtnLoadBmp,  PROP_STATE, 0);

                events_process_mouse(ultimo_x, ultimo_y, 0, 0);
                precisa_redesenhar = true;
            }
        }

        if (precisa_redesenhar) {
            Flush_Grafico_Janela();
        }

        sys_sleep(euTenhoFocoJanelaReal ? 16 : 32);
    }

    sys_exit();
    return 0;
}
