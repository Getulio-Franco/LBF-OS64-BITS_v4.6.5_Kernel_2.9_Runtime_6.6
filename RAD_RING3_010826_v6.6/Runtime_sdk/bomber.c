#include "sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"

// Componentes do Sistema
#include "components/TOS_IPC.h"     
#include "components/TOSSerial.h"   

#define M_PI 3.14159265358979323846f

// Protótipos obrigatórios
void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;

// Dimensões da Janela Atualizadas para Acomodar TILE_SIZE Maior
int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth = 620;
const int winHeight = 580;

/* ============================================================================
 * SINTETIZADOR DE EFEITOS SONOROS (SFX PROCEDURAL)
 * ============================================================================ */
#define SFX_SHORT_SAMPLES 6000     
#define SFX_MEDIUM_SAMPLES 12000   
#define SFX_LONG_SAMPLES  20000    

static int16_t sfx_plant_bomb[SFX_SHORT_SAMPLES * 2] __attribute__((aligned(4096)));
static int16_t sfx_explosion[SFX_LONG_SAMPLES * 2] __attribute__((aligned(4096)));
static int16_t sfx_enemy_die[SFX_MEDIUM_SAMPLES * 2] __attribute__((aligned(4096)));
static int16_t sfx_player_die[SFX_LONG_SAMPLES * 2] __attribute__((aligned(4096)));
static int16_t sfx_powerup[SFX_SHORT_SAMPLES * 2] __attribute__((aligned(4096)));

static float custom_sinf(float x) {
    while (x > M_PI)  x -= 2.0f * M_PI;
    while (x < -M_PI) x += 2.0f * M_PI;
    float x2 = x * x;
    float x3 = x * x2;
    float x5 = x3 * x2;
    float x7 = x5 * x2;
    return x - (x3 / 6.0f) + (x5 / 120.0f) - (x7 / 5040.0f);
}

void Init_Audio_SFX(void) {
    uint32_t sample_rate = 48000;
    
    for (int i = 0; i < SFX_SHORT_SAMPLES; i++) {
        float freq = 300.0f - (150.0f * ((float)i / SFX_SHORT_SAMPLES));
        float rad_step = (2.0f * M_PI * freq) / sample_rate;
        float envelope = 1.0f - ((float)i / SFX_SHORT_SAMPLES);
        int16_t val = (int16_t)(custom_sinf(i * rad_step) * 18000.0f * envelope);
        sfx_plant_bomb[i*2] = val; sfx_plant_bomb[i*2+1] = val;
    }

    for (int i = 0; i < SFX_LONG_SAMPLES; i++) {
        float freq = 120.0f - (100.0f * ((float)i / SFX_LONG_SAMPLES));
        if (freq < 20.0f) freq = 20.0f;
        float rad_step = (2.0f * M_PI * freq) / sample_rate;
        float envelope = 1.0f - ((float)i / SFX_LONG_SAMPLES);
        float noise = (float)(((uint32_t)i * 1103515245U + 12345U) % 32768) / 32768.0f - 0.5f;
        int16_t val = (int16_t)((custom_sinf(i * rad_step) * 0.4f + noise * 0.6f) * 22000.0f * envelope);
        sfx_explosion[i*2] = val; sfx_explosion[i*2+1] = val;
    }

    for (int i = 0; i < SFX_MEDIUM_SAMPLES; i++) {
        float freq = 800.0f - (600.0f * ((float)i / SFX_MEDIUM_SAMPLES));
        float rad_step = (2.0f * M_PI * freq) / sample_rate;
        float envelope = 1.0f - ((float)i / SFX_MEDIUM_SAMPLES);
        int16_t val = (int16_t)(custom_sinf(i * rad_step) * 16000.0f * envelope);
        sfx_enemy_die[i*2] = val; sfx_enemy_die[i*2+1] = val;
    }

    for (int i = 0; i < SFX_LONG_SAMPLES; i++) {
        float freq = 400.0f - (380.0f * ((float)i / SFX_LONG_SAMPLES));
        float rad_step = (2.0f * M_PI * freq) / sample_rate;
        float envelope = 1.0f - ((float)i / SFX_LONG_SAMPLES);
        int16_t val = (int16_t)(custom_sinf(i * rad_step) * 20000.0f * envelope);
        sfx_player_die[i*2] = val; sfx_player_die[i*2+1] = val;
    }

    for (int i = 0; i < SFX_SHORT_SAMPLES; i++) {
        float freq = 600.0f + (400.0f * custom_sinf(i * 0.05f));
        float rad_step = (2.0f * M_PI * freq) / sample_rate;
        float envelope = 1.0f - ((float)i / SFX_SHORT_SAMPLES);
        int16_t val = (int16_t)(custom_sinf(i * rad_step) * 15000.0f * envelope);
        sfx_powerup[i*2] = val; sfx_powerup[i*2+1] = val;
    }
}

