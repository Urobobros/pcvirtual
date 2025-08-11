#include "codex_cga.h"
#include "font8x8_basic.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CGA_COLS 80
#define CGA_ROWS 25
#define CGA_WIDTH  (CGA_COLS * 8)
#define CGA_HEIGHT (CGA_ROWS * 8)

struct CodexCga {
    uint8_t* mem;
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    uint32_t pixels[CGA_WIDTH * CGA_HEIGHT];
    int menu_active;
};

static const uint32_t cga_palette[16] = {
    0xFF000000, 0xFF0000AA, 0xFF00AA00, 0xFF00AAAA,
    0xFFAA0000, 0xFFAA00AA, 0xFFAA5500, 0xFFAAAAAA,
    0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF,
    0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF
};

static void draw_char_pix(CodexCga* c, int col, int row, uint8_t ch, uint32_t fg, uint32_t bg) {
    for (int y = 0; y < 8; ++y) {
        uint8_t bits = (uint8_t)font8x8_basic[ch][y];
        for (int x = 0; x < 8; ++x) {
            uint32_t color = (bits & (1 << x)) ? fg : bg;
            c->pixels[(row * 8 + y) * CGA_WIDTH + col * 8 + x] = color;
        }
    }
}

static void draw_string(CodexCga* c, int col, int row, const char* s, uint32_t fg, uint32_t bg) {
    for (int i = 0; s[i]; ++i) {
        draw_char_pix(c, col + i, row, (uint8_t)s[i], fg, bg);
    }
}

static void draw_menu(CodexCga* c) {
    const char* line1 = " Exit ";
    const char* line2 = "Enter = Exit";
    const char* line3 = "Esc = Resume";
    int row = CGA_ROWS / 2 - 1;
    int col1 = (CGA_COLS - (int)strlen(line1)) / 2;
    int col2 = (CGA_COLS - (int)strlen(line2)) / 2;
    int col3 = (CGA_COLS - (int)strlen(line3)) / 2;
    uint32_t fg = cga_palette[15];
    uint32_t bg = cga_palette[4];
    draw_string(c, col1, row,     line1, fg, bg);
    draw_string(c, col2, row + 1, line2, fg, bg);
    draw_string(c, col3, row + 2, line3, fg, bg);
}

CodexCga* codex_cga_create(uint8_t* memory) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        return NULL;
    }
    CodexCga* c = (CodexCga*)malloc(sizeof(CodexCga));
    if (!c) {
        SDL_Quit();
        return NULL;
    }
    memset(c, 0, sizeof(*c));
    c->mem = memory;
    c->window = SDL_CreateWindow("CGA", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, CGA_WIDTH, CGA_HEIGHT, 0);
    if (!c->window) {
        codex_cga_destroy(c);
        return NULL;
    }
    c->renderer = SDL_CreateRenderer(c->window, -1, SDL_RENDERER_ACCELERATED);
    c->texture = SDL_CreateTexture(c->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, CGA_WIDTH, CGA_HEIGHT);
    return c;
}

void codex_cga_destroy(CodexCga* c) {
    if (!c) return;
    if (c->texture) SDL_DestroyTexture(c->texture);
    if (c->renderer) SDL_DestroyRenderer(c->renderer);
    if (c->window) SDL_DestroyWindow(c->window);
    SDL_Quit();
    free(c);
}

void codex_cga_update(CodexCga* c) {
    if (!c) return;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) exit(0);
        if (e.type == SDL_KEYDOWN) {
            if (c->menu_active) {
                if (e.key.keysym.sym == SDLK_RETURN) {
                    exit(0);
                } else if (e.key.keysym.sym == SDLK_ESCAPE) {
                    c->menu_active = 0;
                }
            } else {
                if (e.key.keysym.sym == SDLK_ESCAPE) {
                    c->menu_active = 1;
                }
            }
        }
    }
    uint8_t* vram = c->mem + 0xB8000;
    for (int r = 0; r < CGA_ROWS; ++r) {
        for (int ccol = 0; ccol < CGA_COLS; ++ccol) {
            int offset = 2 * (r * CGA_COLS + ccol);
            uint8_t ch = vram[offset];
            uint8_t attr = vram[offset + 1];
            uint32_t fg = cga_palette[attr & 0x0F];
            uint32_t bg = cga_palette[(attr >> 4) & 0x07];
            for (int y = 0; y < 8; ++y) {
                uint8_t bits = (uint8_t)font8x8_basic[ch][y];
                for (int x = 0; x < 8; ++x) {
                    uint32_t color = (bits & (1 << x)) && !(attr & 0x80) ? fg : bg;
                    c->pixels[(r * 8 + y) * CGA_WIDTH + ccol * 8 + x] = color;
                }
            }
        }
    }
    if (c->menu_active) {
        draw_menu(c);
    }
    SDL_UpdateTexture(c->texture, NULL, c->pixels, CGA_WIDTH * sizeof(uint32_t));
    SDL_RenderClear(c->renderer);
    SDL_RenderCopy(c->renderer, c->texture, NULL, NULL);
    SDL_RenderPresent(c->renderer);
}

void codex_cga_dump_text(CodexCga* c, const char* path) {
    if (!c || !path) return;
    FILE* f = fopen(path, "w");
    if (!f) return;
    uint8_t* vram = c->mem + 0xB8000;
    for (int r = 0; r < CGA_ROWS; ++r) {
        for (int ccol = 0; ccol < CGA_COLS; ++ccol) {
            int offset = 2 * (r * CGA_COLS + ccol);
            uint8_t ch = vram[offset];
            if (ch < 32 || ch > 126) ch = '.';
            fputc(ch, f);
        }
        fputc('\n', f);
    }
    fclose(f);
}

