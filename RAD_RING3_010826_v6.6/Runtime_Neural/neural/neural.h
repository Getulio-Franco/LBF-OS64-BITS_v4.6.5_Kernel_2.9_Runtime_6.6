#ifndef NEURAL_H
#define NEURAL_H
#include <stdint.h>
#include <stddef.h>

// Tabela de IDs de Chamadas (Identificador RAX)
#define SYS_IA_MATH_EXEC      100
#define SYS_IA_LOGIC_EXEC     101
#define SYS_IA_ROUTER_EXEC    102
#define SYS_IA_SYNTH_EXEC     103
#define SYS_IA_MEMORY_EXEC    104 
#define SYS_IA_KNOWLEDGE_EXEC 105   
#define SYS_IA_DIALOG_EXEC    106
#define SYS_IA_EMOTION_EXEC   107
#define SYS_IA_LEARN_EXEC     108
#define SYS_IA_REASON_EXEC    109 
#define SYS_IA_PERSONA_EXEC   110
#define SYS_IA_PATTERN_EXEC   113
#define SYS_IA_EMBED_EXEC     116
#define SYS_IA_FICHAS_EXEC    117
#define SYS_IA_SOCIAL_EXEC    118
#define SYS_IA_CATALOGO_EXEC  119

// Códigos de erro padronizados
#define NEURAL_OK                    0
#define NEURAL_ERR_NULL_PTR         -1
#define NEURAL_ERR_INVALID_SYSCALL  -2
#define NEURAL_ERR_DIVISION_BY_ZERO -3
#define NEURAL_ERR_INVALID_MODEL    -4

/**
 * Dispatcher Central 100% Ring 3.
 * Suporta 1 ID de operação + 7 argumentos genéricos de 64 bits.
 * 
 * @return Código de erro (negativo) ou resultado da operação (positivo/zero)
 */
uint64_t neural_syscall(
    uint64_t syscall_num,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3,
    uint64_t arg4,
    uint64_t arg5,
    uint64_t arg6,
    uint64_t arg7
);

#endif /* NEURAL_H */
