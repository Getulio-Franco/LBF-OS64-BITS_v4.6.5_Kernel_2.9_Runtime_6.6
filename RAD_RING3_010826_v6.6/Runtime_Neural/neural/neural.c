#include "neural.h"
#include <stddef.h>
#include "Runtime_Neural/core/iamath.h"
#include "Runtime_Neural/core/ialogic.h"
#include "Runtime_Neural/core/iasynth.h"
#include "Runtime_Neural/core/iarouter.h"
#include "Runtime_Neural/core/iamemory.h"
#include "Runtime_Neural/core/iaknowledge.h"
#include "Runtime_Neural/core/iadialog.h"
#include "Runtime_Neural/core/iaemotion.h"
#include "Runtime_Neural/core/ialearn.h"
#include "Runtime_Neural/core/iareason.h"
#include "Runtime_Neural/core/iapersona.h"
#include "Runtime_Neural/core/iapattern.h"
#include "Runtime_Neural/core/iaembed.h"
#include "Runtime_Neural/core/iafichas.h"
#include "Runtime_Neural/core/iasocial.h"
#include "Runtime_Neural/core/iacatalogo.h"

uint64_t neural_syscall(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    switch (syscall_num) {
        case SYS_IA_MATH_EXEC: {
            int modelo_id = (int)arg1; // Recebe o ID
            union { uint64_t raw; float f; } u_x1 = { .raw = arg2 };
            union { uint64_t raw; float f; } u_x2 = { .raw = arg3 };
            
            if (modelo_id < 0 || modelo_id > 3) return (uint64_t)-1;
            
            // O Kernel busca a struct real usando o ID
            const NeuronioMatematico* modelo = &BancoDeDadosMatematico[modelo_id];
            float res = iamath_processar(modelo, u_x1.f, u_x2.f);
            
            union { float f; uint64_t raw; } u_res = { .f = res };
            return u_res.raw;
        }
        
        case SYS_IA_LOGIC_EXEC: {
            int porta_id = (int)arg1; // Recebe o ID
            union { uint64_t raw; float f; } u_x1 = { .raw = arg2 };
            union { uint64_t raw; float f; } u_x2 = { .raw = arg3 };
            
            if (porta_id < 0 || porta_id > 6) return (uint64_t)-1;
            
            // O Kernel busca a struct real usando o ID
            const PesosMLP* p = &BancoDeDadosPortas[porta_id];
            int res = mlp_processar(p, u_x1.f, u_x2.f);
            return (uint64_t)res;
        }
        
        case SYS_IA_ROUTER_EXEC: {
            const char* texto_usuario = (const char*)arg1;
            RouterDecision* out_decisao = (RouterDecision*)arg2;
            if (!texto_usuario || !out_decisao) return (uint64_t)-1;
            *out_decisao = iarouter_processar(texto_usuario);
            return 0; 
        }
        
        case SYS_IA_SYNTH_EXEC: {
            const char* prompt_original = (const char*)arg1;
            const char* raw_result = (const char*)arg2;
            char* buffer_saida = (char*)arg3;
            if (!prompt_original || !raw_result || !buffer_saida) return (uint64_t)-1;
            iasynth_gerar_resposta(prompt_original, raw_result, buffer_saida);
            return 0; 
        }

        // Núcleo de Memória de longo prazo (iamemory)
        case SYS_IA_MEMORY_EXEC: {
            int op = (int)arg1;
            switch (op) {
                case 0: { // STORE
                    const char* texto = (const char*)arg2;
                    if (!texto) return (uint64_t)-1;
                    return (uint64_t)iamemory_armazenar(texto);
                }
                case 1: { // RECALL
                    const char* chave = (const char*)arg2;
                    char* out = (char*)arg3;
                    if (!chave || !out) return (uint64_t)-1;
                    return (uint64_t)iamemory_recordar(chave, out);
                }
                case 2:  // FORGET
                    return (uint64_t)iamemory_esquecer((const char*)arg2);
                case 3:  // COUNT
                    return (uint64_t)iamemory_contar();
                case 4:  // SERIALIZE (buffer NULL = consulta tamanho)
                    return (uint64_t)iamemory_serializar((void*)arg2, (int)arg3);
                case 5:  // LOAD
                    return (uint64_t)iamemory_carregar((const void*)arg2, (int)arg3);
                case 6:  // CLEAR (reseta a sessão volátil)
                    iamemory_limpar();
                    return 0;
                default:
                    return (uint64_t)-1;
            }
        }
        
        // Núcleo de Conhecimento de Mundo
        case SYS_IA_KNOWLEDGE_EXEC: {
            int op = (int)arg1;
            switch (op) {
                case 0: { // LEARN
                    const char* p = (const char*)arg2;
                    const char* r = (const char*)arg3;
                    if (!p || !r) return (uint64_t)-1;
                    return (uint64_t)iaknowledge_aprender(p, r);
                }
                case 1: { // ASK
                    const char* p = (const char*)arg2;
                    char* out = (char*)arg3;
                    if (!p || !out) return (uint64_t)-1;
                    return (uint64_t)iaknowledge_consultar(p, out);
                }
                case 2:  return (uint64_t)iaknowledge_contar();
                case 3:  iaknowledge_limpar(); return 0;
                default: return (uint64_t)-1;
            }
        }

        // Núcleo de Contexto de Conversa
        case SYS_IA_DIALOG_EXEC: {
            int op = (int)arg1;
            switch (op) {
                case 0: { // REG_RESULTADO
                    union { uint64_t raw; float f; } u = { .raw = arg2 };
                    iadialog_registrar_resultado(u.f, (int)arg3, (int)arg4);
                    return 0;
                }
                case 1:  return (uint64_t)iadialog_tem_resultado();
                case 2: {
                    union { float f; uint64_t raw; } u = { .f = iadialog_ultimo_resultado() };
                    return u.raw;
                }
                case 3:  iadialog_reset(); return 0;
                case 4:  return (uint64_t)iadialog_turnos();
                default: return (uint64_t)-1;
            }
        }
        
        // Núcleo de Emoção / Personalidade
        case SYS_IA_EMOTION_EXEC: {
            int op = (int)arg1;
            switch (op) {
                case 0:  return (uint64_t)iaemotion_evento((int)arg2);
                case 1:  return (uint64_t)iaemotion_humor();
                case 2: {
                    char* out = (char*)arg2;
                    if (!out) return (uint64_t)-1;
                    iaemotion_frase_estado(out);
                    return 0;
                }
                case 3:  iaemotion_reset(); return 0;
                default: return (uint64_t)-1;
            }
        }
        
        // Núcleo de Aprendizado por Feedback
        case SYS_IA_LEARN_EXEC: {
            int op = (int)arg1;
            switch (op) {
                case 0:
                    ialearn_registrar((const char*)arg2, (int)arg3, (int)arg4);
                    return 0;
                case 1:  return (uint64_t)ialearn_feedback((int)arg2);
                case 2:  return (uint64_t)ialearn_total();
                default: return (uint64_t)-1;
            }
        }
        
                // Núcleo de Raciocínio em Cadeia
        case SYS_IA_REASON_EXEC: {
            int op = (int)arg1;
            if (op == 0) {
                const char* texto = (const char*)arg2;
                ReasonResult* out = (ReasonResult*)arg3;
                if (!texto || !out) return (uint64_t)-1;
                return (uint64_t)iareason_processar(texto, out);
            }
            if (op == 1) return (uint64_t)iareason_pode((const char*)arg2);
            return (uint64_t)-1;
        }

        // Núcleo de Personalidade
        case SYS_IA_PERSONA_EXEC: {
            const char* p = (const char*)arg1;
            char* out = (char*)arg2;
            int len = (int)arg3;
            if (!p || !out) return (uint64_t)-1;
            return (uint64_t)iapersona_consultar(p, out, len);
        }

        // Núcleo de Padrões do Usuário
        case SYS_IA_PATTERN_EXEC: {
            int op = (int)arg1;
            switch (op) {
                case 0: iapattern_registrar((int)arg2); return 0;
                case 1: return (uint64_t)iapattern_total();
                case 2: return (uint64_t)iapattern_dominante();
                case 3: iapattern_observacao((char*)arg2); return 0;
                default: return (uint64_t)-1;
            }
        }
        
        // Mini-cérebro semântico (Opção B)
        case SYS_IA_EMBED_EXEC: {
            int op = (int)arg1;
            switch (op) {
                case 0: { // SIM: retorna similaridade * 1000
                    const char* a = (const char*)arg2;
                    const char* b = (const char*)arg3;
                    if (!a || !b) return (uint64_t)-1;
                    float s = iaembed_similaridade_texto(a, b);
                    return (uint64_t)(int)(s * 1000.0f);
                }
                case 1: return (uint64_t)iaembed_vocab_tam();
                case 2: { // PALAVRA conhecida?
                    float tmp[EMB_DIM];
                    return (uint64_t)iaembed_palavra((const char*)arg2, tmp);
                }
                default: return (uint64_t)-1;
            }
        }
        
        // Núcleo Biblioteca (fichas de conhecimento)
        case SYS_IA_FICHAS_EXEC: {
            int op = (int)arg1;
            if (op == 0) {
                const char* t = (const char*)arg2;
                char* out = (char*)arg3;
                int len = (int)arg4;
                if (!t || !out) return (uint64_t)-1;
                return (uint64_t)iafichas_consultar(t, out, len);
            }
            if (op == 1) return (uint64_t)iafichas_contar();
            return (uint64_t)-1;
        }
        
        // Núcleo Livro de Convívio
        case SYS_IA_SOCIAL_EXEC: {
            int op = (int)arg1;
            if (op == 0) {
                const char* t = (const char*)arg2;
                char* out = (char*)arg3;
                int len = (int)arg4;
                if (!t || !out) return (uint64_t)-1;
                return (uint64_t)iasocial_consultar(t, out, len);
            }
            if (op == 1) return (uint64_t)iasocial_contar();
            return (uint64_t)-1;
        }
        
        // Núcleo Bibliotecária (descreve os livros)
        case SYS_IA_CATALOGO_EXEC: {
            int op = (int)arg1;
            if (op == 0) {
                const char* t = (const char*)arg2;
                char* out = (char*)arg3;
                int len = (int)arg4;
                if (!t || !out) return (uint64_t)-1;
                return (uint64_t)iacatalogo_consultar(t, out, len);
            }
            return (uint64_t)-1;
        }
        
        //-----------------
        default: return (uint64_t)-1;
    }
}
