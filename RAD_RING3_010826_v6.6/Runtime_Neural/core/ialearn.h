#ifndef IALEARN_H
#define IALEARN_H
/* ============================================================================
* IALEARN - CORE DE APRENDIZADO POR FEEDBACK (Portátil, volátil)
* Registra a última decisão do router e, ao receber feedback
* (positivo/negativo), ajusta os pesos do roteador online.
* ============================================================================ */
#include <stdint.h>
#include <stddef.h>

/* Guarda o texto + decisão escolhida (aguardando feedback) */
void ialearn_registrar(const char* texto, int acionar_math, int acionar_logic);
/* Aplica o treino no router: 1 = reforça, 0 = corrige. Retorna 1 se treinou. */
int  ialearn_feedback(int positivo);
/* Total de ajustes de aprendizado realizados */
int  ialearn_total(void);

#endif // IALEARN_H
