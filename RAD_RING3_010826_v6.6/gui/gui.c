#include "gui.h"
#include "system/graphics.h" // Usa a liblib/syscalls para desenhar
#include "system/string.h"
#include "system/malloc.h"   // Usa o malloc do Ring3!

#define MAX_GUI_WINDOWS 32
TForm* window_stack[MAX_GUI_WINDOWS];
int window_count = 0;

static uint64_t gui_event_queue[32];
static int ev_head = 0;
static int ev_tail = 0;

TControl* g_focused_control = NULL;

/* --- AUXILIARES E BORDAS --- */
void gui_get_abs_pos(TControl* ctrl, int* x, int* y) {
    if (!ctrl) return;
    *x = ctrl->Left;
    *y = ctrl->Top;
    TWinControl* parent = ctrl->Parent;
    while (parent) {
        *x += parent->Control.Left;
        *y += parent->Control.Top;
        parent = parent->Control.Parent;
    }
}

/* =========================================================================
 * 1. FUNÇÕES AUXILIARES E DE BORDAS
 * ========================================================================= */

void draw_sunken_border(int x, int y, int w, int h) {
    sys_draw_rect(x, y, w, 1, 0x808080);         // Topo escuro
    sys_draw_rect(x, y, 1, h, 0x808080);         // Esquerda escura
    sys_draw_rect(x, y + h - 1, w, 1, 0xFFFFFF); // Base clara
    sys_draw_rect(x + w - 1, y, 1, h, 0xFFFFFF); // Direita clara
}

void gui_draw_raised_border(int x, int y, int w, int h) {
    sys_draw_rect(x, y, w, 1, 0xFFFFFF);         // Topo claro
    sys_draw_rect(x, y, 1, h, 0xFFFFFF);         // Esquerda clara
    sys_draw_rect(x, y + h - 1, w, 1, 0x404040); // Base escura
    sys_draw_rect(x + w - 1, y, 1, h, 0x404040); // Direita escura
}

void gui_add_to_parent(TWinControl* parent, TControl* child) {
    if (!parent || !child) return;
    TForm* form = (TForm*)parent;
    if (form->ControlCount < 100) {
        form->Controls[form->ControlCount] = child;
        form->ControlCount++;
        child->Parent = parent;
    }
}

/* =========================================================================
 * 2. MOTOR DE RENDERIZAÇÃO
 * ========================================================================= */

void gui_render_control(TControl* ctrl) {
    if (!ctrl || !ctrl->Visible) return;

    // 1. Desenha o componente (Botão, Edit, etc) via seu OnPaint ponteiro
    if (ctrl->OnPaint) {
        ctrl->OnPaint(ctrl);
    }

    // 2. Se estiver selecionado (Modo Design), desenha as alças por cima
    if (ctrl->IsSelected) {
        draw_selection_handles(ctrl->Left, ctrl->Top, ctrl->Width, ctrl->Height);
    }
}

/* =========================================================================
 * 3. IMPLEMENTAÇÃO DOS DESENHOS
 * ========================================================================= */

void draw_selection_handles(int x, int y, int w, int h) {
    int s = 4; // Tamanho do quadradinho (4x4 pixels)
    uint32_t color = 0x000000; // Preto

    // Cantos
    sys_draw_rect(x - s/2,     y - s/2,     s, s, color); // Topo-Esquerda
    sys_draw_rect(x + w - s/2, y - s/2,     s, s, color); // Topo-Direita
    sys_draw_rect(x - s/2,     y + h - s/2, s, s, color); // Base-Esquerda
    sys_draw_rect(x + w - s/2, y + h - s/2, s, s, color); // Base-Direita

    // Meios das bordas
    sys_draw_rect(x + w/2 - s/2, y - s/2,         s, s, color); // Meio-Topo
    sys_draw_rect(x + w/2 - s/2, y + h - s/2,     s, s, color); // Meio-Base
    sys_draw_rect(x - s/2,       y + h/2 - s/2,     s, s, color); // Meio-Esquerda
    sys_draw_rect(x + w - s/2,   y + h/2 - s/2,     s, s, color); // Meio-Direita
}

static void draw_bs_none(TForm* form) {
    TControl* ctrl = &form->Win.Control;
    
    sys_draw_rect(ctrl->Left, ctrl->Top, ctrl->Width, ctrl->Height, ctrl->Color);
    sys_draw_rect(ctrl->Left, ctrl->Top, ctrl->Width, 1, 0x000000); 
    sys_draw_rect(ctrl->Left, ctrl->Top + ctrl->Height - 1, ctrl->Width, 1, 0x000000); 
    
    for (int i = 0; i < form->ControlCount; i++) {
        if (form->Controls[i]) gui_render_control(form->Controls[i]);
    }
}

static void draw_bs_single(TForm* form) {
    TControl* ctrl = &form->Win.Control;
    int x = ctrl->Left, y = ctrl->Top, w = ctrl->Width, h = ctrl->Height;

    sys_draw_rect(x, y, w, h, ctrl->Color);
    gui_draw_raised_border(x, y, w, h);

    if (h > 25) {
        uint32_t title_color = form->ActiveFocus ? 0x000080 : 0x707070;
        uint32_t text_color = form->ActiveFocus ? 0xFFFFFF : 0xD0D0D0;

        sys_draw_rect(x + 4, y + 4, w - 8, 20, title_color); 
        sys_draw_string(x + 8, y + 6, ctrl->Caption, text_color, 1); 

        // Botão Fechar [X]
        sys_draw_rect(x + w - 22, y + 6, 16, 16, 0xC0C0C0);
        gui_draw_raised_border(x + w - 22, y + 6, 16, 16); 
        sys_draw_string(x + w - 18, y + 7, "x", 0x000000, 1);
    }

    for (int i = 0; i < form->ControlCount; i++) {
        if (form->Controls[i]) gui_render_control(form->Controls[i]);
    }
}

void gui_draw_form(TForm* form) {
    if (!form || !form->Win.Control.Visible) return;

    switch (form->BorderStyle) {
        case bsNone:
            draw_bs_none(form);
            break;
        case bsSingle:
        case bsSizeable: 
        case bsDialog:
        default:
            draw_bs_single(form);
            break;
    }
}

void gui_set_border_style(TForm* form, TBorderStyle style) {
    if (form) {
        form->BorderStyle = style;
    }
}


/* --- FÁBRICAS (CONSTRUTORES) --- */

TForm* gui_create_form(char* name, char* caption, int pid) {
    TForm* form = (TForm*)malloc(sizeof(TForm)); 
    if (!form) return NULL;
    memset(form, 0, sizeof(TForm));
    
    form->BorderStyle = bsSingle;
    strcpy(form->Win.Control.Name, name);
    strcpy(form->Win.Control.Caption, caption);
    
    form->Win.Control.Left = 50;
    form->Win.Control.Top = 50;
    form->Win.Control.Width = 400;
    form->Win.Control.Height = 300;
    form->Win.Control.Visible = true;
    form->Win.Control.Color = 0x00C0C0C0;
    form->Win.Control.OnPaint = (void*)gui_draw_form;
    form->Win.Draggable = true;

    form->OwnerPID = pid;
    form->IsDragging = false;
    form->ControlCount = 0;
    return form;
}

// CORREÇÃO LD: Ponte VCL para criação de Window/Form
TForm* gui_create_window(const char* name, const char* caption, int x, int y, int w, int h) {
    TForm* form = gui_create_form((char*)name, (char*)caption, 0);
    if (form) {
        form->Win.Control.Left = x;
        form->Win.Control.Top = y;
        form->Win.Control.Width = w;
        form->Win.Control.Height = h;
    }
    return form;
}

TMemo* gui_create_memo(TWinControl* parent, char* name) {
    TMemo* memo = (TMemo*)malloc(sizeof(TMemo)); 
    if (!memo) return NULL;
    memset(memo, 0, sizeof(TMemo));
    
    strcpy(memo->Win.Control.Name, name);
    memo->Win.Control.Width = 150;
    memo->Win.Control.Height = 100;
    memo->Win.Control.Visible = true;
    memo->Win.Control.Color = 0xFFFFFF;
    memo->Win.Control.OnPaint = (void*)gui_draw_memo;
    memo->Win.Control.Type = TYPE_MEMO; 
    
    // CORREÇÃO AQUI: Esvazia o array estático em vez de tentar setar NULL
    memo->Win.Control.Caption[0] = '\0'; 
    
    gui_add_to_parent(parent, (TControl*)memo);
    return memo;
}

/* =========================================================================
 * COMPONENTE: TWEBIMAGE (Visualizador de Imagens BMP)
 * ========================================================================= */
