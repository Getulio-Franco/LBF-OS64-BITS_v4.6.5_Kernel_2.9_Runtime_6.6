#include "iapattern.h"
#include <string.h>

static int g_cont[PATTERN_CATS] = {0, 0, 0, 0, 0, 0};
static int g_total = 0;

static void pat_num_str(int v, char* buf) {
    int i = 0;
    char tmp[12]; int t = 0;
    if (v == 0) tmp[t++] = '0';
    while (v > 0 && t < 11) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
    for (int k = t - 1; k >= 0; k--) buf[i++] = tmp[k];
    buf[i] = '\0';
}

void iapattern_registrar(int categoria) {
    if (categoria < 0 || categoria >= PATTERN_CATS) return;
    g_cont[categoria]++;
    g_total++;
}

int iapattern_total(void) { return g_total; }

int iapattern_dominante(void) {
    if (g_total < 3) return -1;
    int melhor = 0;
    for (int i = 1; i < PATTERN_CATS; i++)
        if (g_cont[i] > g_cont[melhor]) melhor = i;
    return melhor;
}

void iapattern_observacao(char* out) {
    if (!out) return;
    if (g_total < 5) {
        strcpy(out, "Ainda estou aprendendo seus habitos. Converse mais comigo!");
        return;
    }
    int d = iapattern_dominante();
    int pct = (g_cont[d] * 100) / g_total;
    char num[12];

    if (d < 0 || pct < 40) {
        strcpy(out, "Voce e bastante variado: nao tenho um padrao dominante ainda.");
        return;
    }

    pat_num_str(pct, num);
    strcpy(out, "Notei que voce ");
    switch (d) {
        case 0: strcat(out, "gosta de calculos ("); break;
        case 1: strcat(out, "gosta de portas logicas ("); break;
        case 2: strcat(out, "e curioso e faz muitas perguntas ("); break;
        case 3: strcat(out, "gosta de guardar lembrancas ("); break;
        case 4: strcat(out, "gosta de conversar e se conectar ("); break;
        case 5:
        default: strcat(out, "gosta de problemas de raciocinio ("); break;
    }
    strcat(out, num);
    strcat(out, "% das interacoes).");
}
