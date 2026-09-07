/*
====================================================================
Arquivo: webpage.c (Teste TWebPage)
Versão: 1.0
Data: 31/08/2026
Autor: LBF-OS Team + AI Assistant

Descrição:
    App de teste do componente TWebPage (SDK Ring 3).
    3 páginas hardcoded para validar:
      - Pagina 1: titulo + texto + 2 links (um p/ pagina 2)
      - Pagina 2: texto + link de volta + slot de imagem
      - Pagina 3: varios links p/ testar scroll
    O clique em link dispara OnNavigate -> carrega outra pagina.

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

// Protótipos de renderização
void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);

extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;
extern void GUI_Memo_AddStr(TGUIControl* memo, const char* str);
extern void GUI_Memo_Clear(TGUIControl* memo);

// SDK TWebPage + web_html
extern TGUIControl* GUI_CreateWebPage(TGUIEnvironment* app, int x, int y, int w, int h,
                                      TEventNavigate onNav);
extern void GUI_WebPage_SetDoc(TGUIControl* pg, WebDoc* doc);
extern void GUI_WebPage_Clear(TGUIControl* pg);

extern WebDoc* webdoc_create(void);
extern void    webdoc_destroy(WebDoc* doc);
extern void    webdoc_add_line(WebDoc* doc, const char* text, uint8_t style,
                               int link_id, int img_slot);
extern int     webdoc_add_link(WebDoc* doc, const char* url);
extern int     webdoc_add_image(WebDoc* doc, const char* url);
extern void    webdoc_finalize(WebDoc* doc);

// Variáveis da aplicação
int my_app_slot = -1;
TGUIEnvironment MyApp;

const int winWidth  = 640;
const int winHeight = 540;

TGUIControl* PageBox    = NULL;   // TWebPage
TGUIControl* BtnPage1   = NULL;
TGUIControl* BtnPage2   = NULL;
TGUIControl* BtnPage3   = NULL;
TGUIControl* BtnClear   = NULL;
TGUIControl* LogMemo    = NULL;
TGUIControl* LblCurrent = NULL;

static WebDoc* g_current_doc = NULL;
static char g_current_name[64] = "(nenhuma)";

/* ============================================================================
* ESTRUTURA IPC
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
    AppWindowInfoExtended* ext = (AppWindowInfoExtended*)&IPC_WINDOW_LIST[my_app_slot];
    if (ext->tem_evento_teclado == 1) {
        char key = (char)ext->fila_teclado_virtual;
        ext->tem_evento_teclado = 0;
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
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_VISIBLE, 0);
    uint32_t* b0 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_0;
    uint32_t* b1 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_1;
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
* CONSTRUTORES DE PÁGINAS DE TESTE
* ============================================================================ */
// Pagina 1: titulo + texto + 2 links (um p/ pagina 2)
static WebDoc* build_page_1(void) {
    WebDoc* d = webdoc_create();
    if (!d) return NULL;

    webdoc_add_line(d, "Pagina de Teste 1", WEB_STYLE_TITLE, -1, -1);
    webdoc_add_line(d, "Esta e a primeira pagina do teste do TWebPage.", WEB_STYLE_TEXT, -1, -1);
    webdoc_add_line(d, "Abaixo temos dois links clicaveis:", WEB_STYLE_TEXT, -1, -1);

    int lnk2 = webdoc_add_link(d, "pagina://2");
    webdoc_add_line(d, "Ir para a PAGINA 2", WEB_STYLE_LINK, lnk2, -1);

    int lnk3 = webdoc_add_link(d, "pagina://3");
    webdoc_add_line(d, "Ir para a PAGINA 3 (scroll)", WEB_STYLE_LINK, lnk3, -1);

    webdoc_add_line(d, "", WEB_STYLE_TEXT, -1, -1);
    webdoc_add_line(d, "Clique em um link azul para navegar.", WEB_STYLE_TEXT, -1, -1);

    webdoc_finalize(d);
    return d;
}

// Pagina 2: texto + link de volta + slot de imagem (placeholder)
static WebDoc* build_page_2(void) {
    WebDoc* d = webdoc_create();
    if (!d) return NULL;

    webdoc_add_line(d, "Pagina de Teste 2", WEB_STYLE_TITLE, -1, -1);
    webdoc_add_line(d, "Voce navegou da pagina 1 para ca via OnNavigate!", WEB_STYLE_TEXT, -1, -1);

    int lnk1 = webdoc_add_link(d, "pagina://1");
    webdoc_add_line(d, "Voltar para PAGINA 1", WEB_STYLE_LINK, lnk1, -1);

    webdoc_add_line(d, "", WEB_STYLE_TEXT, -1, -1);
    webdoc_add_line(d, "Slot de imagem (sem BMP carregado, mostra placeholder):", WEB_STYLE_TEXT, -1, -1);

    int img = webdoc_add_image(d, "(placeholder)");
    webdoc_add_line(d, "", WEB_STYLE_IMG, -1, img);

    webdoc_finalize(d);
    return d;
}