TWebImage* gui_create_webimage(TWinControl* parent, char* name) {
    TWebImage* img = (TWebImage*)malloc(sizeof(TWebImage));
    if (!img) return NULL;
    memset(img, 0, sizeof(TWebImage));
    
    strcpy(img->Win.Control.Name, name);
    img->Win.Control.Width = 100;
    img->Win.Control.Height = 100;
    img->Win.Control.Visible = true;
    img->Win.Control.Color = 0x000000; // Fundo preto
    img->Win.Control.OnPaint = (void*)gui_draw_webimage;
    img->Win.Control.Type = TYPE_WEBIMAGE;
    
    img->Pixels = NULL;
    img->PixW = 0;
    img->PixH = 0;
    
    gui_add_to_parent(parent, (TControl*)img);
    return img;
}

/* ============================================================================
 * COMPONENTE: TWebPage (Canvas rolável com links clicáveis e imagens)
 * ============================================================================ */
TWebPage* gui_create_webpage(TWinControl* parent, char* name) {
    TWebPage* pg = (TWebPage*)malloc(sizeof(TWebPage));
    if (!pg) return NULL;
    memset(pg, 0, sizeof(TWebPage));
    
    strcpy(pg->Win.Control.Name, name);
    pg->Win.Control.Width = 400;
    pg->Win.Control.Height = 300;
    pg->Win.Control.Visible = true;
    pg->Win.Control.Color = 0xFFFFFF; // Fundo branco
    pg->Win.Control.OnPaint = (void*)gui_draw_webpage;
    pg->Win.Control.Type = TYPE_WEBPAGE;
    
    pg->Doc = NULL;
    pg->ScrollY = 0;
    pg->VScrollBar = NULL;
    
    gui_add_to_parent(parent, (TControl*)pg);
    return pg;
}

TEdit* gui_create_edit(TWinControl* parent, char* name) {
    TEdit* edit = (TEdit*)malloc(sizeof(TEdit)); 
    if (!edit) return NULL;
    memset(edit, 0, sizeof(TEdit));

    strcpy(edit->Win.Control.Name, name);
    edit->Win.Control.Type = TYPE_EDIT;
    edit->Win.Control.Width = 120;
    edit->Win.Control.Height = 22;
    edit->Win.Control.Visible = true;
    edit->Win.Control.Color = 0xFFFFFF;
    edit->Win.Control.OnPaint = (void*)gui_draw_edit;

    edit->MaxLength = 255; 
    edit->CursorPos = 0;
    
    gui_add_to_parent(parent, (TControl*)edit);
    return edit;
}

TButton* gui_create_button(TWinControl* parent, char* name, char* caption) {
    TButton* btn = (TButton*)malloc(sizeof(TButton));
    memset(btn, 0, sizeof(TButton));
    strcpy(btn->Win.Control.Name, name);
    strcpy(btn->Win.Control.Caption, caption);
    btn->Win.Control.Width = 75;
    btn->Win.Control.Height = 25;
    btn->Win.Control.Visible = true;
    btn->Win.Control.Color = 0xC0C0C0;
    btn->Win.Control.OnPaint = (void*)gui_draw_button;
    
    gui_add_to_parent(parent, (TControl*)btn);
    return btn;
}

TCheckBox* gui_create_checkbox(TWinControl* parent, char* name, char* caption) {
    TCheckBox* cb = (TCheckBox*)malloc(sizeof(TCheckBox));
    memset(cb, 0, sizeof(TCheckBox));
    strcpy(cb->Win.Control.Name, name);
    strcpy(cb->Win.Control.Caption, caption);
    
    cb->Win.Control.Type = TYPE_CHECKBOX; 
    cb->Win.Control.Width = 100;
    cb->Win.Control.Height = 20;
    cb->Win.Control.Visible = true;
    cb->Win.Control.OnPaint = (void*)gui_draw_checkbox;
    
    gui_add_to_parent(parent, (TControl*)cb);
    return cb;
}

TRadioButton* gui_create_radiobutton(TWinControl* parent, char* name, char* caption) {
    TRadioButton* rb = (TRadioButton*)malloc(sizeof(TRadioButton));
    memset(rb, 0, sizeof(TRadioButton));
    strcpy(rb->Win.Control.Name, name);
    strcpy(rb->Win.Control.Caption, caption);
    
    rb->Win.Control.Type = TYPE_RADIOBUTTON; 
    rb->Win.Control.Visible = true;
    rb->Win.Control.OnPaint = (void*)gui_draw_radiobutton;
    
    gui_add_to_parent(parent, (TControl*)rb);
    return rb;
}

// CORREÇÃO LD: Ponte VCL para criação do Radio Button antigo
TRadioButton* gui_create_radio(TWinControl* parent, int x, int y, const char* text) {
    TRadioButton* rb = gui_create_radiobutton(parent, "Radio", (char*)text);
    if (rb) {
        rb->Win.Control.Left = x;
        rb->Win.Control.Top = y;
    }
    return rb;
}

TComboBox* gui_create_combobox(TWinControl* parent, char* name) {
    TComboBox* combo = (TComboBox*)malloc(sizeof(TComboBox));
    memset(combo, 0, sizeof(TComboBox));
    
    // Configura o tipo do controle para identificação na propriedade
    combo->Win.Control.Type = TYPE_COMBOBOX; 
    
    strcpy(combo->Win.Control.Name, name);
    combo->Win.Control.Width = 100;
    combo->Win.Control.Height = 22;
    combo->Win.Control.Visible = true;
    combo->Win.Control.Color = 0xFFFFFF;
    combo->Win.Control.OnPaint = (void*)gui_draw_combobox;
    
    // Inicializa a string do Caption como vazia por segurança
    combo->Win.Control.Caption[0] = '\0';
    
    gui_add_to_parent(parent, (TControl*)combo);
    return combo;
}

TLabel* gui_create_label(TWinControl* parent, char* name, char* caption) {
    TLabel* lbl = (TLabel*)malloc(sizeof(TLabel));
    memset(lbl, 0, sizeof(TLabel));
    strcpy(lbl->Graphic.Control.Name, name);
    strcpy(lbl->Graphic.Control.Caption, caption);
    lbl->Graphic.Control.Visible = true;
    lbl->Graphic.Control.OnPaint = (void*)gui_draw_label;
    
    gui_add_to_parent(parent, (TControl*)lbl);
    return lbl;
}

TPanel* gui_create_panel(TWinControl* parent, char* name) {
    TPanel* pnl = (TPanel*)malloc(sizeof(TPanel));
    memset(pnl, 0, sizeof(TPanel));
    strcpy(pnl->Win.Control.Name, name);
    pnl->Win.Control.Width = 150;
    pnl->Win.Control.Height = 100;
    pnl->Win.Control.Visible = true;
    pnl->Win.Control.Color = 0xC0C0C0;
    pnl->Win.Control.OnPaint = (void*)gui_draw_panel;
    
    gui_add_to_parent(parent, (TControl*)pnl);
    return pnl;
}

TImage* gui_create_image(TWinControl* parent, char* name) {
    TImage* img = (TImage*)malloc(sizeof(TImage));
    if (!img) return NULL;
    memset(img, 0, sizeof(TImage));
    
    strcpy(img->Graphic.Control.Name, name);
    img->Graphic.Control.Visible = true;
    img->Graphic.Control.Type = TYPE_IMAGE;
    img->Graphic.Control.OnPaint = (void*)gui_draw_image;
    img->Graphic.Control.Color = 0xFFFFFF;  // fundo branco padrão
    
    img->Pixels = NULL;
    img->PixW = 0;
    img->PixH = 0;
    img->Stretch = true;  // padrão: estica p/ caber
    img->PixOwned = false;
    
    gui_add_to_parent(parent, (TControl*)img);
    return img;
}

/* =========================================================================
 * COMPONENTE: TSTRINGGRID
 * ========================================================================= */

TStringGrid* gui_create_stringgrid(TWinControl* parent, char* name, int cols, int rows) {
    TStringGrid* grid = (TStringGrid*)malloc(sizeof(TStringGrid));
    if (!grid) return NULL;
    memset(grid, 0, sizeof(TStringGrid));

    strcpy(grid->Win.Control.Name, name);
    grid->Win.Control.Type = TYPE_STRINGGRID;
    grid->Win.Control.Width = 300;
    grid->Win.Control.Height = 180;
    grid->Win.Control.Visible = true;
    grid->Win.Control.Enabled = true;   // OBRIGATÓRIO (memset zera p/ false)
    grid->Win.Control.Color = 0xFFFFFF;
    grid->Win.Control.OnPaint = (void*)gui_draw_stringgrid;

    grid->ColCount = (cols > 0) ? cols : 4;
    grid->RowCount = (rows > 0) ? rows : 4;
    grid->FixedCols = 1;
    grid->FixedRows = 1;
    grid->DefaultColWidth = 60;
    grid->DefaultRowHeight = 20;
    grid->ScrollX = 0;
    grid->ScrollY = 0;
    grid->ReadOnly = true;
    grid->EditRow = -1;
    grid->EditCol = -1;
    grid->EditText = NULL;

    gui_add_to_parent(parent, (TControl*)grid);
    return grid;
}

