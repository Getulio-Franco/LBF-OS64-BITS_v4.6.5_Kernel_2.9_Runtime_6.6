/* ============================================================================
EXISTENCIA.C - Assistente Neural v10.0
(Persona + Reason + Pattern | Portavel, sem dependencia de SO)
Em memória da primeira assistente pessoal "Existência" (Delphi 7, 2012).
"Nenhum software se faz de um dia para outro, mas alguns nunca deveriam
ser perdidos."
============================================================================ */
#include "Runtime_sdk/sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"
#include "../system/sysutils.h"
#include "Runtime_Neural/system_lib/neurallib.h"
#include "Runtime_sdk/components/TOS_IPC.h"

void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;
extern void GUI_Memo_AddStr(TGUIControl* memo, const char* str);
extern void GUI_Memo_Clear(TGUIControl* memo);

int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth = 600;
const int winHeight = 450;

TGUIControl* EditPrompt  = NULL;
TGUIControl* BtnEnviar   = NULL;
TGUIControl* BtnLimpar   = NULL;
TGUIControl* ChatMemo    = NULL;
TGUIControl* LabelStatus = NULL;

/* ABI WIRE FORMAT v1 (congelado) */
typedef struct {
    int acionar_math;
    int acionar_logic;
    char comando_limpo[64];
} ABI_RouterDecision;

/* ABI WIRE FORMAT do Reason (espelha ReasonResult do iareason.h) */
typedef struct {
    float resultado_final;
    int num_passos;
    int valido;
    char trace[256];
} ABI_ReasonResult;

/* ============================================================================
* DETECÇÃO DE INTENÇÕES (camada de interação)
* ============================================================================ */
static int detectar_intencao_memoria(const char* t) {
    if (strstr(t, "meu nome e") || strstr(t, "me chamo") || strstr(t, "lembre que") || strstr(t, "anota") || strstr(t, "guarda que")) return 1;
    if (strstr(t, "esqueca") || strstr(t, "esquece")) return 3;
    if (strstr(t, "o que voce lembra") || strstr(t, "quantas lembrancas")) return 4;
    if (strstr(t, "meu nome") || strstr(t, "lembra") || strstr(t, "o que eu disse")) return 2;
    return 0;
}

/* ANTES do knowledge: protege "quem e voce" */
static int detectar_intencao_persona(const char* t) {
    if (strstr(t, "quem e voce") || strstr(t, "me conte sobre voce") || strstr(t, "qual seu nome") ||
        strstr(t, "seu nome") || strstr(t, "cor favorita") || strstr(t, "quantos anos voce tem") ||
        strstr(t, "voce gosta de mim") || strstr(t, "qual seu proposito") || strstr(t, "para que voce existe") ||
        strstr(t, "voce tem sentimentos") || strstr(t, "voce sonha") || strstr(t, "voce tem medo")) return 1;
    return 0;
}

/* 1 = aprender | 2 = consultar (agora com compreensão semântica de paráfrases!) */
static int detectar_intencao_conhecimento(const char* t) {
    if (strstr(t, "aprenda:")) return 1;
    if (strstr(t, "o que e") || strstr(t, "quem foi") || strstr(t, "quem criou") ||
        strstr(t, "qual a") || strstr(t, "qual o") || strstr(t, "onde fica") ||
        strstr(t, "quando foi")) return 2;
        
    /* Opção B: se a frase "parece" com alguma âncora de conhecimento, consulta */
    static const char* ANC[] = {
        "qual a capital do brasil",
        "quem foi santos dumont",
        "onde fica a torre eiffel",
        "qual o maior planeta do sistema solar"
    };
    for (int i = 0; i < 4; i++) {
        if (sys_ia_embed_sim(t, ANC[i]) > 500) return 2; // cosseno > 0.5
    }
    return 0;
}