static int sound_cooldown = 0;
static bool sound_busy = false;

static inline void play_sound(int16_t* buffer, uint32_t size, bool force_play) {
    if (sound_busy && !force_play) return;
    if (force_play || sound_cooldown == 0) {
        if (force_play) {
            sys_audio_stop();
            sound_busy = false;
        }
        sys_audio_play(buffer, size, 0);
        sound_busy = true;
        sound_cooldown = 3;
    }
}

/* ============================================================================
 * COMUNICAÇÃO IPC
 * ============================================================================ */
typedef struct {
    uint8_t dummy[sizeof(IPC_WINDOW_LIST[0])]; 
    volatile uint8_t fila_teclado_virtual;
    volatile uint8_t tem_evento_teclado;
} __attribute__((packed)) AppWindowInfoExtended;

char Obter_Tecla_Entrada(void) {
    if (my_app_slot < 0) return 0;
    AppWindowInfoExtended* ext_slot = (AppWindowInfoExtended*)&IPC_WINDOW_LIST[my_app_slot];

    if (ext_slot->tem_evento_teclado == 1) {
        char key = (char)ext_slot->fila_teclado_virtual;
        ext_slot->tem_evento_teclado = 0; 
        return key;
    }
    return 0;
}

/* ============================================================================
 * LÓGICA DO BOMBERMAN
 * ============================================================================ */
#define MAP_COLS 13
#define MAP_ROWS 11

// Tamanho do quadrado ajustado de 32 para 40
#define TILE_SIZE 40

// Offsets recalculados para centralizar o mapa de 520x440 na janela de 620x580
#define OFFSET_X 50
#define OFFSET_Y 90

#define TILE_EMPTY 0
#define TILE_WALL  1
#define TILE_BLOCK 2
#define TILE_BOMB  3
#define TILE_FIRE  4

#define MAX_BOMBS 3
#define MAX_ENEMIES 4
#define MAX_POWERUPS 10

#define POWERUP_BOMB 1
#define POWERUP_RANGE 2
#define POWERUP_SPEED 3

typedef struct {
    int gx, gy;
    int timer;
    int range;
    int active;
} Bomb;

typedef struct {
    int gx, gy;
    int timer;
    int active;
} FireTile;

typedef struct {
    int gx, gy;
    int dir_x, dir_y;
    int move_timer;
    int alive;
    int speed;
} Enemy;

typedef struct {
    int gx, gy;
    int type;
    int active;
} PowerUp;

static int grid[MAP_ROWS][MAP_COLS];
static Bomb bombs[MAX_BOMBS];
static FireTile fire_map[MAP_ROWS][MAP_COLS];
static Enemy enemies[MAX_ENEMIES];
static PowerUp powerups[MAX_POWERUPS];

static int player_gx = 1;
static int player_gy = 1;
static int player_lives = 3;
static int score = 0;
static int fase = 1;
static int max_bombs = 1;
static int bomb_range = 2;

static int game_over = 0;
static int game_won = 0;
static uint32_t game_ticks = 0;
static int enemies_alive = 0;

