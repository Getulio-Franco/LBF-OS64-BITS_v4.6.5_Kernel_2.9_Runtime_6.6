#include "libgui.h"

/* ============================================================================
 * 1. CRIAÇÃO E INICIALIZAÇÃO DO COMPONENTE STRINGGRID
 *    Sincronizado com TGUIControl (libgui.h) e TStringGrid (gui.h do Kernel)
 * ============================================================================ */
TGUIControl* GUI_CreateStringGrid(TGUIEnvironment* app, int x, int y, int w, int h, int cols, int rows) {
    if (!app) return NULL;

    TGUIControl* grid = (TGUIControl*)malloc(sizeof(TGUIControl));
    if (!grid) return NULL;
    memset(grid, 0, sizeof(TGUIControl));

    // --- Geometria e configuração básica ---
    grid->Type = TYPE_STRINGGRID;
    grid->Left = x;
    grid->Top = y;
    grid->Width = w;
    grid->Height = h;
    grid->ColCount = (cols > 0) ? cols : 4;
    grid->RowCount = (rows > 0) ? rows : 4;
    grid->GridAllocatedRows = grid->RowCount;   // capacidade FIXA de memória
    grid->FixedCols = 1;
    grid->FixedRows = 1;
    grid->DefaultColWidth = 60;
    grid->DefaultRowHeight = 20;

    // --- Estado v2: scrolls, ordenação e resize ---
    grid->ScrollY = 0;
    grid->ScrollX = 0;              // índice da 1ª coluna de dados visível (pulo de bloco)
    grid->SortColumn = -1;          // -1 = sem ordenação
    grid->SortDirection = 0;        // 0 = neutro, 1 = asc, -1 = desc
    grid->GridResizing = false;
    grid->GridResizeCol = -1;
    for (int i = 0; i < 16; i++) grid->GridColWidths[i] = 0; // 0 = usa DefaultColWidth

    // --- Estado inicial seguro (memset zera Enabled p/ false!) ---
    grid->Enabled  = true;
    grid->ReadOnly = true;
    grid->Editing  = false;
    grid->SelectedRow = 0;
    grid->SelectedCol = 0;
    grid->EditBuffer[0] = '\0';

    // --- Matriz de células alocada pela CAPACIDADE (não pelo RowCount dinâmico) ---
    grid->GridCells = (char***)malloc(sizeof(char**) * grid->GridAllocatedRows);
    if (!grid->GridCells) { free(grid); return NULL; }
    for (int r = 0; r < grid->GridAllocatedRows; r++) {
        grid->GridCells[r] = (char**)malloc(sizeof(char*) * grid->ColCount);
        if (!grid->GridCells[r]) {
            for (int k = 0; k < r; k++) free(grid->GridCells[k]);
            free(grid->GridCells);
            free(grid);
            return NULL;
        }
        for (int c = 0; c < grid->ColCount; c++) {
            grid->GridCells[r][c] = (char*)malloc(64);
            if (grid->GridCells[r][c]) grid->GridCells[r][c][0] = '\0';
        }
    }

    // --- Registra na SDK ---
    GUI_RegisterControl(app, grid, "Grid");

    // --- Cria o objeto espelho no subsistema gráfico (Ring 3 / gui.c) ---
    TStringGrid* kernel_grid = gui_create_stringgrid((TWinControl*)app->MainWindow, grid->Name, grid->ColCount, grid->RowCount);
    if (!kernel_grid) {
        // Blindagem: não deixa ponteiro pendurado no app->Controls
        for (int i = 0; i < app->ControlCount; i++)
            if (app->Controls[i] == grid) app->Controls[i] = NULL;
        GUI_DestroyStringGrid(grid);
        return NULL;
    }
    grid->KernelHandle = (uint64_t)kernel_grid;

    // --- Sincronização inicial completa com o draw ---
    gui_set_prop((void*)grid->KernelHandle, PROP_LEFT,     (uint64_t)grid->Left);
    gui_set_prop((void*)grid->KernelHandle, PROP_TOP,      (uint64_t)grid->Top);
    gui_set_prop((void*)grid->KernelHandle, PROP_WIDTH,    (uint64_t)grid->Width);
    gui_set_prop((void*)grid->KernelHandle, PROP_HEIGHT,   (uint64_t)grid->Height);
    gui_set_prop((void*)grid->KernelHandle, PROP_CAPTION,  (uintptr_t)grid->GridCells);
    gui_set_prop((void*)grid->KernelHandle, PROP_ENABLED,  1);
    gui_set_prop((void*)grid->KernelHandle, PROP_READONLY, 1);
    gui_set_prop((void*)grid->KernelHandle, PROP_STATE,    (uint64_t)grid->RowCount);
    return grid;
}

