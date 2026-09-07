#include "ialogic.h"

// Banco de Pesos Ajustáveis via Backpropagation/Treinamento
const PesosMLP BancoDeDadosPortas[7] = {
    { 0.5f,  0.5f, -0.7f,   0.0f,  0.0f,  0.0f,   1.0f,  0.0f, -0.5f }, // AND
    { 0.5f,  0.5f, -0.2f,   0.0f,  0.0f,  0.0f,   1.0f,  0.0f, -0.5f }, // OR
    {-1.0f,  0.0f,  0.5f,   0.0f,  0.0f,  0.0f,   1.0f,  0.0f, -0.5f }, // NOT
    {-0.5f, -0.5f,  0.7f,   0.0f,  0.0f,  0.0f,   1.0f,  0.0f, -0.5f }, // NAND
    {-0.5f, -0.5f,  0.2f,   0.0f,  0.0f,  0.0f,   1.0f,  0.0f, -0.5f }, // NOR
    { 1.0f,  1.0f, -0.5f,   1.0f,  1.0f, -1.5f,   1.0f, -2.0f, -0.5f }, // XOR
    { 1.0f,  1.0f, -0.5f,   1.0f,  1.0f, -1.5f,  -2.0f,  1.5f,  0.7f }  // XNOR
};

/**
 * Função de ativação degrau (step function)
 * @param soma Valor acumulado da soma ponderada
 * @return 1 se soma > 0, caso contrário 0
 */
static int mlp_ativar(float soma) {
    return (soma > 0.0f) ? 1 : 0;
}

int mlp_processar(const PesosMLP* p, float x1, float x2) {
    // Validação de ponteiro nulo
    if (!p) return -1;
    
    // Camada oculta 1 (sempre ativa)
    float soma_h1 = (x1 * p->h1_w1) + (x2 * p->h1_w2) + p->h1_bias;
    int out_h1 = mlp_ativar(soma_h1);
    
    // Camada oculta 2 (opcional - só ativa se tiver pesos diferentes de zero)
    int out_h2 = 0;
    if (p->h2_w1 != 0.0f || p->h2_w2 != 0.0f || p->h2_bias != 0.0f) {
        float soma_h2 = (x1 * p->h2_w1) + (x2 * p->h2_w2) + p->h2_bias;
        out_h2 = mlp_ativar(soma_h2);
    }
    
    // Camada de saída
    float soma_out = ((float)out_h1 * p->out_w1) + ((float)out_h2 * p->out_w2) + p->out_bias;
    
    return mlp_ativar(soma_out);
}