static void int_to_str(int num, char* str) {
    int i = 0;
    if (num == 0) { str[0] = '0'; str[1] = '\0'; return; }
    int is_neg = 0;
    if (num < 0) { is_neg = 1; num = -num; }
    do {
        str[i++] = (num % 10) + '0';
        num /= 10;
    } while (num > 0);
    if (is_neg) str[i++] = '-';
    str[i] = '\0';
    int start = 0, end = i - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++; end--;
    }
}

void Init_Map(void) {
    for (int r = 0; r < MAP_ROWS; r++) {
        for (int c = 0; c < MAP_COLS; c++) {
            fire_map[r][c].active = 0;
            
            if (r == 0 || r == MAP_ROWS - 1 || c == 0 || c == MAP_COLS - 1 || (r % 2 == 0 && c % 2 == 0)) {
                grid[r][c] = TILE_WALL;
            } else {
                if ((r <= 2 && c <= 2)) {
                    grid[r][c] = TILE_EMPTY;
                } else {
                    grid[r][c] = ((r + c + game_ticks) % 3 == 0) ? TILE_BLOCK : TILE_EMPTY;
                }
            }
        }
    }
}

void Spawn_PowerUp(int gx, int gy) {
    int rand_type = (game_ticks + gx * 7 + gy * 13) % 3 + 1;
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (!powerups[i].active) {
            powerups[i].gx = gx;
            powerups[i].gy = gy;
            powerups[i].type = rand_type;
            powerups[i].active = 1;
            break;
        }
    }
}

void Init_Enemies(int quantidade) {
    int max_enemies = (quantidade < MAX_ENEMIES) ? quantidade : MAX_ENEMIES;
    int spawn_positions[MAX_ENEMIES][2] = {
        {MAP_COLS - 2, 1},
        {1, MAP_ROWS - 2},
        {MAP_COLS - 2, MAP_ROWS - 2},
        {MAP_COLS - 4, 3}
    };

    enemies_alive = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (i < max_enemies) {
            enemies[i].gx = spawn_positions[i][0];
            enemies[i].gy = spawn_positions[i][1];
            enemies[i].dir_x = (i % 2 == 0) ? 1 : -1;
            enemies[i].dir_y = 0;
            enemies[i].move_timer = 0;
            enemies[i].alive = 1;
            enemies[i].speed = 18 + (i * 4);
            enemies_alive++;
        } else {
            enemies[i].alive = 0;
        }
        grid[enemies[i].gy][enemies[i].gx] = TILE_EMPTY;
    }
}

void Reset_Full_Game(void) {
    player_lives = 3;
    score = 0;
    fase = 1;
    game_over = 0;
    game_won = 0;
    player_gx = 1;
    player_gy = 1;
    max_bombs = 1;
    bomb_range = 2;
    game_ticks = 0;

    for (int i = 0; i < MAX_BOMBS; i++) bombs[i].active = 0;
    for (int i = 0; i < MAX_POWERUPS; i++) powerups[i].active = 0;

    Init_Map();
    Init_Enemies(3 + fase);
    sys_audio_stop();
}

void Advance_Fase(void) {
    fase++;
    player_gx = 1;
    player_gy = 1;
    max_bombs++;
    if (max_bombs > MAX_BOMBS) max_bombs = MAX_BOMBS;
    bomb_range++;
    if (bomb_range > 4) bomb_range = 4;
    
    for (int i = 0; i < MAX_BOMBS; i++) bombs[i].active = 0;
    for (int i = 0; i < MAX_POWERUPS; i++) powerups[i].active = 0;
    
    Init_Map();
    Init_Enemies(3 + fase);
    game_won = 0;
}

