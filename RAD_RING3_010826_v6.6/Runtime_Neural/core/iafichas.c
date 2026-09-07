#include "iafichas.h"
#include "iaembed.h"
#include <string.h>

#define FICH_LIMIAR  0.45f
#define FICH_CLAREZA 0.30f
#define MAX_MODULOS  4

extern const Ficha FICHAS_C[];    extern const int FICHAS_C_TAM;
extern const Ficha FICHAS_HIST[]; extern const int FICHAS_HIST_TAM;

/* --- Catálogo: 1 livro = 1 linha --- */
typedef struct {
    const char* nome;
    const char* aliases[3];
    const Ficha* fichas;
    int tam;
} ModuloLivro;

static ModuloLivro CATALOGO[MAX_MODULOS];
static int CAT_TAM = 0;

static void catalogo_init(void) {
    if (CAT_TAM) return;
    CATALOGO[CAT_TAM].nome = "programacao em C";
    CATALOGO[CAT_TAM].aliases[0] = "linguagem c";
    CATALOGO[CAT_TAM].aliases[1] = "programacao";
    CATALOGO[CAT_TAM].aliases[2] = "c";
    CATALOGO[CAT_TAM].fichas = FICHAS_C;
    CATALOGO[CAT_TAM].tam = FICHAS_C_TAM;
    CAT_TAM++;
    CATALOGO[CAT_TAM].nome = "historia da computacao";
    CATALOGO[CAT_TAM].aliases[0] = "historia";
    CATALOGO[CAT_TAM].aliases[1] = "turing";
    CATALOGO[CAT_TAM].aliases[2] = NULL;
    CATALOGO[CAT_TAM].fichas = FICHAS_HIST;
    CATALOGO[CAT_TAM].tam = FICHAS_HIST_TAM;
    CAT_TAM++;
}

/* --- Accessors usados pela Bibliotecária (iacatalogo) --- */
int          iafichas_modulos(void)             { catalogo_init(); return CAT_TAM; }
const char*  iafichas_modulo_nome(int m)        { catalogo_init(); return CATALOGO[m].nome; }
const char*  iafichas_modulo_alias(int m, int i){ catalogo_init(); return CATALOGO[m].aliases[i]; }
const Ficha* iafichas_modulo_fichas(int m)      { catalogo_init(); return CATALOGO[m].fichas; }
int          iafichas_modulo_tam(int m)         { catalogo_init(); return CATALOGO[m].tam; }

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

static float ficha_score(const Ficha* f, const char* q, const float* emb_q) {
    if (strstr(q, f->nome)) return 1.0f;
    for (int i = 0; i < 3 && f->sinonimos[i]; i++)
        if (strstr(q, f->sinonimos[i])) return 1.0f;
    float emb_f[EMB_DIM];
    iaembed_frase(f->nome, emb_f);
    return iaembed_cosseno(emb_q, emb_f);
}

/* Primeiro parente (filho ou irmão) — para o "Veja tambem" */
static const char* primeiro_parente(const Ficha* f) {
    for (int m = 0; m < CAT_TAM; m++) {
        for (int i = 0; i < CATALOGO[m].tam; i++) {
            const Ficha* g = &CATALOGO[m].fichas[i];
            if (g == f) continue;
            int parente = (g->familia && strcmp(g->familia, f->nome) == 0) ||
                          (f->familia && g->familia && strcmp(g->familia, f->familia) == 0);
            if (parente) return g->nome;
        }
    }
    return NULL;
}

