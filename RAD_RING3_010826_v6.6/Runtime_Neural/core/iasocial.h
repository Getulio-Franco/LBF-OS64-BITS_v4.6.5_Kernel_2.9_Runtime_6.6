#ifndef IASOCIAL_H
#define IASOCIAL_H
/* ============================================================================
* IASOCIAL - CORE LIVRO DE CONVÍVIO (Portátil)
* Small talk contextual: saudações, bem-estar, despedidas, empatia,
* educação e continuidade. Respostas variam por HUMOR dela, CONTEXTO
* da conversa, e o estado do usuário pode contagiar o humor dela.
* Gatilhos curtos (<=4 letras) casam só como palavra inteira.
* ============================================================================ */
#include <stdint.h>
#include <stddef.h>

#define SOC_FIXA     0  // texto único
#define SOC_HUMOR    1  // [0] humor<50 | [1] 50-79 | [2] >=80
#define SOC_CONTEXTO 2  // [0] fazendo contas | [1] lógica | [2] geral

typedef struct {
    const char* gatilhos[4];  // sinônimos que disparam (NULL termina)
    int tipo;
    int efeito;               // 0 nenhum | EMOC_CONTAGIO | EMOC_EMPATIA
    const char* textos[4];    // variantes conforme o tipo
    const char* continua;     // pergunta de volta p/ conversa não morrer
} EntradaSocial;

int iasocial_consultar(const char* texto, char* out, int out_len);
int iasocial_contar(void);

#endif // IASOCIAL_H
