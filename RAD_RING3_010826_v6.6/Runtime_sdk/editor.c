#include "sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"
#include "components/TOS_IPC.h"
#include "../system/sysutils.h"

// Protótipos de renderização gráfica
void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);

// Funções externas do sistema de foco e mouse
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;

// Protótipos de manipulação de Memo
extern void GUI_Memo_AddStr(TGUIControl* memo, const char* str);
extern void GUI_Memo_Clear(TGUIControl* memo);

// Variáveis globais do app
int my_app_slot = -1;
TGUIEnvironment MyApp;

const int winWidth = 600;
const int winHeight = 500;

// Componentes da Interface
TGUIControl* MemoTexto   = NULL;
TGUIControl* EditArquivo = NULL;
TGUIControl* BtnCarregar = NULL;
TGUIControl* BtnSalvar   = NULL;

// Barra de Status Inferior
TGUIControl* LabelLnCol  = NULL;
TGUIControl* LabelStatus = NULL;

// Controle do Cursor
static int linha_cursor = 1;
static int coluna_cursor = 1;

#define MEMO_X 10
#define MEMO_Y 75
#define MEMO_W 580
#define MEMO_H 355 // Altura ajustada para liberar espaço para a barra de status

#define STATUS_Y 440
#define CHAR_W 8
#define CHAR_H 16

/* ============================================================================
 * FUNÇÕES AUXILIARES DE STATUS E CURSOR (Usando sysutils.h)
 * ============================================================================ */
char* GUI_Memo_GetText(TGUIControl* memo) {
    if (!memo || !memo->Buffer) return NULL;
    return (char*)memo->Buffer;
}

static void Editor_Atualizar_LnCol(void) {
    char buf[32];
    char num_ln[12], num_col[12];

    IntToStr(linha_cursor, num_ln);
    IntToStr(coluna_cursor, num_col);

    strcpy(buf, "Ln ");
    strcat(buf, num_ln);
    strcat(buf, ", Col ");
    strcat(buf, num_col);

    if (LabelLnCol) GUI_Edit_SetText(LabelLnCol, buf);
}

static void Editor_Set_Status(const char* msg) {
    if (LabelStatus) GUI_Edit_SetText(LabelStatus, (char*)msg);
}

static int Editor_Compr_Linha(int linha) {
    char* texto = GUI_Memo_GetText(MemoTexto);
    int ln = 1, comp = 0;

    if (!texto) return 0;

    for (char* p = texto; *p; p++) {
        if (*p == '\n') {
            if (ln == linha) return comp;
            ln++;
            comp = 0;
        } else if (ln == linha) {
            comp++;
        }
    }
    return (ln == linha) ? comp : 0;
}

static void Editor_Recalcular_LnCol_Fim(void) {
    char* texto = GUI_Memo_GetText(MemoTexto);
    int ln = 1, col = 1;

    if (texto) {
        for (char* p = texto; *p; p++) {
            if (*p == '\n') { ln++; col = 1; }
            else col++;
        }
    }
    linha_cursor = ln;
    coluna_cursor = col;
}

/* ============================================================================
 * COMUNICAÇÃO E JANELA IPC
 * ============================================================================ */
typedef struct {
    uint8_t dummy[sizeof(IPC_WINDOW_LIST[0])];
    volatile uint8_t fila_teclado_virtual;
    volatile uint8_t tem_evento_teclado;
} __attribute__((packed)) AppWindowInfoExtended;

