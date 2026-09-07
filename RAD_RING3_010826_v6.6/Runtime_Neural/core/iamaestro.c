#include "iamaestro.h"
#include "iaemotion.h"
#include <stddef.h>

static int g_rot = 0;
static int g_ultima_perguntou = 0;

static const char* AB_NEUTRAS[] = {"", "Vamos la: ", "Entao: "};
static const char* AB_FELIZ[]   = {"Claro! ", "Com prazer! ", "Boa pergunta! "};
static const char* AB_TRISTE[]  = {"", "Hmm, vamos ver... "};
static const char* FECHOS[] = {
    "Quer saber mais algum detalhe?",
    "Posso ajudar com mais alguma coisa?",
    "Quer que eu mostre um exemplo?",
    "Ficou claro, ou quer que eu explique de outro jeito?"
};

const char* iamaestro_abertura(void) {
    int h = iaemotion_humor();
    const char** lista; int n;
    if (h >= 70)      { lista = AB_FELIZ;   n = 3; }
    else if (h < 45)  { lista = AB_TRISTE;  n = 2; }
    else              { lista = AB_NEUTRAS; n = 3; }
    g_rot++;
    return lista[g_rot % n];
}

const char* iamaestro_fecho(void) {
    if (g_ultima_perguntou) return NULL;   // regra de ouro: sem interrogatório
    g_rot++;
    return FECHOS[g_rot % 4];
}

void iamaestro_registra(int perguntou) {
    g_ultima_perguntou = perguntou;
}
