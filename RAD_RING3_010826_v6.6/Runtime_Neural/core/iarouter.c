#include "iarouter.h"
#include "iaembed.h"
#include "../system/string.h"

#define TAM_VETOR 3

typedef struct {
    const char* palavra;
    float v[TAM_VETOR];
} Embed;

/* Dicionário exato (sinal forte) — mantido */
static const Embed DICIONARIO_ROUTER[] = {
    {"soma", {0.9f,0.0f,0.8f}}, {"some", {0.9f,0.0f,0.8f}},
    {"somar", {0.9f,0.0f,0.8f}}, {"mais", {0.9f,0.0f,0.8f}},
    {"adicione", {0.9f,0.0f,0.8f}},
    {"subtraia", {0.9f,0.0f,0.8f}}, {"subtra", {0.9f,0.0f,0.8f}},
    {"subtrair", {0.9f,0.0f,0.8f}}, {"menos", {0.9f,0.0f,0.8f}},
    {"multiplique", {0.9f,0.0f,0.8f}}, {"multiplicar", {0.9f,0.0f,0.8f}},
    {"vezes", {0.9f,0.0f,0.8f}},
    {"divida", {0.9f,0.0f,0.8f}}, {"dividir", {0.9f,0.0f,0.8f}},
    {"and", {0.0f,0.9f,0.8f}}, {"or", {0.0f,0.9f,0.8f}},
    {"not", {0.0f,0.9f,0.8f}}, {"nand", {0.0f,0.9f,0.8f}},
    {"nor", {0.0f,0.9f,0.8f}}, {"xor", {0.0f,0.9f,0.8f}},
    {"xnor", {0.0f,0.9f,0.8f}}
};
#define TAM_DICIONARIO (sizeof(DICIONARIO_ROUTER) / sizeof(DICIONARIO_ROUTER[0]))

/* Pesos MUTÁVEIS (ialearn ajusta online) */
static float PESOS_INTENCAO[2][TAM_VETOR] = {
    { 1.5f, -0.8f,  0.2f}, // Neurônio Math
    {-0.8f,  1.5f,  0.2f}  // Neurônio Logic
};
static float BIAS_INTENCAO[2] = {-0.5f, -0.5f};

static void extrair_vetor_frase(const char* texto, float* out) {
    for (int d = 0; d < TAM_VETOR; d++) out[d] = 0.0f;
    if (!texto) return;
    size_t len = strlen(texto);
    size_t i = 0;
    while (i < len) {
        while (i < len && texto[i] == ' ') i++;
        char tok[32]; int t = 0;
        while (i < len && texto[i] != ' ' && t < 31) {
            char c = texto[i];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
                tok[t++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
            i++;
        }
        tok[t] = '\0';
        if (t == 0) continue;
        for (size_t k = 0; k < TAM_DICIONARIO; k++) {
            if (strcmp(tok, DICIONARIO_ROUTER[k].palavra) == 0) {
                for (int d = 0; d < TAM_VETOR; d++) out[d] += DICIONARIO_ROUTER[k].v[d];
                break;
            }
        }
    }
}

/* --- B3: âncoras semânticas (iaembed) --- */
static const char* ANC_MATH[]  = {"soma","some","subtraia","menos","multiplique","divida","conta","calculo"};
static const char* ANC_LOGIC[] = {"and","or","not","xor","logica","porta","bit","verdadeiro"};
#define N_ANC_MATH  8
#define N_ANC_LOGIC 8

static float score_semantico(const char* texto, const char** ancs, int n) {
    float vt[EMB_DIM];
    iaembed_frase(texto, vt);
    float melhor = 0.0f;
    for (int i = 0; i < n; i++) {
        float va[EMB_DIM];
        iaembed_frase(ancs[i], va);
        float s = iaembed_cosseno(vt, va);
        if (s > melhor) melhor = s;
    }
    return melhor;
}

RouterDecision iarouter_processar(const char* texto_usuario) {
    RouterDecision decisao;
    decisao.acionar_math = 0;
    decisao.acionar_logic = 0;
    decisao.comando_limpo[0] = '\0';
    if (!texto_usuario) return decisao;

    float vetor_frase[TAM_VETOR];
    extrair_vetor_frase(texto_usuario, vetor_frase);

    /* --- B3: dicionário mudo? Consulta o mini-cérebro --- */
    int tem_palavra = 0;
    for (int d = 0; d < TAM_VETOR; d++) if (vetor_frase[d] != 0.0f) tem_palavra = 1;
    if (!tem_palavra) {
        float sm = score_semantico(texto_usuario, ANC_MATH, N_ANC_MATH);
        float sl = score_semantico(texto_usuario, ANC_LOGIC, N_ANC_LOGIC);
        if (sm > 0.5f && sm >= sl) {
            vetor_frase[0] = 0.9f; vetor_frase[2] = 0.8f; // protótipo MATH
        } else if (sl > 0.5f) {
            vetor_frase[1] = 0.9f; vetor_frase[2] = 0.8f; // protótipo LOGIC
        }
    }

    /* Forward Pass (pesos treinados pelo ialearn continuam valendo) */
    float ativacoes[2] = {0.0f, 0.0f};
    for (int neur = 0; neur < 2; neur++) {
        float soma = BIAS_INTENCAO[neur];
        for (int d = 0; d < TAM_VETOR; d++) soma += vetor_frase[d] * PESOS_INTENCAO[neur][d];
        ativacoes[neur] = (soma > 0.0f) ? soma : 0.0f;
    }

    if (ativacoes[0] > ativacoes[1] && ativacoes[0] > 0.0f) decisao.acionar_math = 1;
    else if (ativacoes[1] > ativacoes[0] && ativacoes[1] > 0.0f) decisao.acionar_logic = 1;

    strncpy(decisao.comando_limpo, texto_usuario, 63);
    decisao.comando_limpo[63] = '\0';
    return decisao;
}

/* Treino online (ialearn) — sem alteração */
static float clamp_peso(float v) {
    if (v > 3.0f) return 3.0f;
    if (v < -3.0f) return -3.0f;
    return v;
}

void iarouter_treinar(const char* texto, int delta_math, int delta_logic, float taxa) {
    float v[TAM_VETOR];
    extrair_vetor_frase(texto, v);
    int tem = 0;
    for (int d = 0; d < TAM_VETOR; d++) if (v[d] != 0.0f) tem = 1;
    if (!tem) return;
    for (int d = 0; d < TAM_VETOR; d++) {
        PESOS_INTENCAO[0][d] = clamp_peso(PESOS_INTENCAO[0][d] + taxa * (float)delta_math * v[d]);
        PESOS_INTENCAO[1][d] = clamp_peso(PESOS_INTENCAO[1][d] + taxa * (float)delta_logic * v[d]);
    }
    BIAS_INTENCAO[0] = clamp_peso(BIAS_INTENCAO[0] + taxa * (float)delta_math * 0.2f);
    BIAS_INTENCAO[1] = clamp_peso(BIAS_INTENCAO[1] + taxa * (float)delta_logic * 0.2f);
}