static int detectar_emocao(const char* t) {
    if (strstr(t, "obrigad") || strstr(t, "valeu") || strstr(t, "isso mesmo") ||
        strstr(t, "muito bem") || strstr(t, "boa menina") || strstr(t, "te amo")) return 0;
    if (strstr(t, "errado") || strstr(t, "nao foi isso") || strstr(t, "burra") ||
        strstr(t, "que pena") || strstr(t, "nao gosto")) return 1;
    if (strstr(t, "como voce esta") || strstr(t, "como esta voce") ||
        strstr(t, "tudo bem com voce")) return 3;
    return -1;
}

static int detectar_intencao_padrao(const char* t) {
    if (strstr(t, "o que voce nota") || strstr(t, "qual meu padrao") ||
        strstr(t, "meus habitos") || strstr(t, "o que voce percebe")) return 1;
    return 0;
}

static int token_eh_numero(const char* tok) {
    if (!tok || !*tok) return 0;
    char c = tok[0];
    if (c == '-' || c == '+') c = tok[1];
    return (c >= '0' && c <= '9') || c == '.' || c == ',';
}

static int detectar_op_math(const char* t) {
    if (strstr(t, "soma")  || strstr(t, "some")  || strstr(t, "mais"))   return 0;
    if (strstr(t, "subtra")|| strstr(t, "menos"))                       return 1;
    if (strstr(t, "multipl")|| strstr(t, "vezes"))                      return 2;
    if (strstr(t, "divida")|| strstr(t, "dividir"))                     return 3;
    return 0;
}

static int detectar_porta_logic(const char* t) {
    if (strstr(t, "xnor")) return 6;
    if (strstr(t, "nand")) return 3;
    if (strstr(t, "xor"))  return 5;
    if (strstr(t, "nor"))  return 4;
    if (strstr(t, "and"))  return 0;
    if (strstr(t, "not"))  return 2;
    if (strstr(t, "or"))   return 1;
    return 0;
}

static void Set_Status(const char* msg) {
    if (LabelStatus) GUI_Edit_SetText(LabelStatus, (char*)msg);
}

static void Set_Status_Humor(const char* msg) {
    char buf[80];
    char num[16];
    IntToStr(sys_ia_emotion_humor(), num);
    strcpy(buf, msg);
    strcat(buf, " | Humor: ");
    strcat(buf, num);
    strcat(buf, "%");
    Set_Status(buf);
}

