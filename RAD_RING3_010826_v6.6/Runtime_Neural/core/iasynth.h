#ifndef IASYNTH_H
#define IASYNTH_H

#include <stddef.h>

/**
 * Tipos de resposta sintetizada
 */
typedef enum {
    SYNTH_CONVERSACIONAL = 0,  // Tom amigável, natural
    SYNTH_DIRETO = 1,          // Tom técnico, robótico
    SYNTH_ERRO = 2             // Resposta de erro
} SynthStyle;

/**
 * Sintetiza a resposta final em texto formatado
 * @param prompt_original Prompt original do usuário
 * @param raw_result Resultado bruto do processamento do core
 * @param buffer_saida Buffer para receber a resposta formatada
 */
void iasynth_gerar_resposta(const char* prompt_original, const char* raw_result, char* buffer_saida);

/**
 * Determina o estilo de resposta baseado no contexto
 * @param prompt_original Prompt do usuário para análise de contexto
 * @return Estilo de resposta (CONVERSACIONAL ou DIRETO)
 */
SynthStyle iasynth_determinar_estilo(const char* prompt_original);

#endif // IASYNTH_H
