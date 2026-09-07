/*
====================================================================
Arquivo: TWebPage.c
Versão: 1.0
Data: 31/08/2026
Autor: LBF-OS Team + AI Assistant

Descrição:
    Componente TWebPage da SDK Ring 3 (canvas rolável com texto,
    títulos, links clicáveis e imagens inline).
    O modelo de dados (WebDoc) é passado via GUI_WebPage_SetDoc.
    A renderização e o tratamento de scroll/clique em link são
    feitos pelo engine (gui.c / libgui.c).

Testado em:
    - LBF-OS Base_v4.6.5 / Kernel v2.9 / Runtime v6.3
====================================================================
*/
#include "libgui.h"

/* ============================================================================
* CRIAÇÃO DO COMPONENTE TWebPage
* ============================================================================ */
TGUIControl* GUI_CreateWebPage(TGUIEnvironment* app, int x, int y, int w, int h,
                               TEventNavigate onNav) {
    if (!app) return NULL;

    TGUIControl* pg = (TGUIControl*)malloc(sizeof(TGUIControl));
    if (!pg) return NULL;
    memset(pg, 0, sizeof(TGUIControl));

    pg->Type = TYPE_WEBPAGE;
    pg->Left = x;
    pg->Top = y;
    pg->Width = w;
    pg->Height = h;
    pg->IsSelected = false;
    pg->Doc = NULL;
    pg->ScrollY = 0;
    pg->OnNavigate = onNav;

    GUI_RegisterControl(app, pg, "WebPage");

    pg->KernelHandle = (uint64_t)gui_create_webpage((TWinControl*)app->MainWindow, pg->Name);
    if (pg->KernelHandle == 0) {
        free(pg);
        return NULL;
    }

    gui_set_prop((void*)pg->KernelHandle, PROP_LEFT,   (uint64_t)pg->Left);
    gui_set_prop((void*)pg->KernelHandle, PROP_TOP,    (uint64_t)pg->Top);
    gui_set_prop((void*)pg->KernelHandle, PROP_WIDTH,  (uint64_t)pg->Width);
    gui_set_prop((void*)pg->KernelHandle, PROP_HEIGHT, (uint64_t)pg->Height);
    gui_set_prop((void*)pg->KernelHandle, PROP_CAPTION, 0);   // sem documento
    gui_set_prop((void*)pg->KernelHandle, PROP_SCROLL_Y, 0);
    return pg;
}

/* ============================================================================
* DEFINE O DOCUMENTO A SER RENDERIZADO
* ============================================================================ */
void GUI_WebPage_SetDoc(TGUIControl* pg, WebDoc* doc) {
    if (!pg || !pg->KernelHandle) return;
    pg->Doc = doc;
    pg->ScrollY = 0;
    gui_set_prop((void*)pg->KernelHandle, PROP_CAPTION, (uintptr_t)doc);
    gui_set_prop((void*)pg->KernelHandle, PROP_SCROLL_Y, 0);
}

/* ============================================================================
* LIMPA O COMPONENTE
* ============================================================================ */
void GUI_WebPage_Clear(TGUIControl* pg) {
    if (!pg || !pg->KernelHandle) return;
    pg->Doc = NULL;
    pg->ScrollY = 0;
    gui_set_prop((void*)pg->KernelHandle, PROP_CAPTION, 0);
    gui_set_prop((void*)pg->KernelHandle, PROP_SCROLL_Y, 0);
}

/* ============================================================================
* ATUALIZA O SCROLL PROGRAMATICAMENTE
* ============================================================================ */
void GUI_WebPage_SetScroll(TGUIControl* pg, int value) {
    if (!pg || !pg->KernelHandle) return;
    if (value < 0) value = 0;
    pg->ScrollY = value;
    gui_set_prop((void*)pg->KernelHandle, PROP_SCROLL_Y, (uint64_t)value);
}
