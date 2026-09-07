#ifndef IACATALOGO_H
#define IACATALOGO_H
/* ============================================================================
* IACATALOGO - CORE BIBLIOTECÁRIA (Portátil)
* Varre a estante da iafichas e descreve os livros: visão geral
* ("quais livros voce tem?") e detalhe por livro com capítulos
* (famílias) e exemplos — tudo gerado dos próprios dados.
* ============================================================================ */
#include <stdint.h>
#include <stddef.h>

int iacatalogo_consultar(const char* texto, char* out, int out_len);

#endif // IACATALOGO_H
