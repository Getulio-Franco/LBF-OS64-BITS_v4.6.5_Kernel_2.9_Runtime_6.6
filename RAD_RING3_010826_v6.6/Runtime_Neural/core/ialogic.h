#ifndef IALOGIC_H
#define IALOGIC_H

/**
 * Estrutura de pesos para uma MLP (Multi-Layer Perceptron) de 2 camadas
 * Suporta até 2 neurônios ocultos + 1 neurônio de saída
 */
typedef struct {
    // Camada oculta 1
    float h1_w1, h1_w2, h1_bias;
    
    // Camada oculta 2 (opcional - pode ser desativada com zeros)
    float h2_w1, h2_w2, h2_bias;
    
    // Camada de saída
    float out_w1, out_w2, out_bias;
} PesosMLP;

// Banco de pesos pré-treinados para portas lógicas
extern const PesosMLP BancoDeDadosPortas[7];

/**
 * Processa a propagação direta (forward pass) na MLP
 * @param p Ponteiro para a estrutura de pesos
 * @param x1 Primeira entrada (0.0f ou 1.0f)
 * @param x2 Segunda entrada (0.0f ou 1.0f)
 * @return Saída binária (0 ou 1) ou -1 em caso de erro
 */
int mlp_processar(const PesosMLP* p, float x1, float x2);

#endif // IALOGIC_H
