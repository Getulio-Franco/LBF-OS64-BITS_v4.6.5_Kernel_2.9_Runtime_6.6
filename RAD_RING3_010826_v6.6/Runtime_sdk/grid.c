#include "sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"

// Componentes do Sistema encapsulados
#include "components/TOS_IPC.h"

// Protótipos do Kernel
void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);

// Funções externas de controle de hardware e foco
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;

// Variáveis de controle do ambiente da aplicação
#define MAX_PROCESSOS 100  // LIMITE MÁXIMO DE SEGURANÇA
TProcessInfo lista_ps[MAX_PROCESSOS];
int my_app_slot = -1;
TGUIEnvironment MyApp;

// Flag global para forçar atualização da tela após callbacks
static bool g_solicitar_redesenho = false;

// Configurações de Dimensão
const int winWidth = 550;
const int winHeight = 410;

// Ponteiros de Controle RAD
TGUIControl* GridProcessos = NULL;
TGUIControl* BtnAtualizar  = NULL;

/* ============================================================================
* ESTRUTURA AUXILIAR IPC (Mapeamento de Eventos Estendidos)
* ============================================================================ */
typedef struct {
    uint8_t dummy[sizeof(IPC_WINDOW_LIST[0])];
    volatile uint8_t fila_teclado_virtual;
    volatile uint8_t tem_evento_teclado;
} __attribute__((packed)) AppWindowInfoExtended;

/* ============================================================================
* FUNÇÕES AUXILIARES E DE AMBIENTE
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

/* ============================================================================
* CALLBACKS DE EVENTOS RAD
* ============================================================================ */
void OnBtnAtualizarClick(void* sender) {
    (void)sender;
    if (!GridProcessos) return;

    GUI_Grid_SetScroll(GridProcessos, 0);

    // Busca processos
    for (int z = 0; z < MAX_PROCESSOS; z++) lista_ps[z].pid = 0;
    int qtd_processos = sys_get_ps_data(lista_ps, MAX_PROCESSOS);

    // Limita a 100 para segurança
    if (qtd_processos > 100) qtd_processos = 100;

    // Calcula linhas: cabeçalho (1) + processos
    int total_linhas = qtd_processos + 1;
    if (total_linhas < 2) total_linhas = 2;

    // ATUALIZA RING 3 + KERNEL
    GridProcessos->RowCount = total_linhas;
    gui_set_prop((void*)GridProcessos->KernelHandle, PROP_STATE, (uint64_t)total_linhas);

    // Limpa as 4 colunas antes de preencher
    for (int r = 1; r < total_linhas; r++) {
        GUI_Grid_SetCells(GridProcessos, 0, r, "");
        GUI_Grid_SetCells(GridProcessos, 1, r, "");
        GUI_Grid_SetCells(GridProcessos, 2, r, "");
        GUI_Grid_SetCells(GridProcessos, 3, r, "");
    }

    int row = 1;
    for (int k = 0; k < qtd_processos && row < total_linhas; k++) {
        if (lista_ps[k].pid != 0) {
            char pid_str[16], index_str[16];
            itoa(row, index_str, 10);
            itoa(lista_ps[k].pid, pid_str, 10);
            GUI_Grid_SetCells(GridProcessos, 0, row, index_str);
            GUI_Grid_SetCells(GridProcessos, 1, row, pid_str);
            GUI_Grid_SetCells(GridProcessos, 2, row, lista_ps[k].name);
            // NOVO: coluna 4 com texto fixo p/ testar o scroll horizontal
            GUI_Grid_SetCells(GridProcessos, 3, row, "EM EXECUCAO");
            row++;
        }
    }
    g_solicitar_redesenho = true;
}

