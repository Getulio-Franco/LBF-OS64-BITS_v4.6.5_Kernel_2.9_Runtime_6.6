#ifndef IAROUTER_H
#define IAROUTER_H

/**
 * Estrutura de decisão do roteador neural
 * Indica qual core deve processar a requisição
 */
typedef struct {
    int acionar_math;      // 1 se deve usar iamath, 0 caso contrário
    int acionar_logic;     // 1 se deve usar ialogic, 0 caso contrário
    char comando_limpo[64]; // Texto processado/limpo para o core
} RouterDecision;

/**
 * Processa o texto do usuário e decide qual núcleo deve responder
 * @param texto_usuario Texto de entrada do usuário
 * @return Estrutura RouterDecision com a decisão do roteador
 */
RouterDecision iarouter_processar(const char* texto_usuario);

/* Treino online: ajusta pesos p/ a frase (deltas -1/0/+1 por neurônio) */
void iarouter_treinar(const char* texto, int delta_math, int delta_logic, float taxa);

#endif // IAROUTER_H