char Obter_Tecla_Entrada(void) {
    AppWindowInfoExtended* ext_slot = (AppWindowInfoExtended*)&IPC_WINDOW_LIST[my_app_slot];
    if (ext_slot->tem_evento_teclado == 1) {
        char key = (char)ext_slot->fila_teclado_virtual;
        ext_slot->tem_evento_teclado = 0;
        return key;
    }
    return get_key();
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

/* ============================================================================
 * EVENTOS DOS BOTÕES
 * ============================================================================ */
void OnBtnCarregarClick(void* sender) {
    char* caminho = GUI_Edit_GetText(EditArquivo);
    if (!caminho || caminho[0] == '\0') {
        Editor_Set_Status("Caminho invalido!");
        return;
    }

    GUI_Memo_Clear(MemoTexto);

    char buffer[4097];
    memset(buffer, 0, sizeof(buffer));

    int bytes_lidos = sys_fat_read(caminho, (void*)buffer, 4096);

    if (bytes_lidos > 0) {
        if (bytes_lidos > 4096) bytes_lidos = 4096;
        buffer[bytes_lidos] = '\0';
        GUI_Memo_AddStr(MemoTexto, buffer);
        Editor_Set_Status("Arquivo carregado com sucesso.");
    } else {
        Editor_Set_Status("Arquivo vazio ou nao encontrado.");
    }

    Editor_Recalcular_LnCol_Fim();
    Editor_Atualizar_LnCol();
}

void OnBtnSalvarClick(void* sender) {
    char* caminho = GUI_Edit_GetText(EditArquivo);
    char* conteudo = GUI_Memo_GetText(MemoTexto);

    if (!caminho || caminho[0] == '\0') {
        Editor_Set_Status("Caminho invalido!");
        return;
    }

    if (!conteudo) conteudo = "";

    int status = sys_fat_write(caminho, (void*)conteudo, (uint32_t)strlen(conteudo));

    if (status == 0) {
        Editor_Set_Status("Arquivo salvo com sucesso!");
    } else {
        Editor_Set_Status("[Erro] Falha ao gravar no disco.");
    }
}

/* ============================================================================
 * FUNÇÃO PRINCIPAL
 * ============================================================================ */
int main(int argc, char* argv[]) {
    static int ultimo_x = 0;
    static int ultimo_y = 0;
    static int mouse_hold_timer = 0;
    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;
    void* ultimo_controle_focado = NULL;

    graphics_init_app(winWidth, winHeight);
    wm_init();

    my_app_slot = OS_IPC_RegisterApp("Bloco de Notas LBF", winWidth, winHeight);
    if (my_app_slot == -1) return -1;

    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Bloco de Notas v1.1", winWidth, winHeight);

    // Cor cinza original do formulário
    if (MyApp.MainWindow) {
        gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0xC0C0C0);
    }

    // Topo idêntico ao seu primeiro código
    GUI_CreateLabel(&MyApp, 10, 42, "Arquivo:");
    EditArquivo = GUI_CreateEdit(&MyApp, 70, 38, 300, 25, "0:/nota.txt", NULL);
    BtnCarregar = GUI_CreateButton(&MyApp, 380, 38, 100, 25, "CARREGAR", OnBtnCarregarClick);
    BtnSalvar   = GUI_CreateButton(&MyApp, 490, 38, 100, 25, "SALVAR", OnBtnSalvarClick);

    // Editor Memo
    MemoTexto = GUI_CreateMemo(&MyApp, MEMO_X, MEMO_Y, MEMO_W, MEMO_H);
    gui_set_prop(MemoTexto, PROP_COLOR, 0x000000);

    // Barra de Status na parte inferior do formulário
    LabelLnCol  = GUI_CreateLabel(&MyApp, 10, STATUS_Y, "Ln 1, Col 1");
    LabelStatus = GUI_CreateLabel(&MyApp, 150, STATUS_Y, "Pronto");

    // Configuração inicial do foco
    g_focused_control = (void*)MemoTexto;
    ultimo_controle_focado = (void*)MemoTexto;
    gui_set_prop(MemoTexto, PROP_SET_FOCUS, 1);

    Flush_Grafico_Janela();

    /* =========================================================================
     * LOOP PRINCIPAL DE EVENTOS
     * ========================================================================= */
    while(1) {
        if (IPC_WINDOW_LIST[my_app_slot].is_active == 0) {
            Tratar_Fechamento_Software();
            break;
        }

        bool euTenhoFoco = (IPC_CONTROL->active_focus_slot == my_app_slot);
        if (MyApp.MainWindow) {
            ((TForm*)MyApp.MainWindow)->ActiveFocus = euTenhoFoco;
        }

        bool precisa_redesenhar = false;

        if (primeiro_desenho) {
            primeiro_desenho = false;
            precisa_redesenhar = true;
        }

        if (euTenhoFoco != ultimo_estado_foco) {
            ultimo_estado_foco = euTenhoFoco;
            precisa_redesenhar = true;
        }

        if (g_focused_control != NULL) {
            ultimo_controle_focado = g_focused_control;
        } else if (ultimo_controle_focado != NULL) {
            g_focused_control = ultimo_controle_focado;
            gui_set_prop((TGUIControl*)ultimo_controle_focado, PROP_SET_FOCUS, 1);
        }

        if (euTenhoFoco) {
            char key = Obter_Tecla_Entrada();
            if (key != 0) {
                unsigned char k = (unsigned char)key;

                // Atualização da Posição do Cursor
                if (k >= 32 && k <= 126) {
                    coluna_cursor++;
                } else if (k == 13 || k == 10) {
                    linha_cursor++;
                    coluna_cursor = 1;
                } else if (k == 8) { // Backspace
                    if (coluna_cursor > 1) {
                        coluna_cursor--;
                    } else if (linha_cursor > 1) {
                        linha_cursor--;
                        coluna_cursor = Editor_Compr_Linha(linha_cursor) + 1;
                    }
                }

                GUI_ProcessKeyboard(&MyApp, key);
                Editor_Atualizar_LnCol();
                precisa_redesenhar = true;
            }

            if (IPC_WINDOW_LIST[my_app_slot].has_click_event == 1) {
                if (mouse_hold_timer == 0) {
                    int rel_x = IPC_WINDOW_LIST[my_app_slot].local_click_x;
                    int rel_y = IPC_WINDOW_LIST[my_app_slot].local_click_y;

                    ultimo_x = rel_x;
                    ultimo_y = rel_y;
                    mouse_hold_timer = 2;

                    events_process_mouse(rel_x, rel_y, 1, 0);

                    // Atualiza a posição do cursor quando o usuário clica no texto
                    if (rel_x >= MEMO_X && rel_x < (MEMO_X + MEMO_W) &&
                        rel_y >= MEMO_Y && rel_y < (MEMO_Y + MEMO_H)) {
                        linha_cursor = (rel_y - MEMO_Y) / CHAR_H + 1;
                        coluna_cursor = (rel_x - MEMO_X) / CHAR_W + 1;

                        int total_linhas = 1;
                        char* texto = GUI_Memo_GetText(MemoTexto);
                        if (texto) {
                            for (char* p = texto; *p; p++) {
                                if (*p == '\n') total_linhas++;
                            }
                        }

                        if (linha_cursor > total_linhas) linha_cursor = total_linhas;
                        if (linha_cursor < 1) linha_cursor = 1;

                        int comp = Editor_Compr_Linha(linha_cursor);
                        if (coluna_cursor > comp + 1) coluna_cursor = comp + 1;
                        if (coluna_cursor < 1) coluna_cursor = 1;

                        Editor_Atualizar_LnCol();
                    }

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
                    events_process_mouse(ultimo_x, ultimo_y, 0, 0);
                    precisa_redesenhar = true;
                }
            }
        }

        if (precisa_redesenhar) {
            Flush_Grafico_Janela();
        }

        sys_sleep(euTenhoFoco ? 16 : 32);
    }

    sys_exit();
    return 0;
}
