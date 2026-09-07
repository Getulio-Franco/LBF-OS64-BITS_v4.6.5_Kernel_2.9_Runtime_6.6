#include "iadialog.h"

typedef struct {
    float ultimo_resultado;
    int   tem_resultado;
    int   ultima_op;
    int   foi_logic;
    int   turnos;
} DialogContext;

static DialogContext g_ctx = {0.0f, 0, 0, 0, 0};

void iadialog_reset(void) {
    g_ctx.ultimo_resultado = 0.0f;
    g_ctx.tem_resultado = 0;
    g_ctx.ultima_op = 0;
    g_ctx.foi_logic = 0;
    g_ctx.turnos = 0;
}

void iadialog_registrar_resultado(float valor, int op, int foi_logic) {
    g_ctx.ultimo_resultado = valor;
    g_ctx.tem_resultado = 1;
    g_ctx.ultima_op = op;
    g_ctx.foi_logic = foi_logic;
    g_ctx.turnos++;
}

int iadialog_tem_resultado(void) {
    return g_ctx.tem_resultado;
}

float iadialog_ultimo_resultado(void) {
    return g_ctx.ultimo_resultado;
}

int iadialog_turnos(void) {
    return g_ctx.turnos;
}

int iadialog_foi_logic(void) { return g_ctx.foi_logic; }

int iadialog_ultima_op(void) { return g_ctx.ultima_op; }