void Plant_Bomb(void) {
    if (grid[player_gy][player_gx] == TILE_BOMB) return;

    int active_bombs = 0;
    for (int i = 0; i < MAX_BOMBS; i++) {
        if (bombs[i].active) active_bombs++;
    }
    
    if (active_bombs >= max_bombs) return;

    for (int i = 0; i < MAX_BOMBS; i++) {
        if (!bombs[i].active) {
            bombs[i].gx = player_gx;
            bombs[i].gy = player_gy;
            bombs[i].timer = 60;
            bombs[i].range = bomb_range;
            bombs[i].active = 1;
            grid[player_gy][player_gx] = TILE_BOMB;
            play_sound(sfx_plant_bomb, sizeof(sfx_plant_bomb), false);
            break;
        }
    }
}

void Trigger_Explosion(int b_idx) {
    bombs[b_idx].active = 0;
    int gx = bombs[b_idx].gx;
    int gy = bombs[b_idx].gy;
    int range = bombs[b_idx].range;

    grid[gy][gx] = TILE_EMPTY;
    fire_map[gy][gx].active = 1;
    fire_map[gy][gx].timer = 15;

    play_sound(sfx_explosion, sizeof(sfx_explosion), true);

    int dx[] = {0, 0, 1, -1};
    int dy[] = {1, -1, 0, 0};

    for (int d = 0; d < 4; d++) {
        for (int r = 1; r <= range; r++) {
            int nx = gx + dx[d] * r;
            int ny = gy + dy[d] * r;

            if (nx < 0 || nx >= MAP_COLS || ny < 0 || ny >= MAP_ROWS) break;

            if (grid[ny][nx] == TILE_WALL) break;

            fire_map[ny][nx].active = 1;
            fire_map[ny][nx].timer = 15;

            if (grid[ny][nx] == TILE_BLOCK) {
                grid[ny][nx] = TILE_EMPTY;
                score += 50;
                if ((game_ticks + nx * 7 + ny * 13) % 100 < 30) {
                    Spawn_PowerUp(nx, ny);
                }
                break;
            }
        }
    }
}

