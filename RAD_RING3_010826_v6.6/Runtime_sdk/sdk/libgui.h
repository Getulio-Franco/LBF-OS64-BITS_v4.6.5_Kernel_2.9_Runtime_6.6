#ifndef LIBGUI_H
#define LIBGUI_H

/*
====================================================================
Header da SDK gráfica do Ring 3 (libgui)
Versão consolidada StringGrid v2
====================================================================
*/

#include <stdint.h>
#include <stdbool.h>
#include "../system/liblib.h"
#include "../system/malloc.h"
#include "../system/string.h"
#include "../gui/gui.h"   // Kernel: TYPE_*, props, TListViewItem, WebDoc, TStringGrid

#define MAX_APP_CONTROLS 64

/* ============================================================================
 * 1. TIPOS DE EVENTO (callbacks da SDK)
 * ============================================================================ */
typedef void (*TEventClick)(void* sender);
typedef void (*TEventHover)(void* sender);
typedef void (*TEventChange)(void* sender);
typedef void (*TEventScroll)(void* sender, int new_position);
typedef void (*TEventItemClick)(void* sender, int item_index);
typedef void (*TNotifyEvent)(void* sender);
typedef void (*TEventNavigate)(void* sender, const char* url);

/* ============================================================================
 * 2. STRUCT UNIFICADA DE CONTROLE (nó da SDK)
 * ============================================================================ */
typedef struct TGUIControl {
    // --- Núcleo ---
    int Type;
    int Left, Top, Width, Height;
    uint64_t KernelHandle;
    bool IsSelected;
    char Name[32];
    struct TGUIControl* Parent;
    void* Owner;

    // --- Extensões de texto / Edit / Memo ---
    char* Buffer;
    char Text[256];
    int CursorPos;
    int MaxLength;
    int AllocatedSize;
    int TextLength;
    int ScrollY;

    // --- ComboBox ---
    char Items[8][16];
    int ItemCount;
    int ItemIndex;
    bool DroppedDown;

    // --- CheckBox / RadioButton ---
    bool Checked;
    int GroupIndex;

    // --- Sub-formulários (TForm) / Painéis ---
    bool Modal;
    int BorderStyle;
    int BevelWidth;

    // --- ScrollBar ---
    int Min, Max, Position, PageSize;
    int Orientation;
    TEventScroll OnScroll;

    // --- ListView ---
    TListViewItem* LVItems;
    int LVItemCount;
    int LVAllocatedItems;
    int LVItemIndex;
    struct TGUIControl* VScrollBar;

    // --- StringGrid (v2 consolidado) ---
    int RowCount;
    int ColCount;
    int FixedRows;
    int FixedCols;
    int DefaultColWidth;
    int DefaultRowHeight;
    int ScrollX;                // v2: índice da 1ª coluna de dados visível (pulo de bloco)
    int GridAllocatedRows;      // v2: capacidade de memória alocada
    char*** GridCells;          // Matriz [rows][cols][string] em Ring 3
    int GridColWidths[16];      // v2: larguras individuais por coluna
    int SelectedRow;
    int SelectedCol;
    int SortColumn;             // v2: -1 = sem ordenação
    int SortDirection;          // v2: 1 = asc, -1 = desc
    bool GridResizing;          // v2: modo redimensionamento ativo
    int GridResizeCol;          // v2: coluna sendo redimensionada

    // --- Estado do Grid: proteção + edição ---
    bool ReadOnly;              // true = não edita células
    bool Enabled;               // false = componente cinza/inerte
    bool Editing;               // true = célula em modo de edição
    char EditBuffer[64];        // buffer vivo compartilhado c/ Kernel

    // --- WebImage (TWebImage v1.3) ---
    uint32_t* Pixels;
    int PixW, PixH;
    bool PixOwned;

    // --- WebPage (TWebPage v1.0) ---
    WebDoc* Doc;
    TEventNavigate OnNavigate;

    // --- Eventos nativos ---
    TEventClick     OnClick;
    TEventHover     OnEnter;
    TEventHover     OnLeave;
    TEventChange    OnChange;
    TEventItemClick OnItemClick;
} TGUIControl;

