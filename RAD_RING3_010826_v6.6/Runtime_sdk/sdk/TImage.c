/*
====================================================================
Arquivo: TImage.c
Versão: 1.1
Data: 01/09/2026
Autor: LBF-OS Team + AI Assistant

Descrição:
    Componente TImage da SDK Ring 3 (visualizador de imagens BMP).
    v1.1: Refatorado seguindo a lógica do TWebImage:
      - Decode BMP 24/32-bit (BI_RGB/BI_BITFIELDS)
      - Buffer estático fora da pilha (512x512 max)
      - SetPixels com cópia opcional
      - SetFromFile via syscall sys_fat_read
      - GetDebug para diagnóstico
      - Clear libera memória
    Design: SDK carrega/decodifica, kernel renderiza com escala.

Testado em:
    - LBF-OS Base_v4.6.5 / Kernel v2.9 / Runtime v6.3
====================================================================
*/
#include "libgui.h"
#include "../system/liblib.h"
#include "../system/string.h"

// =========================================================================
// LIMITES E BUFFERS ESTÁTICOS (fora da pilha — vivem no .bss)
// Teto 512x512: arquivo 512*512*3+54 ≈ 786KB | pixels 512*512*4 = 1MB
// =========================================================================
#define IMG_MAX_W   512
#define IMG_MAX_H   512
#define IMG_MAX_PIX (IMG_MAX_W * IMG_MAX_H)

static uint8_t  g_img_file_buf[IMG_MAX_W * IMG_MAX_H * 3 + 1024];
static uint32_t g_img_px_buf[IMG_MAX_PIX];

// =========================================================================
// DIAGNÓSTICO DA ÚLTIMA TENTATIVA DE CARGA
// =========================================================================
static int g_dbg_bytes = 0;
static int g_dbg_bpp   = 0;
static int g_dbg_comp  = 0;
static int g_dbg_rc    = 0;

void GUI_Image_GetDebug(int* bytes, int* bpp, int* comp, int* rc) {
    if (bytes) *bytes = g_dbg_bytes;
    if (bpp)   *bpp   = g_dbg_bpp;
    if (comp)  *comp  = g_dbg_comp;
    if (rc)    *rc    = g_dbg_rc;
}