void Update_Game(void) {
    if (game_over || game_won) return;

    game_ticks++;

    if (sound_cooldown > 0) sound_cooldown--;

    // 1. Processar Bombas
    for (int i = 0; i < MAX_BOMBS; i++) {
        if (bombs[i].active) {
            bombs[i].timer--;
            if (bombs[i].timer <= 0) {
                Trigger_Explosion(i);
            }
        }
    }

    // 2. Processar Fogo, Colisão de Dano e Destruição
    for (int r = 0; r < MAP_ROWS; r++) {
        for (int c = 0; c < MAP_COLS; c++) {
            if (fire_map[r][c].active) {
                fire_map[r][c].timer--;
                if (fire_map[r][c].timer <= 0) {
                    fire_map[r][c].active = 0;
                }

                // Dano no Jogador pelo Fogo
                if (r == player_gy && c == player_gx) {
                    player_lives--;
                    play_sound(sfx_player_die, sizeof(sfx_player_die), true);
                    player_gx = 1; player_gy = 1;
                    if (player_lives <= 0) game_over = 1;
                }

                // Inimigo atinge o fogo (Morre na bomba)
                for (int e = 0; e < MAX_ENEMIES; e++) {
                    if (enemies[e].alive && enemies[e].gx == c && enemies[e].gy == r) {
                        enemies[e].alive = 0;
                        enemies_alive--;
                        score += 200;
                        play_sound(sfx_enemy_die, sizeof(sfx_enemy_die), false);
                        if ((game_ticks + e * 11) % 100 < 40) {
                            Spawn_PowerUp(c, r);
                        }
                    }
                }
            }
        }
    }

    // 3. Coleta de Power-ups
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (powerups[i].active && powerups[i].gx == player_gx && powerups[i].gy == player_gy) {
            play_sound(sfx_powerup, sizeof(sfx_powerup), false);
            switch(powerups[i].type) {
                case POWERUP_BOMB:
                    max_bombs++;
                    if (max_bombs > MAX_BOMBS) max_bombs = MAX_BOMBS;
                    break;
                case POWERUP_RANGE:
                    bomb_range++;
                    if (bomb_range > 4) bomb_range = 4;
                    break;
                case POWERUP_SPEED:
                    break;
            }
            powerups[i].active = 0;
        }
    }

    // 4. Movimentação e Colisão dos Inimigos
    for (int e = 0; e < MAX_ENEMIES; e++) {
        if (!enemies[e].alive) continue;

        enemies[e].move_timer++;
        if (enemies[e].move_timer >= enemies[e].speed) {
            enemies[e].move_timer = 0;

            if ((game_ticks + e * 7) % 10 < 6) {
                int dx = player_gx - enemies[e].gx;
                int dy = player_gy - enemies[e].gy;
                if (dx < 0) dx = -dx;
                if (dy < 0) dy = -dy;
                if (dx > dy) {
                    enemies[e].dir_x = (player_gx > enemies[e].gx) ? 1 : -1;
                    enemies[e].dir_y = 0;
                } else {
                    enemies[e].dir_y = (player_gy > enemies[e].gy) ? 1 : -1;
                    enemies[e].dir_x = 0;
                }
            } else {
                if ((game_ticks + e * 13) % 4 == 0) {
                    int dir = (game_ticks + e * 3) % 4;
                    enemies[e].dir_x = (dir == 0) ? 1 : (dir == 1) ? -1 : 0;
                    enemies[e].dir_y = (dir == 2) ? 1 : (dir == 3) ? -1 : 0;
                }
            }

            int nx = enemies[e].gx + enemies[e].dir_x;
            int ny = enemies[e].gy + enemies[e].dir_y;

            // Inimigo só se move para espaços livres, sem bombas ou fogo
            if (nx >= 0 && nx < MAP_COLS && ny >= 0 && ny < MAP_ROWS &&
                grid[ny][nx] == TILE_EMPTY && !fire_map[ny][nx].active) {
                enemies[e].gx = nx;
                enemies[e].gy = ny;
            }

            // Colisão Direta: Inimigo encostou no jogador -> Jogador Morre
            if (enemies[e].gx == player_gx && enemies[e].gy == player_gy) {
                player_lives--;
                play_sound(sfx_player_die, sizeof(sfx_player_die), true);
                player_gx = 1; player_gy = 1;
                if (player_lives <= 0) game_over = 1;
            }
        }
    }

    if (enemies_alive == 0) {
        game_won = 1;
    }
}

/* ============================================================================
 * RENDERING GRÁFICO
 * ============================================================================ */