/* ============================================================================
 * 2. MANIPULAÇÃO DE CÉLULAS (limitadas pela CAPACIDADE = segurança de memória)
 * ============================================================================ */
void GUI_Grid_SetCells(TGUIControl* grid, int col, int row, const char* text) {
    if (!grid || grid->Type != TYPE_STRINGGRID || !grid->GridCells) return;
    if (row < 0 || row >= grid->GridAllocatedRows) return;   // trava de memória
    if (col < 0 || col >= grid->ColCount) return;
    if (text) {
        strncpy(grid->GridCells[row][col], text, 63);
        grid->GridCells[row][col][63] = '\0';
    } else {
        grid->GridCells[row][col][0] = '\0';
    }
    // Notifica o draw para re-renderizar
    if (grid->KernelHandle) {
        gui_set_prop((void*)grid->KernelHandle, PROP_CAPTION, (uintptr_t)grid->GridCells);
    }
}

char* GUI_Grid_GetCells(TGUIControl* grid, int col, int row) {
    if (!grid || grid->Type != TYPE_STRINGGRID || !grid->GridCells) return "";
    if (row < 0 || row >= grid->GridAllocatedRows) return "";
    if (col < 0 || col >= grid->ColCount) return "";
    return grid->GridCells[row][col];
}

void GUI_Grid_Clear(TGUIControl* grid) {
    if (!grid || grid->Type != TYPE_STRINGGRID || !grid->GridCells) return;
    for (int r = 0; r < grid->GridAllocatedRows; r++) {
        for (int c = 0; c < grid->ColCount; c++) {
            if (grid->GridCells[r][c]) grid->GridCells[r][c][0] = '\0';
        }
    }
    if (grid->KernelHandle) {
        gui_set_prop((void*)grid->KernelHandle, PROP_CAPTION, (uintptr_t)grid->GridCells);
    }
}

/* ============================================================================
 * 3. DESTRUIÇÃO (libera pela CAPACIDADE, não pelo RowCount)
 * ============================================================================ */
void GUI_DestroyStringGrid(TGUIControl* grid) {
    if (!grid || grid->Type != TYPE_STRINGGRID) return;
    if (grid->GridCells) {
        int cap = (grid->GridAllocatedRows > 0) ? grid->GridAllocatedRows : grid->RowCount;
        for (int r = 0; r < cap; r++) {
            if (grid->GridCells[r]) {
                for (int c = 0; c < grid->ColCount; c++) {
                    if (grid->GridCells[r][c]) free(grid->GridCells[r][c]);
                }
                free(grid->GridCells[r]);
            }
        }
        free(grid->GridCells);
        grid->GridCells = NULL;
    }
    free(grid);
}

/* ============================================================================
 * 4. SCROLL VERTICAL (trava pelas linhas de DADOS, respeitando o cabeçalho)
 * ============================================================================ */
void GUI_Grid_SetScroll(TGUIControl* grid, int value) {
    if (!grid || !grid->KernelHandle) return;
    if (value < 0) value = 0;

    int row_h = (grid->DefaultRowHeight > 0) ? grid->DefaultRowHeight : 20;
    int visible_rows = (grid->Height - 4) / row_h;
    int data_total   = grid->RowCount - grid->FixedRows;
    int data_visible = visible_rows  - grid->FixedRows;
    int max_scroll = data_total - data_visible;
    if (max_scroll < 0) max_scroll = 0;
    if (value > max_scroll) value = max_scroll;

    grid->ScrollY = value;
    gui_set_prop((void*)grid->KernelHandle, PROP_SCROLL_Y, (uint64_t)grid->ScrollY);
}

