#include "Runtime_sdk/sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/sysutils.h"
#include "../system/string.h" // Provavelmente contém IntToStr ou similar
#include "../system/liblib.h"
// Inclusão exclusiva da interface cliente da IA (Totalmente desacoplada dos cores)
#include "Runtime_Neural/system_lib/neurallib.h"
// Componentes do Sistema encapsulados
#include "Runtime_sdk/components/TOS_IPC.h"

// Protótipos de renderização gráfica
void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);

// Funções externas de controle
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;
extern void GUI_Memo_AddStr(TGUIControl* memo, const char* str);
extern void GUI_Memo_Clear(TGUIControl* memo);

// Variáveis de controle do ambiente da aplicação
int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth = 550;
const int winHeight = 410;

// Ponteiros de Controle RAD
TGUIControl* ExeMemo      = NULL;
TGUIControl* BtnTestMath  = NULL;
TGUIControl* BtnTestLogic = NULL;
TGUIControl* BtnTestRoute = NULL;
TGUIControl* BtnTestSynth = NULL;
TGUIControl* BtnTestAll   = NULL;

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
* AUXILIARES DE FORMATAÇÃO (Sem printf/sprintf)
* ============================================================================ */

// Converte inteiro para string (simplificado)
void IntToStr_Custom(int value, char* buffer) {
    int i = 0;
    int is_negative = 0;
    
    if (value < 0) {
        is_negative = 1;
        value = -value;
    }
    
    // Extrai dígitos
    do {
        buffer[i++] = (value % 10) + '0';
        value /= 10;
    } while (value > 0);
    
    if (is_negative) {
        buffer[i++] = '-';
    }
    buffer[i] = '\0';
    
    // Inverte a string
    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = buffer[start];
        buffer[start] = buffer[end];
        buffer[end] = temp;
        start++;
        end--;
    }
}

// Adiciona resultado float ao memo sem sprintf
void Memo_Add_Float_Result(TGUIControl* memo, const char* label, float result) {
    char buffer_int[16];
    char buffer_frac[16];
    
    // Parte inteira
    int int_part = (int)result;
    IntToStr_Custom(int_part, buffer_int);
    
    // Parte decimal (simples, 2 casas)
    float frac_part = result - (float)int_part;
    if (frac_part < 0) frac_part = -frac_part;
    int frac_val = (int)(frac_part * 100 + 0.5f); // Arredonda
    
    // Monta string manualmente: Label + Int + "." + Frac
    char final_str[64];
    int idx = 0;
    
    // Copia Label
    for (int k = 0; label[k] != '\0'; k++) final_str[idx++] = label[k];
    
    // Copia Inteiro
    for (int k = 0; buffer_int[k] != '\0'; k++) final_str[idx++] = buffer_int[k];
    
    final_str[idx++] = '.';
    
    // Copia Fracionário (garantindo 2 dígitos)
    if (frac_val < 10) final_str[idx++] = '0';
    IntToStr_Custom(frac_val, buffer_frac);
    for (int k = 0; buffer_frac[k] != '\0'; k++) final_str[idx++] = buffer_frac[k];
    
    final_str[idx++] = '\n';
    final_str[idx] = '\0';
    
    GUI_Memo_AddStr(memo, final_str);
}

/* ============================================================================
* CALLBACKS DE TESTE DA NEURALLIB.H
* ============================================================================ */

#include "../system/sysutils.h" // Para FloatToStr e IntToStr

void OnBtnTestMathClick(void* sender) {
    GUI_Memo_Clear(ExeMemo);
    GUI_Memo_AddStr(ExeMemo, "=== TESTE MATH (100) ===\n");
    
    // ID 0 = Soma. Vamos somar 2.5 + 3.5
    float resultado = sys_ia_math(0, 2.5f, 3.5f); 
    
    char buffer[32];
    FloatToStr(resultado, buffer);
    
    GUI_Memo_AddStr(ExeMemo, "Soma (2.5 + 3.5) = ");
    GUI_Memo_AddStr(ExeMemo, buffer);
    GUI_Memo_AddStr(ExeMemo, "\n");
}