/* ============================================================================
* PROCESSAMENTO v10:
* Memory -> Persona -> Knowledge -> Emotion/Learn -> Pattern -> Reason -> Router
* ============================================================================ */
/* ============================================================================
 ASSISTENTE_PROCESSAR - Camada de interação (Existência)
 Ordem de prioridade (regra de ouro):
   1. Memória         (o que é pessoal vem primeiro)
   2. Social          (livro de convívio: saudações, empatia)
   3. Emoção/Learn    (educação: obrigado, errou, como voce esta)
   4. Biblioteca      (fichas: C, História...)
   5. Bibliotecária   (descreve os livros)
   6. Persona         (quem é ela)
   7. Knowledge       (fatos do mundo / aprenda:)
   8. Pattern         (observa o usuário)
   9. Reason          (raciocínio em cadeia)
  10. Router → Math/Logic → Synth (conta e portas)
 Cada bloco responde e dá return; o primeiro que casar vence.
============================================================================ */
static void Assistente_Processar(const char* texto) {
    /* --- buffers estáticos únicos (sem duplicatas mascaradas) --- */
    static char linha[300];
    static char raw_result[32];
    static char resp[300];
    static char lembranca[64];
    static char fato[128];
    static char resp_p[160];
    static char obs[160];
    static ABI_ReasonResult rr;

    /* eco da mensagem do usuário */
    strcpy(linha, "Voce: ");
    strncat(linha, texto, 200);
    strcat(linha, "\n");
    GUI_Memo_AddStr(ChatMemo, linha);

    /* ============================================================
       1. CORE MEMORY — lembrar / esquecer / contar
       ============================================================ */
    int intencao_mem = detectar_intencao_memoria(texto);
    if (intencao_mem != 0) {
        if (intencao_mem == 1) {
            sys_ia_memory_store(texto);
            sys_ia_emotion_event(2);
            sys_ia_pattern_reg(3);
            GUI_Memo_AddStr(ChatMemo, "Existencia: Guardado na minha memoria!\n\n");
            Set_Status_Humor("Core Memory: lembranca armazenada (volatil).");
        }
        else if (intencao_mem == 2) {
            memset(lembranca, 0, sizeof(lembranca));
            if (sys_ia_memory_recall(texto, lembranca) >= 0) {
                sys_ia_emotion_event(2);
                sys_ia_pattern_reg(3);
                strcpy(resp, "Existencia: Eu lembro. Voce disse: '");
                strncat(resp, lembranca, 200);
                strcat(resp, "'\n\n");
                GUI_Memo_AddStr(ChatMemo, resp);
                Set_Status_Humor("Core Memory: lembranca recuperada.");
            } else {
                GUI_Memo_AddStr(ChatMemo, "Existencia: Ainda nao lembro disso.\n\n");
                Set_Status_Humor("Core Memory: sem correspondencia.");
            }
        }
        else if (intencao_mem == 3) {
            if (sys_ia_memory_forget(texto) >= 0) {
                GUI_Memo_AddStr(ChatMemo, "Existencia: Esquecido, como preferir.\n\n");
                Set_Status_Humor("Core Memory: esquecimento.");
            } else {
                GUI_Memo_AddStr(ChatMemo, "Existencia: Nao encontrei isso para esquecer.\n\n");
                Set_Status_Humor("Core Memory: nada para esquecer.");
            }
            sys_ia_pattern_reg(3);
        }
        else { /* intencao_mem == 4 (contagem) */
            char num[16];
            IntToStr(sys_ia_memory_count(), num);
            strcpy(resp, "Existencia: Tenho ");
            strcat(resp, num);
            strcat(resp, " lembrancas nesta sessao.\n\n");
            GUI_Memo_AddStr(ChatMemo, resp);
            sys_ia_pattern_reg(3);
            Set_Status_Humor("Core Memory: contagem.");
        }
        return;
    }

    /* ============================================================
       2. CORE SOCIAL — livro de convívio (saudações, empatia)
       ============================================================ */
    {
        static char soc_resp[256];
        memset(soc_resp, 0, sizeof(soc_resp));
        if (sys_ia_social_consultar(texto, soc_resp, sizeof(soc_resp)) >= 0) {
            strcpy(linha, "Existencia: ");
            strncat(linha, soc_resp, 240);
            strcat(linha, "\n\n");
            GUI_Memo_AddStr(ChatMemo, linha);
            Set_Status_Humor("Core Social: convivio.");
            return;
        }
    }

    /* ============================================================
       3. CORE EMOTION + LEARN — educação (obrigado, errado, como está)
          ANTES do saber, para não ser roubado pelo Knowledge.
       ============================================================ */
    int emoc = detectar_emocao(texto);
    if (emoc == 0 || emoc == 1) {
        int humor_novo = sys_ia_emotion_event(emoc);
        int treinou    = sys_ia_learn_feedback(emoc == 0 ? 1 : 0);
        sys_ia_pattern_reg(4);

        if (emoc == 0) {
            GUI_Memo_AddStr(ChatMemo, "Existencia: De nada! Fico muito feliz em ajudar!\n");
        } else {
            GUI_Memo_AddStr(ChatMemo, "Existencia: Poxa... vou me esforcar para fazer melhor da proxima vez.\n");
        }
        if (treinou) {
            GUI_Memo_AddStr(ChatMemo, "Existencia: (ajustei meus roteamentos com o seu feedback.)\n");
        }
        GUI_Memo_AddStr(ChatMemo, "\n");

        char num[16], buf[100];
        IntToStr(humor_novo, num);
        strcpy(buf, "Emotion: humor ");
        strcat(buf, num);
        strcat(buf, "%");
        if (treinou) strcat(buf, " | Learn: pesos ajustados.");
        Set_Status(buf);
        return;
    }
    if (emoc == 3) {
        char frase[64];
        memset(frase, 0, sizeof(frase));
        sys_ia_emotion_frase(frase);
        sys_ia_pattern_reg(4);
        strcpy(linha, "Existencia: ");
        strncat(linha, frase, 200);
        strcat(linha, " E voce, como esta?\n\n");
        GUI_Memo_AddStr(ChatMemo, linha);
        Set_Status_Humor("Core Emotion: estado consultado.");
        return;
    }

    /* ============================================================
       4. CORE BIBLIOTECA — fichas (C, História, futuros livros)
       ============================================================ */
    {
        static char ficha_resp[512];
        memset(ficha_resp, 0, sizeof(ficha_resp));
        int strat = sys_ia_fichas_consultar(texto, ficha_resp, sizeof(ficha_resp));
        if (strat >= 0) {
            strcpy(linha, "Existencia: ");
            strncat(linha, ficha_resp, 400);
            strcat(linha, "\n\n");
            GUI_Memo_AddStr(ChatMemo, linha);
            Set_Status_Humor("Core Biblioteca: ficha consultada.");
            return;
        }
    }

    /* ============================================================
       5. CORE CATALOGO — a Bibliotecária (descreve os livros)
       ============================================================ */
    {
        static char cat_resp[512];
        memset(cat_resp, 0, sizeof(cat_resp));
        if (sys_ia_catalogo_consultar(texto, cat_resp, sizeof(cat_resp)) >= 0) {
            strcpy(linha, "Existencia: ");
            strncat(linha, cat_resp, 400);
            strcat(linha, "\n\n");
            GUI_Memo_AddStr(ChatMemo, linha);
            Set_Status_Humor("Core Catalogo: bibliotecaria.");
            return;
        }
    }

    /* ============================================================
       6. CORE PERSONA — autoconsciência
       ============================================================ */
    if (detectar_intencao_persona(texto)) {
        memset(resp_p, 0, sizeof(resp_p));
        if (sys_ia_persona_ask(texto, resp_p, sizeof(resp_p))) {
            sys_ia_emotion_event(2);
            sys_ia_pattern_reg(4);
            strcpy(linha, "Existencia: ");
            strncat(linha, resp_p, 240);
            strcat(linha, "\n\n");
            GUI_Memo_AddStr(ChatMemo, linha);
            Set_Status_Humor("Core Persona: autoconsciencia.");
        } else {
            GUI_Memo_AddStr(ChatMemo, "Existencia: Ainda nao sei responder sobre isso.\n\n");
        }
        return;
    }

    /* ============================================================
       7. CORE KNOWLEDGE — fatos do mundo / aprenda: pergunta = resposta
       ============================================================ */
    int intencao_con = detectar_intencao_conhecimento(texto);
    if (intencao_con == 1) {
        char work[256];
        strncpy(work, texto, 255); work[255] = '\0';
        char* eq = strchr(work, '=');
        if (eq) {
            *eq = '\0';
            char* perg = work;
            char* pfx  = strstr(perg, "aprenda:");
            if (pfx) perg = pfx + 8;
            while (*perg == ' ') perg++;
            char* resp_k = eq + 1;
            while (*resp_k == ' ') resp_k++;
            if (perg[0] && resp_k[0]) {
                sys_ia_knowledge_learn(perg, resp_k);
                sys_ia_emotion_event(2);
                sys_ia_pattern_reg(2);
                GUI_Memo_AddStr(ChatMemo, "Existencia: Aprendido! Pode me perguntar.\n\n");
                Set_Status_Humor("Core Knowledge: fato aprendido.");
            } else {
                GUI_Memo_AddStr(ChatMemo, "Existencia: Formato: 'aprenda: pergunta = resposta'.\n\n");
            }
        } else {
            GUI_Memo_AddStr(ChatMemo, "Existencia: Formato: 'aprenda: pergunta = resposta'.\n\n");
        }
        return;
    }
    if (intencao_con == 2) {
        memset(fato, 0, sizeof(fato));
        if (sys_ia_knowledge_ask(texto, fato) >= 0) {
            sys_ia_emotion_event(2);
            sys_ia_pattern_reg(2);
            strcpy(linha, "Existencia: ");
            strncat(linha, fato, 240);
            strcat(linha, "\n\n");
            GUI_Memo_AddStr(ChatMemo, linha);
            Set_Status_Humor("Core Knowledge: fato recuperado.");
        } else {
            GUI_Memo_AddStr(ChatMemo, "Existencia: Ainda nao sei isso. Me ensine: 'aprenda: pergunta = resposta'.\n\n");
            Set_Status_Humor("Core Knowledge: sem correspondencia.");
        }
        return;
    }

    /* ============================================================
       8. CORE PATTERN — observação sobre o usuário
       ============================================================ */
    if (detectar_intencao_padrao(texto)) {
        memset(obs, 0, sizeof(obs));
        sys_ia_pattern_obs(obs);
        strcpy(linha, "Existencia: ");
        strncat(linha, obs, 240);
        strcat(linha, "\n\n");
        GUI_Memo_AddStr(ChatMemo, linha);
        Set_Status_Humor("Core Pattern: observacao.");
        return;
    }

    /* ============================================================
       9. CORE REASON — raciocínio em cadeia
       ============================================================ */
    if (sys_ia_reason_can(texto)) {
        memset(&rr, 0, sizeof(rr));
        if (sys_ia_reason(texto, &rr) && rr.valido) {
            sys_ia_pattern_reg(5);
            sys_ia_emotion_event(2);
            GUI_Memo_AddStr(ChatMemo, "Existencia: [raciocinando] ");
            GUI_Memo_AddStr(ChatMemo, rr.trace);
            GUI_Memo_AddStr(ChatMemo, "\n");
            FloatToStr(rr.resultado_final, raw_result);
            strcpy(linha, "Existencia: Resposta final: ");
            strcat(linha, raw_result);
            strcat(linha, "\n\n");
            GUI_Memo_AddStr(ChatMemo, linha);
            sys_ia_dialog_reg_result(rr.resultado_final, 0, 0);
            Set_Status_Humor("Core Reason: cadeia concluida.");
        } else {
            GUI_Memo_AddStr(ChatMemo, "Existencia: Nao consegui concluir esse raciocinio.\n\n");
        }
        return;
    }

    /* ============================================================
       10. ROUTER → MATH / LOGIC → SYNTH
       ============================================================ */
    ABI_RouterDecision dec;
    memset(&dec, 0, sizeof(dec));
    int status = sys_ia_router(texto, &dec);
    if (status != 0 || (dec.acionar_math == 0 && dec.acionar_logic == 0)) {
        GUI_Memo_AddStr(ChatMemo, "Existencia: Nao entendi. Tente 'soma 2 3', 'quem e voce' ou 'tenho 10, dei 3, dobrei'.\n\n");
        Set_Status_Humor("Roteador: nenhuma intencao ativada.");
        return;
    }

    /* 10.1 PARSE dos números */
    float nums[2] = {0.0f, 0.0f};
    int n_nums = 0;
    char work[160];
    strncpy(work, texto, 159); work[159] = '\0';
    char* tok = strtok(work, " ");
    while (tok && n_nums < 2) {
        if (token_eh_numero(tok)) nums[n_nums++] = StrToFloat(tok);
        tok = strtok(NULL, " ");
    }

    /* 10.2 Diálogo: continuação com 1 operando ("e mais 3") */
    int continuacao = 0;
    if (n_nums == 1 && dec.acionar_math && sys_ia_dialog_tem_result()) {
        nums[1] = nums[0];
        nums[0] = sys_ia_dialog_ultimo_result();
        continuacao = 1;
    }
    if (n_nums < 2 && !continuacao) {
        GUI_Memo_AddStr(ChatMemo, "Existencia: Preciso de dois numeros. Ex: 'soma 2 3'.\n\n");
        Set_Status_Humor("Faltaram operandos.");
        return;
    }

    /* 10.3 Execução */
    if (dec.acionar_math) {
        int op  = detectar_op_math(texto);
        float r = sys_ia_math(op, nums[0], nums[1]);
        FloatToStr(r, raw_result);
        sys_ia_dialog_reg_result(r, op, 0);
        sys_ia_emotion_event(2);
        sys_ia_pattern_reg(0);
        Set_Status_Humor(continuacao ? "Core Dialog: continuacao aplicada." : "Roteado para: CORE MATH");
    } else {
        int porta = detectar_porta_logic(texto);
        int r     = sys_ia_logic(porta, nums[0], nums[1]);
        IntToStr(r, raw_result);
        sys_ia_dialog_reg_result((float)r, porta, 1);
        sys_ia_emotion_event(2);
        sys_ia_pattern_reg(1);
        Set_Status_Humor("Roteado para: CORE LOGIC");
    }
    sys_ia_learn_registrar(texto, dec.acionar_math, dec.acionar_logic);

    /* 10.4 Synth (montagem da resposta final) */
    char resposta[256];
    memset(resposta, 0, sizeof(resposta));
    sys_ia_synth(texto, raw_result, resposta);

    strcpy(linha, "Existencia: ");
    strncat(linha, resposta, 240);
    strcat(linha, "\n\n");
    GUI_Memo_AddStr(ChatMemo, linha);
}