/* ============================================================================
 * 5. SCROLL HORIZONTAL (v2: PULO DE BLOCO — índice de coluna, não pixels)
 *    Sempre resta pelo menos 1 coluna de dados visível (igual ao Delphi)
 * ============================================================================ */
void GUI_Grid_SetScrollX(TGUIControl* grid, int value) {
    if (!grid || !grid->KernelHandle) return;
    int maxs = grid->ColCount - grid->FixedCols - 1;
    if (maxs < 0) maxs = 0;
    if (value < 0) value = 0;
    if (value > maxs) value = maxs;

    grid->ScrollX = value;
    gui_set_prop((void*)grid->KernelHandle, PROP_SCROLL_X, (uint64_t)grid->ScrollX);
}

/* ============================================================================
 * 6. CONTROLE DE LINHAS SEGURO (trava na capacidade alocada)
 * ============================================================================ */
void GUI_Grid_SetRowCount(TGUIControl* grid, int rows) {
    if (!grid || grid->Type != TYPE_STRINGGRID) return;
    if (rows < 1) rows = 1;
    if (rows > grid->GridAllocatedRows) rows = grid->GridAllocatedRows; // nunca estoura

    // Se estava editando uma linha que vai sumir, cancela a edição
    if (grid->Editing && grid->SelectedRow >= rows) GUI_Grid_EndEdit(grid, false);

    grid->RowCount = rows;
    if (grid->SelectedRow >= rows) grid->SelectedRow = rows - 1;

    if (grid->KernelHandle)
        gui_set_prop((void*)grid->KernelHandle, PROP_STATE, (uint64_t)rows);

    // Re-trava os dois scrolls para o novo tamanho
    GUI_Grid_SetScroll(grid, grid->ScrollY);
    GUI_Grid_SetScrollX(grid, grid->ScrollX);
}

/* ============================================================================
 * 7. SELEÇÃO COM AUTO-SCROLL (linha selecionada sempre visível)
 * ============================================================================ */
static void grid_ensure_row_visible(TGUIControl* grid, int row) {
    if (row < grid->FixedRows) return; // linha fixa sempre visível
    int row_h = (grid->DefaultRowHeight > 0) ? grid->DefaultRowHeight : 20;
    int visible_rows = (grid->Height - 4) / row_h;
    int data_visible = visible_rows - grid->FixedRows;
    if (data_visible < 1) data_visible = 1;

    int first_data = grid->FixedRows + grid->ScrollY;
    int last_data  = first_data + data_visible - 1;

    if (row < first_data)     GUI_Grid_SetScroll(grid, row - grid->FixedRows);
    else if (row > last_data) GUI_Grid_SetScroll(grid, grid->ScrollY + (row - last_data));
}

void GUI_Grid_SelectCell(TGUIControl* grid, int row, int col) {
    if (!grid || grid->Type != TYPE_STRINGGRID) return;
    if (row < 0 || row >= grid->RowCount || col < 0 || col >= grid->ColCount) return;

    grid->SelectedRow = row;
    grid->SelectedCol = col;
    grid_ensure_row_visible(grid, row);

    // Sincroniza o DESTAQUE AZUL no draw: row (low) | col (high)
    if (grid->KernelHandle) {
        uint64_t packed = ((uint64_t)(uint32_t)row) | (((uint64_t)(uint32_t)col) << 32);
        gui_set_prop((void*)grid->KernelHandle, PROP_SELECTED_CELL, packed);
    }
}

void GUI_Grid_GetSelectedCell(TGUIControl* grid, int* row, int* col) {
    if (row) *row = grid ? grid->SelectedRow : -1;
    if (col) *col = grid ? grid->SelectedCol : -1;
}

/* ============================================================================
 * 8. DIMENSÕES (largura por coluna / altura da linha — sincronizadas)
 * ============================================================================ */