// Desenha a seta de ordenação (▲ ou ▼) no cabeçalho
static void draw_sort_arrow(int cx, int cy, bool asc, uint32_t color) {
    for (int i = 0; i < 4; i++) {
        int wdt = asc ? (7 - 2 * i) : (1 + 2 * i);
        sys_draw_rect(cx - wdt / 2, cy + i, wdt, 1, color);
    }
}

void gui_draw_stringgrid(TStringGrid* grid) {
    if (!grid || !grid->Win.Control.Visible) return;

    int x, y;
    gui_get_abs_pos((TControl*)grid, &x, &y);
    int w = grid->Win.Control.Width;
    int h = grid->Win.Control.Height;

    bool enabled = grid->Win.Control.Enabled;
    uint32_t txt_color = enabled ? 0x000000 : 0x808080;

    sys_draw_rect(x, y, w, h, grid->Win.Control.Color);
    draw_sunken_border(x, y, w, h);

    int scrollbar_w  = 16;
    int hscrollbar_h = 16;
    int row_h = (grid->DefaultRowHeight > 0) ? grid->DefaultRowHeight : 20;
    int fixed_rows = grid->FixedRows;
    int fixed_cols = grid->FixedCols;

    // ---- Larguras ----
    int ncols = (grid->ColCount < 16) ? grid->ColCount : 16;
    int col_wdt[16], col_off[16];
    int total_w = 0;
    for (int c = 0; c < ncols; c++) {
        col_wdt[c] = (grid->ColWidths[c] > 0) ? grid->ColWidths[c] : grid->DefaultColWidth;
        col_off[c] = total_w;
        total_w += col_wdt[c];
    }
    int fixed_w = (fixed_cols <= ncols) ? col_off[fixed_cols] : total_w;
    int grid_usable_w = w - scrollbar_w - 4;

    // ---- SCROLL HORIZONTAL POR COLUNA (pulo de bloco) ----
    int data_cols    = ncols - fixed_cols;
    int max_scroll_x = data_cols - 1;                 // sempre resta 1 coluna de dados
    if (max_scroll_x < 0) max_scroll_x = 0;
    if (grid->ScrollX < 0) grid->ScrollX = 0;
    if (grid->ScrollX > max_scroll_x) grid->ScrollX = max_scroll_x;
    int start_col = fixed_cols + grid->ScrollX;       // 1ª coluna de dados visível

    bool show_hscroll = (total_w > grid_usable_w);

    int vtrack_h = h - 4 - (show_hscroll ? hscrollbar_h + 2 : 0);
    int visible_rows = vtrack_h / row_h;
    if (visible_rows <= 0) visible_rows = 1;

    int scrollbar_x = x + w - scrollbar_w - 2;
    int scrollbar_y = y + 2;
    int scrollbar_h = vtrack_h;

    // ---- Vertical ----
    int rows_total   = grid->RowCount - fixed_rows;
    int rows_visible = visible_rows  - fixed_rows;
    if (rows_total < 0) rows_total = 0;
    if (rows_visible < 1) rows_visible = 1;
    if (grid->ScrollY < 0) grid->ScrollY = 0;
    if (rows_total > rows_visible) {
        if (grid->ScrollY > rows_total - rows_visible) grid->ScrollY = rows_total - rows_visible;
    } else grid->ScrollY = 0;

    // ---- LAYOUT DAS COLUNAS VISÍVEIS (fixas + dados EMPACOTADOS) ----
    int vis_col[16], vis_x[16], vis_w[16], vis_n = 0;
    for (int c = 0; c < fixed_cols && c < ncols; c++) {          // fixas: posição natural
        if (col_off[c] >= grid_usable_w) break;
        int dw = col_wdt[c];
        if (col_off[c] + dw > grid_usable_w) dw = grid_usable_w - col_off[c];
        vis_col[vis_n] = c; vis_x[vis_n] = col_off[c]; vis_w[vis_n] = dw; vis_n++;
    }
    int fixed_drawn = vis_n;
    int accx = fixed_w;                                          // dados: coladas na fixa
    for (int c = start_col; c < ncols; c++) {
        if (accx >= grid_usable_w) break;
        int dw = col_wdt[c];
        if (accx + dw > grid_usable_w) dw = grid_usable_w - accx;
        vis_col[vis_n] = c; vis_x[vis_n] = accx; vis_w[vis_n] = dw; vis_n++;
        accx += col_wdt[c];
    }
    int data_drawn = vis_n - fixed_drawn;

    // ================== CÉLULAS ==================
    for (int r = 0; r < visible_rows; r++) {
        int actual_r;
        if (r < fixed_rows) actual_r = r;
        else {
            actual_r = fixed_rows + (r - fixed_rows) + grid->ScrollY;
            if (actual_r >= grid->RowCount) break;
        }
        int cell_y = y + 2 + (r * row_h);

        for (int i = 0; i < vis_n; i++) {
            int actual_c = vis_col[i];
            int draw_w   = vis_w[i];
            int cell_x   = x + 2 + vis_x[i];

            bool is_fixed = (actual_r < fixed_rows) || (actual_c < fixed_cols);
            bool is_selected = enabled &&
                               (actual_r == grid->SelectedRow) &&
                               (actual_c == grid->SelectedCol);

            // --- Célula em edição ---
            if (enabled && grid->EditText &&
                actual_r == grid->EditRow && actual_c == grid->EditCol) {
                sys_draw_rect(cell_x, cell_y, draw_w, row_h, 0xFFFFFF);
                sys_draw_rect(cell_x, cell_y, draw_w, 1, 0x808080);
                sys_draw_rect(cell_x, cell_y + row_h - 1, draw_w, 1, 0x808080);
                sys_draw_rect(cell_x, cell_y, 1, row_h, 0x808080);
                sys_draw_rect(cell_x + draw_w - 1, cell_y, 1, row_h, 0x808080);

                char tmp[64];
                int max_chars = (draw_w - 6) / 8;
                if (max_chars <= 0) max_chars = 1;
                if (max_chars > 63) max_chars = 63;
                strncpy(tmp, grid->EditText, max_chars);
                tmp[max_chars] = '\0';
                sys_draw_string(cell_x + 4, cell_y + 4, tmp, 0x000000, 1);

                int len = 0;
                while (grid->EditText[len] && len < max_chars) len++;
                sys_draw_rect(cell_x + 4 + (len * 8), cell_y + 4, 1, row_h - 8, 0x000000);
                continue;
            }

            // --- Fundo ---
            uint32_t bg;
            if (!enabled)         bg = 0xD4D0C8;
            else if (is_selected) bg = 0x000080;
            else if (is_fixed)    bg = 0xD4D0C8;
            else                  bg = grid->Win.Control.Color;
            sys_draw_rect(cell_x, cell_y, draw_w, row_h, bg);

            // --- Bordas ---
            if (is_fixed && enabled && !is_selected) gui_draw_raised_border(cell_x, cell_y, draw_w, row_h);
            else {
                sys_draw_rect(cell_x, cell_y, draw_w, 1, 0xC0C0C0);
                sys_draw_rect(cell_x, cell_y, 1, row_h, 0xC0C0C0);
            }

            // --- Texto ---
            uint32_t tcol = is_selected ? 0xFFFFFF : txt_color;
            if (grid->Cells && grid->Cells[actual_r] && grid->Cells[actual_r][actual_c]) {
                char* t = grid->Cells[actual_r][actual_c];
                if (t && t[0]) {
                    char tmp[64];
                    int max_chars = (draw_w - 6) / 8;
                    if (max_chars <= 0) max_chars = 1;
                    if (max_chars > 63) max_chars = 63;
                    strncpy(tmp, t, max_chars);
                    tmp[max_chars] = '\0';
                    sys_draw_string(cell_x + 4, cell_y + 4, tmp, tcol, 1);
                }
            }

            // --- Seta de ordenação ---
            if (is_fixed && actual_c == grid->SortColumn && grid->SortDirection != 0) {
                draw_sort_arrow(cell_x + draw_w - 8, cell_y + 5,
                                grid->SortDirection == 1, tcol);
            }
        }
    }

    // ================== SCROLLBAR VERTICAL ==================
    sys_draw_rect(scrollbar_x, scrollbar_y, scrollbar_w, scrollbar_h, 0xD4D0C8);
    sys_draw_rect(scrollbar_x, scrollbar_y, 1, scrollbar_h, 0x808080);
    int thumb_h = scrollbar_h, thumb_y = scrollbar_y;
    if (rows_total > rows_visible) {
        thumb_h = (rows_visible * scrollbar_h) / rows_total;
        if (thumb_h < 15) thumb_h = 15;
        int max_track = scrollbar_h - thumb_h;
        int max_val = rows_total - rows_visible;
        if (max_val > 0) thumb_y = scrollbar_y + ((grid->ScrollY * max_track) / max_val);
    }
    sys_draw_rect(scrollbar_x, thumb_y, scrollbar_w, thumb_h, 0xC0C0C0);
    sys_draw_rect(scrollbar_x, thumb_y, scrollbar_w, 1, 0xFFFFFFFF);
    sys_draw_rect(scrollbar_x, thumb_y, 1, thumb_h, 0xFFFFFFFF);
    sys_draw_rect(scrollbar_x, thumb_y + thumb_h - 1, scrollbar_w, 1, 0x404040);
    sys_draw_rect(scrollbar_x + scrollbar_w - 1, thumb_y, 1, thumb_h, 0x404040);

    // ================== SCROLLBAR HORIZONTAL (thumb por colunas) ==================
    if (show_hscroll) {
        int hx = x + 2;
        int hy = y + 2 + vtrack_h + 2;
        int hw = grid_usable_w;
        sys_draw_rect(hx, hy, hw, hscrollbar_h, 0xD4D0C8);
        sys_draw_rect(hx, hy, hw, 1, 0x808080);

        // ✅ CORREÇÃO: thumb proporcional à LARGURA rolável (pixels), não à contagem de colunas
        int scroll_total   = total_w - fixed_w;        // tudo que pode rolar
        int scroll_visible = grid_usable_w - fixed_w;  // o que cabe na tela
        if (scroll_total < 1) scroll_total = 1;

        int thumb_w = (scroll_visible * hw) / scroll_total;
        if (thumb_w < 15) thumb_w = 15;
        if (thumb_w > hw) thumb_w = hw;

        int max_track = hw - thumb_w;
        int thumb_x = hx + (max_scroll_x > 0 ? (grid->ScrollX * max_track) / max_scroll_x : 0);

        sys_draw_rect(thumb_x, hy, thumb_w, hscrollbar_h, 0xC0C0C0);
        sys_draw_rect(thumb_x, hy, thumb_w, 1, 0xFFFFFFFF);
        sys_draw_rect(thumb_x, hy, 1, hscrollbar_h, 0xFFFFFFFF);
        sys_draw_rect(thumb_x, hy + hscrollbar_h - 1, thumb_w, 1, 0x404040);
        sys_draw_rect(thumb_x + thumb_w - 1, hy, 1, hscrollbar_h, 0x404040);
    }
}

