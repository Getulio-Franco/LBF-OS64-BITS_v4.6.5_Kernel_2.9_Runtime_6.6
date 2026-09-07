#include "iaknowledge.h"
#include "iaembed.h"
#include <string.h>

static FatSlot g_fatos[KNOW_MAX_SLOTS];
static int g_inicializado = 0;

/* sqrt sem libm (Newton) - mantém o core 100% portável */
static float raiz_quadrada(float x) {
    if (x <= 0.0f) return 0.0f;
    float r = x;
    for (int i = 0; i < 25; i++) r = 0.5f * (r + x / r);
    return r;
}

/* Embedding semântico real: agora vem do mini-cérebro */
static void gerar_embedding(const char* texto, float* out) {
    iaembed_frase(texto, out);
}

/* Embedding com normalização L2: similaridade = cosseno (idêntico = 1.0) */
/*static void gerar_embedding(const char* texto, float* out) {
    for (int i = 0; i < KNOW_TAM_VETOR; i++) out[i] = 0.0f;
    if (!texto) return;
    for (int i = 0; texto[i] != '\0'; i++) {
        char c = texto[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c >= 'a' && c <= 'z') out[c & 7] += 1.0f;
    }
    float soma2 = 0.0f;
    for (int i = 0; i < KNOW_TAM_VETOR; i++) soma2 += out[i] * out[i];
    if (soma2 > 0.0f) {
        float norm = raiz_quadrada(soma2);
        for (int i = 0; i < KNOW_TAM_VETOR; i++) out[i] /= norm;
    }
}*/

static float similaridade(const float* a, const float* b) {
    float soma = 0.0f;
    for (int i = 0; i < KNOW_TAM_VETOR; i++) soma += a[i] * b[i];
    return soma;
}

static int buscar_melhor(const float* emb, float* out_score) {
    int melhor = -1;
    float melhor_score = 0.0f;
    for (int i = 0; i < KNOW_MAX_SLOTS; i++) {
        if (!g_fatos[i].ativo) continue;
        float s = similaridade(emb, g_fatos[i].embedding);
        if (s > melhor_score) { melhor_score = s; melhor = i; }
    }
    if (out_score) *out_score = melhor_score;
    return melhor;
}

/* Fatos-semente (sem acento, p/ casar com a digitação do usuário) */
static void garantir_seeds(void) {
    if (g_inicializado) return;
    g_inicializado = 1;
    iaknowledge_aprender("qual a capital do brasil", "A capital do Brasil e Brasilia.");
    iaknowledge_aprender("quem foi santos dumont", "Santos Dumont foi o pai da aviacao, inventor do 14-Bis.");
    iaknowledge_aprender("qual o maior planeta do sistema solar", "O maior planeta do sistema solar e Jupiter.");
    iaknowledge_aprender("onde fica a torre eiffel", "A Torre Eiffel fica em Paris, na Franca.");
    iaknowledge_aprender("quem criou a existencia", "Nasci em 2012 em Delphi 7 e renasci em 2026 no NeuralRuntime. Meu criador me deu este nome em homenagem a primeira versao.");
}

int iaknowledge_aprender(const char* pergunta, const char* resposta) {
    if (!pergunta || !pergunta[0] || !resposta || !resposta[0]) return -1;

    float emb[KNOW_TAM_VETOR];
    gerar_embedding(pergunta, emb);

    /* Pergunta quase igual? Atualiza a resposta (cosseno > 0.92) */
    float score;
    int existente = buscar_melhor(emb, &score);
    if (existente >= 0 && score > 0.99f) {
        strncpy(g_fatos[existente].resposta, resposta, KNOW_TAM_RESP - 1);
        g_fatos[existente].resposta[KNOW_TAM_RESP - 1] = '\0';
        return existente;
    }

    /* Procura slot livre */
    for (int i = 0; i < KNOW_MAX_SLOTS; i++) {
        if (!g_fatos[i].ativo) {
            for (int d = 0; d < KNOW_TAM_VETOR; d++) g_fatos[i].embedding[d] = emb[d];
            strncpy(g_fatos[i].pergunta, pergunta, KNOW_TAM_PERG - 1);
            g_fatos[i].pergunta[KNOW_TAM_PERG - 1] = '\0';
            strncpy(g_fatos[i].resposta, resposta, KNOW_TAM_RESP - 1);
            g_fatos[i].resposta[KNOW_TAM_RESP - 1] = '\0';
            g_fatos[i].ativo = 1;
            return i;
        }
    }
    return -1; // base cheia
}

int iaknowledge_consultar(const char* pergunta, char* out_resposta) {
    if (!pergunta || !out_resposta) return -1;
    garantir_seeds();

    float emb[KNOW_TAM_VETOR];
    gerar_embedding(pergunta, emb);
    float score;
    int idx = buscar_melhor(emb, &score);
    if (idx < 0 || score < KNOW_LIMIAR) return -1;

    strncpy(out_resposta, g_fatos[idx].resposta, KNOW_TAM_RESP - 1);
    out_resposta[KNOW_TAM_RESP - 1] = '\0';
    return idx;
}

int iaknowledge_contar(void) {
    garantir_seeds();
    int n = 0;
    for (int i = 0; i < KNOW_MAX_SLOTS; i++)
        if (g_fatos[i].ativo) n++;
    return n;
}

void iaknowledge_limpar(void) {
    for (int i = 0; i < KNOW_MAX_SLOTS; i++) g_fatos[i].ativo = 0;
    g_inicializado = 0;
}
