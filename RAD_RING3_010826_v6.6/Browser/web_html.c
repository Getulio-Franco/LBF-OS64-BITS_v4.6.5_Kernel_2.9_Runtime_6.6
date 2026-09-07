/*
====================================================================
Arquivo: web_html.c
Versão: 1.1
Data: 31/08/2026
Autor: LBF-OS Team + AI Assistant

Descrição:
    Construtor do modelo WebDoc (linhas/links/imagens) consumido
    pelo componente TWebPage.
    v1.1: CORRIGIDO — webdoc_add_raw_line declarada ANTES de
    webdoc_add_line (elimina implicit declaration / conflicting
    types); todas as APIs com ponteiros WebDoc* corretos.

Testado em:
    - LBF-OS Base_v4.6.5 / Kernel v2.9 / Runtime v6.3
====================================================================
*/
#include "Runtime_sdk/sdk/libgui.h"
#include "../system/liblib.h"
#include "../system/string.h"

#define WEBDOC_INIT_LINES 32
#define WEBDOC_INIT_LINKS 16
#define WEBDOC_INIT_IMGS   8
#define WEBDOC_WRAP 48   // quebra linhas longas (cabe em box de ~400px+)

/* ============================================================================
* INTERNA: adiciona UMA linha física (sem quebra)
* IMPORTANTE: declarada ANTES de webdoc_add_line (é static!)
* ============================================================================ */
static void webdoc_add_raw_line(WebDoc* doc, const char* text, uint8_t style,
                                int link_id, int img_slot) {
    if (!doc || !doc->lines) return;
    if (doc->lc >= doc->lc_allocated) {
        int new_cap = doc->lc_allocated * 2;
        if (new_cap < WEBDOC_INIT_LINES) new_cap = WEBDOC_INIT_LINES;
        WebLine* p = (WebLine*)realloc(doc->lines, new_cap * sizeof(WebLine));
        if (!p) return;
        doc->lines = p;
        doc->lc_allocated = new_cap;
    }
    WebLine* ln = &doc->lines[doc->lc];
    memset(ln, 0, sizeof(WebLine));
    if (text) {
        strncpy(ln->text, text, 127);
        ln->text[127] = '\0';
    }
    ln->style    = style;
    ln->link_id  = link_id;
    ln->img_slot = img_slot;
    doc->lc++;
}

/* ============================================================================
* CRIA UM WEBDOC VAZIO (aloca buffers iniciais)
* ============================================================================ */
WebDoc* webdoc_create(void) {
    WebDoc* doc = (WebDoc*)malloc(sizeof(WebDoc));
    if (!doc) return NULL;
    memset(doc, 0, sizeof(WebDoc));

    doc->lines = (WebLine*)malloc(WEBDOC_INIT_LINES * sizeof(WebLine));
    doc->lc_allocated = doc->lines ? WEBDOC_INIT_LINES : 0;
    doc->lc = 0;

    doc->links = (WebLink*)malloc(WEBDOC_INIT_LINKS * sizeof(WebLink));
    doc->kc_allocated = doc->links ? WEBDOC_INIT_LINKS : 0;
    doc->kc = 0;

    doc->imgs = (WebImgSlot*)malloc(WEBDOC_INIT_IMGS * sizeof(WebImgSlot));
    doc->ic_allocated = doc->imgs ? WEBDOC_INIT_IMGS : 0;
    doc->ic = 0;

    doc->total_h = 0;
    return doc;
}

/* ============================================================================
* DESTROY
* ============================================================================ */
void webdoc_destroy(WebDoc* doc) {
    if (!doc) return;
    if (doc->lines) free(doc->lines);
    if (doc->links) free(doc->links);
    // Nota: os pixels em imgs[].px NÃO são liberados aqui —
    // quem carregou a imagem é responsável por ela.
    if (doc->imgs) free(doc->imgs);
    free(doc);
}