/* --- HANDLERS DE DESENHO (DRAW) --- */

void gui_draw_panel(TPanel* panel) {
    int x, y;
    gui_get_abs_pos((TControl*)panel, &x, &y);
    int w = panel->Win.Control.Width, h = panel->Win.Control.Height;

    sys_draw_rect(x, y, w, h, panel->Win.Control.Color);
    gui_draw_raised_border(x, y, w, h);
}

void gui_draw_label(TLabel* label) {
    int x, y;
    gui_get_abs_pos((TControl*)label, &x, &y);
    sys_draw_string(x, y, label->Graphic.Control.Caption, 0x000000, 1);
}

/*void gui_draw_image(TImage* img) {
    (void)img;
}*/

void gui_draw_image(TImage* img) {
    if (!img || !img->Graphic.Control.Visible) return;
    
    int x, y;
    gui_get_abs_pos((TControl*)img, &x, &y);
    int w = img->Graphic.Control.Width;
    int h = img->Graphic.Control.Height;
    
    // Fundo do componente
    sys_draw_rect(x, y, w, h, img->Graphic.Control.Color);
    draw_sunken_border(x, y, w, h);
    
    // Se não houver imagem, desenha placeholder
    if (!img->Pixels || img->PixW <= 0 || img->PixH <= 0) {
        sys_draw_string(x + 10, y + 10, "[Sem imagem]", 0x808080, 1);
        return;
    }
    
    int img_w = img->PixW;
    int img_h = img->PixH;
    
    int dst_w, dst_h;
    if (img->Stretch) {
        // v1.1: ESCALA para caber no box (letterbox com proporção)
        int box_w = w - 4;  // margem interna
        int box_h = h - 4;
        
        if ((long long)img_w * box_h > (long long)img_h * box_w) {
            // Imagem é mais larga que o box -> limita pela largura
            dst_w = box_w;
            dst_h = (int)(((long long)img_h * box_w) / img_w);
        } else {
            // Imagem é mais alta que o box -> limita pela altura
            dst_h = box_h;
            dst_w = (int)(((long long)img_w * box_h) / img_h);
        }
        
        // Centraliza na caixa
        x += (w - dst_w) / 2;
        y += (h - dst_h) / 2;
    } else {
        // v1.1: TAMANHO NATURAL (sem escala)
        dst_w = img_w;
        dst_h = img_h;
        x += 2;  // margem interna
        y += 2;
    }
    
    if (dst_w < 1) dst_w = 1;
    if (dst_h < 1) dst_h = 1;
    
    // v1.1: Blit com escala nearest-neighbor
    for (int dy = 0; dy < dst_h; dy++) {
        int sy = (int)(((long long)dy * img_h) / dst_h);
        if (sy >= img_h) sy = img_h - 1;
        const uint32_t* row = &img->Pixels[sy * img_w];
        for (int dx = 0; dx < dst_w; dx++) {
            int sx = (int)(((long long)dx * img_w) / dst_w);
            if (sx >= img_w) sx = img_w - 1;
            sys_draw_rect(x + dx, y + dy, 1, 1, row[sx] & 0x00FFFFFF);
        }
    }
}

void gui_draw_combobox(TComboBox* combo) {
    int x, y;
    gui_get_abs_pos((TControl*)combo, &x, &y);
    int w = combo->Win.Control.Width;

    // 1. Desenha o fundo branco e a borda clássica para dentro (sunken)
    sys_draw_rect(x, y, w, 22, combo->Win.Control.Color);
    draw_sunken_border(x, y, w, 22);
    
    // 2. Desenha o botão da setinha "V" na direita
    sys_draw_rect(x + w - 20, y + 2, 18, 18, 0xC0C0C0);
    sys_draw_string(x + w - 15, y + 5, "V", 0x000000, 1);

    // 3. NOVIDADE: Desenha o texto do item selecionado atualmente
    // Se houver algum texto no Caption, nós renderizamos ele dentro da área branca
    if (combo->Win.Control.Caption[0] != '\0') {
        // x + 5 para dar um pequeno espaçamento da borda esquerda
        // y + 5 para alinhar verticalmente com o botão
        sys_draw_string(x + 5, y + 5, combo->Win.Control.Caption, 0x000000, 1);
    }
}

void gui_draw_radiobutton(TRadioButton* rb) {
    int x, y;
    gui_get_abs_pos((TControl*)rb, &x, &y);

    int cx = x + 6; 
    int cy = y + 8; 

    sys_draw_rect(cx - 3, cy - 6, 6, 1, 0xFFFFFF);
    sys_draw_rect(cx - 5, cy - 5, 10, 2, 0xFFFFFF);
    sys_draw_rect(cx - 6, cy - 3, 12, 6, 0xFFFFFF);
    sys_draw_rect(cx - 5, cy + 3, 10, 2, 0xFFFFFF);
    sys_draw_rect(cx - 3, cy + 5, 6, 1, 0xFFFFFF);

    if (rb->Checked) {
        sys_draw_rect(cx - 2, cy - 3, 4, 1, 0x000000);
        sys_draw_rect(cx - 3, cy - 2, 6, 4, 0x000000);
        sys_draw_rect(cx - 2, cy + 2, 4, 1, 0x000000);
    }

    sys_draw_string(x + 20, y + 3, rb->Win.Control.Caption, 0x000000, 1);
}

void gui_draw_button(TButton* btn) {
    int x, y;
    gui_get_abs_pos((TControl*)btn, &x, &y);
    int w = btn->Win.Control.Width;
    int h = btn->Win.Control.Height;

    sys_draw_rect(x, y, w, h, btn->Win.Control.Color);
    if (btn->Win.Control.State == 2) draw_sunken_border(x, y, w, h);
    else gui_draw_raised_border(x, y, w, h);            
    
    sys_draw_string(x + (w/4), y + (h/4), btn->Win.Control.Caption, 0, 1);
}

