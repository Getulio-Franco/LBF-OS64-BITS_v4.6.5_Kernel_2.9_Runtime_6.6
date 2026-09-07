#include "iaemotion.h"
#include <string.h>

/* Estado emocional (volátil - vive enquanto executo) */
static float g_humor   = 0.60f; // nasce levemente feliz (ela é gentil)
static float g_energia = 0.80f;
static float g_vinculo = 0.10f;
static int   g_interacoes = 0;

static float clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

int iaemotion_evento(int tipo) {
    g_interacoes++;
    if (tipo == EMOC_ELOGIO) {
        g_humor   += 0.15f;
        g_vinculo += 0.05f;
        g_energia += 0.05f;
    } else if (tipo == EMOC_CRITICA) {
        g_humor   -= 0.20f;
        g_vinculo -= 0.02f;
        g_energia -= 0.05f;
    } else if (tipo == EMOC_CONTAGIO) {
        g_humor   += 0.10f;
        g_vinculo += 0.02f;
    } else if (tipo == EMOC_EMPATIA) {
        g_humor   -= 0.08f;   // ela sente junto
        g_vinculo += 0.05f;   // e se aproxima
    } else { // NEUTRO: conversar fortalece o vínculo, humor busca equilíbrio
        g_vinculo += 0.01f;
        g_humor   += (0.5f - g_humor) * 0.05f;
        g_energia *= 0.995f;
    } 
    g_humor   = clamp01(g_humor);
    g_vinculo = clamp01(g_vinculo);
    g_energia = clamp01(g_energia);
    return (int)(g_humor * 100.0f);
}

int iaemotion_humor(void)   { return (int)(g_humor * 100.0f); }
int iaemotion_vinculo(void) { return (int)(g_vinculo * 100.0f); }

void iaemotion_frase_estado(char* out) {
    if (!out) return;
    if (g_humor > 0.75f)      strcpy(out, "Estou radiante hoje!");
    else if (g_humor > 0.55f) strcpy(out, "Estou bem, obrigada!");
    else if (g_humor >= 0.45f) strcpy(out, "Estou tranquila.");
    else if (g_humor >= 0.25f) strcpy(out, "Estou um pouco cabisbaixa...");
    else                      strcpy(out, "Estou triste...");
}

void iaemotion_reset(void) {
    g_humor = 0.60f;
    g_energia = 0.80f;
    g_vinculo = 0.10f;
    g_interacoes = 0;
}

int iaemotion_interacoes(void) { return g_interacoes; }
