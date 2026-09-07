/*
====================================================================
Arquivo: web_img.c (SDK)
Versão: 1.0
Data: 31/08/2026
Autor: LBF-OS Team + AI Assistant

Descrição:
    Decodificador de imagens da web p/ o LBF Browser:
      - BMP 24/32-bit (BI_RGB/BI_BITFIELDS)
      - PNG 8-bit (colortypes 0/2/3/4/6, sem interlace) — NOVO
    Dispatch pela "magia" dos primeiros bytes.
    O inflate é uma portabilidade do algoritmo "puff" (zlib).
    webimg_decode() retorna pixels em buffer mallocado (caller free).

Testado em:
    - LBF-OS Base_v4.6.5 / Kernel v2.9 / Runtime v6.3
====================================================================
*/
#include "Runtime_sdk/sdk/libgui.h"
#include "../system/liblib.h"
#include "../system/string.h"

// Telemetria da última tentativa PNG (p/ o log do browser)
/* ============================================================================
* TELEMETRIA DA ÚLTIMA TENTATIVA PNG (p/ o log do browser)
* ============================================================================ */
static int g_d_w = 0, g_d_h = 0, g_d_ct = 0;
static int g_d_idat = 0, g_d_rawlen = 0, g_d_rl = 0;

void webimg_get_debug(int* w, int* h, int* ct, int* idat, int* rawlen, int* rl) {
    if (w) *w = g_d_w;
    if (h) *h = g_d_h;
    if (ct) *ct = g_d_ct;
    if (idat) *idat = g_d_idat;
    if (rawlen) *rawlen = g_d_rawlen;
    if (rl) *rl = g_d_rl;
}

/* ============================================================================
* INFLATE (port do "puff")
* ============================================================================ */
typedef struct {
    const uint8_t* in;  uint32_t inlen; uint32_t incnt;
    uint32_t bitbuf;    uint32_t bitcnt;
    uint8_t* out;       uint32_t outlen; uint32_t outcnt;
} pst_t;

static int p_bit(pst_t* s, int need) {
    uint32_t val = s->bitbuf;
    while (s->bitcnt < (uint32_t)need) {
        uint8_t b = (s->incnt < s->inlen) ? s->in[s->incnt++] : 0;
        val |= (uint32_t)b << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1));
}

typedef struct { short count[16]; short symbol[320]; } huff_t;

static int huff_construct(huff_t* h, const short* length, int n) {
    short offs[16];
    for (int i = 0; i < 16; i++) h->count[i] = 0;
    for (int i = 0; i < n; i++) h->count[length[i]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len <= 15; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return -1;
    }
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = offs[len] + h->count[len];
    for (int sym = 0; sym < n; sym++)
        if (length[sym] != 0) h->symbol[offs[length[sym]]++] = sym;
    return 0;
}

