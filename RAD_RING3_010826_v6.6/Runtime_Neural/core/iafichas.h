#ifndef IAFICHAS_H
#define IAFICHAS_H
/* ============================================================================
* IAFICHAS - CORE BIBLIOTECA (Opção 3 | Portátil)
* Módulos de fichas de conhecimento: nome, família, sinônimos, rótulos,
* atributos e texto. Estratégias genéricas: DEFINIR, RECEITA, RECOMENDAR,
* COMBINAR e ESCLARECER. Matching exato + semântico (iaembed).
* ============================================================================ */
#include <stdint.h>
#include <stddef.h>

typedef struct {
    const char* chave;
    const char* valor;
} FichaAtributo;

typedef struct {
    const char* nome;
    const char* familia;        // NULL se raiz
    const char* sinonimos[3];   // NULL termina
    const char* rotulos[4];     // NULL termina
    FichaAtributo atributos[4]; // chave NULL termina
    const char* texto;
} Ficha;

/* Módulo 1: Programação em C */
extern const Ficha FICHAS_C[];
extern const int FICHAS_C_TAM;

/* Retorna: 0 DEFINIR | 1 RECEITA | 2 RECOMENDAR | 3 COMBINAR | 4 ESCLARECER | -1 sem match */
int iafichas_consultar(const char* texto, char* out, int out_len);
int iafichas_contar(void);

/* Acesso ao catálogo (usado pela Bibliotecária) */
int          iafichas_modulos(void);
const char*  iafichas_modulo_nome(int m);
const char*  iafichas_modulo_alias(int m, int i);
const Ficha* iafichas_modulo_fichas(int m);
int          iafichas_modulo_tam(int m);

#endif // IAFICHAS_H
