#include "libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"

/* ============================================================================
 * DECLARAÇÕES DE PROTÓTIPOS EXTERNOS
 * ============================================================================ */
extern void GUI_ComboBox_Rotate(TGUIControl* combo);
extern void GUI_CheckBox_Toggle(TGUIControl* cb);
extern void GUI_RadioButton_Select(TGUIEnvironment* app, TGUIControl* target_rb);
extern void GUI_Edit_SetFocus(TGUIControl* edit);
extern void GUI_Memo_SetFocus(TGUIControl* memo);
extern void GUI_Memo_AddChar(TGUIControl* memo, char key);
extern void GUI_Memo_SetScroll(TGUIControl* memo, int value);
extern void GUI_ScrollBar_SetPosition(TGUIControl* sb, int position);

// --- StringGrid (implementados em TStringGrid.c) ---
extern void GUI_Grid_SetScroll(TGUIControl* grid, int value);
extern void GUI_Grid_SetScrollX(TGUIControl* grid, int value);
extern void GUI_Grid_BeginEdit(TGUIControl* grid, int row, int col);
extern void GUI_Grid_EndEdit(TGUIControl* grid, bool commit);
extern void GUI_Grid_SelectCell(TGUIControl* grid, int row, int col);
extern void GUI_Grid_SetColWidth(TGUIControl* grid, int col, int width);

// Variável global de controle de foco do subsistema do Kernel
extern void* g_focused_control;
extern int64_t sys_gui_get_prop(void* handle, int prop);

/* ============================================================================
 * HELPERS DE ORDENAÇÃO DO STRINGGRID (usados pelo clique no cabeçalho)
 * ============================================================================ */
static TGUIControl* g_sort_grid = NULL;
static int g_sort_col = 0, g_sort_dir = 1;