typedef struct {
    int SlotID;
    TForm* MainWindow;
    TGUIControl* Controls[MAX_APP_CONTROLS];
    int ControlCount;
    TGUIControl* ActiveFocus;
} TGUIEnvironment;

/* ============================================================================
 * 3. GERENCIAMENTO DO MOTOR CENTRAL
 * ============================================================================ */
void GUI_InitApplication(TGUIEnvironment* app, int slot_id, const char* title, int w, int h);
void GUI_RegisterControl(TGUIEnvironment* app, TGUIControl* ctrl, const char* prefix);
bool GUI_ProcessMouseClick(TGUIEnvironment* app, int mouse_x, int mouse_y);
void GUI_ProcessKeyboard(TGUIEnvironment* app, char key);

/* ============================================================================
 * 4. CRIAÇÃO DE COMPONENTES
 * ============================================================================ */
// --- Básicos ---
TGUIControl* GUI_CreateButton(TGUIEnvironment* app, int x, int y, int w, int h, const char* caption, TEventClick onClick);
TGUIControl* GUI_CreateLabel(TGUIEnvironment* app, int x, int y, const char* caption);
TGUIControl* GUI_CreateEdit(TGUIEnvironment* app, int x, int y, int w, int h, const char* initialText, TEventChange onChange);
TGUIControl* GUI_CreateMemo(TGUIEnvironment* app, int x, int y, int w, int h);
TGUIControl* GUI_CreateComboBox(TGUIEnvironment* app, int x, int y, int w, int h, TEventChange onChange);
TGUIControl* GUI_CreateCheckBox(TGUIEnvironment* app, int x, int y, const char* caption, TEventChange onChange);
TGUIControl* GUI_CreateRadioButton(TGUIEnvironment* app, int x, int y, const char* caption, TEventChange onChange);
TGUIControl* GUI_CreateImage(TGUIEnvironment* app, int x, int y, int w, int h, const char* path);
TGUIControl* GUI_CreateForm(const char* title, int x, int y, int width, int height);
TGUIControl* GUI_CreatePanel(TGUIEnvironment* app, int x, int y, int w, int h);

// --- ScrollBar ---
TGUIControl* GUI_CreateScrollBar(TGUIEnvironment* app, int x, int y, int w, int h, int orientation);
void         GUI_ScrollBar_SetPosition(TGUIControl* sb, int position);
void         GUI_ScrollBar_SetRange(TGUIControl* sb, int min, int max, int pagesize);

// --- ListView ---
TGUIControl* GUI_CreateListView(TGUIEnvironment* app, int x, int y, int w, int h, TNotifyEvent onChange);
void GUI_ListView_AddItem(TGUIControl* lv, const char* name, uint32_t size, uint8_t attributes);
void GUI_ListView_Clear(TGUIControl* lv);
void GUI_ListView_GetItem(TGUIControl* lv, int index, char* out_name, uint32_t* out_size, uint8_t* out_attr);

/* ============================================================================
 * 5. MÉTODOS AUXILIARES (GETTERS / SETTERS / UTILS)
 * ============================================================================ */
// --- Edit ---
char* GUI_Edit_GetText(TGUIControl* edit);
void  GUI_Edit_SetText(TGUIControl* edit, const char* text);
void  GUI_Edit_SetFocus(TGUIControl* edit);
void  GUI_Edit_AddChar(TGUIControl* edit, char key);

// --- Memo ---
void  GUI_Memo_AddChar(TGUIControl* memo, char key);
void  GUI_Memo_AddStr(TGUIControl* memo, const char* str);
void  GUI_Memo_SetFocus(TGUIControl* memo);
void  GUI_Memo_SetScroll(TGUIControl* memo, int value);
void  GUI_Memo_Clear(TGUIControl* memo);

// --- ComboBox ---
void  GUI_ComboBox_AddItem(TGUIControl* combo, const char* texto);
void  GUI_ComboBox_Rotate(TGUIControl* combo);
char* GUI_ComboBox_GetText(TGUIControl* combo);

// --- CheckBox / RadioButton ---
void GUI_CheckBox_Toggle(TGUIControl* cb);
void GUI_RadioButton_Select(TGUIEnvironment* app, TGUIControl* target_rb);

