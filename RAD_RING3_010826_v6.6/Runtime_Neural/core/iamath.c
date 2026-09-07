#include "iamath.h"

// Matriz de Embeddings treinável/rebalanceável
static const float MATRIZ_EMBEDDING_MATH[4][4] = {
    { 1.00f,  0.00f,  0.00f,  0.00f}, // Soma
    { 0.00f,  1.00f,  0.00f,  0.00f}, // Subtração
    { 0.00f,  0.00f,  1.00f,  0.00f}, // Multiplicação
    { 0.00f,  0.00f,  0.00f,  1.00f}  // Divisão
};

// Matriz de Pesos Synapses (Podem ser alterados no treinamento)
static const float PESOS_CLASSIFICADOR_MATH[4][4] = {
    { 1.50f, -0.50f, -0.50f, -0.50f}, // Neurônio 0: Soma
    {-0.50f,  1.50f, -0.50f, -0.50f}, // Neurônio 1: Subtração
    {-0.50f, -0.50f,  1.50f, -0.50f}, // Neurônio 2: Multiplicação
    {-0.50f, -0.50f, -0.50f,  1.50f}  // Neurônio 3: Divisão
};

const NeuronioMatematico BancoDeDadosMatematico[4] = {
    { 1.0f,  1.0f,  0.0f, 0}, // ID 0: Soma
    { 1.0f, -1.0f,  0.0f, 0}, // ID 1: Subtração
    { 0.0f,  0.0f,  0.0f, 1}, // ID 2: Multiplicação
    { 0.0f,  0.0f,  0.0f, 2}  // ID 3: Divisão
};

float iamath_processar(const NeuronioMatematico* modelo, float x1, float x2) {
    if (!modelo) return 0.0f;
    
    switch (modelo->tipo_operacao) {
        case 0: // Soma ponderada
            return (x1 * modelo->peso_x1) + (x2 * modelo->peso_x2) + modelo->bias;
            
        case 1: // Multiplicação
            return x1 * x2;
            
        case 2: // Divisão
            if (x2 == 0.0f) {
                return 0.0f; // Retorna 0 em vez de NaN/Inf
            }
            return x1 / x2;
            
        default:
            return 0.0f; // Operação inválida
    }
}

int iamath_classificar_intencao(const float* vetor_acumulado) {
    if (!vetor_acumulado) return -1;
    
    int id_escolhido = 0;
    float maior_ativacao = -9999.0f;
    
    for (int p = 0; p < 4; p++) {
        float ativacao = 0.0f;
        for (int d = 0; d < 4; d++) {
            ativacao += vetor_acumulado[d] * PESOS_CLASSIFICADOR_MATH[p][d];
        }
        
        if (ativacao > maior_ativacao) {
            maior_ativacao = ativacao;
            id_escolhido = p;
        }
    }
    
    return id_escolhido;
}