void Render_Game(void) {
    uint32_t* buf = (uint32_t*)graphics_get_buffer();
    if (!buf) return;

    // Fundo do HUD
    graphics_fill_rect(OFFSET_X, OFFSET_Y - 40, MAP_COLS * TILE_SIZE, 32, 0x111111);

    char hud_buf[16];
    
    // SCORE
    sys_draw_string(OFFSET_X + 10, OFFSET_Y - 28, "SCORE:", 0xFFFFFF, 1);
    int_to_str(score, hud_buf);
    sys_draw_string(OFFSET_X + 75, OFFSET_Y - 28, hud_buf, 0xFFFF00, 1);

    // VIDAS
    sys_draw_string(OFFSET_X + 200, OFFSET_Y - 28, "VIDAS:", 0xFFFFFF, 1);
    int_to_str(player_lives, hud_buf);
    sys_draw_string(OFFSET_X + 265, OFFSET_Y - 28, hud_buf, 0xFF0000, 1);

    // FASE
    sys_draw_string(OFFSET_X + 380, OFFSET_Y - 28, "FASE:", 0xFFFFFF, 1);
    int_to_str(fase, hud_buf);
    sys_draw_string(OFFSET_X + 435, OFFSET_Y - 28, hud_buf, 0x00FFFF, 1);

    // Grid do Mapa
    for (int r = 0; r < MAP_ROWS; r++) {
        for (int c = 0; c < MAP_COLS; c++) {
            int px = OFFSET_X + c * TILE_SIZE;
            int py = OFFSET_Y + r * TILE_SIZE;

            if (grid[r][c] == TILE_WALL) {
                graphics_fill_rect(px, py, TILE_SIZE, TILE_SIZE, 0x555555);
                graphics_draw_rect(px, py, TILE_SIZE, TILE_SIZE, 0x222222);
            } else if (grid[r][c] == TILE_BLOCK) {
                graphics_fill_rect(px, py, TILE_SIZE, TILE_SIZE, 0x8B4513);
                graphics_draw_rect(px, py, TILE_SIZE, TILE_SIZE, 0x5C2E0B);
            } else {
                graphics_fill_rect(px, py, TILE_SIZE, TILE_SIZE, 0x1E4D2B);
            }

            // Bomba
            if (grid[r][c] == TILE_BOMB) {
                graphics_fill_rect(px + 8, py + 8, TILE_SIZE - 16, TILE_SIZE - 16, 0x111111);
                graphics_fill_rect(px + 16, py + 3, 8, 5, 0xFF0000);
            }

            // Fogo da Explosão
            if (fire_map[r][c].active) {
                graphics_fill_rect(px, py, TILE_SIZE, TILE_SIZE, 0xFF4500);
                graphics_fill_rect(px + 6, py + 6, TILE_SIZE - 12, TILE_SIZE - 12, 0xFFFF00);
            }
        }
    }

    // Power-ups
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (powerups[i].active) {
            int px = OFFSET_X + powerups[i].gx * TILE_SIZE;
            int py = OFFSET_Y + powerups[i].gy * TILE_SIZE;
            int color = (powerups[i].type == POWERUP_BOMB) ? 0xFF0000 : 
                        (powerups[i].type == POWERUP_RANGE) ? 0x00FF00 : 0x0000FF;
            graphics_fill_rect(px + 6, py + 6, TILE_SIZE - 12, TILE_SIZE - 12, color);
        }
    }

    // Inimigos
    for (int e = 0; e < MAX_ENEMIES; e++) {
        if (enemies[e].alive) {
            int px = OFFSET_X + enemies[e].gx * TILE_SIZE;
            int py = OFFSET_Y + enemies[e].gy * TILE_SIZE;
            graphics_fill_rect(px + 5, py + 5, TILE_SIZE - 10, TILE_SIZE - 10, 0xFF0055);
            graphics_fill_rect(px + 10, py + 10, 5, 5, 0xFFFFFF);
            graphics_fill_rect(px + 25, py + 10, 5, 5, 0xFFFFFF);
        }
    }

    // Jogador (Bomberman)
    int p_px = OFFSET_X + player_gx * TILE_SIZE;
    int p_py = OFFSET_Y + player_gy * TILE_SIZE;
    graphics_fill_rect(p_px + 5, p_py + 5, TILE_SIZE - 10, TILE_SIZE - 10, 0x00CCFF);
    graphics_fill_rect(p_px + 10, p_py + 10, 5, 5, 0x000000);
    graphics_fill_rect(p_px + 25, p_py + 10, 5, 5, 0x000000);

    // Telas de Fim de Jogo
    if (game_over) {
        graphics_fill_rect(OFFSET_X + 100, OFFSET_Y + 150, 320, 90, 0x440000);
        graphics_draw_rect(OFFSET_X + 100, OFFSET_Y + 150, 320, 90, 0xFF0000);
        sys_draw_string(OFFSET_X + 190, OFFSET_Y + 170, "GAME OVER!", 0xFFFFFF, 1);
        sys_draw_string(OFFSET_X + 120, OFFSET_Y + 200, "Pressione 'R' para Reiniciar", 0xFFFF00, 1);
    } else if (game_won) {
        graphics_fill_rect(OFFSET_X + 100, OFFSET_Y + 150, 320, 90, 0x004400);
        graphics_draw_rect(OFFSET_X + 100, OFFSET_Y + 150, 320, 90, 0x00FF00);
        sys_draw_string(OFFSET_X + 175, OFFSET_Y + 170, "VOCE VENCEU!", 0xFFFFFF, 1);
        sys_draw_string(OFFSET_X + 120, OFFSET_Y + 200, "Pressione 'R' p/ Proxima Fase", 0xFFFF00, 1);
    }
}

