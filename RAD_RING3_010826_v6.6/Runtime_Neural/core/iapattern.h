#ifndef IAPATTERN_H
#define IAPATTERN_H
/* ============================================================================
* IAPATTERN - CORE DE DETECÇÃO DE PADRÕES DO USUÁRIO (Portátil)
* Conta categorias de uso e gera observações sobre os hábitos do usuário.
* ============================================================================ */
#include <stdint.h>
#include <stddef.h>

#define PATTERN_CAT_MATH      0
#define PATTERN_CAT_LOGIC     1
#define PATTERN_CAT_KNOWLEDGE 2
#define PATTERN_CAT_MEMORY    3
#define PATTERN_CAT_SOCIAL    4
#define PATTERN_CAT_REASON    5
#define PATTERN_CATS          6

void iapattern_registrar(int categoria);
int  iapattern_total(void);
int  iapattern_dominante(void);
void iapattern_observacao(char* out);

#endif // IAPATTERN_H
