#ifndef IAMAESTRO_H
#define IAMAESTRO_H
/* ============================================================================
* IAMAESTRO - CAMADA DE CONTROLE DE RESPOSTAS (Portátil)
* Balanceia a interação: aberturas variadas por humor, fechos de
* continuidade com rotação, e anti-repetição (nunca duas perguntas
* seguidas). Chamado diretamente pelos cores conversacionais.
* ============================================================================ */
#include <stdint.h>
#include <stddef.h>

const char* iamaestro_abertura(void);          // "" | "Claro! " | "Boa pergunta! "...
const char* iamaestro_fecho(void);             // NULL (se a anterior perguntou) | pergunta
void        iamaestro_registra(int perguntou); // o core avisa se a resposta terminou em pergunta

#endif // IAMAESTRO_H