void Flush_Grafico_Janela(void) {
    if (my_app_slot == -1) return;
    gui_draw_form((TForm*)MyApp.MainWindow);
    gui_render_form((TForm*)MyApp.MainWindow);
    Render_Game();
    OS_IPC_FlipBuffers(my_app_slot, winWidth, winHeight);
}

void Tratar_Fechamento_Software(void) {
    if (my_app_slot == -1) return;
    sys_audio_stop();
    sound_busy = false;
    if (MyApp.MainWindow) {
        gui_set_prop(MyApp.MainWindow, PROP_VISIBLE, 0);
    }
    uint32_t* b0 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_0;
    uint32_t* b1 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_1;
    if (b0) memset(b0, 0, winWidth * winHeight * 4);
    if (b1) memset(b1, 0, winWidth * winHeight * 4);
    IPC_WINDOW_LIST[my_app_slot].is_active = 0;
    sys_sleep(50); 
}

/* ============================================================================
 * MAIN
 * ============================================================================ */
int main(int argc, char* argv[]) {
    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;

    graphics_init_app(winWidth, winHeight);
    wm_init();

    my_app_slot = OS_IPC_RegisterApp("Bomberman LBF", winWidth, winHeight);
    if (my_app_slot == -1) return -1;

    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Bomberman LBF v1.1", winWidth, winHeight);

    if (MyApp.MainWindow) {
        gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0x000000); 
    }

    Init_Audio_SFX();
    Reset_Full_Game();
    Flush_Grafico_Janela();

    while (1) {
        if (IPC_WINDOW_LIST[my_app_slot].is_active == 0) {
            Tratar_Fechamento_Software();
            break;
        }

        bool precisa_redesenhar = false;

        if (primeiro_desenho) {
            primeiro_desenho = false;
            precisa_redesenhar = true;
        }

        bool euTenhoFoco = (IPC_CONTROL->active_focus_slot == my_app_slot);
        if (euTenhoFoco != ultimo_estado_foco) {
            ultimo_estado_foco = euTenhoFoco;
            if (MyApp.MainWindow) { ((TForm*)MyApp.MainWindow)->ActiveFocus = euTenhoFoco; }
            precisa_redesenhar = true;
        }

        if (euTenhoFoco) {
            char key = Obter_Tecla_Entrada();
            if (key != 0) {
                int next_gx = player_gx;
                int next_gy = player_gy;
                bool move = false;

                if (key == 'a' || key == 'A' || key == '4') {
                    next_gx--; move = true;
                } else if (key == 'd' || key == 'D' || key == '6') {
                    next_gx++; move = true;
                } else if (key == 'w' || key == 'W' || key == '8') {
                    next_gy--; move = true;
                } else if (key == 's' || key == 'S' || key == '2') {
                    next_gy++; move = true;
                } else if (key == ' ' || key == '5') {
                    Plant_Bomb();
                } else if (key == 'r' || key == 'R') {
                    if (game_over) {
                        Reset_Full_Game();
                    } else if (game_won) {
                        Advance_Fase();
                    }
                }

                if (move && !game_over && !game_won) {
                    if (next_gx >= 0 && next_gx < MAP_COLS && 
                        next_gy >= 0 && next_gy < MAP_ROWS) {
                        if (grid[next_gy][next_gx] == TILE_EMPTY && 
                            !fire_map[next_gy][next_gx].active) {
                            player_gx = next_gx;
                            player_gy = next_gy;
                        }
                    }
                    precisa_redesenhar = true;
                }
            }

            Update_Game();
            precisa_redesenhar = true;
        }

        if (precisa_redesenhar) Flush_Grafico_Janela();
        
        sys_sleep(16);
    }

    sys_exit();
    return 0;
}
