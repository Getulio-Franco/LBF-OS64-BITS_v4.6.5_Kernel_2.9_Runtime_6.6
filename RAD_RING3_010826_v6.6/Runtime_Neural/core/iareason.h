#ifndef IAREASON_H
#define IAREASON_H
/* ============================================================================
* IAREASON - CORE DE RACIOCÍNIO EM CADEIA (Portátil, volátil)
* Interpreta sequências: "tenho 10, dei 3, dobrei" -> passos -> resultado.
* ============================================================================ */
#include <stdint.h>
#include <stddef.h>

#define REASON_MAX_PASSOS 8
#define REASON_TAM_TRACE  256

/* LAYOUT CONGELADO (ABI) - a camada de interação espelha esta struct */
typedef struct {
    float resultado_final;
    int   num_passos;
    int   valido;
    char  trace[REASON_TAM_TRACE];
} ReasonResult;

/* Retorna 1 se conseguiu raciocinar (valido), 0 caso contrário */
int iareason_processar(const char* texto, ReasonResult* out);
/* Detecta se o texto tem padrão de cadeia (nº inicial + operações) */
int iareason_pode(const char* texto);

#endif // IAREASON_H
