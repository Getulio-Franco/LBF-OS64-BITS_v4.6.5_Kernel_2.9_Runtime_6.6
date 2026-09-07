#ifndef IAMATH_H
#define IAMATH_H

typedef struct {
    float peso_x1;
    float peso_x2;
    float bias;
    int tipo_operacao;  // 0: soma, 1: sub, 2: mult, 3: div
} NeuronioMatematico;

// Torna o banco de dados visível para o neural.c
extern const NeuronioMatematico BancoDeDadosMatematico[4];

float iamath_processar(const NeuronioMatematico* modelo, float x1, float x2);
int iamath_classificar_intencao(const float* vetor_acumulado_4d);

#endif