void gui_draw_checkbox(TCheckBox* cb) {
    int x, y;
    gui_get_abs_pos((TControl*)cb, &x, &y);
    
    sys_draw_rect(x, y + 2, 14, 14, 0xFFFFFF);
    draw_sunken_border(x, y + 2, 14, 14);
    if (cb->Checked) sys_draw_string(x + 3, y + 3, "X", 0x000000, 1);
    
    sys_draw_string(x + 20, y + 3, cb->Win.Control.Caption, 0x000000, 1);
}

void gui_draw_memo(TMemo* memo) {
    if (!memo) return;

    int x, y;
    gui_get_abs_pos((TControl*)memo, &x, &y);
    int w = memo->Win.Control.Width;
    int h = memo->Win.Control.Height;

    // Fundo e borda
    sys_draw_rect(x, y, w, h, memo->Win.Control.Color);
    draw_sunken_border(x, y, w, h);

    int scrollbar_w = 16;
    int scrollbar_x = x + w - scrollbar_w - 2;
    int scrollbar_y = y + 2;
    int scrollbar_h = h - 4;

    int max_chars_per_line = (w - scrollbar_w - 15) / 8;
    if (max_chars_per_line <= 0) max_chars_per_line = 1;

    char* text = memo->TextPointer;
    int total_lines = 0;
    
    // --- 1. CONTAGEM EXATA DE LINHAS ---
    if (text != NULL && text[0] != '\0') {
        int buf_idx = 0;
        int i = 0;
        while (text[i] != '\0') {
            if (text[i] == '\n' || buf_idx >= max_chars_per_line) {
                total_lines++;
                buf_idx = 0;
                if (text[i] != '\n') buf_idx++;
            } else {
                buf_idx++;
            }
            i++;
        }
        if (buf_idx > 0) total_lines++;
    }
    
    // Se estiver vazio, a linha atual conta como 1
    if (total_lines == 0) total_lines = 1;

    int max_visible_lines = (h - 10) / 16;
    if (max_visible_lines <= 0) max_visible_lines = 1;

    // Trava de segurança do ScrollY
    if (memo->ScrollY < 0) memo->ScrollY = 0;
    if (memo->ScrollY > (total_lines - max_visible_lines) && total_lines > max_visible_lines) {
        memo->ScrollY = total_lines - max_visible_lines;
    } else if (total_lines <= max_visible_lines) {
        memo->ScrollY = 0;
    }

    // --- 2. RENDERIZAÇÃO DO TEXTO ---
    int current_line = 0;
    int cursor_x_offset = 0;
    int cursor_y_offset = 0;
    int buf_idx = 0;

    if (text != NULL && text[0] != '\0') {
        char line_buffer[256];
        int i = 0;

        while (text[i] != '\0') {
            if (text[i] == '\n' || buf_idx >= max_chars_per_line || buf_idx >= 255) {
                line_buffer[buf_idx] = '\0';
                
                // Desenha apenas se estiver na zona visível do scroll
                if (current_line >= memo->ScrollY && (current_line - memo->ScrollY) < max_visible_lines) {
                    int draw_y = y + 5 + ((current_line - memo->ScrollY) * 16);
                    sys_draw_string(x + 5, draw_y, line_buffer, 0x000000, 1);
                }
                
                current_line++;
                buf_idx = 0;

                if (text[i] != '\n') {
                    line_buffer[buf_idx++] = text[i];
                }
            } else {
                line_buffer[buf_idx++] = text[i];
            }
            i++;
        }

        // Desenha a última linha
        if (buf_idx > 0) {
            line_buffer[buf_idx] = '\0';
            if (current_line >= memo->ScrollY && (current_line - memo->ScrollY) < max_visible_lines) {
                int draw_y = y + 5 + ((current_line - memo->ScrollY) * 16);
                sys_draw_string(x + 5, draw_y, line_buffer, 0x000000, 1);
            }
        }
    }

    // =========================================================================
    // CORREÇÃO: O CÁLCULO DO CURSOR AGORA FICA AQUI FORA!
    // =========================================================================
    cursor_x_offset = buf_idx * 8;
    cursor_y_offset = (current_line - memo->ScrollY) * 16;

    // --- 3. BARRA DE ROLAGEM VISUAL ---
    sys_draw_rect(scrollbar_x, scrollbar_y, scrollbar_w, scrollbar_h, 0xD4D0C8); // Fundo (Parte Escura/Canal)
    sys_draw_rect(scrollbar_x, scrollbar_y, 1, scrollbar_h, 0x808080); // Borda

    int thumb_h = scrollbar_h;
    int thumb_y = scrollbar_y;
    
    if (total_lines > max_visible_lines) {
        thumb_h = (max_visible_lines * scrollbar_h) / total_lines;
        if (thumb_h < 15) thumb_h = 15;
        
        int max_scroll_track = scrollbar_h - thumb_h;
        int max_scroll_val = total_lines - max_visible_lines;
        if (max_scroll_val > 0) {
            thumb_y = scrollbar_y + ((memo->ScrollY * max_scroll_track) / max_scroll_val);
        }
    }

    // Desenha o Bloco (Parte Clara)
    sys_draw_rect(scrollbar_x, thumb_y, scrollbar_w, thumb_h, 0xC0C0C0);
    sys_draw_rect(scrollbar_x, thumb_y, scrollbar_w, 1, 0xFFFFFFFF);
    sys_draw_rect(scrollbar_x, thumb_y, 1, thumb_h, 0xFFFFFFFF);
    sys_draw_rect(scrollbar_x, thumb_y + thumb_h - 1, scrollbar_w, 1, 0x404040);
    sys_draw_rect(scrollbar_x + scrollbar_w - 1, thumb_y, 1, thumb_h, 0x404040);

    // --- 4. DESENHO DO CURSOR PISCANTE ---
    if (g_focused_control == (TControl*)memo) {
        // Agora o cursor acompanha perfeitamente a quebra de linha sem sumir
        if (cursor_x_offset + 10 < (w - scrollbar_w) && cursor_y_offset >= 0 && cursor_y_offset < (max_visible_lines * 16)) {
            sys_draw_rect(x + 5 + cursor_x_offset, y + 5 + cursor_y_offset, 2, 14, 0x000000);
        }
    }
}

void gui_draw_webimage(TWebImage* img) {
    if (!img) return;

    int x, y;
    gui_get_abs_pos((TControl*)img, &x, &y);
    int w = img->Win.Control.Width;
    int h = img->Win.Control.Height;

    sys_draw_rect(x, y, w, h, img->Win.Control.Color);
    draw_sunken_border(x, y, w, h);

    if (!img->Pixels || img->PixW <= 0 || img->PixH <= 0) {
        sys_draw_string(x + 5, y + 5, "[Sem Imagem]", 0x808080, 1);
        return;
    }

    // Área útil interna (2px de borda)
    int box_w = w - 4, box_h = h - 4;
    if (box_w < 1 || box_h < 1) return;

    // ---- ESCALA "FIT": preserva proporção (letterbox) ----
    int dst_w, dst_h;
    if ((long long)img->PixW * box_h > (long long)img->PixH * box_w) {
        dst_w = box_w;                                    // limitada pela largura
        dst_h = (int)(((long long)img->PixH * box_w) / img->PixW);
    } else {
        dst_h = box_h;                                    // limitada pela altura
        dst_w = (int)(((long long)img->PixW * box_h) / img->PixH);
    }
    if (dst_w < 1) dst_w = 1;
    if (dst_h < 1) dst_h = 1;

    // Centraliza no box
    int off_x = x + 2 + (box_w - dst_w) / 2;
    int off_y = y + 2 + (box_h - dst_h) / 2;

    // ---- BLIT nearest-neighbor (amostra o pixel fonte) ----
    for (int dy = 0; dy < dst_h; dy++) {
        int sy = (int)(((long long)dy * img->PixH) / dst_h);
        if (sy >= img->PixH) sy = img->PixH - 1;
        const uint32_t* row = &img->Pixels[sy * img->PixW];
        for (int dx = 0; dx < dst_w; dx++) {
            int sx = (int)(((long long)dx * img->PixW) / dst_w);
            if (sx >= img->PixW) sx = img->PixW - 1;
            sys_draw_rect(off_x + dx, off_y + dy, 1, 1, row[sx] & 0x00FFFFFF);
        }
    }
}

/* ============================================================================
 * RENDERIZAÇÃO DO TWebPage (v1.1 — com clip de segurança)
 * Desenha texto, links (azul/sublinhado), imagens inline e scrollbar.
 * 
 * CORREÇÃO v1.1: aplica clip de caracteres baseado na largura útil do box,
 * evitando que textos longos "vazem" para fora do componente.
 * ============================================================================ */
