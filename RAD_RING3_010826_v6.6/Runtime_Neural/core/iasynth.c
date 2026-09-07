#include "iasynth.h"
#include <string.h> 

// Pesos de Tom/Contexto
static const float PESOS_CONTEXTO[2][3] = {
    { 1.0f,  0.5f,  0.8f}, // Conversacional
    {-1.0f, -0.8f, -0.5f}  // Direto
};

static const float BIAS_CONTEXTO[2] = {-1.0f, 0.5f};

static int determinar_estilo(const char* prompt) {
    if (!prompt) return 0;
    
    float features[3] = {0.0f, 0.0f, 1.0f}; 
    
    // Detecção simples de palavras-chave
    if (strstr(prompt, "por favor") || strstr(prompt, "pode")) {
        features[0] = 1.0f;
    }
    if (strstr(prompt, "calcule") || strstr(prompt, "resultado")) {
        features[1] = 1.0f;
    }

    float ativacoes[2] = {0.0f, 0.0f};
    for (int i = 0; i < 2; i++) {
        float soma = BIAS_CONTEXTO[i];
        for (int j = 0; j < 3; j++) {
            soma += features[j] * PESOS_CONTEXTO[i][j];
        }
        ativacoes[i] = (soma > 0) ? soma : 0;
    }

    return (ativacoes[1] > ativacoes[0]) ? 1 : 0;
}

void iasynth_gerar_resposta(const char* prompt_original, const char* raw_result, char* buffer_saida) {
    // Validação rigorosa para evitar crash
    if (!prompt_original || !raw_result || !buffer_saida) return;

    // Inicializa o buffer para garantir string vazia segura
    buffer_saida[0] = '\0';

    int estilo = determinar_estilo(prompt_original);
    size_t max_len = 255; // Limite de segurança para o buffer

    if (estilo == 1) {
        // Estilo Direto
        strncpy(buffer_saida, "Resultado: ", max_len);
        buffer_saida[max_len] = '\0'; // Garante terminação
        
        // Calcula espaço restante
        size_t current_len = strlen(buffer_saida);
        if (current_len < max_len) {
            strncat(buffer_saida, raw_result, max_len - current_len);
        }
    } else {
        // Estilo Conversacional
        strncpy(buffer_saida, "O valor é ", max_len);
        buffer_saida[max_len] = '\0';
        
        size_t current_len = strlen(buffer_saida);
        if (current_len < max_len) {
            strncat(buffer_saida, raw_result, max_len - current_len);
        }
    }
}
