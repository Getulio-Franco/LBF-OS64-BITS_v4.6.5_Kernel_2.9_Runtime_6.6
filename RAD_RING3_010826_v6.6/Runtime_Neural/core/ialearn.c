#include "ialearn.h"
#include "iarouter.h"
#include <string.h>

static char g_ultimo_texto[128];
static int  g_tem_decisao = 0;
static int  g_ultimo_math = 0;
static int  g_ultimo_logic = 0;
static int  g_total = 0;

#define TAXA_APRENDIZADO 0.1f

void ialearn_registrar(const char* texto, int acionar_math, int acionar_logic) {
    if (!texto) return;
    strncpy(g_ultimo_texto, texto, 127);
    g_ultimo_texto[127] = '\0';
    g_ultimo_math = acionar_math;
    g_ultimo_logic = acionar_logic;
    g_tem_decisao = 1;
}

int ialearn_feedback(int positivo) {
    if (!g_tem_decisao) return 0; // ninguém esperando feedback

    if (positivo) {
        /* Recompensa: reforça o neurônio vencedor */
        iarouter_treinar(g_ultimo_texto,
                         g_ultimo_math ? 1 : 0,
                         g_ultimo_logic ? 1 : 0,
                         TAXA_APRENDIZADO);
    } else {
        /* Punição: enfraquece o escolhido, fortalece o alternativo */
        int dm = g_ultimo_math ? -1 : 1;
        int dl = g_ultimo_logic ? -1 : 1;
        iarouter_treinar(g_ultimo_texto, dm, dl, TAXA_APRENDIZADO);
    }
    g_total++;
    g_tem_decisao = 0; // feedback consumido
    return 1;
}

int ialearn_total(void) {
    return g_total;
}
