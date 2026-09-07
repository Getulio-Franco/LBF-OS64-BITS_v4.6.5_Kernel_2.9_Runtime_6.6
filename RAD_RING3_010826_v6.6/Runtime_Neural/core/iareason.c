#include "iareason.h"
#include <string.h>

/* --- utilidades portáteis (sem libc printf) --- */
static void raz_num_str(float v, char* buf) {
    int i = 0;
    if (v < 0.0f) { buf[i++] = '-'; v = -v; }
    int int_part = (int)v;
    char tmp[16]; int t = 0;
    if (int_part == 0) tmp[t++] = '0';
    while (int_part > 0 && t < 15) { tmp[t++] = (char)('0' + (int_part % 10)); int_part /= 10; }
    for (int k = t - 1; k >= 0; k--) buf[i++] = tmp[k];
    int frac = (int)((v - (float)(int)v) * 100.0f + 0.5f);
    if (frac > 0 && frac < 100) {
        buf[i++] = '.';
        buf[i++] = (char)('0' + (frac / 10) % 10);
        buf[i++] = (char)('0' + frac % 10);
    }
    buf[i] = '\0';
}

static float raz_str_float(const char* t) {
    float v = 0.0f, frac = 0.0f, div = 1.0f;
    int neg = 0, i = 0;
    if (t[i] == '-') { neg = 1; i++; } else if (t[i] == '+') i++;
    while (t[i] >= '0' && t[i] <= '9') { v = v * 10.0f + (float)(t[i] - '0'); i++; }
    if (t[i] == '.' || t[i] == ',') {
        i++;
        while (t[i] >= '0' && t[i] <= '9') { frac = frac * 10.0f + (float)(t[i] - '0'); div *= 10.0f; i++; }
    }
    v += frac / div;
    return neg ? -v : v;
}

static int raz_eh_numero(const char* t) {
    if (!t || !*t) return 0;
    int i = 0;
    if (t[i] == '-' || t[i] == '+') i++;
    if (!(t[i] >= '0' && t[i] <= '9')) return 0;
    for (; t[i]; i++) {
        if (!(t[i] >= '0' && t[i] <= '9') && t[i] != '.' && t[i] != ',') return 0;
    }
    return 1;
}

static void trace_cat(char* trace, int tam, const char* pedaco) {
    int atual = (int)strlen(trace);
    int i = 0;
    while (pedaco[i] && atual < tam - 1) trace[atual++] = pedaco[i++];
    trace[atual] = '\0';
}

/* Classificação do token de operação: 0=nenhum 1=soma 2=sub 3=mult 4=div
* 5=mult implícita x2 6=div implícita /2 */
static int raz_tipo_op(const char* t) {
    if (!strcmp(t,"somei")||!strcmp(t,"some")||!strcmp(t,"mais")||
        !strcmp(t,"ganhei")||!strcmp(t,"comprei")||!strcmp(t,"adicionei")) return 1;
    if (!strcmp(t,"dei")||!strcmp(t,"gastei")||!strcmp(t,"perdi")||
        !strcmp(t,"subtrai")||!strcmp(t,"menos")||!strcmp(t,"tirei")) return 2;
    if (!strcmp(t,"multipliquei")||!strcmp(t,"multiplique")||!strcmp(t,"vezes")) return 3;
    if (!strcmp(t,"dobrei")||!strcmp(t,"dobro")||!strcmp(t,"duplicar")) return 5;
    if (!strcmp(t,"dividi")||!strcmp(t,"divida")) return 4;
    if (!strcmp(t,"metade")) return 6;
    return 0;
}

int iareason_processar(const char* texto, ReasonResult* out) {
    if (!texto || !out) return 0;
    out->resultado_final = 0.0f;
    out->num_passos = 0;
    out->valido = 0;
    out->trace[0] = '\0';

    /* Tokeniza (minúsculas, sem pontuação) */
    char toks[32][24];
    int ntok = 0;
    size_t len = strlen(texto), i = 0;
    while (i < len && ntok < 32) {
        while (i < len && texto[i] == ' ') i++;
        int t = 0;
        while (i < len && texto[i] != ' ' && t < 23) {
            char c = texto[i];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '.' || c == ',' || c == '-') {
                toks[ntok][t++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
            }
            i++;
        }
        if (t > 0) { toks[ntok][t] = '\0'; ntok++; }
    }

    /* Valor inicial: primeiro token numérico */
    int inicio = -1;
    for (int k = 0; k < ntok; k++) if (raz_eh_numero(toks[k])) { inicio = k; break; }
    if (inicio < 0) return 0;

    float valor = raz_str_float(toks[inicio]);
    char num[24];
    raz_num_str(valor, num);
    trace_cat(out->trace, REASON_TAM_TRACE, "Inicio: ");
    trace_cat(out->trace, REASON_TAM_TRACE, num);

    /* Percorre as operações em ordem */
    for (int k = inicio + 1; k < ntok && out->num_passos < REASON_MAX_PASSOS; k++) {
        int tipo = raz_tipo_op(toks[k]);
        if (tipo == 0) continue;

        if (tipo == 5) { valor *= 2.0f; }
        else if (tipo == 6) { if (valor != 0.0f) valor /= 2.0f; }
        else {
            /* precisa do próximo número */
            int j = k + 1;
            while (j < ntok && !raz_eh_numero(toks[j])) j++;
            if (j >= ntok) continue;
            float n = raz_str_float(toks[j]);
            if (tipo == 1) valor += n;
            else if (tipo == 2) valor -= n;
            else if (tipo == 3) valor *= n;
            else if (tipo == 4) { if (n != 0.0f) valor /= n; }
            k = j;
            trace_cat(out->trace, REASON_TAM_TRACE, " | ");
            trace_cat(out->trace, REASON_TAM_TRACE, toks[k > j ? j : k]);
            trace_cat(out->trace, REASON_TAM_TRACE, " ");
            raz_num_str(n, num);
            trace_cat(out->trace, REASON_TAM_TRACE, num);
        }
        if (tipo == 5 || tipo == 6) {
            trace_cat(out->trace, REASON_TAM_TRACE, " | ");
            trace_cat(out->trace, REASON_TAM_TRACE, (tipo == 5) ? "dobrei" : "metade");
        }
        trace_cat(out->trace, REASON_TAM_TRACE, " -> ");
        raz_num_str(valor, num);
        trace_cat(out->trace, REASON_TAM_TRACE, num);
        out->num_passos++;
    }

    out->resultado_final = valor;
    out->valido = (out->num_passos > 0) ? 1 : 0;
    return out->valido;
}

int iareason_pode(const char* texto) {
    ReasonResult tmp;
    return iareason_processar(texto, &tmp);
}
