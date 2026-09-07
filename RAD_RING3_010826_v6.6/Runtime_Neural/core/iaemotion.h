#ifndef IAEMOTION_H
#define IAEMOTION_H
/* ============================================================================
* IAEMOTION - CORE DE EMOÇÃO / PERSONALIDADE (Portátil, volátil)
* Estado emocional que evolui com a interação:
* elogios animam, críticas entristecem, conversar fortalece o vínculo.
* 100% independente de SO (RAM pura, C padrão).
* ============================================================================ */
#include <stdint.h>
#include <stddef.h>

#define EMOC_ELOGIO   0
#define EMOC_CRITICA  1
#define EMOC_NEUTRO   2
#define EMOC_CONTAGIO 3  // alegria do usuário anima ela
#define EMOC_EMPATIA  4  // tristeza do usuário: ela entristece e se aproxima

/* Aplica o evento emocional e retorna o novo humor (0..100) */
int  iaemotion_evento(int tipo);
/* Humor atual (0..100) */
int  iaemotion_humor(void);
/* Vínculo com o usuário (0..100) */
int  iaemotion_vinculo(void);
/* Frase que descreve o estado atual */
void iaemotion_frase_estado(char* out);
/* Reseta a personalidade */
void iaemotion_reset(void);

int iaemotion_interacoes(void);

#endif // IAEMOTION_H
