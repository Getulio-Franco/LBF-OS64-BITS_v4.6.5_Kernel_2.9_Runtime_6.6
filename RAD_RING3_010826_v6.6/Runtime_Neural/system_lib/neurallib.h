#ifndef NEURALLIB_H
#define NEURALLIB_H
#include <stdint.h>
#include <stddef.h>
#include "Runtime_Neural/neural/neural.h"

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

/* ============================================================================
* NEURALLIB.H - ABI ESTÁVEL (contrato entre núcleo e camada de interação)
* Nunca incluir headers dos cores aqui. Nunca expor structs internas.
* ============================================================================ */

// --- MATH (100) ---
static inline float sys_ia_math(int modelo_id, float x1, float x2) {
    union { float f; uint64_t raw; } u_x1 = { .f = x1 };
    union { float f; uint64_t raw; } u_x2 = { .f = x2 };
    uint64_t ret = neural_syscall(SYS_IA_MATH_EXEC, (uint64_t)modelo_id, u_x1.raw, u_x2.raw, 0,0,0,0);
    union { uint64_t raw; float f; } u_res = { .raw = ret };
    return u_res.f;
}

// --- LOGIC (101) ---
static inline int sys_ia_logic(int porta_id, float x1, float x2) {
    union { float f; uint64_t raw; } u_x1 = { .f = x1 };
    union { float f; uint64_t raw; } u_x2 = { .f = x2 };
    return (int)neural_syscall(SYS_IA_LOGIC_EXEC, (uint64_t)porta_id, u_x1.raw, u_x2.raw, 0,0,0,0);
}

// --- ROUTER (102) ---
static inline int sys_ia_router(const char* texto, void* out_decisao) {
    return (int)neural_syscall(SYS_IA_ROUTER_EXEC, (uint64_t)texto, (uint64_t)out_decisao, 0,0,0,0,0);
}

// --- SYNTH (103) ---
static inline int sys_ia_synth(const char* prompt, const char* raw_result, char* out_buf) {
    return (int)neural_syscall(SYS_IA_SYNTH_EXEC, (uint64_t)prompt, (uint64_t)raw_result, (uint64_t)out_buf, 0,0,0,0);
}

// --- MEMORY (104) ---
static inline int sys_ia_memory_store(const char* texto) {
    return (int)neural_syscall(SYS_IA_MEMORY_EXEC, 0, (uint64_t)texto, 0,0,0,0,0);
}
static inline int sys_ia_memory_recall(const char* chave, char* out_buf) {
    return (int)neural_syscall(SYS_IA_MEMORY_EXEC, 1, (uint64_t)chave, (uint64_t)out_buf, 0,0,0,0);
}
static inline int sys_ia_memory_forget(const char* chave) {
    return (int)neural_syscall(SYS_IA_MEMORY_EXEC, 2, (uint64_t)chave, 0,0,0,0,0);
}
static inline int sys_ia_memory_count(void) {
    return (int)neural_syscall(SYS_IA_MEMORY_EXEC, 3, 0,0,0,0,0,0);
}
static inline int sys_ia_memory_save(void* buf, int max_bytes) {
    return (int)neural_syscall(SYS_IA_MEMORY_EXEC, 4, (uint64_t)buf, (uint64_t)max_bytes, 0,0,0,0);
}
static inline int sys_ia_memory_load(const void* buf, int tam_bytes) {
    return (int)neural_syscall(SYS_IA_MEMORY_EXEC, 5, (uint64_t)buf, (uint64_t)tam_bytes, 0,0,0,0);
}
static inline int sys_ia_memory_clear(void) {
    return (int)neural_syscall(SYS_IA_MEMORY_EXEC, 6, 0,0,0,0,0,0);
}

// --- KNOWLEDGE (105) ---
static inline int sys_ia_knowledge_learn(const char* pergunta, const char* resposta) {
    return (int)neural_syscall(SYS_IA_KNOWLEDGE_EXEC, 0, (uint64_t)pergunta, (uint64_t)resposta, 0,0,0,0);
}
static inline int sys_ia_knowledge_ask(const char* pergunta, char* out_resposta) {
    return (int)neural_syscall(SYS_IA_KNOWLEDGE_EXEC, 1, (uint64_t)pergunta, (uint64_t)out_resposta, 0,0,0,0);
}
static inline int sys_ia_knowledge_count(void) {
    return (int)neural_syscall(SYS_IA_KNOWLEDGE_EXEC, 2, 0,0,0,0,0,0);
}