void OnBtnTestLogicClick(void* sender) {
    GUI_Memo_Clear(ExeMemo);
    GUI_Memo_AddStr(ExeMemo, "=== TESTE LOGIC (101) ===\n");
    
    // ID 0 = AND. Vamos testar 1 AND 1
    int res1 = sys_ia_logic(0, 1.0f, 1.0f); 
    // ID 5 = XOR. Vamos testar 1 XOR 1
    int res2 = sys_ia_logic(5, 1.0f, 1.0f); 
    
    char buffer[16];
    IntToStr(res1, buffer);
    GUI_Memo_AddStr(ExeMemo, "AND (1, 1) = ");
    GUI_Memo_AddStr(ExeMemo, buffer);
    GUI_Memo_AddStr(ExeMemo, "\n");
    
    IntToStr(res2, buffer);
    GUI_Memo_AddStr(ExeMemo, "XOR (1, 1) = ");
    GUI_Memo_AddStr(ExeMemo, buffer);
    GUI_Memo_AddStr(ExeMemo, "\n");
}

void OnBtnTestRouteClick(void* sender) {
    GUI_Memo_Clear(ExeMemo);
    GUI_Memo_AddStr(ExeMemo, "=== TESTE SYS_IA_ROUTER (102) ===\n");
    
    const char* prompt = "soma 2 + 2";
    
    // Definimos a estrutura localmente para garantir que o tamanho do buffer 
    // seja compatível com o que o Kernel espera, sem incluir headers internos.
    typedef struct {
        int acionar_math;
        int acionar_logic;
        char comando_limpo[64];
    } LocalRouterDecision;
    
    LocalRouterDecision dec_local;
    
    GUI_Memo_AddStr(ExeMemo, "[1] Roteando: 'soma 2 + 2'... \n");
    int status = sys_ia_router(prompt, &dec_local);
    
    char buffer[32];
    IntToStr_Custom(status, buffer);
    GUI_Memo_AddStr(ExeMemo, "Status: ");
    GUI_Memo_AddStr(ExeMemo, buffer);
    GUI_Memo_AddStr(ExeMemo, "\n");
    
    if (status == 0) {
        GUI_Memo_AddStr(ExeMemo, "Math: ");
        IntToStr_Custom(dec_local.acionar_math, buffer);
        GUI_Memo_AddStr(ExeMemo, buffer);
        
        GUI_Memo_AddStr(ExeMemo, " | Logic: ");
        IntToStr_Custom(dec_local.acionar_logic, buffer);
        GUI_Memo_AddStr(ExeMemo, buffer);
        
        GUI_Memo_AddStr(ExeMemo, "\nComando Limpo: '");
        GUI_Memo_AddStr(ExeMemo, dec_local.comando_limpo);
        GUI_Memo_AddStr(ExeMemo, "'\n");
    } else {
        GUI_Memo_AddStr(ExeMemo, "Erro ao rotear.\n");
    }
    
    GUI_Memo_AddStr(ExeMemo, "\n=== Teste Router Concluido ===\n");
}

void OnBtnTestSynthClick(void* sender) {
    GUI_Memo_Clear(ExeMemo);
    GUI_Memo_AddStr(ExeMemo, "=== TESTE SYS_IA_SYNTH (103) ===\n");
    
    const char* prompt = "Resultado";
    const char* raw = "42.00";
    
    // Aumentamos o buffer para garantir espaço seguro
    char buffer_saida[256]; 

    
    GUI_Memo_AddStr(ExeMemo, "[1] Sintetizando... ");
    int status = sys_ia_synth(prompt, raw, buffer_saida);
    
    char buffer[32];
    IntToStr_Custom(status, buffer);
    GUI_Memo_AddStr(ExeMemo, "Status: ");
    GUI_Memo_AddStr(ExeMemo, buffer);
    GUI_Memo_AddStr(ExeMemo, "\n");
    
    if (status == 0) {
        GUI_Memo_AddStr(ExeMemo, "Resposta: '");
        GUI_Memo_AddStr(ExeMemo, buffer_saida);
        GUI_Memo_AddStr(ExeMemo, "'\n");
    }
    
    GUI_Memo_AddStr(ExeMemo, "\n=== Teste Synth Concluido ===\n");
}