void GUI_Grid_SetColWidth(TGUIControl* grid, int col, int width) {
    if (!grid || grid->Type != TYPE_STRINGGRID) return;
    if (col < 0 || col >= 16 || col >= grid->ColCount) return;
    if (width <= 0) width = grid->DefaultColWidth;
    if (width < 24) width = 24; // largura mínima usável

    grid->GridColWidths[col] = width;                    // Ring 3 (clique)
    if (grid->KernelHandle) {
        // Empacota: col (high 32) | width (low 32)
        uint64_t packed = ((uint64_t)(uint32_t)col << 32) | (uint64_t)(uint32_t)width;
        gui_set_prop((void*)grid->KernelHandle, PROP_COL_WIDTH, packed); // draw
    }
}

void GUI_Grid_SetRowHeight(TGUIControl* grid, int height) {
    if (!grid || grid->Type != TYPE_STRINGGRID || height <= 0) return;
    grid->DefaultRowHeight = height;
    if (grid->KernelHandle)
        gui_set_prop((void*)grid->KernelHandle, PROP_ROW_HEIGHT, (uint64_t)height);
}

/* ============================================================================
 * 9. MODO DE EDIÇÃO INLINE (TEdit embutido na célula)
 *    NOTA: EndEdit vem antes porque SetReadOnly/SetEnabled/SetRowCount a usam.
 * ============================================================================ */
void GUI_Grid_EndEdit(TGUIControl* grid, bool commit) {
    if (!grid || grid->Type != TYPE_STRINGGRID || !grid->Editing) return;
    if (commit && !grid->ReadOnly) {
        GUI_Grid_SetCells(grid, grid->SelectedCol, grid->SelectedRow, grid->EditBuffer);
    }
    grid->Editing = false;
    // 0xFFFFFFFFFFFFFFFF = encerra a edição no draw
    if (grid->KernelHandle)
        gui_set_prop((void*)grid->KernelHandle, PROP_EDIT_CELL, 0xFFFFFFFFFFFFFFFFULL);
}

void GUI_Grid_BeginEdit(TGUIControl* grid, int row, int col) {
    if (!grid || grid->Type != TYPE_STRINGGRID) return;
    if (grid->ReadOnly || !grid->Enabled || grid->Editing) return;
    if (row < grid->FixedRows || col < grid->FixedCols) return;   // célula fixa não edita
    if (row >= grid->RowCount || col >= grid->ColCount) return;

    grid->Editing = true;
    grid->SelectedRow = row;
    grid->SelectedCol = col;
    grid_ensure_row_visible(grid, row);   // rola até a célula se estiver fora da tela

    // Pré-preenche o buffer com o conteúdo atual da célula
    strncpy(grid->EditBuffer, grid->GridCells[row][col], 63);
    grid->EditBuffer[63] = '\0';

    if (grid->KernelHandle) {
        // Buffer vivo compartilhado com o draw
        gui_set_prop((void*)grid->KernelHandle, PROP_EDIT_TEXT, (uintptr_t)grid->EditBuffer);
        // Coordenadas empacotadas: row (low) | col (high)
        uint64_t packed = ((uint64_t)(uint32_t)row) | (((uint64_t)(uint32_t)col) << 32);
        gui_set_prop((void*)grid->KernelHandle, PROP_EDIT_CELL, packed);
    }
}

/* ============================================================================
 * 10. PROTEÇÕES (ReadOnly / Enabled)
 * ============================================================================ */
void GUI_Grid_SetReadOnly(TGUIControl* grid, bool read_only) {
    if (!grid || grid->Type != TYPE_STRINGGRID) return;
    grid->ReadOnly = read_only;
    if (read_only) GUI_Grid_EndEdit(grid, false);   // cancela edição pendente
    if (grid->KernelHandle)
        gui_set_prop((void*)grid->KernelHandle, PROP_READONLY, read_only ? 1 : 0);
}

void GUI_Grid_SetEnabled(TGUIControl* grid, bool enabled) {
    if (!grid || grid->Type != TYPE_STRINGGRID) return;
    grid->Enabled = enabled;
    if (!enabled) GUI_Grid_EndEdit(grid, false);    // cancela edição pendente
    if (grid->KernelHandle)
        gui_set_prop((void*)grid->KernelHandle, PROP_ENABLED, enabled ? 1 : 0);
}
