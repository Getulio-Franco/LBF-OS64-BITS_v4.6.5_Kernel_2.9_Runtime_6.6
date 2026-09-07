#include "iasocial.h"
#include "iaemotion.h"
#include "iadialog.h"
#include <string.h>

extern const EntradaSocial LIVRO_SOCIAL[];
extern const int LIVRO_SOCIAL_TAM;

static void cat_lim(char* dst, int cap, const char* src) {
    if (!src) return;
    int i = (int)strlen(dst);
    int j = 0;
    while (src[j] && i < cap - 1) dst[i++] = src[j++];
    dst[i] = '\0';
}

static void para_minusculas(const char* src, char* dst, int cap) {
    int i = 0;
    for (; src[i] && i < cap - 1; i++) {
        char c = src[i];
        dst[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    dst[i] = '\0';
}

/* Gatilho curto: só palavra inteira ("sim" não casa com "assim") */
static int palavra_inteira(const char* q, const char* g) {
    int gl = (int)strlen(g);
    const char* p = q;
    while ((p = strstr(p, g)) != NULL) {
        int antes_ok  = (p == q) || (*(p - 1) == ' ');
        const char* fim = p + gl;
        int depois_ok = (*fim == '\0') || (*fim == ' ') || (*fim == '?') ||
                        (*fim == '!') || (*fim == ',') || (*fim == '.');
        if (antes_ok && depois_ok) return 1;
        p = fim;
    }
    return 0;
}

static int casar_gatilho(const char* q, const char* g) {
    if (!g) return 0;
    if (strlen(g) <= 4) return palavra_inteira(q, g);
    return strstr(q, g) != NULL;
}

int iasocial_consultar(const char* texto, char* out, int out_len) {
    if (!texto || !out || out_len <= 0) return -1;
    out[0] = '\0';

    char q[256];
    para_minusculas(texto, q, sizeof(q));

    for (int i = 0; i < LIVRO_SOCIAL_TAM; i++) {
        const EntradaSocial* e = &LIVRO_SOCIAL[i];
        int casou = 0;
        for (int g = 0; g < 4 && e->gatilhos[g]; g++) {
            if (casar_gatilho(q, e->gatilhos[g])) { casou = 1; break; }
        }
        if (!casou) continue;

        /* O estado do usuário contagia o humor dela */
        if (e->efeito) iaemotion_evento(e->efeito);

        const char* txt;
        if (e->tipo == SOC_HUMOR) {
            int h = iaemotion_humor();
            txt = (h < 50) ? e->textos[0] : (h < 80) ? e->textos[1] : e->textos[2];
        } else if (e->tipo == SOC_CONTEXTO) {
            if (iadialog_tem_resultado() && iadialog_foi_logic()) txt = e->textos[1];
            else if (iadialog_tem_resultado())                    txt = e->textos[0];
            else                                                  txt = e->textos[2];
        } else {
            txt = e->textos[0];
        }

        cat_lim(out, out_len, txt);
        if (e->continua) {
            cat_lim(out, out_len, " ");
            cat_lim(out, out_len, e->continua);
        }
        return 0;
    }
    return -1;
}

int iasocial_contar(void) { return LIVRO_SOCIAL_TAM; }