void OnBtnTestAllClick(void* sender) {
    GUI_Memo_Clear(ExeMemo);
    GUI_Memo_AddStr(ExeMemo, "=== TESTE COMPLETO ===\n\n");
    
    OnBtnTestMathClick(sender);
    sys_sleep(100); // Pequena pausa visual
    OnBtnTestLogicClick(sender);
    sys_sleep(100);
    OnBtnTestRouteClick(sender);
    sys_sleep(100);
    OnBtnTestSynthClick(sender);
    
    GUI_Memo_AddStr(ExeMemo, "\n=== TODOS OS TESTES FINALIZADOS ===\n");
}

/* ============================================================================
* MAIN (LOOP DE EVENTOS RAD COM DEBOUNCE)
* ============================================================================ */
int main(int argc, char* argv[]) {
    static int ultimo_x = 0;
    static int ultimo_y = 0;
    static int mouse_hold_timer = 0;
    static int debounce_timer = 0;
    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;
    
    graphics_init_app(winWidth, winHeight);
    wm_init();
    
    my_app_slot = OS_IPC_RegisterApp("Teste NeuralRuntime ELF", winWidth, winHeight);
    if (my_app_slot == -1) return -1;
    
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Testador Neural Runtime v2.0", winWidth, winHeight);
    
    if (MyApp.MainWindow) {
        gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0xC0C0C0);
    }
    
    int btnW = 250;
    int ctrlH = 28;
    
    BtnTestMath  = GUI_CreateButton(&MyApp, 10, 40,  btnW, ctrlH, "1. TESTAR MATH (100)",  OnBtnTestMathClick);
    BtnTestLogic = GUI_CreateButton(&MyApp, 270, 40, btnW, ctrlH, "2. TESTAR LOGIC (101)", OnBtnTestLogicClick);
    BtnTestRoute = GUI_CreateButton(&MyApp, 10, 75,  btnW, ctrlH, "3. TESTAR ROUTER (102)", OnBtnTestRouteClick);
    BtnTestSynth = GUI_CreateButton(&MyApp, 270, 75, btnW, ctrlH, "4. TESTAR SYNTH (103)",  OnBtnTestSynthClick);
    BtnTestAll   = GUI_CreateButton(&MyApp, 10, 110, 510, ctrlH, "5. TESTAR TODAS AS SYSCALLS", OnBtnTestAllClick);
    
    ExeMemo = GUI_CreateMemo(&MyApp, 10, 145, 530, 255);
    gui_set_prop(ExeMemo, PROP_COLOR, 0x000000);
    
    GUI_Memo_AddStr(ExeMemo, "--- TESTADOR NEURAL RUNTIME v2.0 ---\n");
    GUI_Memo_AddStr(ExeMemo, "Clique nos botoes para testar cada syscall.\n");
    
    Flush_Grafico_Janela();
    
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
        
        bool euTenhoFocoJanelaReal = (IPC_CONTROL->active_focus_slot == my_app_slot);
        if (euTenhoFocoJanelaReal != ultimo_estado_foco) {
            ultimo_estado_foco = euTenhoFocoJanelaReal;
            if (MyApp.MainWindow) {
                ((TForm*)MyApp.MainWindow)->ActiveFocus = euTenhoFocoJanelaReal;
            }
            precisa_redesenhar = true;
        }
        
        char key = Obter_Tecla_Entrada();
        if (key != 0) {
            GUI_ProcessKeyboard(&MyApp, key);
            precisa_redesenhar = true;
        }
        
        if (debounce_timer > 0) {
            debounce_timer--;
        }
        
        if (IPC_WINDOW_LIST[my_app_slot].has_click_event == 1) {
            if (mouse_hold_timer == 0 && debounce_timer == 0) {
                int rel_x = IPC_WINDOW_LIST[my_app_slot].local_click_x;
                int rel_y = IPC_WINDOW_LIST[my_app_slot].local_click_y;
                ultimo_x = rel_x;
                ultimo_y = rel_y;
                mouse_hold_timer = 2;
                debounce_timer = 12;
                events_process_mouse(rel_x, rel_y, 1, 0);
                if (GUI_ProcessMouseClick(&MyApp, rel_x, rel_y)) {
                    precisa_redesenhar = true;
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
        
        if (precisa_redesenhar) {
            Flush_Grafico_Janela();
        }
        
        sys_sleep(euTenhoFocoJanelaReal ? 16 : 32);
    }
    
    sys_exit();
    return 0;
}