static bool grid_is_number(const char* s) {
    if (!s || !*s) return false;
    for (int i = 0; s[i]; i++) if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

static int grid_row_cmp(int r1, int r2) {
    char* a = g_sort_grid->GridCells[r1][g_sort_col];
    char* b = g_sort_grid->GridCells[r2][g_sort_col];
    int res;
    if (grid_is_number(a) && grid_is_number(b)) {
        long n1 = 0, n2 = 0;
        for (int i = 0; a[i]; i++) n1 = n1 * 10 + (a[i] - '0');
        for (int i = 0; b[i]; i++) n2 = n2 * 10 + (b[i] - '0');
        res = (n1 > n2) - (n1 < n2);
    } else {
        res = strcmp(a ? a : "", b ? b : "");
    }
    return res * g_sort_dir;
}

static void grid_swap_rows(TGUIControl* g, int r1, int r2) {
    for (int c = 0; c < g->ColCount; c++) {
        char* t = g->GridCells[r1][c];
        g->GridCells[r1][c] = g->GridCells[r2][c];
        g->GridCells[r2][c] = t;
    }
}

static void grid_sort_rows(TGUIControl* g, int col, int dir) {
    g_sort_grid = g; g_sort_col = col; g_sort_dir = dir;
    int start = g->FixedRows, end = g->RowCount - 1;
    for (int a = start; a < end; a++)
        for (int b = a + 1; b <= end; b++)
            if (grid_row_cmp(a, b) > 0) grid_swap_rows(g, a, b);
}

/* ============================================================================
 * GERENCIAMENTO E REGISTRO
 * ============================================================================ */
void GUI_InitApplication(TGUIEnvironment* app, int slot_id, const char* title, int w, int h) {
    if (!app) return;
    app->SlotID = slot_id;
    app->ControlCount = 0;
    app->ActiveFocus = NULL;
    app->MainWindow = gui_create_form("MainApp", (char*)title, 1);
    if (app->MainWindow) {
        gui_set_prop(app->MainWindow, PROP_LEFT, 0);
        gui_set_prop(app->MainWindow, PROP_TOP, 0);
        gui_set_prop(app->MainWindow, PROP_WIDTH, w);
        gui_set_prop(app->MainWindow, PROP_HEIGHT, h);
        app->MainWindow->Win.Draggable = true;
        wm_add_window(app->MainWindow);
    }
}

void GUI_RegisterControl(TGUIEnvironment* app, TGUIControl* ctrl, const char* prefix) {
    if (!app || !ctrl || app->ControlCount >= MAX_APP_CONTROLS) return;
    int id = app->ControlCount + 1;
    char id_str[8];
    int idx = 0;
    if (id >= 10) id_str[idx++] = '0' + (id / 10);
    id_str[idx++] = '0' + (id % 10);
    id_str[idx] = '\0';
    strcpy(ctrl->Name, prefix);
    strcat(ctrl->Name, id_str);
    app->Controls[app->ControlCount++] = ctrl;
}

static void GetAbsolutePosition(TGUIControl* ctrl, int* abs_x, int* abs_y) {
    int x = 0, y = 0;
    TGUIControl* current = ctrl;
    while (current != NULL) {
        x += current->Left;
        y += current->Top;
        current = current->Parent;
    }
    *abs_x = x;
    *abs_y = y;
}

/* ============================================================================
 * MOTOR CENTRAL DE EVENTOS (MOUSE)
 * ============================================================================ */
bool GUI_ProcessMouseClick(TGUIEnvironment* app, int mouse_x, int mouse_y) {
    if (!app) return false;

    for (int i = app->ControlCount - 1; i >= 0; i--) {
        TGUIControl* ctrl = app->Controls[i];
        if (!ctrl) continue;
        int abs_x, abs_y;
        GetAbsolutePosition(ctrl, &abs_x, &abs_y);
        if (mouse_x < abs_x || mouse_x > (abs_x + ctrl->Width) ||
            mouse_y < abs_y || mouse_y > (abs_y + ctrl->Height)) continue;

        // Se clicou em algo que NÃO é StringGrid, confirma edição pendente
        if (ctrl->Type != TYPE_STRINGGRID && app->ActiveFocus &&
            app->ActiveFocus->Type == TYPE_STRINGGRID && app->ActiveFocus->Editing) {
            GUI_Grid_EndEdit(app->ActiveFocus, true);
        }

        if (ctrl->Type == TYPE_BUTTON) {
            if (ctrl->OnClick != NULL) ctrl->OnClick(ctrl);
            return true;
        }
        if (ctrl->Type == TYPE_COMBOBOX) { GUI_ComboBox_Rotate(ctrl); return true; }
        if (ctrl->Type == TYPE_CHECKBOX) { GUI_CheckBox_Toggle(ctrl); return true; }
        if (ctrl->Type == TYPE_RADIOBUTTON) { GUI_RadioButton_Select(app, ctrl); return true; }

        // --- TEdit ---
        if (ctrl->Type == TYPE_EDIT) {
            app->ActiveFocus = ctrl;
            g_focused_control = (void*)ctrl->KernelHandle;
            GUI_Edit_SetFocus(ctrl);
            return true;
        }

        // --- TMemo ---
        if (ctrl->Type == TYPE_MEMO) {
            app->ActiveFocus = ctrl;
            g_focused_control = (void*)ctrl->KernelHandle;
            int limite_barra_x = abs_x + ctrl->Width - 18;
            if (mouse_x >= limite_barra_x && mouse_x <= (abs_x + ctrl->Width)) {
                int metade_vertical = abs_y + (ctrl->Height / 2);
                GUI_Memo_SetScroll(ctrl, ctrl->ScrollY + (mouse_y < metade_vertical ? -2 : 2));
            } else {
                GUI_Memo_SetFocus(ctrl);
            }
            return true;
        }

        // --- ScrollBar ---
        if (ctrl->Type == TYPE_SCROLLBAR) {
            int metade_vertical = ctrl->Height / 2;
            int local_y = mouse_y - abs_y;
            int jump = (ctrl->PageSize > 0) ? ctrl->PageSize : 1;
            GUI_ScrollBar_SetPosition(ctrl, ctrl->Position + (local_y < metade_vertical ? -jump : jump));
            return true;
        }

        // --- ListView ---
        if (ctrl->Type == TYPE_LISTVIEW) {
            app->ActiveFocus = ctrl;
            g_focused_control = (void*)ctrl->KernelHandle;
            TListView* kernel_lv = (TListView*)ctrl->KernelHandle;
            if (kernel_lv) {
                int local_y = mouse_y - abs_y - 4;
                int current_scroll = (int)gui_get_prop((void*)ctrl->KernelHandle, PROP_SCROLL_Y);
                int calculated_index = (local_y / 18) + current_scroll;
                if (calculated_index < 0) calculated_index = 0;
                if (calculated_index >= kernel_lv->ItemCount) calculated_index = kernel_lv->ItemCount - 1;
                kernel_lv->ItemIndex = calculated_index;
                ctrl->LVItemIndex = calculated_index;
                if (ctrl->OnItemClick != NULL) ctrl->OnItemClick(ctrl, calculated_index);
            }
            return true;
        }

        // --- WebPage ---
        if (ctrl->Type == TYPE_WEBPAGE) {
            app->ActiveFocus = ctrl;
            int limite_barra_x = abs_x + ctrl->Width - 18;
            if (mouse_x >= limite_barra_x && mouse_x <= (abs_x + ctrl->Width)) {
                int metade = abs_y + (ctrl->Height / 2);
                int delta = (mouse_y < metade) ? -3 : 3;
                int novo = ctrl->ScrollY + delta;
                if (novo < 0) novo = 0;
                ctrl->ScrollY = novo;
                gui_set_prop((void*)ctrl->KernelHandle, PROP_SCROLL_Y, (uint64_t)novo);
            } else {
                TWebPage* kpg = (TWebPage*)ctrl->KernelHandle;
                if (kpg && kpg->Doc) {
                    int local_y = mouse_y - abs_y - 5;
                    int line = (local_y / 16) + kpg->ScrollY;
                    if (line >= 0 && line < kpg->Doc->lc) {
                        WebLine* wl = &kpg->Doc->lines[line];
                        if (wl->style == WEB_STYLE_LINK &&
                            wl->link_id >= 0 && wl->link_id < kpg->Doc->kc) {
                            if (ctrl->OnNavigate) ctrl->OnNavigate(ctrl, kpg->Doc->links[wl->link_id].url);
                        }
                    }
                }
            }
            return true;
        }

        // =====================================================================
        // --- STRINGGRID v2 (Foco, Scroll, Seleção, Edição, Ordenação, Resize) ---
        // =====================================================================
        if (ctrl->Type == TYPE_STRINGGRID) {
            if (!ctrl->Enabled) return true;
            app->ActiveFocus = ctrl;
            g_focused_control = (void*)ctrl->KernelHandle;

            int local_x = mouse_x - abs_x - 2;
            int local_y = mouse_y - abs_y - 2;
            int row_h = (ctrl->DefaultRowHeight > 0) ? ctrl->DefaultRowHeight : 20;
            int fixed_h = ctrl->FixedRows * row_h;

            // Pré-calcula larguras e fixed_w (espelho do draw do Kernel)
            int ncols = (ctrl->ColCount < 16) ? ctrl->ColCount : 16;
            int col_wdt[16], col_off[16];
            int total_w = 0;
            for (int c = 0; c < ncols; c++) {
                col_wdt[c] = (ctrl->GridColWidths[c] > 0) ? ctrl->GridColWidths[c] : ctrl->DefaultColWidth;
                col_off[c] = total_w;
                total_w += col_wdt[c];
            }
            int fixed_w = (ctrl->FixedCols <= ncols) ? col_off[ctrl->FixedCols] : total_w;
            int usable_w = ctrl->Width - 16 - 4;
            bool show_hscroll = ((total_w - fixed_w) > (usable_w - fixed_w));

            // 1) ScrollBar VERTICAL (últimos 18px à direita)
            int limite_barra_x = abs_x + ctrl->Width - 18;
            if (mouse_x >= limite_barra_x && mouse_x <= (abs_x + ctrl->Width)) {
                if (ctrl->Editing) GUI_Grid_EndEdit(ctrl, true);
                int metade = abs_y + (ctrl->Height / 2);
                GUI_Grid_SetScroll(ctrl, ctrl->ScrollY + (mouse_y < metade ? -1 : 1));
                return true;
            }

            // 2) ScrollBar HORIZONTAL (últimos 18px embaixo, pulo de bloco)
            if (show_hscroll && mouse_y >= abs_y + ctrl->Height - 18) {
                if (ctrl->Editing) GUI_Grid_EndEdit(ctrl, true);
                int metade = abs_x + 2 + (usable_w / 2);
                GUI_Grid_SetScrollX(ctrl, ctrl->ScrollX + (mouse_x < metade ? -1 : 1));
                return true;
            }

            // 3) MODO REDIMENSIONAMENTO: próximo clique define a largura
            if (ctrl->GridResizing) {
                int col = ctrl->GridResizeCol;
                int off = (col < ctrl->FixedCols) ? col_off[col] : col_off[col] - ctrl->ScrollX;
                int new_w = local_x - off;
                if (new_w < 24) new_w = 24;
                GUI_Grid_SetColWidth(ctrl, col, new_w);
                ctrl->GridResizing = false;
                return true;
            }

            // 4) Mapeia a LINHA
            int clicked_row;
            if (local_y < fixed_h) clicked_row = local_y / row_h;
            else clicked_row = ((local_y - fixed_h) / row_h) + ctrl->FixedRows + ctrl->ScrollY;

            // 5) Mapeia a COLUNA (layout empacotado após a fixa, respeitando ScrollX)
            int clicked_col = -1;
            if (local_x < fixed_w) {
                int accx = 0;
                for (int c = 0; c < ctrl->FixedCols && c < ncols; c++) {
                    if (local_x >= accx && local_x < accx + col_wdt[c]) { clicked_col = c; break; }
                    accx += col_wdt[c];
                }
            } else {
                int accx = fixed_w;
                for (int c = ctrl->FixedCols + ctrl->ScrollX; c < ncols; c++) {
                    if (local_x >= accx && local_x < accx + col_wdt[c]) { clicked_col = c; break; }
                    accx += col_wdt[c];
                }
            }

            if (clicked_row < 0 || clicked_row >= ctrl->RowCount || clicked_col < 0) return true;

            // 6) CLIQUE NO CABEÇALHO: redimensionar OU ordenar
            if (clicked_row < ctrl->FixedRows && clicked_col >= ctrl->FixedCols) {
                int off = col_off[clicked_col] - ctrl->ScrollX;
                int right = off + col_wdt[clicked_col];
                if (local_x >= right - 4) {
                    ctrl->GridResizing = true;
                    ctrl->GridResizeCol = clicked_col;
                } else {
                    if (ctrl->SortColumn == clicked_col) ctrl->SortDirection = -ctrl->SortDirection;
                    else { ctrl->SortColumn = clicked_col; ctrl->SortDirection = 1; }
                    grid_sort_rows(ctrl, ctrl->SortColumn, ctrl->SortDirection);
                    uint64_t pk = ((uint64_t)(uint32_t)ctrl->SortColumn << 32) |
                                  (uint64_t)(uint32_t)ctrl->SortDirection;
                    gui_set_prop((void*)ctrl->KernelHandle, PROP_SORT, pk);
                    gui_set_prop((void*)ctrl->KernelHandle, PROP_CAPTION, (uintptr_t)ctrl->GridCells);
                }
                return true;
            }

            // 7) Célula de dados: seleção + edição inline
            if (ctrl->Editing) GUI_Grid_EndEdit(ctrl, true);
            if (!ctrl->ReadOnly && !ctrl->Editing &&
                ctrl->SelectedRow == clicked_row && ctrl->SelectedCol == clicked_col &&
                clicked_row >= ctrl->FixedRows && clicked_col >= ctrl->FixedCols) {
                GUI_Grid_BeginEdit(ctrl, clicked_row, clicked_col);
            } else {
                GUI_Grid_SelectCell(ctrl, clicked_row, clicked_col);
                if (ctrl->OnItemClick != NULL) ctrl->OnItemClick(ctrl, clicked_row);
            }
            return true;
        }

        // --- Painel (consome o clique em área vazia) ---
        if (ctrl->Type == TYPE_PANEL) return true;
    }

    // Clicou fora de tudo: confirma edição pendente do Grid (se houver)
    if (app->ActiveFocus && app->ActiveFocus->Type == TYPE_STRINGGRID && app->ActiveFocus->Editing) {
        GUI_Grid_EndEdit(app->ActiveFocus, true);
    }
    app->ActiveFocus = NULL;
    g_focused_control = NULL;
    return false;
}

/* ============================================================================
 * MOTOR CENTRAL DE EVENTOS (TECLADO)
 * ============================================================================ */
void GUI_ProcessKeyboard(TGUIEnvironment* app, char key) {
    if (!app || !app->ActiveFocus || key == 0) return;
    TGUIControl* ctrl = app->ActiveFocus;

    // --- TEdit ---
    if (ctrl->Type == TYPE_EDIT) {
        int len = strlen(ctrl->Text);
        if (key == '\b') {
            if (len > 0) ctrl->Text[len - 1] = '\0';
        } else if (key >= 32 && key <= 126 && len < 254) {
            ctrl->Text[len] = key;
            ctrl->Text[len + 1] = '\0';
        }
        gui_set_prop((void*)ctrl->KernelHandle, PROP_CAPTION, (uintptr_t)ctrl->Text);
        if (ctrl->OnChange) ctrl->OnChange(ctrl);
    }
    // --- TMemo ---
    else if (ctrl->Type == TYPE_MEMO) {
        GUI_Memo_AddChar(ctrl, key);
        if (ctrl->OnChange) ctrl->OnChange(ctrl);
    }
    // --- TStringGrid (navegação + edição inline) ---
    else if (ctrl->Type == TYPE_STRINGGRID) {
        if (!ctrl->Enabled) return;

        // MODO EDIÇÃO ATIVO
        if (ctrl->Editing) {
            if (key == 13) GUI_Grid_EndEdit(ctrl, true);       // Enter confirma
            else if (key == 27) GUI_Grid_EndEdit(ctrl, false); // Esc cancela
            else if (key == '\b') {
                int len = strlen(ctrl->EditBuffer);
                if (len > 0) ctrl->EditBuffer[len - 1] = '\0';
            }
            else if (key >= 32 && key <= 126) {
                int len = strlen(ctrl->EditBuffer);
                if (len < 63) {
                    ctrl->EditBuffer[len] = key;
                    ctrl->EditBuffer[len + 1] = '\0';
                }
            }
            return;
        }

        // MODO NAVEGAÇÃO
        if (key == 17 || key == 'w') GUI_Grid_SetScroll(ctrl, ctrl->ScrollY - 1);
        else if (key == 18 || key == 's') GUI_Grid_SetScroll(ctrl, ctrl->ScrollY + 1);
        else if (key == 19 || key == 'a') GUI_Grid_SetScrollX(ctrl, ctrl->ScrollX - 1);  // Scroll horizontal esquerda
        else if (key == 20 || key == 'd') GUI_Grid_SetScrollX(ctrl, ctrl->ScrollX + 1);  // Scroll horizontal direita
        else if (key == 13 && !ctrl->ReadOnly) {
            GUI_Grid_BeginEdit(ctrl, ctrl->SelectedRow, ctrl->SelectedCol);
        }
    }
}