// =========================================================================
// DECODIFICADOR BMP — v1.1
// Aceita: 24/32 bits, BI_RGB(0)/BI_BITFIELDS(3), bottom-up/top-down
// Erros: -1 header | -2 magia | -3 formato | -4 dim | -5 >512x512 | -6 trunc
// =========================================================================
static int bmp_decode(const uint8_t* data, int len,
                      int* out_w, int* out_h, uint32_t* out_px, int max_px) {
    if (!data || len < 54 || !out_w || !out_h || !out_px) return -1;
    if (data[0] != 'B' || data[1] != 'M') return -2;
    
    uint32_t data_offset = *(const uint32_t*)(data + 10);
    int32_t  w    = *(const int32_t*)(data + 18);
    int32_t  h    = *(const int32_t*)(data + 22);
    uint16_t bpp  = *(const uint16_t*)(data + 28);
    uint32_t comp = *(const uint32_t*)(data + 30);
    
    if (bpp != 24 && bpp != 32) return -3;
    if (comp != 0 && comp != 3) return -3;
    if (w <= 0 || h == 0) return -4;
    
    int height = (h > 0) ? h : -h;
    if (w * height > max_px) return -5;
    
    int bytes_per_px = bpp / 8;
    int row_size = ((w * bytes_per_px + 3) / 4) * 4;
    
    for (int y = 0; y < height; y++) {
        int src_row = (h > 0) ? (height - 1 - y) : y;
        const uint8_t* row = data + data_offset + (uint32_t)src_row * row_size;
        if ((long)(row - data) + w * bytes_per_px > len) return -6;
        
        for (int x = 0; x < w; x++) {
            uint8_t b = row[x * bytes_per_px + 0];
            uint8_t g = row[x * bytes_per_px + 1];
            uint8_t r = row[x * bytes_per_px + 2];
            out_px[y * w + x] = 0xFF000000u | ((uint32_t)r << 16) |
                                ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
    
    *out_w = w;
    *out_h = height;
    return 0;
}

// =========================================================================
// CRIAÇÃO DO COMPONENTE
// =========================================================================
TGUIControl* GUI_CreateImage(TGUIEnvironment* app, int x, int y, int w, int h,
                             const char* path) {
    if (!app) return NULL;
    
    TGUIControl* img = (TGUIControl*)malloc(sizeof(TGUIControl));
    if (!img) return NULL;
    memset(img, 0, sizeof(TGUIControl));
    
    img->Type = TYPE_IMAGE;
    img->Left = x;
    img->Top = y;
    img->Width = w;
    img->Height = h;
    img->IsSelected = false;
    img->Pixels = NULL;
    img->PixW = 0;
    img->PixH = 0;
    img->PixOwned = false;
    
    GUI_RegisterControl(app, img, "Image");
    
    img->KernelHandle = (uint64_t)gui_create_image((TWinControl*)app->MainWindow, img->Name);
    if (img->KernelHandle == 0) {
        free(img);
        return NULL;
    }
    
    gui_set_prop((void*)img->KernelHandle, PROP_LEFT,   (uint64_t)img->Left);
    gui_set_prop((void*)img->KernelHandle, PROP_TOP,    (uint64_t)img->Top);
    gui_set_prop((void*)img->KernelHandle, PROP_WIDTH,  (uint64_t)img->Width);
    gui_set_prop((void*)img->KernelHandle, PROP_HEIGHT, (uint64_t)img->Height);
    
    // Se um caminho foi fornecido, carrega a imagem
    if (path && path[0] != '\0') {
        GUI_Image_SetFromFile(img, path);
    }
    
    return img;
}

// =========================================================================
// SET PIXELS — guarda no tamanho NATURAL; o kernel escala no draw
// =========================================================================
void GUI_Image_SetPixels(TGUIControl* img, const uint32_t* px, int w, int h, bool copy) {
    if (!img || !img->KernelHandle) return;
    
    // Libera a imagem anterior, se era nossa
    if (img->PixOwned && img->Pixels) {
        free(img->Pixels);
    }
    
    if (copy && px && w > 0 && h > 0) {
        int total = w * h;
        img->Pixels = (uint32_t*)malloc(total * sizeof(uint32_t));
        if (!img->Pixels) {
            img->PixW = 0;
            img->PixH = 0;
            img->PixOwned = false;
            return;
        }
        for (int i = 0; i < total; i++) {
            img->Pixels[i] = px[i];
        }
        img->PixOwned = true;
    } else {
        // Sem cópia: o caller é o dono do buffer
        img->Pixels = (uint32_t*)px;
        img->PixOwned = false;
    }
    
    img->PixW = w;
    img->PixH = h;
    
    // Sincroniza com o kernel: ponteiro via PROP_CAPTION, dimensões via PROP_STATE
    gui_set_prop((void*)img->KernelHandle, PROP_CAPTION, (uintptr_t)img->Pixels);
    uint64_t packed = ((uint64_t)img->PixH << 32) | (uint64_t)img->PixW;
    gui_set_prop((void*)img->KernelHandle, PROP_STATE, packed);
}

// =========================================================================
// SET FROM FILE — syscall sys_fat_read + decode + SetPixels
// Retorna: 0 ok | -1 leitura | -2 magia | -3 formato | -4 dim | -5 >512 | -6 trunc
// =========================================================================
int GUI_Image_SetFromFile(TGUIControl* img, const char* bmp_path) {
    g_dbg_bytes = g_dbg_bpp = g_dbg_comp = g_dbg_rc = 0;
    
    if (!img || !bmp_path || bmp_path[0] == '\0') return -99;
    
    // SYSCALL direta do FAT32
    int bytes_read = sys_fat_read(bmp_path, (void*)g_img_file_buf, sizeof(g_img_file_buf));
    g_dbg_bytes = bytes_read;
    
    if (bytes_read <= 0) {
        g_dbg_rc = -1;
        GUI_Image_Clear(img);
        return -1;
    }
    
    if (bytes_read >= 54) {
        g_dbg_bpp  = (int)(*(const uint16_t*)(g_img_file_buf + 28));
        g_dbg_comp = (int)(*(const uint32_t*)(g_img_file_buf + 30));
    }
    
    int w = 0, h = 0;
    int rc = bmp_decode(g_img_file_buf, bytes_read, &w, &h, g_img_px_buf, IMG_MAX_PIX);
    g_dbg_rc = rc;
    
    if (rc != 0) {
        GUI_Image_Clear(img);
        return rc;
    }
    
    GUI_Image_SetPixels(img, g_img_px_buf, w, h, true);
    return 0;
}

// =========================================================================
// CLEAR
// =========================================================================
void GUI_Image_Clear(TGUIControl* img) {
    if (!img || !img->KernelHandle) return;
    
    if (img->PixOwned && img->Pixels) {
        free(img->Pixels);
    }
    
    img->Pixels = NULL;
    img->PixW = 0;
    img->PixH = 0;
    img->PixOwned = false;
    
    gui_set_prop((void*)img->KernelHandle, PROP_CAPTION, 0);
    gui_set_prop((void*)img->KernelHandle, PROP_STATE, 0);
}

// =========================================================================
// SET STRETCH (controla se a imagem estica ou fica em tamanho natural)
// =========================================================================
void GUI_Image_SetStretch(TGUIControl* img, bool stretch) {
    if (!img || !img->KernelHandle) return;
    // O kernel lê isso da struct TImage->Stretch, mas a SDK não tem acesso direto.
    // Por enquanto, Stretch é sempre true (padrão do gui_create_image).
    // Se quiser controlar, precisaria de uma PROP_STRETCH no kernel.
    (void)stretch;
}