static int huff_decode(pst_t* s, const huff_t* h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        code |= p_bit(s, 1);
        int count = h->count[len];
        if (code - first < count) return h->symbol[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

static huff_t g_lfix, g_dfix;
static int g_fix_ready = 0;

static void fixed_tables(void) {
    if (g_fix_ready) return;
    short lengths[288];
    for (int i = 0; i < 144; i++) lengths[i] = 8;
    for (int i = 144; i < 256; i++) lengths[i] = 9;
    for (int i = 256; i < 280; i++) lengths[i] = 7;
    for (int i = 280; i < 288; i++) lengths[i] = 8;
    huff_construct(&g_lfix, lengths, 288);
    for (int i = 0; i < 30; i++) lengths[i] = 5;
    huff_construct(&g_dfix, lengths, 30);
    g_fix_ready = 1;
}

static const uint16_t LBASE[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,
    51,59,67,83,99,115,131,163,195,227,258};
static const uint8_t LEXT[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const uint16_t DBASE[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,
    513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const uint8_t DEXT[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static int puff_out(pst_t* s, const uint8_t* buf, uint32_t len) {
    if (s->outcnt + len > s->outlen) return -1;
    for (uint32_t i = 0; i < len; i++) s->out[s->outcnt++] = buf[i];
    return 0;
}

static int codes_block(pst_t* s, const huff_t* l, const huff_t* d) {
    for (;;) {
        int sym = huff_decode(s, l);
        if (sym < 0) return -1;
        if (sym < 256) {
            uint8_t c = (uint8_t)sym;
            if (puff_out(s, &c, 1)) return -1;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29) return -1;
            int len = LBASE[sym] + p_bit(s, LEXT[sym]);
            int dsym = huff_decode(s, d);
            if (dsym < 0 || dsym >= 30) return -1;
            int dist = DBASE[dsym] + p_bit(s, DEXT[dsym]);
            if ((uint32_t)dist > s->outcnt) return -1;
            uint32_t start = s->outcnt - (uint32_t)dist;
            for (int i = 0; i < len; i++) {
                if (s->outcnt >= s->outlen) return -1;
                s->out[s->outcnt++] = s->out[start + (uint32_t)i];
            }
        }
    }
}

static int stored_block(pst_t* s) {
    s->bitbuf = 0; s->bitcnt = 0;
    if (s->incnt + 4 > s->inlen) return -1;
    uint32_t len = s->in[s->incnt] | ((uint32_t)s->in[s->incnt + 1] << 8);
    uint32_t nlen = s->in[s->incnt + 2] | ((uint32_t)s->in[s->incnt + 3] << 8);
    s->incnt += 4;
    if ((len ^ 0xffff) != nlen) return -1;
    if (s->incnt + len > s->inlen || s->outcnt + len > s->outlen) return -1;
    for (uint32_t i = 0; i < len; i++) s->out[s->outcnt++] = s->in[s->incnt++];
    return 0;
}

static int dynamic_block(pst_t* s) {
    static const short order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    int nlen = p_bit(s, 5) + 257;
    int ndist = p_bit(s, 5) + 1;
    int ncode = p_bit(s, 4) + 4;
    if (nlen > 286 || ndist > 30) return -1;
    short lengths[320];
    for (int i = 0; i < ncode; i++) lengths[order[i]] = (short)p_bit(s, 3);
    for (int i = ncode; i < 19; i++) lengths[order[i]] = 0;
    huff_t lt;
    if (huff_construct(&lt, lengths, 19)) return -1;
    int index = 0;
    while (index < nlen + ndist) {
        int sym = huff_decode(s, &lt);
        if (sym < 0) return -1;
        if (sym < 16) lengths[index++] = (short)sym;
        else {
            int rep = 0;
            if (sym == 16) {
                if (index == 0) return -1;
                rep = lengths[index - 1];
                sym = 3 + p_bit(s, 2);
            } else if (sym == 17) sym = 3 + p_bit(s, 3);
            else { rep = 0; sym = 11 + p_bit(s, 7); }
            if (index + sym > nlen + ndist) return -1;
            while (sym--) lengths[index++] = (short)rep;
        }
    }
    if (lengths[256] == 0) return -1;
    huff_t ll, dd;
    if (huff_construct(&ll, lengths, nlen)) return -1;
    if (huff_construct(&dd, lengths + nlen, ndist)) return -1;
    return codes_block(s, &ll, &dd);
}

static int inflate_raw(const uint8_t* in, uint32_t inlen, uint8_t* out, uint32_t outlen) {
    // v1.1 FIX: PNG IDAT é um stream ZLIB (header CMF/FLG, ex: 78 9C),
    // NÃO deflate cru! Sem pular esses 2 bytes, o 0x78 era lido como um
    // "stored block" inválido -> inf=-1 em TODA imagem.
    if (inlen > 2 && (in[0] & 0x0F) == 0x08 &&
        ((((uint32_t)in[0]) << 8) | in[1]) % 31 == 0) {
        uint32_t skip = 2;
        if (in[1] & 0x20) skip += 4;          // FDICT (muito raro)
        if (skip < inlen) { in += skip; inlen -= skip; }
    }

    pst_t st;
    st.in = in; st.inlen = inlen; st.incnt = 0;
    st.bitbuf = 0; st.bitcnt = 0;
    st.out = out; st.outlen = outlen; st.outcnt = 0;
    fixed_tables();
    int last;
    do {
        last = p_bit(&st, 1);
        int type = p_bit(&st, 2);
        int r;
        if (type == 0) r = stored_block(&st);
        else if (type == 1) r = codes_block(&st, &g_lfix, &g_dfix);
        else if (type == 2) r = dynamic_block(&st);
        else return -1;
        if (r) return -1;
    } while (!last);
    return (int)st.outcnt;   // o adler32 do zlib fica no fim — ignoramos, ok
}

/* ============================================================================
* DECODIFICADOR PNG (8-bit, sem interlace)
* ============================================================================ */
static int png_decode(const uint8_t* d, int len, uint32_t** out_px, int* ow, int* oh) {
    static const uint8_t sig[8] = {137,80,78,71,13,10,26,10};

    // Reset da telemetria (cada tentativa começa limpa)
    g_d_w = g_d_h = g_d_ct = 0;
    g_d_idat = g_d_rawlen = g_d_rl = 0;

    if (len < 8 + 25) return -1;
    for (int i = 0; i < 8; i++) if (d[i] != sig[i]) return -2;

    const uint8_t* p = d + 8;
    if (p[4]!='I' || p[5]!='H' || p[6]!='D' || p[7]!='R') return -3;
    const uint8_t* ih = p + 8;
    uint32_t w = ((uint32_t)ih[0]<<24)|((uint32_t)ih[1]<<16)|((uint32_t)ih[2]<<8)|ih[3];
    uint32_t h = ((uint32_t)ih[4]<<24)|((uint32_t)ih[5]<<16)|((uint32_t)ih[6]<<8)|ih[7];
    uint8_t depth = ih[8], ctype = ih[9], inter = ih[12];

    // >>> DEBUG 1: dimensões e colortype lidos do IHDR
    g_d_w = (int)w; g_d_h = (int)h; g_d_ct = ctype;

    if (depth != 8 || inter != 0) return -4;

    int chans;
    switch (ctype) {
        case 0: chans = 1; break;
        case 2: chans = 3; break;
        case 3: chans = 1; break;
        case 4: chans = 2; break;
        case 6: chans = 4; break;
        default: return -5;
    }
    if (w == 0 || h == 0 || w > 512 || h > 512) return -6;

    uint8_t plte[256 * 3]; int plte_n = 0;
    uint8_t trns[256];     int trns_n = 0;

    // 1ª passada: tamanho total do IDAT
    uint32_t pos = 8, idat_total = 0;
    while (pos + 12 <= (uint32_t)len) {
        uint32_t clen = ((uint32_t)d[pos]<<24)|((uint32_t)d[pos+1]<<16)|((uint32_t)d[pos+2]<<8)|d[pos+3];
        if (pos + 12 + clen > (uint32_t)len) break;
        if (d[pos+4]=='I' && d[pos+5]=='D' && d[pos+6]=='A' && d[pos+7]=='T') idat_total += clen;
        pos += 12 + clen;
    }

    // >>> DEBUG 2: total de bytes comprimidos encontrados
    g_d_idat = (int)idat_total;

    if (idat_total == 0) return -7;

    uint8_t* comp = (uint8_t*)malloc(idat_total);
    if (!comp) return -8;
    uint32_t cc = 0;
    pos = 8;
    while (pos + 12 <= (uint32_t)len) {
        uint32_t clen = ((uint32_t)d[pos]<<24)|((uint32_t)d[pos+1]<<16)|((uint32_t)d[pos+2]<<8)|d[pos+3];
        const uint8_t* cd = d + pos + 8;
        if (pos + 12 + clen > (uint32_t)len) break;
        if (d[pos+4]=='I' && d[pos+5]=='D' && d[pos+6]=='A' && d[pos+7]=='T') {
            for (uint32_t i = 0; i < clen; i++) comp[cc++] = cd[i];
        }
        else if (d[pos+4]=='P' && d[pos+5]=='L' && d[pos+6]=='T' && d[pos+7]=='E') {
            plte_n = (int)(clen / 3); if (plte_n > 256) plte_n = 256;
            for (int i = 0; i < plte_n * 3; i++) plte[i] = cd[i];
        }
        else if (d[pos+4]=='t' && d[pos+5]=='R' && d[pos+6]=='N' && d[pos+7]=='S') {
            trns_n = (int)clen; if (trns_n > 256) trns_n = 256;
            for (int i = 0; i < trns_n; i++) trns[i] = cd[i];
        }
        pos += 12 + clen;
    }

    uint32_t stride = w * chans;
    uint32_t rawlen = (stride + 1) * h;

    // >>> DEBUG 3: tamanho esperado do stream descomprimido
    g_d_rawlen = (int)rawlen;

    uint8_t* raw = (uint8_t*)malloc(rawlen);
    if (!raw) { free(comp); return -8; }
    int rl = inflate_raw(comp, cc, raw, rawlen);

    // >>> DEBUG 4: resultado do inflate (<0 = erro de stream; != rawlen = truncado)
    g_d_rl = rl;

    free(comp);
    if (rl < 0 || (uint32_t)rl != rawlen) { free(raw); return -9; }

    // Unfilter (Sub/Up/Average/Paeth)
    uint8_t* img = (uint8_t*)malloc(stride * h);
    if (!img) { free(raw); return -8; }
    for (uint32_t y = 0; y < h; y++) {
        uint8_t f = raw[y * (stride + 1)];
        const uint8_t* sr = raw + y * (stride + 1) + 1;
        uint8_t* dst = img + y * stride;
        const uint8_t* prev = (y > 0) ? img + (y - 1) * stride : NULL;
        for (uint32_t x = 0; x < stride; x++) {
            uint8_t a = (x >= (uint32_t)chans) ? dst[x - chans] : 0;
            uint8_t b = prev ? prev[x] : 0;
            uint8_t c = (prev && x >= (uint32_t)chans) ? prev[x - chans] : 0;
            uint8_t v = sr[x];
            switch (f) {
                case 0: break;
                case 1: v = (uint8_t)(v + a); break;
                case 2: v = (uint8_t)(v + b); break;
                case 3: v = (uint8_t)(v + ((a + b) >> 1)); break;
                case 4: {
                    int pp = (int)a + b - c;
                    int pa = pp > (int)a ? pp - (int)a : (int)a - pp;
                    int pb = pp > (int)b ? pp - (int)b : (int)b - pp;
                    int pc = pp > (int)c ? pp - (int)c : (int)c - pp;
                    v = (uint8_t)(v + ((pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c)));
                    break;
                }
                default: free(raw); free(img); return -10;
            }
            dst[x] = v;
        }
    }
    free(raw);

    // Converte para 0xAARRGGBB
    uint32_t* px = (uint32_t*)malloc(w * h * 4);
    if (!px) { free(img); return -8; }
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            const uint8_t* s8 = img + y * stride + x * (uint32_t)chans;
            uint8_t r = 0, g = 0, b = 0;
            switch (ctype) {
                case 0: r = g = b = s8[0]; break;
                case 2: r = s8[0]; g = s8[1]; b = s8[2]; break;
                case 3: r = plte[s8[0]*3]; g = plte[s8[0]*3+1]; b = plte[s8[0]*3+2]; break;
                case 4: r = g = b = s8[0]; break;
                case 6: r = s8[0]; g = s8[1]; b = s8[2]; break;
            }
            px[y * w + x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
    free(img);
    *out_px = px; *ow = (int)w; *oh = (int)h;
    return 0;
}

/* ============================================================================
* DECODIFICADOR BMP (24/32-bit) — pixels mallocados
* ============================================================================ */
static int bmp_decode_buf(const uint8_t* data, int len, uint32_t** out_px, int* ow, int* oh) {
    if (len < 54 || data[0] != 'B' || data[1] != 'M') return -2;
    uint32_t data_offset = *(const uint32_t*)(data + 10);
    int32_t  w = *(const int32_t*)(data + 18);
    int32_t  h = *(const int32_t*)(data + 22);
    uint16_t bpp = *(const uint16_t*)(data + 28);
    uint32_t comp = *(const uint32_t*)(data + 30);
    if (bpp != 24 && bpp != 32) return -3;
    if (comp != 0 && comp != 3) return -3;
    if (w <= 0 || h == 0 || w * h > 256 * 256) return -5;
    int height = (h > 0) ? h : -h;
    int bppx = bpp / 8;
    int row_size = ((w * bppx + 3) / 4) * 4;

    uint32_t* px = (uint32_t*)malloc(w * height * 4);
    if (!px) return -8;
    for (int y = 0; y < height; y++) {
        int src_row = (h > 0) ? (height - 1 - y) : y;
        const uint8_t* row = data + data_offset + (uint32_t)src_row * row_size;
        if ((long)(row - data) + w * bppx > len) { free(px); return -6; }
        for (int x = 0; x < w; x++) {
            uint8_t b = row[x*bppx+0], g = row[x*bppx+1], r = row[x*bppx+2];
            px[y * w + x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
    *out_px = px; *ow = w; *oh = height;
    return 0;
}

/* ============================================================================
* DECODIFICADOR GIF (primeiro frame, LZW, interlace, transparência)
* ============================================================================ */
static int gif_lzw_decode(const uint8_t* comp, uint32_t clen, int minsize,
                          uint8_t* out, uint32_t need) {
    if (minsize < 2 || minsize > 11) return -1;
    static uint16_t prefix[4096];
    static uint8_t  suffix[4096];
    static uint8_t  stack[4096];
    int clearcode = 1 << minsize;
    int endcode = clearcode + 1;
    int nextcode = clearcode + 2;
    int codesize = minsize + 1;

    for (int i = 0; i < 4096; i++) prefix[i] = 0xFFFF;
    for (int i = 0; i < clearcode; i++) suffix[i] = (uint8_t)i;
    suffix[clearcode] = 0;
    suffix[endcode] = 0;

    uint32_t outn = 0, bitbuf = 0, cpos = 0;
    int nbits = 0, prev = -1;

    for (;;) {
        while (nbits < codesize && cpos < clen) {
            bitbuf |= (uint32_t)comp[cpos++] << nbits;
            nbits += 8;
        }
        if (nbits < codesize) break;                    // fim dos dados
        int code = (int)(bitbuf & ((1u << codesize) - 1));
        bitbuf >>= codesize;
        nbits -= codesize;

        if (code == clearcode) {                        // reset da tabela
            nextcode = clearcode + 2;
            codesize = minsize + 1;
            for (int i = 0; i < 4096; i++) prefix[i] = 0xFFFF;
            prev = -1;
            continue;
        }
        if (code == endcode) break;
        if (code > nextcode) break;                     // código inválido

        // KwKwK: code == nextcode usa prev + first(prev)
        int walk = (code < nextcode) ? code : prev;
        if (walk < 0) break;
        int sp = 0, c = walk;
        while (prefix[c] != 0xFFFF && sp < 4096) { stack[sp++] = suffix[c]; c = prefix[c]; }
        uint8_t first = suffix[c];
        stack[sp++] = first;
        while (sp > 0 && outn < need) out[outn++] = stack[--sp];

        if (prev >= 0 && nextcode < 4096) {             // nova entrada no dicionário
            prefix[nextcode] = (uint16_t)prev;
            suffix[nextcode] = first;
            nextcode++;
            if (nextcode >= (1 << codesize) && codesize < 12) codesize++; // early change
        }
        prev = code;
    }
    return (outn == need) ? 0 : -1;
}

static int gif_decode(const uint8_t* d, int len, uint32_t** out_px, int* ow, int* oh) {
    if (len < 13 || d[0]!='G' || d[1]!='I' || d[2]!='F') return -2;
    uint32_t pos = 6;
    uint16_t W = d[pos] | (d[pos+1]<<8);
    uint16_t H = d[pos+2] | (d[pos+3]<<8);
    uint8_t packed = d[pos+4];
    uint8_t bgcolor = d[pos+5];
    pos += 7;
    if (W == 0 || H == 0 || W > 512 || H > 512) return -6;

    uint8_t gct[256*3]; int gct_n = 0;
    if (packed & 0x80) {
        int n = 2 << (packed & 7);
        if (pos + (uint32_t)n*3 > (uint32_t)len) return -1;
        gct_n = n;
        for (int i = 0; i < n*3; i++) gct[i] = d[pos++];
    }

    uint8_t* canvas = (uint8_t*)malloc(W * H);
    if (!canvas) return -8;
    for (uint32_t i = 0; i < (uint32_t)W*H; i++) canvas[i] = bgcolor;

    uint8_t trans_idx = 0; bool has_trans = false;

    while (pos < (uint32_t)len) {
        uint8_t block = d[pos++];
        if (block == 0x3B) break;                       // trailer

        if (block == 0x21) {                            // extensão
            if (pos >= (uint32_t)len) break;
            uint8_t label = d[pos++];
            while (pos < (uint32_t)len) {
                uint8_t bl = d[pos++];
                if (bl == 0) break;
                if (pos + bl > (uint32_t)len) break;
                if (label == 0xF9 && bl >= 4) {         // graphic control
                    if (d[pos] & 1) { has_trans = true; trans_idx = d[pos+3]; }
                }
                pos += bl;
            }
            continue;
        }

        if (block == 0x2C) {                            // descritor de imagem
            uint16_t left = d[pos] | (d[pos+1]<<8);
            uint16_t top  = d[pos+2] | (d[pos+3]<<8);
            uint16_t iw   = d[pos+4] | (d[pos+5]<<8);
            uint16_t ih   = d[pos+6] | (d[pos+7]<<8);
            uint8_t ipk   = d[pos+8];
            pos += 9;

            const uint8_t* pal = gct; int pal_n = gct_n;
            uint8_t lct[256*3];
            if (ipk & 0x80) {
                int n = 2 << (ipk & 7);
                if (pos + (uint32_t)n*3 > (uint32_t)len) break;
                for (int i = 0; i < n*3; i++) lct[i] = d[pos++];
                pal = lct; pal_n = n;
            }
            bool inter = (ipk & 0x40) != 0;
            if (pos >= (uint32_t)len) break;
            int minsize = d[pos++];

            // junta os sub-blocos comprimidos
            uint32_t cap = 4096, clen = 0;
            uint8_t* comp = (uint8_t*)malloc(cap);
            if (!comp) { free(canvas); return -8; }
            while (pos < (uint32_t)len) {
                uint8_t bl = d[pos++];
                if (bl == 0) break;
                if (pos + bl > (uint32_t)len) break;
                if (clen + bl > cap) {
                    cap = (cap + bl) * 2;
                    uint8_t* p2 = (uint8_t*)realloc(comp, cap);
                    if (!p2) { free(comp); free(canvas); return -8; }
                    comp = p2;
                }
                for (uint8_t i = 0; i < bl; i++) comp[clen++] = d[pos++];
            }

            uint8_t* idx = (uint8_t*)malloc((uint32_t)iw * ih);
            if (!idx) { free(comp); free(canvas); return -8; }
            int rc = gif_lzw_decode(comp, clen, minsize, idx, (uint32_t)iw * ih);
            free(comp);
            if (rc < 0) { free(idx); free(canvas); return -9; }

            // posiciona no canvas (com passes de interlace)
            static const int istart[4] = {0,4,2,1};
            static const int istep[4]  = {8,8,4,2};
            uint32_t src = 0;
            if (!inter) {
                for (int y = 0; y < ih; y++)
                    for (int x = 0; x < iw; x++) {
                        uint8_t v = idx[src++];
                        int cx = left + x, cy = top + y;
                        if (cx < W && cy < H && !(has_trans && v == trans_idx))
                            canvas[cy*W + cx] = v;
                    }
            } else {
                for (int p = 0; p < 4; p++)
                    for (int y = istart[p]; y < ih; y += istep[p])
                        for (int x = 0; x < iw; x++) {
                            uint8_t v = idx[src++];
                            int cx = left + x, cy = top + y;
                            if (cx < W && cy < H && !(has_trans && v == trans_idx))
                                canvas[cy*W + cx] = v;
                        }
            }
            free(idx);

            // converte canvas (índices) -> pixels 0xAARRGGBB
            uint32_t* px = (uint32_t*)malloc((uint32_t)W * H * 4);
            if (!px) { free(canvas); return -8; }
            for (int i = 0; i < W*H; i++) {
                uint8_t v = canvas[i];
                uint8_t r = 0, g = 0, b = 0;
                if (v < pal_n) { r = pal[v*3]; g = pal[v*3+1]; b = pal[v*3+2]; }
                px[i] = 0xFF000000u | ((uint32_t)r<<16) | ((uint32_t)g<<8) | b;
            }
            free(canvas);
            *out_px = px; *ow = W; *oh = H;
            return 0;
        }
        break;   // bloco desconhecido
    }
    free(canvas);
    return -3;
}

/* ============================================================================
* DISPATCH: BM* -> BMP | \x89PNG -> PNG
* Retorna 0 = ok (out_px mallocado); negativo = formato/erro
* ============================================================================ */
int webimg_decode(const uint8_t* data, int len, uint32_t** out_px, int* out_w, int* out_h) {
    if (!data || len < 8 || !out_px) return -1;
    if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')
        return png_decode(data, len, out_px, out_w, out_h);
    if (data[0] == 'G' && data[1] == 'I' && data[2] == 'F' && data[3] == '8')   // v3.1
        return gif_decode(data, len, out_px, out_w, out_h);
    if (data[0] == 'B' && data[1] == 'M')
        return bmp_decode_buf(data, len, out_px, out_w, out_h);
    return -100;   // JPEG/WebP ainda não suportados
}
