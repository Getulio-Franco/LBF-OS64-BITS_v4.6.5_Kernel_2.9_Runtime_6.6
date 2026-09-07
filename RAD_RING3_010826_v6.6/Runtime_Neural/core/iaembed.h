#ifndef IAEMBED_H
#define IAEMBED_H
/* ============================================================================
* IAEMBED - MINI-CÉREBRO SEMÂNTICO (Opção B | Portátil, sem SO)
* Vetores densos de 16 eixos semânticos interpretáveis.
* Sinônimos e conceitos próximos ficam próximos no espaço vetorial:
* "bolo" ~ "torta" (comida/festa), "capital" ~ "cidade" (lugar).
* O vocabulário é DADOS (iaembed_vocab dentro do .c): cresce sem tocar no motor.
* ============================================================================ */
#include <stdint.h>
#include <stddef.h>

#define EMB_DIM 16
/* Eixos: 0 quantidade | 1 matematica | 2 logica | 3 pessoa | 4 emocao |
* 5 comida | 6 festa | 7 tempo | 8 lugar | 9 objeto | 10 acao |
* 11 conhecimento | 12 memoria | 13 comunicacao | 14 natureza | 15 tecnologia */

int   iaembed_palavra(const char* palavra, float* out_vec); // 1 se conhecida
void  iaembed_frase(const char* texto, float* out_vec);     // média + norma L2
float iaembed_cosseno(const float* a, const float* b);
float iaembed_similaridade_texto(const char* a, const char* b); // 0..1
int   iaembed_vocab_tam(void);

#endif // IAEMBED_H