// Pagina 3: muitas linhas p/ testar scroll
static WebDoc* build_page_3(void) {
    WebDoc* d = webdoc_create();
    if (!d) return NULL;

    webdoc_add_line(d, "Pagina de Teste 3 - Scroll", WEB_STYLE_TITLE, -1, -1);
    webdoc_add_line(d, "Esta pagina tem 30 linhas para testar a rolagem.", WEB_STYLE_TEXT, -1, -1);
    webdoc_add_line(d, "Clique na calha direita (18px) para subir/descer 3 linhas.", WEB_STYLE_TEXT, -1, -1);

    char line[80];
    for (int i = 1; i <= 30; i++) {
        strcpy(line, "Linha de numero ");
        char n[8]; IntToStr(i, n);
        strcat(line, n);
        strcat(line, " do documento longo.");
        webdoc_add_line(d, line, WEB_STYLE_TEXT, -1, -1);
    }

    int lnk1 = webdoc_add_link(d, "pagina://1");
    webdoc_add_line(d, "Voltar para PAGINA 1", WEB_STYLE_LINK, lnk1, -1);

    webdoc_finalize(d);
    return d;
}

/* ============================================================================
* CARREGA UMA PAGINA (libera a anterior e seta a nova)
* ============================================================================ */
static void load_page(const char* name, WebDoc* doc) {
    if (g_current_doc) {
        webdoc_destroy(g_current_doc);
        g_current_doc = NULL;
    }
    g_current_doc = doc;
    strncpy(g_current_name, name, 63);
    g_current_name[63] = '\0';

    GUI_WebPage_SetDoc(PageBox, doc);

    char caption[96];
    strcpy(caption, "Pagina atual: ");
    strcat(caption, g_current_name);
    Label_SetText(LblCurrent, caption);
}

/* ============================================================================
* CALLBACK OnNavigate (disparado pelo engine quando clica em link)
* ============================================================================ */
void OnPageNavigate(void* sender, const char* url) {
    (void)sender;
    if (!url) return;

    GUI_Memo_AddStr(LogMemo, "[NAV] Link clicado: ");
    GUI_Memo_AddStr(LogMemo, url);
    GUI_Memo_AddStr(LogMemo, "\n");
    Flush_Grafico_Janela();

    if (strcmp(url, "pagina://1") == 0) {
        load_page("Pagina 1", build_page_1());
    } else if (strcmp(url, "pagina://2") == 0) {
        load_page("Pagina 2", build_page_2());
    } else if (strcmp(url, "pagina://3") == 0) {
        load_page("Pagina 3 (scroll)", build_page_3());
    } else {
        GUI_Memo_AddStr(LogMemo, "  -> URL desconhecida, ignorada.\n");
    }
    Flush_Grafico_Janela();
}

/* ============================================================================
* BOTÕES DE SELEÇÃO DE PÁGINA
* ============================================================================ */
void OnBtnPage1Click(void* s) { (void)s; load_page("Pagina 1", build_page_1()); Flush_Grafico_Janela(); }
void OnBtnPage2Click(void* s) { (void)s; load_page("Pagina 2", build_page_2()); Flush_Grafico_Janela(); }
void OnBtnPage3Click(void* s) { (void)s; load_page("Pagina 3 (scroll)", build_page_3()); Flush_Grafico_Janela(); }

void OnBtnClearClick(void* s) {
    (void)s;
    GUI_WebPage_Clear(PageBox);
    if (g_current_doc) { webdoc_destroy(g_current_doc); g_current_doc = NULL; }
    Label_SetText(LblCurrent, "Pagina atual: (nenhuma)");
    GUI_Memo_AddStr(LogMemo, "[OK] TWebPage limpo.\n");
    Flush_Grafico_Janela();
}