// --- Utilitários gerais ---
void GUI_DestroyForm(TGUIControl* form);
void GUI_SetParent(TGUIControl* control, TGUIControl* new_parent);

/* ============================================================================
 * 6. COMPONENTE TWEBIMAGE (v1.3 — visualizador de imagens BMP)
 * ============================================================================ */
TGUIControl* GUI_CreateWebImage(TGUIEnvironment* app, int x, int y, int w, int h);
void GUI_WebImage_SetPixels(TGUIControl* img, const uint32_t* px, int w, int h, bool copy);
int  GUI_WebImage_SetFromFile(TGUIControl* img, const char* bmp_path);
void GUI_WebImage_Clear(TGUIControl* img);
void GUI_WebImage_GetDebug(int* bytes, int* bpp, int* comp, int* rc);

/* ============================================================================
 * 7. COMPONENTE TWEBPAGE (v1.0 — canvas rolável c/ links clicáveis e imagens)
 * ============================================================================ */
TGUIControl* GUI_CreateWebPage(TGUIEnvironment* app, int x, int y, int w, int h, TEventNavigate onNav);
void GUI_WebPage_SetDoc(TGUIControl* pg, WebDoc* doc);
void GUI_WebPage_Clear(TGUIControl* pg);
void GUI_WebPage_SetScroll(TGUIControl* pg, int value);

/* ============================================================================
 * 8. CONSTRUTOR DE DOCUMENTOS WEBDOC (web_html.c)
 * ============================================================================ */
WebDoc* webdoc_create(void);
void    webdoc_destroy(WebDoc* doc);
void    webdoc_add_line(WebDoc* doc, const char* text, uint8_t style, int link_id, int img_slot);
int     webdoc_add_link(WebDoc* doc, const char* url);
int     webdoc_add_image(WebDoc* doc, const char* url);
void    webdoc_finalize(WebDoc* doc);

/* ============================================================================
 * 9. COMPONENTE TIMAGE (v1.1)
 * ============================================================================ */
TGUIControl* GUI_CreateImage(TGUIEnvironment* app, int x, int y, int w, int h, const char* path);
void GUI_Image_SetPixels(TGUIControl* img, const uint32_t* px, int w, int h, bool copy);
int  GUI_Image_SetFromFile(TGUIControl* img, const char* bmp_path);
void GUI_Image_Clear(TGUIControl* img);
void GUI_Image_GetDebug(int* bytes, int* bpp, int* comp, int* rc);
void GUI_Image_SetStretch(TGUIControl* img, bool stretch);

/* ============================================================================
 * 10. COMPONENTE TSTRINGGRID (v2 consolidado)
 * ============================================================================ */
TGUIControl* GUI_CreateStringGrid(TGUIEnvironment* app, int x, int y, int w, int h, int cols, int rows);
void GUI_Grid_SetCells(TGUIControl* grid, int col, int row, const char* text);
char* GUI_Grid_GetCells(TGUIControl* grid, int col, int row);
void GUI_Grid_Clear(TGUIControl* grid);
void GUI_DestroyStringGrid(TGUIControl* grid);
void GUI_Grid_SetScroll(TGUIControl* grid, int value);      // Scroll vertical
void GUI_Grid_SetScrollX(TGUIControl* grid, int value);     // Scroll horizontal (pulo de bloco)
void GUI_Grid_SetColWidth(TGUIControl* grid, int col, int width);
void GUI_Grid_SetRowHeight(TGUIControl* grid, int height);
void GUI_Grid_SetRowCount(TGUIControl* grid, int rows);     // v2: controle seguro
void GUI_Grid_SetReadOnly(TGUIControl* grid, bool read_only);
void GUI_Grid_SetEnabled(TGUIControl* grid, bool enabled);
void GUI_Grid_BeginEdit(TGUIControl* grid, int row, int col);
void GUI_Grid_EndEdit(TGUIControl* grid, bool commit);
void GUI_Grid_SelectCell(TGUIControl* grid, int row, int col);
void GUI_Grid_GetSelectedCell(TGUIControl* grid, int* row, int* col);

#endif // LIBGUI_H