/* ============================================================================
* ENVIO E BOTÕES
* ============================================================================ */
static void Assistente_Enviar(void) {
    char* texto = GUI_Edit_GetText(EditPrompt);
    if (!texto || texto[0] == '\0') return;
    Assistente_Processar(texto);
    GUI_Edit_SetText(EditPrompt, "");
    g_focused_control = (void*)EditPrompt;
    gui_set_prop(EditPrompt, PROP_SET_FOCUS, 1);
}

void OnBtnEnviarClick(void* sender) { Assistente_Enviar(); }

void OnBtnLimparClick(void* sender) {
    GUI_Memo_Clear(ChatMemo);
    sys_ia_dialog_reset();
    GUI_Memo_AddStr(ChatMemo, "Existencia: Conversa limpa. Ola, tudo bem! Em que posso ajudar?\n\n");
    Set_Status_Humor("Memo limpo.");
}

/* ============================================================================
* IPC / JANELA (específica do LBF OS)
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
* MAIN
* ============================================================================ */
int main(int argc, char* argv[]) {
    static int ultimo_x = 0;
    static int ultimo_y = 0;
    static int mouse_hold_timer = 0;
    static int debounce_timer = 0;
    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;
    void* ultimo_controle_focado = NULL;

    graphics_init_app(winWidth, winHeight);
    wm_init();

    my_app_slot = OS_IPC_RegisterApp("Existencia Neural ELF", winWidth, winHeight);
    if (my_app_slot == -1) return -1;

    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Existencia v10.0 - Edicao Portavel", winWidth, winHeight);

    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0xC0C0C0);

    GUI_CreateLabel(&MyApp, 10, 42, "Prompt:");
    EditPrompt = GUI_CreateEdit(&MyApp, 70, 38, 420, 25, "ola existencia", NULL);
    BtnEnviar  = GUI_CreateButton(&MyApp, 500, 38, 90, 25, "ENVIAR", OnBtnEnviarClick);
    BtnLimpar  = GUI_CreateButton(&MyApp, 500, 410, 90, 25, "LIMPAR", OnBtnLimparClick);

    ChatMemo = GUI_CreateMemo(&MyApp, 10, 75, 580, 330);
    gui_set_prop(ChatMemo, PROP_COLOR, 0x000000);

    LabelStatus = GUI_CreateLabel(&MyApp, 10, 415, "Existencia pronta");

    GUI_Memo_AddStr(ChatMemo, "--- EXISTENCIA v10.0 (Persona+Reason+Pattern) ---\n");
    GUI_Memo_AddStr(ChatMemo, "Math/Logic/Memoria/Saber/Dialogo/Emocao/Aprendizado\n");
    GUI_Memo_AddStr(ChatMemo, "Persona: 'quem e voce' | 'qual sua cor favorita'\n");
    GUI_Memo_AddStr(ChatMemo, "Raciocinio: 'tenho 10, dei 3, dobrei'\n");
    GUI_Memo_AddStr(ChatMemo, "Padrao: 'o que voce nota em mim'\n");
    GUI_Memo_AddStr(ChatMemo, "Digite e pressione ENTER.\n\n");

    char frase_ini[64];
    memset(frase_ini, 0, sizeof(frase_ini));
    sys_ia_emotion_frase(frase_ini);
    GUI_Memo_AddStr(ChatMemo, "Existencia: ");
    GUI_Memo_AddStr(ChatMemo, frase_ini);
    GUI_Memo_AddStr(ChatMemo, " Em que posso ajudar?\n\n");
