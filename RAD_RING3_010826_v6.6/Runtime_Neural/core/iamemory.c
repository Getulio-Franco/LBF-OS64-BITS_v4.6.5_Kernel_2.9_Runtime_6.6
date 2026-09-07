/* ============================================================================
* IAMEMORY.C - Core de Memória Volátil - NeuralRuntime Portável
* Nenhuma syscall, nenhum IO, nenhuma dependência de SO.
* Inclui apenas headers padrão: em SO próprio, o -Isystem redireciona
* <string.h> para a implementação interna; em Windows/Linux, usa a libc.
* ============================================================================ */
#include "iamemory.h"
#include "iaembed.h" 
#include <string.h>

static MemoriaSlot g_memoria[MEM_MAX_SLOTS];

static void gerar_embedding(const char* texto, float* out) {
    iaembed_frase(texto, out);
}

/* Embedding semântico: histograma normalizado de caracteres (8 bins) */
/*static void gerar_embedding(const char* texto, float* out) {
    for (int i = 0; i < MEM_TAM_VETOR; i++) out[i] = 0.0f;
    if (!texto) return;
    int total = 0;
    for (int i = 0; texto[i] != '\0'; i++) {
        char c = texto[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c >= 'a' && c <= 'z') { out[c & 7] += 1.0f; total++; }
    }
    if (total > 0)
        for (int i = 0; i < MEM_TAM_VETOR; i++) out[i] /= (float)total;
}*/

static float similaridade(const float* a, const float* b) {
    float soma = 0.0f;
    for (int i = 0; i < MEM_TAM_VETOR; i++) soma += a[i] * b[i];
    return soma;
}

static int buscar_melhor(const float* emb, float* out_score) {
    int melhor = -1;
    float melhor_score = 0.0f;
    for (int i = 0; i < MEM_MAX_SLOTS; i++) {
        if (!g_memoria[i].ativo) continue;
        float s = similaridade(emb, g_memoria[i].embedding);
        if (s > melhor_score) { melhor_score = s; melhor = i; }
    }
    if (out_score) *out_score = melhor_score;
    return melhor;
}

int iamemory_armazenar(const char* texto) {
    if (!texto || !texto[0]) return -1;

    float emb[MEM_TAM_VETOR];
    gerar_embedding(texto, emb);

    /* Memória quase idêntica? Reforça em vez de duplicar */
    float score;
    int existente = buscar_melhor(emb, &score);
    if (existente >= 0 && score > 0.6f) {
        g_memoria[existente].forca += 0.1f;
        if (g_memoria[existente].forca > 1.0f) g_memoria[existente].forca = 1.0f;
        return existente;
    }

    /* Procura slot livre */
    int slot = -1;
    for (int i = 0; i < MEM_MAX_SLOTS; i++)
        if (!g_memoria[i].ativo) { slot = i; break; }

    /* Cheio? Substitui a memória mais fraca (esquecimento natural) */
    if (slot == -1) {
        float menor = 9999.0f;
        for (int i = 0; i < MEM_MAX_SLOTS; i++)
            if (g_memoria[i].forca < menor) { menor = g_memoria[i].forca; slot = i; }
    }

    MemoriaSlot* m = &g_memoria[slot];
    for (int i = 0; i < MEM_TAM_VETOR; i++) m->embedding[i] = emb[i];
    strncpy(m->texto, texto, MEM_TAM_TEXTO - 1);
    m->texto[MEM_TAM_TEXTO - 1] = '\0';
    m->forca = 0.5f;
    m->ativo = 1;
    return slot;
}

int iamemory_recordar(const char* chave, char* out_texto) {
    if (!chave || !out_texto) return -1;
    float emb[MEM_TAM_VETOR];
    gerar_embedding(chave, emb);
    float score;
    int idx = buscar_melhor(emb, &score);
    if (idx < 0 || score < MEM_LIMIAR) return -1;

    /* Reforço Hebbiano: o que é lembrado, fortalece */
    g_memoria[idx].forca += 0.1f;
    if (g_memoria[idx].forca > 1.0f) g_memoria[idx].forca = 1.0f;

    strncpy(out_texto, g_memoria[idx].texto, MEM_TAM_TEXTO - 1);
    out_texto[MEM_TAM_TEXTO - 1] = '\0';
    return idx;
}

int iamemory_esquecer(const char* chave) {
    if (!chave) return -1;
    float emb[MEM_TAM_VETOR];
    gerar_embedding(chave, emb);
    float score;
    int idx = buscar_melhor(emb, &score);
    if (idx < 0 || score < MEM_LIMIAR) return -1;
    g_memoria[idx].ativo = 0;
    g_memoria[idx].forca = 0.0f;
    return idx;
}

int iamemory_contar(void) {
    int n = 0;
    for (int i = 0; i < MEM_MAX_SLOTS; i++)
        if (g_memoria[i].ativo) n++;
    return n;
}

void iamemory_decair(void) {
    for (int i = 0; i < MEM_MAX_SLOTS; i++) {
        if (!g_memoria[i].ativo) continue;
        g_memoria[i].forca *= 0.95f;
        if (g_memoria[i].forca < 0.05f) g_memoria[i].ativo = 0;
    }
}

void iamemory_limpar(void) {
    for (int i = 0; i < MEM_MAX_SLOTS; i++) {
        g_memoria[i].ativo = 0;
        g_memoria[i].forca = 0.0f;
        g_memoria[i].texto[0] = '\0';
    }
}

/* --- Serialização em buffer (abstração p/ camada de interação) ---
* Apenas memcpy: o core não sabe nem pergunta onde o buffer vai parar. */
int iamemory_serializar(void* buffer, int max_bytes) {
    int necessario = (int)(sizeof(MemoriaSlot) * MEM_MAX_SLOTS);
    if (!buffer) return necessario;          // consulta de tamanho
    if (max_bytes < necessario) return -1;
    memcpy(buffer, g_memoria, (size_t)necessario);
    return necessario;
}

int iamemory_carregar(const void* buffer, int tam_bytes) {
    int necessario = (int)(sizeof(MemoriaSlot) * MEM_MAX_SLOTS);
    if (!buffer || tam_bytes != necessario) return -1;
    memcpy(g_memoria, buffer, (size_t)necessario);
    return iamemory_contar();
}
