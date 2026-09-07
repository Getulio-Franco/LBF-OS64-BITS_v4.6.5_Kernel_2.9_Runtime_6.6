#ifndef IADIALOG_H
#define IADIALOG_H
/* ============================================================================
* IADIALOG - CORE DE CONTEXTO DE CONVERSA (Portátil, volátil)
* Guarda o último resultado/operação para habilitar frases de continuação:
* "e mais 3", "multiplique por 10", "divida por 2"...
* ============================================================================ */
#include <stdint.h>
#include <stddef.h>

void  iadialog_reset(void);
void  iadialog_registrar_resultado(float valor, int op, int foi_logic);
int   iadialog_tem_resultado(void);
float iadialog_ultimo_resultado(void);
int   iadialog_turnos(void);
int   iadialog_foi_logic(void);
int   iadialog_ultima_op(void);

#endif // IADIALOG_H