//Testar_Embed(); // teste aqui -----------------
    g_focused_control = (void*)EditPrompt;
    ultimo_controle_focado = (void*)EditPrompt;
    gui_set_prop(EditPrompt, PROP_SET_FOCUS, 1);

    Flush_Grafico_Janela();

    while(1) {
        if (IPC_WINDOW_LIST[my_app_slot].is_active == 0) {
            Tratar_Fechamento_Software();
            break;
        }

        bool precisa_redesenhar = false;

        if (primeiro_desenho) { primeiro_desenho = false; precisa_redesenhar = true; }

        bool euTenhoFoco = (IPC_CONTROL->active_focus_slot == my_app_slot);
        if (MyApp.MainWindow) ((TForm*)MyApp.MainWindow)->ActiveFocus = euTenhoFoco;
        if (euTenhoFoco != ultimo_estado_foco) { ultimo_estado_foco = euTenhoFoco; precisa_redesenhar = true; }

        if (g_focused_control != NULL) {
            ultimo_controle_focado = g_focused_control;
        } else if (ultimo_controle_focado != NULL) {
            g_focused_control = ultimo_controle_focado;
            gui_set_prop((TGUIControl*)ultimo_controle_focado, PROP_SET_FOCUS, 1);
        }

        if (euTenhoFoco) {
            char key = Obter_Tecla_Entrada();
            if (key != 0) {
                if (key == 13) Assistente_Enviar();
                else GUI_ProcessKeyboard(&MyApp, key);
                precisa_redesenhar = true;
            }

            if (debounce_timer > 0) debounce_timer--;

            if (IPC_WINDOW_LIST[my_app_slot].has_click_event == 1) {
                if (mouse_hold_timer == 0 && debounce_timer == 0) {
                    int rel_x = IPC_WINDOW_LIST[my_app_slot].local_click_x;
                    int rel_y = IPC_WINDOW_LIST[my_app_slot].local_click_y;
                    ultimo_x = rel_x; ultimo_y = rel_y;
                    mouse_hold_timer = 2; debounce_timer = 12;
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
                if (mouse_hold_timer == 0) { events_process_mouse(ultimo_x, ultimo_y, 0, 0); precisa_redesenhar = true; }
            }
        }

        if (precisa_redesenhar) Flush_Grafico_Janela();
        sys_sleep(euTenhoFoco ? 16 : 32);
    }

    sys_exit();
    return 0;
}