/* ============================================================================
* ADICIONA UMA LINHA (com quebra automática por palavra em WEBDOC_WRAP)
* ============================================================================ */
void webdoc_add_line(WebDoc* doc, const char* text, uint8_t style,
                     int link_id, int img_slot) {
    if (!doc || !doc->lines) return;
    
    // v1.2 FIX: imagem é UMA linha física sempre (o engine desenha o
    // bitmap, não o texto) — sem wrap, senão a imagem duplica!
    if (style == WEB_STYLE_IMG) {
        webdoc_add_raw_line(doc, text, style, link_id, img_slot);
        return;
    }
    
    if (!text || text[0] == '\0') {
        webdoc_add_raw_line(doc, "", style, link_id, img_slot);
        return;
    }

    const char* p = text;
    while (*p) {
        int remaining = (int)strlen(p);
        if (remaining <= WEBDOC_WRAP) {
            webdoc_add_raw_line(doc, p, style, link_id, img_slot);
            break;
        }
        // quebra na última palavra que cabe
        int cut = WEBDOC_WRAP;
        int last_space = -1;
        for (int i = 0; i < WEBDOC_WRAP && p[i]; i++) {
            if (p[i] == ' ') last_space = i;
        }
        if (last_space > 0) cut = last_space;

        char tmp[WEBDOC_WRAP + 1];
        for (int i = 0; i < cut; i++) tmp[i] = p[i];
        tmp[cut] = '\0';

        webdoc_add_raw_line(doc, tmp, style, link_id, img_slot);
        p += cut;
        while (*p == ' ') p++;   // pula os espaços da quebra
    }
}

/* ============================================================================
* ADICIONA UM LINK — retorna o índice (link_id) p/ usar em webdoc_add_line
* ============================================================================ */
int webdoc_add_link(WebDoc* doc, const char* url) {
    if (!doc || !doc->links) return -1;
    if (doc->kc >= doc->kc_allocated) {
        int new_cap = doc->kc_allocated * 2;
        WebLink* p = (WebLink*)realloc(doc->links, new_cap * sizeof(WebLink));
        if (!p) return -1;
        doc->links = p;
        doc->kc_allocated = new_cap;
    }
    int idx = doc->kc;
    if (url) {
        strncpy(doc->links[idx].url, url, 159);
        doc->links[idx].url[159] = '\0';
    } else {
        doc->links[idx].url[0] = '\0';
    }
    doc->kc++;
    return idx;
}

/* ============================================================================
* ADICIONA SLOT DE IMAGEM — retorna o índice (img_slot)
* ============================================================================ */
int webdoc_add_image(WebDoc* doc, const char* url) {
    if (!doc || !doc->imgs) return -1;
    if (doc->ic >= doc->ic_allocated) {
        int new_cap = doc->ic_allocated * 2;
        WebImgSlot* p = (WebImgSlot*)realloc(doc->imgs, new_cap * sizeof(WebImgSlot));
        if (!p) return -1;
        doc->imgs = p;
        doc->ic_allocated = new_cap;
    }
    int idx = doc->ic;
    memset(&doc->imgs[idx], 0, sizeof(WebImgSlot));
    if (url) {
        strncpy(doc->imgs[idx].url, url, 159);
        doc->imgs[idx].url[159] = '\0';
    }
    doc->ic++;
    return idx;
}

/* ============================================================================
* FINALIZE (calcula total_h em pixels: 16px por linha + altura das imgs)
* ============================================================================ */
void webdoc_finalize(WebDoc* doc) {
    if (!doc) return;
    int h = 0;
    for (int i = 0; i < doc->lc; i++) {
        if (doc->lines[i].style == WEB_STYLE_IMG &&
            doc->lines[i].img_slot >= 0 && doc->lines[i].img_slot < doc->ic) {
            WebImgSlot* im = &doc->imgs[doc->lines[i].img_slot];
            h += (im->h > 0 ? im->h : 200);  // fallback se não carregada
        } else {
            h += 16;
        }
    }
    doc->total_h = h;
}