void gui_draw_webpage(TWebPage* pg) {
    if (!pg) return;

    int x, y;
    gui_get_abs_pos((TControl*)pg, &x, &y);
    int w = pg->Win.Control.Width;
    int h = pg->Win.Control.Height;

    // Fundo e borda
    sys_draw_rect(x, y, w, h, pg->Win.Control.Color);
    draw_sunken_border(x, y, w, h);

    // Se não houver documento, desenha placeholder
    if (!pg->Doc || pg->Doc->lc == 0) {
        sys_draw_string(x + 10, y + 10, "[Pagina vazia]", 0x808080, 1);
        return;
    }

    WebDoc* doc = pg->Doc;
    int scrollbar_w = 16;
    int scrollbar_x = x + w - scrollbar_w - 2;
    int scrollbar_y = y + 2;
    int scrollbar_h = h - 4;

    // v1.1: máximo de caracteres que cabem no box (fonte 8px por char)
    // Garante que nada vaze para fora da área útil.
    int max_chars = (w - scrollbar_w - 14) / 8;
    if (max_chars <= 0) max_chars = 1;
    if (max_chars > 127) max_chars = 127;

    // Área útil para imagens (sem scrollbar, com margem de 10px)
    int content_w = w - scrollbar_w - 10;
    int max_visible_lines = (h - 10) / 16;
    if (max_visible_lines <= 0) max_visible_lines = 1;

    // Trava de segurança do ScrollY
    int total_lines = doc->lc;
    if (pg->ScrollY < 0) pg->ScrollY = 0;
    if (pg->ScrollY > (total_lines - max_visible_lines) && total_lines > max_visible_lines) {
        pg->ScrollY = total_lines - max_visible_lines;
    } else if (total_lines <= max_visible_lines) {
        pg->ScrollY = 0;
    }

    // --- RENDERIZAÇÃO DAS LINHAS VISÍVEIS ---
    int draw_y = y + 5;
    for (int i = pg->ScrollY; i < doc->lc && (i - pg->ScrollY) < max_visible_lines; i++) {
        WebLine* line = &doc->lines[i];

        // v1.1: clip de segurança — copia só o que cabe no box
        char tmp[128];
        int len = 0;
        while (line->text[len] != '\0' && len < max_chars) {
            tmp[len] = line->text[len];
            len++;
        }
        tmp[len] = '\0';

        // Estilo: 0=texto, 1=título, 2=link, 3=imagem
        if (line->style == WEB_STYLE_TITLE) {
            // Título: negrito simulado (desenha 2x com offset)
            sys_draw_string(x + 10, draw_y, tmp, 0x000000, 1);
            sys_draw_string(x + 11, draw_y, tmp, 0x000000, 1);
        }
        else if (line->style == WEB_STYLE_LINK) {
            // Link: azul + sublinhado (só no trecho visível)
            sys_draw_string(x + 10, draw_y, tmp, 0x0000FF, 1);
            sys_draw_rect(x + 10, draw_y + 14, len * 8, 1, 0x0000FF);
        }
        else if (line->style == WEB_STYLE_IMG && line->img_slot >= 0 && line->img_slot < doc->ic) {
            // Imagem inline: blit com escala nearest-neighbor
            WebImgSlot* img = &doc->imgs[line->img_slot];
            if (img->px && img->w > 0 && img->h > 0 && img->loaded) {
                int img_w = img->w;
                int img_h = img->h;

                // v3.1 FIX: tamanho NATURAL — só REDUZ se não couber,
                // NUNCA amplia (comportamento de browser real)
                int dst_w = img_w;
                int dst_h = img_h;
                if (dst_w > content_w) {
                    dst_h = (int)(((long long)dst_h * content_w) / dst_w);
                    dst_w = content_w;
                }
                if (dst_h > 200) {
                    dst_w = (int)(((long long)dst_w * 200) / dst_h);
                    dst_h = 200;
                }
                if (dst_w < 1) dst_w = 1;
                if (dst_h < 1) dst_h = 1;

                // Blit nearest-neighbor (igual antes)
                for (int dy = 0; dy < dst_h; dy++) {
                    int sy = (int)(((long long)dy * img_h) / dst_h);
                    if (sy >= img_h) sy = img_h - 1;
                    const uint32_t* row = &img->px[sy * img_w];
                    for (int dx = 0; dx < dst_w; dx++) {
                        int sx = (int)(((long long)dx * img_w) / dst_w);
                        if (sx >= img_w) sx = img_w - 1;
                        sys_draw_rect(x + 10 + dx, draw_y + dy, 1, 1, row[sx] & 0x00FFFFFF);
                    }
                }

                draw_y += dst_h + 4;   // altura real + respiro
                continue;
            }
        }
        else {
            // Texto normal
            sys_draw_string(x + 10, draw_y, tmp, 0x000000, 1);
        }

        draw_y += 16; // Altura da linha
    }

    // --- BARRA DE ROLAGEM VISUAL ---
    sys_draw_rect(scrollbar_x, scrollbar_y, scrollbar_w, scrollbar_h, 0xD4D0C8);
    sys_draw_rect(scrollbar_x, scrollbar_y, 1, scrollbar_h, 0x808080);

    int thumb_h = scrollbar_h;
    int thumb_y = scrollbar_y;

    if (total_lines > max_visible_lines) {
        thumb_h = (max_visible_lines * scrollbar_h) / total_lines;
        if (thumb_h < 15) thumb_h = 15;

        int max_scroll_track = scrollbar_h - thumb_h;
        int max_scroll_val = total_lines - max_visible_lines;
        if (max_scroll_val > 0) {
            thumb_y = scrollbar_y + ((pg->ScrollY * max_scroll_track) / max_scroll_val);
        }
    }

    sys_draw_rect(scrollbar_x, thumb_y, scrollbar_w, thumb_h, 0xC0C0C0);
    sys_draw_rect(scrollbar_x, thumb_y, scrollbar_w, 1, 0xFFFFFFFF);
    sys_draw_rect(scrollbar_x, thumb_y, 1, thumb_h, 0xFFFFFFFF);
    sys_draw_rect(scrollbar_x, thumb_y + thumb_h - 1, scrollbar_w, 1, 0x404040);
    sys_draw_rect(scrollbar_x + scrollbar_w - 1, thumb_y, 1, thumb_h, 0x404040);
}

void gui_draw_edit(TEdit* edit) {
    if (!edit) return;

    int x, y;
    gui_get_abs_pos((TControl*)edit, &x, &y);
    int w = edit->Win.Control.Width;
    int h = edit->Win.Control.Height;

    // Fundo dinâmico: Amarelo se focado, senão cor base (branco)
    uint32_t cor_fundo = edit->Win.Control.Color;
    if (g_focused_control == (TControl*)edit) {
        cor_fundo = 0xFFFFFF; 
    }

    sys_draw_rect(x, y, w, h, cor_fundo);
    draw_sunken_border(x, y, w, h);

    char* texto_físico = edit->Win.Control.Caption;

    // Alinhamento vertical fixo em (y + 4) para nunca mais pular
    if (texto_físico && texto_físico[0] != '\0') {
        sys_draw_string(x + 5, y + 4, texto_físico, 0x000000, 1);
    }

    // Desenha o cursor no final do texto
    if (g_focused_control == (TControl*)edit) {
        int text_width = texto_físico ? (strlen(texto_físico) * 8) : 0; 
        if (text_width + 10 < w) {
            sys_draw_rect(x + 5 + text_width, y + 4, 2, h - 8, 0x000000);
        }
    }
}

TScrollBar* gui_create_scrollbar(TWinControl* parent, char* name, TScrollOrientation orientation) {
    TScrollBar* sb = (TScrollBar*)malloc(sizeof(TScrollBar));
    if (!sb) return NULL;
    memset(sb, 0, sizeof(TScrollBar));

    strcpy(sb->Win.Control.Name, name);
    sb->Win.Control.Type = TYPE_SCROLLBAR;
    sb->Win.Control.Visible = true;
    sb->Win.Control.Color = 0xC0C0C0; // Cinza clássico
    sb->Win.Control.OnPaint = (void*)gui_draw_scrollbar;
    sb->Orientation = orientation;
    
    gui_add_to_parent(parent, (TControl*)sb);
    return sb;
}

