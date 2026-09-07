#ifndef IAKNOWLEDGE_H
#define IAKNOWLEDGE_H
/* ============================================================================
* IAKNOWLEDGE - CORE DE CONHECIMENTO DE MUNDO (Portável, volátil)
* Pares pergunta→resposta com recall por similaridade de embeddings.
* Inclui fatos-semente e permite aprender novos via camada de interação.
* ============================================================================ */
#include <stdint.h>
#include <stddef.h>

#define KNOW_MAX_SLOTS 64
#define KNOW_TAM_PERG  64
#define KNOW_TAM_RESP  96
#define KNOW_TAM_VETOR 16    // 16 — casa com iaembed_frase!
#define KNOW_LIMIAR    0.55f 

typedef struct {
    float embedding[KNOW_TAM_VETOR];
    char  pergunta[KNOW_TAM_PERG];
    char  resposta[KNOW_TAM_RESP];
    int   ativo;
} FatSlot;

int  iaknowledge_aprender(const char* pergunta, const char* resposta);
int  iaknowledge_consultar(const char* pergunta, char* out_resposta);
int  iaknowledge_contar(void);
void iaknowledge_limpar(void);

#endif // IAKNOWLEDGE_H