/* ============================================================================
* FUNÇÃO PRINCIPAL (MAIN)
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

    my_app_slot = OS_IPC_RegisterApp("Tabela de Processos", winWidth, winHeight);
    if (my_app_slot == -1) return -1;
    graphics_set_slot(my_app_slot);

    GUI_InitApplication(&MyApp, my_app_slot, "Gerenciador StringGrid v2.0", winWidth, winHeight);
    if (MyApp.MainWindow) {
        gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0x000000);
    }

    /* =========================================================================
    * DESIGN DO LAYOUT DE COMPONENTES
    * ========================================================================= */
    BtnAtualizar = GUI_CreateButton(&MyApp, 10, 40, 210, 30, "ATUALIZAR TABELA", OnBtnAtualizarClick);

    // AGORA COM 4 COLUNAS: 60+110+330+200 = 700px > 510px visíveis -> scroll horizontal!
    GridProcessos = GUI_CreateStringGrid(&MyApp, 10, 80, 530, 310, 4, MAX_PROCESSOS + 1);
    if (GridProcessos) {
        GUI_Grid_SetColWidth(GridProcessos, 0, 60);    // [#] fixa
        GUI_Grid_SetColWidth(GridProcessos, 1, 110);   // [PID]
        GUI_Grid_SetColWidth(GridProcessos, 2, 330);   // [NOME DO PROCESSO]
        GUI_Grid_SetColWidth(GridProcessos, 3, 200);   // [STATUS] <- força o hscroll

        GUI_Grid_SetCells(GridProcessos, 0, 0, "#");
        GUI_Grid_SetCells(GridProcessos, 1, 0, "PID");
        GUI_Grid_SetCells(GridProcessos, 2, 0, "NOME DO PROCESSO");
        GUI_Grid_SetCells(GridProcessos, 3, 0, "STATUS");

        GridProcessos->RowCount = 2;
        gui_set_prop((void*)GridProcessos->KernelHandle, PROP_STATE, (uint64_t)2);

        GUI_Grid_SetEnabled(GridProcessos, true);
        GUI_Grid_SetReadOnly(GridProcessos, false);
    }

    g_focused_control = (void*)GridProcessos;
    ultimo_controle_focado = (void*)GridProcessos;
    if (GridProcessos) {
        gui_set_prop(GridProcessos, PROP_SET_FOCUS, 1);
    }

    // Carrega os dados iniciais
    OnBtnAtualizarClick(BtnAtualizar);
    Flush_Grafico_Janela();

    /* =========================================================================
    * LOOP DE EVENTOS CONTINUO
    * ========================================================================= */
    while(1) {
        if (IPC_WINDOW_LIST[my_app_slot].is_active == 0) {
            Tratar_Fechamento_Software();
            break;
        }

        bool precisa_redesenhar = false;

        if (primeiro_desenho) {
            primeiro_desenho = false;
            precisa_redesenhar = true;
        }

        if (g_solicitar_redesenho) {
            g_solicitar_redesenho = false;
            precisa_redesenhar = true;
        }

        bool euTenhoFocoJanelaReal = (IPC_CONTROL->active_focus_slot == my_app_slot);
        if (euTenhoFocoJanelaReal != ultimo_estado_foco) {
            ultimo_estado_foco = euTenhoFocoJanelaReal;
            if (MyApp.MainWindow) {
                ((TForm*)MyApp.MainWindow)->ActiveFocus = euTenhoFocoJanelaReal;
            }
            precisa_redesenhar = true;
        }

        if (g_focused_control != NULL) {
            ultimo_controle_focado = g_focused_control;
        } else if (ultimo_controle_focado != NULL) {
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
                ultimo_x = rel_x;
                ultimo_y = rel_y;
                mouse_hold_timer = 2;

                if (BtnAtualizar && rel_x >= BtnAtualizar->Left && rel_x < (BtnAtualizar->Left + BtnAtualizar->Width) &&
                    rel_y >= BtnAtualizar->Top && rel_y < (BtnAtualizar->Top + BtnAtualizar->Height)) {
                    gui_set_prop(BtnAtualizar, PROP_STATE, 2);
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
                if (BtnAtualizar) {
                    gui_set_prop(BtnAtualizar, PROP_STATE, 0);
                }
                events_process_mouse(ultimo_x, ultimo_y, 0, 0);
                precisa_redesenhar = true;
            }
        }

        if (precisa_redesenhar) {
            Flush_Grafico_Janela();
        }

        sys_sleep(euTenhoFocoJanelaReal ? 16 : 32);
    }

    if (GridProcessos) {
        GUI_DestroyStringGrid(GridProcessos);
        GridProcessos = NULL;
    }

    sys_exit();
    return 0;
}