void gui_draw_scrollbar(TScrollBar* sb) {
    int x, y;
    gui_get_abs_pos((TControl*)sb, &x, &y);
    int w = sb->Win.Control.Width;
    int h = sb->Win.Control.Height;

    // Desenha o canal (fundo escuro da barra)
    sys_draw_rect(x, y, w, h, 0xD4D0C8);
    draw_sunken_border(x, y, w, h);

    // O tamanho total do conteúdo em registros é a soma de Max + PageSize
    int total_content = sb->Max + sb->PageSize;
    int thumb_h = h - 4;
    int thumb_y = y + 2;
    
    if (total_content > 0 && sb->Max > 0) {
        // Altura proporcional da área visível em relação ao total de registros
        thumb_h = (sb->PageSize * h) / total_content;
        if (thumb_h < 15) thumb_h = 15;        // Tamanho mínimo seguro para o thumb
        if (thumb_h > h - 4) thumb_h = h - 4;  // Nunca maior que o canal

        // Espaço útil de movimento dentro da barra
        int track_range = (h - 4) - thumb_h;
        if (track_range < 0) track_range = 0;

        // Posição Y exata baseada na posição atual do scroll
        thumb_y = y + 2 + ((sb->Position * track_range) / sb->Max);
    } else {
        // Se todos os registros cabem na tela (Max <= 0), o thumb ocupa o canal inteiro
        thumb_h = h - 4;
        thumb_y = y + 2;
    }

    sys_draw_rect(x + 1, thumb_y, w - 2, thumb_h, sb->Win.Control.Color);
    gui_draw_raised_border(x + 1, thumb_y, w - 2, thumb_h);
}

TListView* gui_create_listview(TWinControl* parent, char* name) {
    TListView* lv = (TListView*)malloc(sizeof(TListView));
    if (!lv) return NULL;
    memset(lv, 0, sizeof(TListView));

    strcpy(lv->Win.Control.Name, name);
    lv->Win.Control.Type = TYPE_LISTVIEW;
    lv->Win.Control.Visible = true;
    lv->Win.Control.Color = 0xFFFFFF; // Fundo Branco padrão
    lv->Win.Control.OnPaint = (void*)gui_draw_listview;
    lv->ItemIndex = -1; // Nenhum selecionado

    gui_add_to_parent(parent, (TControl*)lv);
    return lv;
}

void gui_draw_listview(TListView* lv) {
    int x, y;
    gui_get_abs_pos((TControl*)lv, &x, &y);
    int w = lv->Win.Control.Width;
    int h = lv->Win.Control.Height;

    // 1. Fundo e Borda para dentro
    sys_draw_rect(x, y, w, h, lv->Win.Control.Color);
    draw_sunken_border(x, y, w, h);

    int item_height = 18;
    int max_visible = (h - 4) / item_height;

    // 2. Renderiza as linhas visíveis de arquivos
    for (int i = 0; i < max_visible; i++) {
        int idx = lv->ScrollY + i;
        if (idx >= lv->ItemCount || !lv->Items) break;

        int row_y = y + 2 + (i * item_height);

        // Se for o item selecionado pelo usuário, desenha o fundo azul de seleção
        if (idx == lv->ItemIndex) {
            sys_draw_rect(x + 2, row_y, w - 4, item_height, 0x000080); // Azul Escuro
            // Desenha o texto em Branco
            sys_draw_string(x + 6, row_y + 3, lv->Items[idx].Name, 0xFFFFFF, 1);
        } else {
            // Desenha o texto em Preto normal
            sys_draw_string(x + 6, row_y + 3, lv->Items[idx].Name, 0x000000, 1);
        }
    }
}

void gui_push_event(uint64_t event_id) {
    gui_event_queue[ev_head] = event_id;
    ev_head = (ev_head + 1) % 32;
}

uint64_t gui_pop_event(void) {
    if (ev_head == ev_tail) return 0;
    uint64_t ev = gui_event_queue[ev_tail];
    ev_tail = (ev_tail + 1) % 32;
    return ev;
}

/* =========================================================================
 * FUNÇÃO: gui_set_prop
 * Define propriedades genéricas em qualquer controle do motor gráfico.
 * ========================================================================= */
