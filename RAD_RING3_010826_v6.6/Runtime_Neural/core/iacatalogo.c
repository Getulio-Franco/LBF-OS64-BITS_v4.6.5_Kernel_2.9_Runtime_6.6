#include "iacatalogo.h"
#include "iafichas.h"
#include <string.h>

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

static void num_str(int v, char* buf) {
    int i = 0;
    char tmp[12]; int t = 0;
    if (v == 0) tmp[t++] = '0';
    while (v > 0 && t < 11) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
    for (int k = t - 1; k >= 0; k--) buf[i++] = tmp[k];
    buf[i] = '\0';
}

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

static int casar_alias(const char* q, int m) {
    for (int i = 0; i < 3; i++) {
        const char* a = iafichas_modulo_alias(m, i);
        if (!a) break;
        if (strlen(a) <= 4) { if (palavra_inteira(q, a)) return 1; }
        else if (strstr(q, a)) return 1;
    }
    return 0;
}

/* Descreve um livro: nome, nº de fichas, capítulos (famílias) e exemplos */
static void descreve_modulo(int m, char* out, int cap, int detalhe) {
    const Ficha* F = iafichas_modulo_fichas(m);
    int n = iafichas_modulo_tam(m);
    char num[12];

    cat_lim(out, cap, "'"); cat_lim(out, cap, iafichas_modulo_nome(m)); cat_lim(out, cap, "'");
    num_str(n, num);
    cat_lim(out, cap, " ("); cat_lim(out, cap, num); cat_lim(out, cap, " fichas)");

    /* capítulos = famílias distintas, na ordem em que aparecem */
    char caps[8][32]; int ncaps = 0;
    for (int i = 0; i < n && ncaps < 8; i++) {
        const char* fam = F[i].familia;
        if (!fam) continue;
        int dup = 0;
        for (int c = 0; c < ncaps; c++) if (strcmp(caps[c], fam) == 0) { dup = 1; break; }
        if (!dup) { strncpy(caps[ncaps], fam, 31); caps[ncaps][31] = '\0'; ncaps++; }
    }
    if (ncaps) {
        cat_lim(out, cap, ", capitulos: ");
        for (int c = 0; c < ncaps; c++) {
            if (c) cat_lim(out, cap, ", ");
            cat_lim(out, cap, caps[c]);
        }
    }
    if (detalhe) {
        cat_lim(out, cap, ". Exemplos: ");
        int ne = 0;
        for (int i = 0; i < n && ne < 5; i++) {
            if (ne) cat_lim(out, cap, ", ");
            cat_lim(out, cap, F[i].nome);
            ne++;
        }
        cat_lim(out, cap, ".");
    }
}

int iacatalogo_consultar(const char* texto, char* out, int out_len) {
    if (!texto || !out || out_len <= 0) return -1;
    out[0] = '\0';

    char q[256];
    para_minusculas(texto, q, sizeof(q));
    int mods = iafichas_modulos();
    char num[12];

    /* --- visão geral da estante --- */
    int overview =
        strstr(q,"o que voce sabe falar")||strstr(q,"quais livros")||strstr(q,"seus livros")||
        strstr(q,"sua biblioteca")||strstr(q,"sobre o que voce fala")||strstr(q,"o que voce pode falar")||
        strstr(q,"quantos livros")||strstr(q,"descreva os livros")||strstr(q,"fale dos livros")||
        strstr(q,"sua estante");
    if (overview) {
        cat_lim(out, out_len, "Eu sei fazer contas, portas logicas, raciocinar, lembrar e conversar. E minha biblioteca tem ");
        num_str(mods, num);
        cat_lim(out, out_len, num);
        cat_lim(out, out_len, " livros: ");
        for (int m = 0; m < mods; m++) {
            if (m) cat_lim(out, out_len, " | ");
            num_str(m + 1, num);
            cat_lim(out, out_len, num);
            cat_lim(out, out_len, ") ");
            descreve_modulo(m, out, out_len, 0);
        }
        cat_lim(out, out_len, ". Pergunte 'o que tem no livro de <tema>'.");
        return 0;
    }

    /* --- detalhe de um livro (precisa da palavra "livro" + apelido) --- */
    if (strstr(q, "livro")) {
        for (int m = 0; m < mods; m++) {
            if (casar_alias(q, m)) {
                cat_lim(out, out_len, "O livro ");
                descreve_modulo(m, out, out_len, 1);
                cat_lim(out, out_len, " Quer comecar por onde?");
                return 0;
            }
        }
    }
    return -1;
}