/* ============================================================================
* MAIN
* ============================================================================ */
int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    static int ultimo_x = 0, ultimo_y = 0, mouse_hold_timer = 0;
    static bool primeiro_desenho = true, ultimo_estado_foco = false;
    void* ultimo_controle_focado = NULL;

    graphics_init_app(winWidth, winHeight);
    wm_init();

    my_app_slot = OS_IPC_RegisterApp("Teste TWebPage", winWidth, winHeight);
    if (my_app_slot == -1) return -1;

    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Teste TWebPage v1.0", winWidth, winHeight);
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0xC0C0C0);

    // =========================================================================
    // LAYOUT
    // =========================================================================
    // Coluna esquerda: botões de páginas + label + log
    BtnPage1 = GUI_CreateButton(&MyApp, 10, 40,  180, 30, "1. PAGINA 1",       OnBtnPage1Click);
    BtnPage2 = GUI_CreateButton(&MyApp, 10, 78,  180, 30, "2. PAGINA 2",       OnBtnPage2Click);
    BtnPage3 = GUI_CreateButton(&MyApp, 10, 116, 180, 30, "3. PAGINA 3 (scroll)", OnBtnPage3Click);
    BtnClear = GUI_CreateButton(&MyApp, 10, 154, 180, 30, "LIMPAR",            OnBtnClearClick);

    LblCurrent = GUI_CreateLabel(&MyApp, 10, 198, "Pagina atual: (nenhuma)");

    GUI_CreateLabel(&MyApp, 10, 228, "Log de Execucao:");
    LogMemo = GUI_CreateMemo(&MyApp, 10, 248, 180, 280);
    gui_set_prop(LogMemo, PROP_COLOR, 0x000000);

    // TWebPage à direita (430 x 490)
    GUI_CreateLabel(&MyApp, 200, 44, "TWebPage:");
    PageBox = GUI_CreateWebPage(&MyApp, 200, 62, 430, 460, OnPageNavigate);
    gui_set_prop(PageBox, PROP_COLOR, 0xFFFFFF);

    // Mensagem inicial
    GUI_Memo_AddStr(LogMemo, "Teste do componente TWebPage\n");
    GUI_Memo_AddStr(LogMemo, "================================\n");
    GUI_Memo_AddStr(LogMemo, "1. PAGINA 1: titulo + texto +\n");
    GUI_Memo_AddStr(LogMemo, "   2 links (navegacao).\n");
    GUI_Memo_AddStr(LogMemo, "2. PAGINA 2: link de volta +\n");
    GUI_Memo_AddStr(LogMemo, "   slot de imagem.\n");
    GUI_Memo_AddStr(LogMemo, "3. PAGINA 3: 30 linhas para\n");
    GUI_Memo_AddStr(LogMemo, "   testar o scroll (clique na\n");
    GUI_Memo_AddStr(LogMemo, "   calha direita p/ rolar).\n");
    GUI_Memo_AddStr(LogMemo, "4. Clique em qualquer link\n");
    GUI_Memo_AddStr(LogMemo, "   AZUL sublinhado -> OnNavigate\n\n");

    // Carrega a página 1 por padrão
    load_page("Pagina 1", build_page_1());

    Flush_Grafico_Janela();

    // =========================================================================
    // LOOP DE EVENTOS
    // =========================================================================
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

                if (BtnPage1 && rel_x >= BtnPage1->Left && rel_x < (BtnPage1->Left + BtnPage1->Width) &&
                    rel_y >= BtnPage1->Top && rel_y < (BtnPage1->Top + BtnPage1->Height)) {
                    gui_set_prop(BtnPage1, PROP_STATE, 2);
                }
                else if (BtnPage2 && rel_x >= BtnPage2->Left && rel_x < (BtnPage2->Left + BtnPage2->Width) &&
                         rel_y >= BtnPage2->Top && rel_y < (BtnPage2->Top + BtnPage2->Height)) {
                    gui_set_prop(BtnPage2, PROP_STATE, 2);
                }
                else if (BtnPage3 && rel_x >= BtnPage3->Left && rel_x < (BtnPage3->Left + BtnPage3->Width) &&
                         rel_y >= BtnPage3->Top && rel_y < (BtnPage3->Top + BtnPage3->Height)) {
                    gui_set_prop(BtnPage3, PROP_STATE, 2);
                }
                else if (BtnClear && rel_x >= BtnClear->Left && rel_x < (BtnClear->Left + BtnClear->Width) &&
                         rel_y >= BtnClear->Top && rel_y < (BtnClear->Top + BtnClear->Height)) {
                    gui_set_prop(BtnClear, PROP_STATE, 2);
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
                if (BtnPage1) gui_set_prop(BtnPage1, PROP_STATE, 0);
                if (BtnPage2) gui_set_prop(BtnPage2, PROP_STATE, 0);
                if (BtnPage3) gui_set_prop(BtnPage3, PROP_STATE, 0);
                if (BtnClear) gui_set_prop(BtnClear, PROP_STATE, 0);
                events_process_mouse(ultimo_x, ultimo_y, 0, 0);
                precisa_redesenhar = true;
            }
        }

        if (precisa_redesenhar) Flush_Grafico_Janela();
        sys_sleep(euTenhoFocoJanelaReal ? 16 : 32);
    }

    if (g_current_doc) webdoc_destroy(g_current_doc);
    sys_exit();
    return 0;
}