int iafichas_consultar(const char* texto, char* out, int out_len) {
    if (!texto || !out || out_len <= 0) return -1;
    out[0] = '\0';
    catalogo_init();

    char q[256];
    para_minusculas(texto, q, sizeof(q));
    float emb_q[EMB_DIM];
    iaembed_frase(q, emb_q);

    int bm = -1, bi = -1, sm = -1, si = -1;
    float s_best = 0.0f, s_sec = 0.0f;
    for (int m = 0; m < CAT_TAM; m++) {
        for (int i = 0; i < CATALOGO[m].tam; i++) {
            float s = ficha_score(&CATALOGO[m].fichas[i], q, emb_q);
            if (s > s_best)      { s_sec = s_best; sm = bm; si = bi; s_best = s; bm = m; bi = i; }
            else if (s > s_sec)  { s_sec = s; sm = m; si = i; }
        }
    }
    if (bm < 0 || s_best < FICH_CLAREZA) return -1;

    /* ESCLARECER */
    if (s_best < FICH_LIMIAR) {
        cat_lim(out, out_len, "Voce quer saber sobre '");
        cat_lim(out, out_len, CATALOGO[bm].fichas[bi].nome);
        cat_lim(out, out_len, "'? Tente 'o que e ");
        cat_lim(out, out_len, CATALOGO[bm].fichas[bi].nome);
        cat_lim(out, out_len, "'.");
        return 4;
    }

    const Ficha* f = &CATALOGO[bm].fichas[bi];
    int quer_lista = strstr(q,"liste")||strstr(q,"quais")||strstr(q,"me mostre")||strstr(q,"temas");
    int quer_como  = strstr(q,"como")||strstr(q,"exemplo")||strstr(q,"sintaxe");
    /* COMBINAR só com comparação explícita (sem gatilho automático) */
    int quer_combo = strstr(q,"diferenca")||strstr(q,"versus")||strstr(q," vs ")||strstr(q," entre ");

    /* RECOMENDAR + pergunta direcionada */
    if (quer_lista) {
        cat_lim(out, out_len, "Sobre "); cat_lim(out, out_len, f->nome);
        cat_lim(out, out_len, " temos: ");
        int achou = 0;
        for (int m = 0; m < CAT_TAM; m++) {
            for (int i = 0; i < CATALOGO[m].tam; i++) {
                const Ficha* g = &CATALOGO[m].fichas[i];
                if (g == f) continue;
                int parente = (g->familia && strcmp(g->familia, f->nome) == 0) ||
                              (f->familia && g->familia && strcmp(g->familia, f->familia) == 0);
                if (parente) {
                    if (achou) cat_lim(out, out_len, ", ");
                    cat_lim(out, out_len, g->nome);
                    achou++;
                }
            }
        }
        if (achou) cat_lim(out, out_len, ". Sobre qual deles voce quer saber?");
        else       cat_lim(out, out_len, ": ainda poucas fichas. Tente 'o que e <tema>'.");
        return 2;
    }

    /* RECEITA agora vem ANTES do COMBINAR */
    if (quer_como) {
        cat_lim(out, out_len, f->nome); cat_lim(out, out_len, ":");
        int tem = 0;
        for (int i = 0; i < 4 && f->atributos[i].chave; i++) {
            cat_lim(out, out_len, " ");
            cat_lim(out, out_len, f->atributos[i].chave);
            cat_lim(out, out_len, ": ");
            cat_lim(out, out_len, f->atributos[i].valor);
            cat_lim(out, out_len, ".");
            tem = 1;
        }
        if (!tem) { cat_lim(out, out_len, " "); cat_lim(out, out_len, f->texto); }
        return 1;
    }

    /* COMBINAR + pergunta direcionada */
    if (quer_combo && sm >= 0 && s_sec >= FICH_LIMIAR) {
        const Ficha* g = &CATALOGO[sm].fichas[si];
        cat_lim(out, out_len, f->nome); cat_lim(out, out_len, ": ");
        cat_lim(out, out_len, f->texto);
        cat_lim(out, out_len, " | ");
        cat_lim(out, out_len, g->nome); cat_lim(out, out_len, ": ");
        cat_lim(out, out_len, g->texto);
        cat_lim(out, out_len, " Quer que eu detalhe um dos dois?");
        return 3;
    }

    /* DEFINIR + "Veja tambem" */
    cat_lim(out, out_len, f->nome); cat_lim(out, out_len, ": ");
    cat_lim(out, out_len, f->texto);
    for (int i = 0; i < 4 && f->atributos[i].chave; i++) {
        if (strcmp(f->atributos[i].chave, "exemplo") == 0) {
            cat_lim(out, out_len, " Ex.: ");
            cat_lim(out, out_len, f->atributos[i].valor);
        }
    }
    const char* parente = primeiro_parente(f);
    if (parente) {
        cat_lim(out, out_len, " Veja tambem: ");
        cat_lim(out, out_len, parente);
        cat_lim(out, out_len, ".");
    }
    return 0;
}

int iafichas_contar(void) {
    catalogo_init();
    int n = 0;
    for (int m = 0; m < CAT_TAM; m++) n += CATALOGO[m].tam;
    return n;
}
