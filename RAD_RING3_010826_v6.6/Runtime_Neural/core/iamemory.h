#ifndef IAMEMORY_H
#define IAMEMORY_H
/* ============================================================================
* IAMEMORY - CORE DE MEMÓRIA (VOLÁTIL) - NeuralRuntime Portável
*
* 100% independente de sistema operacional. Usa apenas C padrão.
* As lembranças vivem SOMENTE em RAM, enquanto o processo executa.
*
* O núcleo NUNCA toca disco/IO: a serialização em buffer abaixo é a
* ABSTRAÇÃO que permite à camada de interação (específica de cada SO)
* decidir se/como persistir — sem que o core saiba onde está rodando.
* ============================================================================ */
#include <stdint.h>
#include <stddef.h>

#define MEM_MAX_SLOTS 32
#define MEM_TAM_TEXTO 64
#define MEM_TAM_VETOR 16   // 16 — casa com iaembed_frase!
#define MEM_LIMIAR    0.45f     // similaridade mínima para recordar

typedef struct {
    float embedding[MEM_TAM_VETOR];
    char  texto[MEM_TAM_TEXTO];
    float forca;   // 0..1 - força Hebbiana (reforço/decaimento)
    int   ativo;
} MemoriaSlot;

/* --- API principal (volátil) --- */
int  iamemory_armazenar(const char* texto);
int  iamemory_recordar(const char* chave, char* out_texto);
int  iamemory_esquecer(const char* chave);
int  iamemory_contar(void);
void iamemory_decair(void);
void iamemory_limpar(void);

/* --- Abstração de persistência (hook p/ camada de interação) ---
* O core apenas copia o estado de/para um buffer fornecido.
* Quem grava no disco (FAT32, NTFS, ext4...) é o software de interação.
* buffer == NULL em serializar retorna o tamanho necessário. */
int  iamemory_serializar(void* buffer, int max_bytes);
int  iamemory_carregar(const void* buffer, int tam_bytes);

#endif // IAMEMORY_H
