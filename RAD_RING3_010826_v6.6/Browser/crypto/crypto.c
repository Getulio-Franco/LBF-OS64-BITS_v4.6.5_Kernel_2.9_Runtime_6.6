#include "Runtime_sdk/sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"
#include "Runtime_sdk/components/TOS_IPC.h"
#include "Browser/crypto/sha256.h"
#include "Browser/crypto/hmac.h"
#include "Browser/crypto/tls_prf.h"
#include "Browser/crypto/x25519.h"
#include "Browser/crypto/aes_gcm.h"
#include "Browser/crypto/rng.h"  

void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;
extern void GUI_Memo_AddStr(TGUIControl* memo, const char* str);
extern void GUI_Memo_Clear(TGUIControl* memo);

int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth = 520, winHeight = 430;
TGUIControl* BtnRun = NULL;
TGUIControl* LogMemo = NULL;

typedef struct {
    uint8_t dummy[sizeof(IPC_WINDOW_LIST[0])];
    volatile uint8_t fila_teclado_virtual;
    volatile uint8_t tem_evento_teclado;
} __attribute__((packed)) AppWindowInfoExtended;

static void hex2bin(const char* hex, uint8_t* out, int n) {
    int hv(char c){ return (c>='0'&&c<='9')?c-'0':(c>='a'&&c<='f')?c-'a'+10:(c>='A'&&c<='F')?c-'A'+10:0; }
    for (int i = 0; i < n; i++) out[i] = (uint8_t)((hv(hex[2*i]) << 4) | hv(hex[2*i+1]));
}

void Flush_Grafico_Janela(void) {
    gui_draw_form((TForm*)MyApp.MainWindow);
    gui_render_form((TForm*)MyApp.MainWindow);
    OS_IPC_FlipBuffers(my_app_slot, winWidth, winHeight);
}

static void hexstr(const uint8_t* b, int n, char* out) {
    static const char* hx = "0123456789abcdef";
    for (int i = 0; i < n; i++) { out[i*2] = hx[b[i]>>4]; out[i*2+1] = hx[b[i]&15]; }
    out[n*2] = '\0';
}

static void check(const char* nome, const uint8_t* got, const char* want) {
    char hg[96];
    hexstr(got, 32, hg);
    GUI_Memo_AddStr(LogMemo, nome);
    if (strcmp(hg, want) == 0) GUI_Memo_AddStr(LogMemo, " [PASS]\n");
    else {
        GUI_Memo_AddStr(LogMemo, " [FAIL]\n  got:  ");
        GUI_Memo_AddStr(LogMemo, hg);
        GUI_Memo_AddStr(LogMemo, "\n  want: ");
        GUI_Memo_AddStr(LogMemo, want);
        GUI_Memo_AddStr(LogMemo, "\n");
    }
}

static void hexstr_n(const uint8_t* b, int n, char* out) {
    static const char* hx = "0123456789abcdef";
    for (int i = 0; i < n; i++) { out[i*2] = hx[b[i]>>4]; out[i*2+1] = hx[b[i]&15]; }
    out[n*2] = '\0';
}

static void check_n(const char* nome, const uint8_t* got, int n, const char* want) {
    char hg[160];
    hexstr_n(got, n, hg);
    GUI_Memo_AddStr(LogMemo, nome);
    if (strcmp(hg, want) == 0) GUI_Memo_AddStr(LogMemo, " [PASS]\n");
    else {
        GUI_Memo_AddStr(LogMemo, " [FAIL]\n  got:  ");
        GUI_Memo_AddStr(LogMemo, hg);
        GUI_Memo_AddStr(LogMemo, "\n  want: ");
        GUI_Memo_AddStr(LogMemo, want);
        GUI_Memo_AddStr(LogMemo, "\n");
    }
}

