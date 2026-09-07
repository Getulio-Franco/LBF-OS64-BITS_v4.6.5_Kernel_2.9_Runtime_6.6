#ifndef IAPERSONA_H
#define IAPERSONA_H
/* ============================================================================
* IAPERSONA - CORE DE PERSONALIDADE / AUTOCONSCIÊNCIA (Portátil)
* Responde perguntas sobre si mesma com coerência e história própria.
* ============================================================================ */
#include <stdint.h>
#include <stddef.h>

/* Retorna 1 se respondeu, 0 se não soube */
int iapersona_consultar(const char* pergunta, char* out_resposta, int out_len);

#endif // IAPERSONA_H