void gui_set_prop(void* control_ptr, TGUIProperty prop, uint64_t value) {
    // Caso especial: limpar o foco global (chamada sem controle específico)
    if (prop == PROP_SET_FOCUS && control_ptr == NULL) {
        if (g_focused_control) {
            TWinControl* old_win = (TWinControl*)g_focused_control;
            old_win->Focused = false;
        }
        g_focused_control = NULL;
        return;
    }

    if (!control_ptr) return;
    TControl* ctrl = (TControl*)control_ptr;

    switch (prop) {
        /* ------------------------------------------------------------------
         * PROP_STATE: estado genérico + payloads empacotados por componente
         * ------------------------------------------------------------------ */
        case PROP_STATE: {
            ctrl->State = (int)value;
            if (ctrl->Type == TYPE_LISTVIEW) {
                TListView* lv = (TListView*)control_ptr;
                lv->ItemCount = (int)value;
            } 
            else if (ctrl->Type == TYPE_STRINGGRID) {
                TStringGrid* grid = (TStringGrid*)control_ptr;
                grid->RowCount = (int)value;  // Atualiza RowCount dinamicamente
            }
            else if (ctrl->Type == TYPE_SCROLLBAR) {
                TScrollBar* sb = (TScrollBar*)control_ptr;
                sb->Max      = (int)(value & 0xFFFFFFFF);
                sb->PageSize = (int)((value >> 32) & 0xFFFFFFFF);
            }
            else if (ctrl->Type == TYPE_WEBIMAGE) {
                TWebImage* img = (TWebImage*)control_ptr;
                img->PixW = (int)(value & 0xFFFFFFFF);
                img->PixH = (int)((value >> 32) & 0xFFFFFFFF);
            }
            else if (ctrl->Type == TYPE_IMAGE) {
                TImage* img = (TImage*)control_ptr;
                img->PixW = (int)(value & 0xFFFFFFFF);
                img->PixH = (int)((value >> 32) & 0xFFFFFFFF);
            }            
            break;
        }

        /* ------------------------------------------------------------------
         * PROPriedades geométricas e visuais (genéricas)
         * ------------------------------------------------------------------ */
        case PROP_LEFT:    ctrl->Left    = (int)value;      break;
        case PROP_TOP:     ctrl->Top     = (int)value;      break;
        case PROP_WIDTH:   ctrl->Width   = (int)value;      break;
        case PROP_HEIGHT:  ctrl->Height  = (int)value;      break;
        case PROP_VISIBLE: ctrl->Visible = (bool)value;     break;
        case PROP_COLOR:   ctrl->Color   = (uint32_t)value; break;

        /* ------------------------------------------------------------------
         * PROP_SET_FOCUS: gerencia foco global (um controle por vez)
         * ------------------------------------------------------------------ */
        case PROP_SET_FOCUS: {
            if (g_focused_control) {
                TWinControl* old_win = (TWinControl*)g_focused_control;
                old_win->Focused = false;
            }
            g_focused_control = ctrl;
            TWinControl* new_win = (TWinControl*)control_ptr;
            new_win->Focused = true;
            break;
        }

        /* ------------------------------------------------------------------
         * PROP_ITEM_INDEX: apenas para ListView
         * ------------------------------------------------------------------ */
        case PROP_ITEM_INDEX: {
            if (ctrl->Type == TYPE_LISTVIEW) {
                TListView* lv = (TListView*)control_ptr;
                lv->ItemIndex = (int)value;
            }
            break;
        }

        /* ------------------------------------------------------------------
         * PROP_CAPTION: ponteiro de dados específico por tipo
         * ------------------------------------------------------------------ */
        case PROP_CAPTION: {
            if (ctrl->Type == TYPE_WEBPAGE) {
                TWebPage* pg = (TWebPage*)control_ptr;
                pg->Doc = (WebDoc*)(uintptr_t)value;
                break;
            }

            char* src = (char*)value;
            if (src) {
                if (ctrl->Type == TYPE_LABEL) {
                    TLabel* lbl = (TLabel*)control_ptr;
                    strcpy(lbl->Graphic.Control.Caption, src);
                }
                else if (ctrl->Type == TYPE_MEMO) {
                    TMemo* memo = (TMemo*)control_ptr;
                    memo->TextPointer = src;
                }
                else if (ctrl->Type == TYPE_LISTVIEW) {
                    TListView* lv = (TListView*)control_ptr;
                    lv->Items = (TListViewItem*)(uintptr_t)value;
                }
                else if (ctrl->Type == TYPE_WEBIMAGE) {
                    TWebImage* img = (TWebImage*)control_ptr;
                    img->Pixels = (uint32_t*)(uintptr_t)value;
                }
                else if (ctrl->Type == TYPE_IMAGE) {
                    TImage* img = (TImage*)control_ptr;
                    img->Pixels = (uint32_t*)(uintptr_t)value;
                } 
                else if (ctrl->Type == TYPE_STRINGGRID) {
                    TStringGrid* grid = (TStringGrid*)control_ptr;
                    grid->Cells = (char***)(uintptr_t)value;
                }
                else {
                    strcpy(ctrl->Caption, src);
                }
            }
            break;
        }

        /* ------------------------------------------------------------------
         * PROP_SCROLL_Y: rolagem vertical
         * ------------------------------------------------------------------ */
        case PROP_SCROLL_Y: {
            if (ctrl->Type == TYPE_MEMO) {
                TMemo* memo = (TMemo*)control_ptr;
                memo->ScrollY = (int)value;
            }
            else if (ctrl->Type == TYPE_LISTVIEW) {
                TListView* lv = (TListView*)control_ptr;
                lv->ScrollY = (int)value;
            }
            else if (ctrl->Type == TYPE_SCROLLBAR) {
                TScrollBar* sb = (TScrollBar*)control_ptr;
                sb->Position = (int)value;
            }
            else if (ctrl->Type == TYPE_WEBPAGE) {
                TWebPage* pg = (TWebPage*)control_ptr;
                pg->ScrollY = (int)value;
            }
            else if (ctrl->Type == TYPE_STRINGGRID) {
                TStringGrid* grid = (TStringGrid*)control_ptr;
                grid->ScrollY = (int)value;
            }
            break;
        }
        
        /* ------------------------------------------------------------------
         * PROP_READONLY: StringGrid somente leitura
         * ------------------------------------------------------------------ */
        case PROP_READONLY: {
            if (ctrl->Type == TYPE_STRINGGRID) {
                ((TStringGrid*)control_ptr)->ReadOnly = (bool)value;
            }
            break;
        }
        
        /* ------------------------------------------------------------------
         * PROP_ENABLED: componente ativo/inativo (genérico)
         * ------------------------------------------------------------------ */
        case PROP_ENABLED: {
            ctrl->Enabled = (bool)value;
            break;
        }
        
        /* ------------------------------------------------------------------
         * PROP_EDIT_CELL: coordenadas da célula em edição (StringGrid)
         * value = (col << 32) | row;  -1 = encerrar edição
         * ------------------------------------------------------------------ */
        case PROP_EDIT_CELL: {
            if (ctrl->Type == TYPE_STRINGGRID) {
                TStringGrid* grid = (TStringGrid*)control_ptr;
                int r = (int)(value & 0xFFFFFFFF);
                int c = (int)((value >> 32) & 0xFFFFFFFF);
                if (r == -1 || value == 0xFFFFFFFFFFFFFFFFULL) {
                    grid->EditRow = -1;
                    grid->EditCol = -1;
                    grid->EditText = NULL;
                } else {
                    grid->EditRow = r;
                    grid->EditCol = c;
                }
            }
            break;
        }
        
        /* ------------------------------------------------------------------
         * PROP_EDIT_TEXT: ponteiro para o buffer de edição (StringGrid)
         * ------------------------------------------------------------------ */
        case PROP_EDIT_TEXT: {
            if (ctrl->Type == TYPE_STRINGGRID) {
                ((TStringGrid*)control_ptr)->EditText = (char*)(uintptr_t)value;
            }
            break;
        }
        
        case PROP_COL_WIDTH: {
            if (ctrl->Type == TYPE_STRINGGRID) {
                TStringGrid* grid = (TStringGrid*)control_ptr;
                int width = (int)(value & 0xFFFFFFFF);        // low  = largura
                int col   = (int)((value >> 32) & 0xFFFFFFFF); // high = coluna
                if (col >= 0 && col < 16) grid->ColWidths[col] = width;
            }
            break;
        }
        
        case PROP_ROW_HEIGHT: {
            if (ctrl->Type == TYPE_STRINGGRID) {
                ((TStringGrid*)control_ptr)->DefaultRowHeight = (int)value;
            }
            break;
        }
        
        case PROP_SELECTED_CELL: {
            if (ctrl->Type == TYPE_STRINGGRID) {
                TStringGrid* grid = (TStringGrid*)control_ptr;
                grid->SelectedRow = (int)(value & 0xFFFFFFFF);
                grid->SelectedCol = (int)((value >> 32) & 0xFFFFFFFF);
            }
            break;
        }
        case PROP_SCROLL_X: {
            if (ctrl->Type == TYPE_STRINGGRID) {
                ((TStringGrid*)control_ptr)->ScrollX = (int)value;
            }
            break;
        }
        case PROP_SORT: {
            if (ctrl->Type == TYPE_STRINGGRID) {
                TStringGrid* grid = (TStringGrid*)control_ptr;
                grid->SortDirection = (int)(value & 0xFFFFFFFF);
                grid->SortColumn    = (int)((value >> 32) & 0xFFFFFFFF);
            }
            break;
        }
        
        default: break;
    }
}

/* =========================================================================
 * FUNÇÃO: gui_get_prop (ADICIONE ESTA LOGO ABAIXO)
 * ========================================================================= */
int64_t gui_get_prop(void* control_ptr, TGUIProperty prop) {
    if (!control_ptr) return 0;
    TControl* ctrl = (TControl*)control_ptr;

    switch (prop) {
        case PROP_LEFT:        return ctrl->Left;
        case PROP_TOP:         return ctrl->Top;
        case PROP_WIDTH:       return ctrl->Width;
        case PROP_HEIGHT:      return ctrl->Height;
        case PROP_VISIBLE:     return ctrl->Visible;
        case PROP_COLOR:       return ctrl->Color;
        case PROP_STATE:       return ctrl->State;

        case PROP_ITEM_INDEX: {
            if (ctrl->Type == TYPE_LISTVIEW) {
                TListView* lv = (TListView*)control_ptr;
                return lv->ItemIndex;
            }
            return -1;
        }
        case PROP_SCROLL_Y: {
            if (ctrl->Type == TYPE_MEMO) {
                TMemo* memo = (TMemo*)control_ptr;
                return memo->ScrollY;
            }
            else if (ctrl->Type == TYPE_LISTVIEW) {
                TListView* lv = (TListView*)control_ptr;
                return lv->ScrollY;
            }
            else if (ctrl->Type == TYPE_SCROLLBAR) {
                TScrollBar* sb = (TScrollBar*)control_ptr;
                return sb->Position; // Retorna a posição correta
            }
            else if (ctrl->Type == TYPE_WEBPAGE) {
                TWebPage* pg = (TWebPage*)control_ptr;
                return pg->ScrollY;
            }            
            return 0;
        }
        case PROP_CAPTION: {
            if (ctrl->Type == TYPE_MEMO) {
                TMemo* memo = (TMemo*)control_ptr;
                return (uintptr_t)memo->TextPointer;
            } 
            else if (ctrl->Type == TYPE_LISTVIEW) {
                TListView* lv = (TListView*)control_ptr;
                return (uintptr_t)lv->Items;
            }
            else if (ctrl->Type == TYPE_WEBIMAGE) {
                TWebImage* img = (TWebImage*)control_ptr;
                return (uintptr_t)img->Pixels;
            }
            else if (ctrl->Type == TYPE_WEBPAGE) {
                TWebPage* pg = (TWebPage*)control_ptr;
                return (uintptr_t)pg->Doc;
            } else if (ctrl->Type == TYPE_STRINGGRID) {
                TStringGrid* grid = (TStringGrid*)control_ptr;
                return (uintptr_t)grid->Cells;
            }
            return (uintptr_t)ctrl->Caption;
        }
        case PROP_SET_FOCUS: {
            return (g_focused_control == control_ptr) ? 1 : 0;
        }
        default: return 0;
    }
}

void gui_set_prop_string(void* control_ptr, TGUIProperty prop, uint64_t str_ptr, int extra_val) {
    if (!control_ptr) return;
    TControl* ctrl = (TControl*)control_ptr;
    char* texto_recebido = (char*)(uintptr_t)str_ptr;
    
    // Para a roleta, o Ring 3 (VCL) só precisa atualizar o texto visível (PROP_CAPTION)
    if (prop == PROP_CAPTION && ctrl->Type == TYPE_COMBOBOX && texto_recebido != NULL) {
        TComboBox* combo = (TComboBox*)control_ptr;
        
        // Copia o texto enviado pela roleta da VCL para o Caption do controle
        strncpy(combo->Win.Control.Caption, texto_recebido, 15);
        combo->Win.Control.Caption[15] = '\0'; // Garante o terminador nulo
        return;
    }
    
    (void)extra_val;
}

void gui_render(uint64_t form_ptr) {
    TForm* form = (TForm*)form_ptr;
    if (form) {
        gui_draw_form(form);
    }
}