static void run_tests(void) {
    uint8_t d[64];
    GUI_Memo_Clear(LogMemo);
    GUI_Memo_AddStr(LogMemo, "== TWebTLS Etapa 1a: vetores oficiais ==\n");

    sha256((const uint8_t*)"", 0, d);
    check_n("SHA256(\"\")", d, 32,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    sha256((const uint8_t*)"abc", 3, d);
    check_n("SHA256(abc)", d, 32,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    sha256((const uint8_t*)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, d);
    check_n("SHA256(2 blocos)", d, 32,
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    uint8_t k1[20]; for (int i = 0; i < 20; i++) k1[i] = 0x0b;
    hmac_sha256(k1, 20, (const uint8_t*)"Hi There", 8, d);
    check_n("HMAC RFC4231 #1", d, 32,
        "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    hmac_sha256((const uint8_t*)"Jefe", 4,
        (const uint8_t*)"what do ya want for nothing?", 28, d);
    check_n("HMAC RFC4231 #2", d, 32,
        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");

    uint8_t p1[48], p2[48];
    tls12_prf((const uint8_t*)"secret", 6, "master secret",
              (const uint8_t*)"client", 6, (const uint8_t*)"random", 6, p1, 48);
    tls12_prf((const uint8_t*)"secret", 6, "master secret",
              (const uint8_t*)"client", 6, (const uint8_t*)"random", 6, p2, 48);
    int ok = 1, nz = 0;
    for (int i = 0; i < 48; i++) { if (p1[i] != p2[i]) ok = 0; if (p1[i]) nz = 1; }
    GUI_Memo_AddStr(LogMemo, "PRF TLS1.2 sanity: ");
    GUI_Memo_AddStr(LogMemo, (ok && nz) ? "[PASS]\n" : "[FAIL]\n");

    GUI_Memo_AddStr(LogMemo, "\n== Etapa 1b: AES-GCM + X25519 ==\n");

    // AES-128 ECB (FIPS-197)
    uint8_t k16[16], pt[16], ct[16];
    hex2bin("000102030405060708090a0b0c0d0e0f", k16, 16);
    hex2bin("00112233445566778899aabbccddeeff", pt, 16);
    aes128_encrypt_block(k16, pt, ct);
    check_n("AES128 ECB FIPS197", ct, 16, "69c4e0d86a7b0430d8cdb78070b4c55a");

    // AES-GCM (McGrew-Viega caso 3: 64B, sem AAD)
    uint8_t gk[16], giv[12], gpt[64], gct[64], gtag[16];
    hex2bin("feffe9928665731c6d6a8f9467308308", gk, 16);
    hex2bin("cafebabefacedbaddecaf888", giv, 12);
    hex2bin("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51"
            "30c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710", gpt, 64);
    aes_gcm_encrypt(gk, giv, NULL, 0, gpt, 64, gct, gtag);
    check_n("GCM ct (caso 3)", gct, 64,
        "f0739205f7b3ed570716566358b6e52c"
        "cb20022b2735ffa6853a21b2e632a50d"
        "0d21046162527e5eb7bba567ff284dcf"
        "5c56c2891f48d1d72a10dad3bbfcbcc0");
    check_n("GCM tag (caso 3)", gtag, 16, "7f1be38667a4496c407ca72f6266923f");

    // Caso 4 (com AAD)
    uint8_t aad[20];
    hex2bin("feedfacedeadbeeffeedfacedeadbeefabaddad2", aad, 20);
    aes_gcm_encrypt(gk, giv, aad, 20, gpt, 64, gct, gtag);
    check_n("GCM tag (caso 4)", gtag, 16, "e8c707f64c948de7ce2de5333d8f24fd");

    // X25519 (RFC 7748 §6.1)
    uint8_t apriv[32], apub[32], bpub[32], shared[32];
    hex2bin("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", apriv, 32);
    x25519_base(apub, apriv);
    check_n("X25519 pub Alice", apub, 32,
        "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
    hex2bin("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", bpub, 32);
    x25519(shared, apriv, bpub);
    check_n("X25519 shared", shared, 32,
        "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");

    // ==================== NOVO: Etapa 1c — RNG real ====================
    GUI_Memo_AddStr(LogMemo, "\n== Etapa 1c: RNG real (DRBG + RDRAND) ==\n");
    uint8_t r1[32], r2[32];
    rng_bytes(r1, 32);
    rng_bytes(r2, 32);
    int diff = 0, nz_rng = 0;   // ← RENOMEADO de 'nz' para 'nz_rng'
    for (int i = 0; i < 32; i++) { 
        if (r1[i] != r2[i]) diff = 1; 
        if (r1[i]) nz_rng = 1;  // ← ajuste aqui
    }
    GUI_Memo_AddStr(LogMemo, "RNG dois blocos distintos: ");
    GUI_Memo_AddStr(LogMemo, (diff && nz_rng) ? "[PASS]\n" : "[FAIL]\n");  // ← e aqui
    char rh[80];
    hexstr_n(r1, 16, rh);
    GUI_Memo_AddStr(LogMemo, "rng[0..15]=");
    GUI_Memo_AddStr(LogMemo, rh);
    GUI_Memo_AddStr(LogMemo, "\n");
    // ================================================================

    GUI_Memo_AddStr(LogMemo, "\n== Fim: cripto completa + RNG real p/ handshake ==\n");
    Flush_Grafico_Janela();
}

void OnBtnRunClick(void* s) { (void)s; run_tests(); }

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    static int ultimo_x = 0, ultimo_y = 0, mouse_hold_timer = 0;
    static bool primeiro_desenho = true, ultimo_estado_foco = false;
    void* ultimo_controle_focado = NULL;

    graphics_init_app(winWidth, winHeight);
    wm_init();
    my_app_slot = OS_IPC_RegisterApp("Crypto Test", winWidth, winHeight);
    if (my_app_slot == -1) return -1;
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "TWebTLS - Crypto Test v1.0", winWidth, winHeight);
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0xC0C0C0);

    BtnRun = GUI_CreateButton(&MyApp, 10, 40, 180, 30, "RODAR TESTES", OnBtnRunClick);
    LogMemo = GUI_CreateMemo(&MyApp, 10, 80, 490, 330);
    gui_set_prop(LogMemo, PROP_COLOR, 0x000000);

    Flush_Grafico_Janela();
    run_tests();   // auto-run

    while (1) {
        if (IPC_WINDOW_LIST[my_app_slot].is_active == 0) break;
        bool precisa_redesenhar = false;
        if (primeiro_desenho) { primeiro_desenho = false; precisa_redesenhar = true; }
        bool foco = (IPC_CONTROL->active_focus_slot == my_app_slot);
        if (foco != ultimo_estado_foco) {
            ultimo_estado_foco = foco;
            if (MyApp.MainWindow) ((TForm*)MyApp.MainWindow)->ActiveFocus = foco;
            precisa_redesenhar = true;
        }
        if (IPC_WINDOW_LIST[my_app_slot].has_click_event == 1) {
            if (mouse_hold_timer == 0) {
                int rel_x = IPC_WINDOW_LIST[my_app_slot].local_click_x;
                int rel_y = IPC_WINDOW_LIST[my_app_slot].local_click_y;
                ultimo_x = rel_x; ultimo_y = rel_y;
                mouse_hold_timer = 2;
                if (BtnRun && rel_x >= BtnRun->Left && rel_x < (BtnRun->Left + BtnRun->Width) &&
                    rel_y >= BtnRun->Top && rel_y < (BtnRun->Top + BtnRun->Height))
                    gui_set_prop(BtnRun, PROP_STATE, 2);
                events_process_mouse(rel_x, rel_y, 1, 0);
                if (GUI_ProcessMouseClick(&MyApp, rel_x, rel_y)) precisa_redesenhar = true;
            }
            IPC_WINDOW_LIST[my_app_slot].has_click_event = 0;
        }
        if (mouse_hold_timer > 0) {
            mouse_hold_timer--;
            if (mouse_hold_timer == 0) {
                if (BtnRun) gui_set_prop(BtnRun, PROP_STATE, 0);
                events_process_mouse(ultimo_x, ultimo_y, 0, 0);
                precisa_redesenhar = true;
            }
        }
        if (precisa_redesenhar) Flush_Grafico_Janela();
        sys_sleep(foco ? 16 : 32);
    }
    sys_exit();
    return 0;
}