// --- DIALOG (106) ---
static inline void sys_ia_dialog_reg_result(float valor, int op, int foi_logic) {
    union { float f; uint64_t raw; } u = { .f = valor };
    neural_syscall(SYS_IA_DIALOG_EXEC, 0, u.raw, (uint64_t)op, (uint64_t)foi_logic, 0,0,0);
}
static inline int sys_ia_dialog_tem_result(void) {
    return (int)neural_syscall(SYS_IA_DIALOG_EXEC, 1, 0,0,0,0,0,0);
}
static inline float sys_ia_dialog_ultimo_result(void) {
    union { uint64_t raw; float f; } u = { .raw = neural_syscall(SYS_IA_DIALOG_EXEC, 2, 0,0,0,0,0,0) };
    return u.f;
}
static inline void sys_ia_dialog_reset(void) {
    neural_syscall(SYS_IA_DIALOG_EXEC, 3, 0,0,0,0,0,0);
}

static inline int sys_ia_emotion_event(int tipo) {
    return (int)neural_syscall(SYS_IA_EMOTION_EXEC, 0, (uint64_t)tipo, 0,0,0,0,0);
}
static inline int sys_ia_emotion_humor(void) {
    return (int)neural_syscall(SYS_IA_EMOTION_EXEC, 1, 0,0,0,0,0,0);
}
static inline void sys_ia_emotion_frase(char* out) {
    neural_syscall(SYS_IA_EMOTION_EXEC, 2, (uint64_t)out, 0,0,0,0,0);
}
static inline void sys_ia_emotion_reset(void) {
    neural_syscall(SYS_IA_EMOTION_EXEC, 3, 0,0,0,0,0,0);
}

static inline void sys_ia_learn_registrar(const char* texto, int math, int logic) {
    neural_syscall(SYS_IA_LEARN_EXEC, 0, (uint64_t)texto, (uint64_t)math, (uint64_t)logic, 0,0,0);
}
static inline int sys_ia_learn_feedback(int positivo) {
    return (int)neural_syscall(SYS_IA_LEARN_EXEC, 1, (uint64_t)positivo, 0,0,0,0,0);
}
static inline int sys_ia_learn_total(void) {
    return (int)neural_syscall(SYS_IA_LEARN_EXEC, 2, 0,0,0,0,0,0);
}

static inline int sys_ia_reason(const char* texto, void* out_result) {
    return (int)neural_syscall(SYS_IA_REASON_EXEC, 0, (uint64_t)texto, (uint64_t)out_result, 0,0,0,0);
}
static inline int sys_ia_reason_can(const char* texto) {
    return (int)neural_syscall(SYS_IA_REASON_EXEC, 1, (uint64_t)texto, 0,0,0,0,0);
}
static inline int sys_ia_persona_ask(const char* pergunta, char* out, int out_len) {
    return (int)neural_syscall(SYS_IA_PERSONA_EXEC, (uint64_t)pergunta, (uint64_t)out, (uint64_t)(uint32_t)out_len, 0,0,0,0);
}
static inline void sys_ia_pattern_reg(int categoria) {
    neural_syscall(SYS_IA_PATTERN_EXEC, 0, (uint64_t)(uint32_t)categoria, 0,0,0,0,0);
}
static inline int sys_ia_pattern_total(void) {
    return (int)neural_syscall(SYS_IA_PATTERN_EXEC, 1, 0,0,0,0,0,0);
}
static inline void sys_ia_pattern_obs(char* out) {
    neural_syscall(SYS_IA_PATTERN_EXEC, 3, (uint64_t)out, 0,0,0,0,0);
}

static inline int sys_ia_embed_sim(const char* a, const char* b) {
    return (int)neural_syscall(SYS_IA_EMBED_EXEC, 0, (uint64_t)a, (uint64_t)b, 0,0,0,0);
}
static inline int sys_ia_embed_vocab(void) {
    return (int)neural_syscall(SYS_IA_EMBED_EXEC, 1, 0,0,0,0,0,0);
}

static inline int sys_ia_fichas_consultar(const char* texto, char* out, int out_len) {
    return (int)neural_syscall(SYS_IA_FICHAS_EXEC, 0, (uint64_t)texto, (uint64_t)out, (uint64_t)(uint32_t)out_len, 0,0,0);
}
static inline int sys_ia_fichas_contar(void) {
    return (int)neural_syscall(SYS_IA_FICHAS_EXEC, 1, 0,0,0,0,0,0);
}

static inline int sys_ia_social_consultar(const char* texto, char* out, int out_len) {
    return (int)neural_syscall(SYS_IA_SOCIAL_EXEC, 0, (uint64_t)texto, (uint64_t)out, (uint64_t)(uint32_t)out_len, 0,0,0);
}

static inline int sys_ia_catalogo_consultar(const char* texto, char* out, int out_len) {
    return (int)neural_syscall(SYS_IA_CATALOGO_EXEC, 0, (uint64_t)texto, (uint64_t)out, (uint64_t)(uint32_t)out_len, 0,0,0);
}

#endif // NEURALLIB_H
