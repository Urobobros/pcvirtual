#include "codex_cga.h"
#include "font8x8_basic.h"
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
#include <SDL2/SDL_syswm.h>
#include <windows.h>
#endif

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
};

static const uint32_t cga_palette[16] = {
    0xFF000000, 0xFF0000AA, 0xFF00AA00, 0xFF00AAAA,
    0xFFAA0000, 0xFFAA00AA, 0xFFAA5500, 0xFFAAAAAA,
    0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF,
    0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF,
};

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
    c->window = SDL_CreateWindow("CGA", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                 CGA_WIDTH, CGA_HEIGHT, 0);
    if (!c->window) {
        codex_cga_destroy(c);
        return NULL;
    }
    c->renderer = SDL_CreateRenderer(c->window, -1, SDL_RENDERER_ACCELERATED);
    c->texture = SDL_CreateTexture(c->renderer, SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STREAMING, CGA_WIDTH, CGA_HEIGHT);
#ifdef _WIN32
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    SDL_GetWindowWMInfo(c->window, &info);
    HMENU hMenu = CreateMenu();
    HMENU hFile = CreateMenu();
    AppendMenu(hFile, MF_STRING, 1, TEXT("Exit"));
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFile, TEXT("File"));
    SetMenu(info.info.win.window, hMenu);
    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
#endif
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
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) exit(0);
#ifdef _WIN32
        if (e.type == SDL_SYSWMEVENT) {
            const SDL_SysWMmsg* msg = e.syswm.msg;
            if (msg->msg.win.msg == WM_COMMAND && LOWORD(msg->msg.win.wParam) == 1)
                exit(0);
        }
#endif
    }

    uint8_t* vram = c->mem + 0xB8000;
    for (int row = 0; row < CGA_ROWS; ++row) {
        for (int col = 0; col < CGA_COLS; ++col) {
            size_t pos = (row * CGA_COLS + col) * 2;
            uint8_t ch = vram[pos];
            uint8_t attr = vram[pos + 1];
            const uint8_t* glyph = (ch < 128) ? font8x8_basic[ch] : font8x8_basic[0];
            uint32_t fg = cga_palette[attr & 0x0F];
            uint32_t bg = cga_palette[(attr >> 4) & 0x0F];
            for (int gy = 0; gy < 8; ++gy) {
                uint8_t bits = glyph[gy];
                for (int gx = 0; gx < 8; ++gx) {
                    uint32_t color = (bits & (1u << gx)) ? fg : bg;
                    c->pixels[(row * 8 + gy) * CGA_WIDTH + col * 8 + gx] = color;
                }
            }
        }
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
    for (int row = 0; row < CGA_ROWS; ++row) {
        for (int col = 0; col < CGA_COLS; ++col) {
            size_t pos = (row * CGA_COLS + col) * 2;
            uint8_t ch = vram[pos];
            if (ch < 32 || ch > 126) ch = '.';
            fputc(ch, f);
        }
        fputc('\n', f);
    }
    fclose(f);
}

