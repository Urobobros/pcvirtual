#include "codex_cga.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <SDL2/SDL_syswm.h>
#include <windows.h>
#endif

#define CGA_WIDTH  320
#define CGA_HEIGHT 200

struct CodexCga {
    uint8_t* mem;
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    uint32_t pixels[CGA_WIDTH * CGA_HEIGHT];
};

static const uint32_t cga_palette[4] = {
    0xFF000000, /* black */
    0xFF00AAAA, /* cyan */
    0xFFAA00AA, /* magenta */
    0xFFFFFFFF  /* white */
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
    c->window = SDL_CreateWindow("CGA", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, CGA_WIDTH, CGA_HEIGHT, 0);
    if (!c->window) {
        codex_cga_destroy(c);
        return NULL;
    }
    c->renderer = SDL_CreateRenderer(c->window, -1, SDL_RENDERER_ACCELERATED);
    c->texture = SDL_CreateTexture(c->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, CGA_WIDTH, CGA_HEIGHT);
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
            if (msg->msg.win.msg == WM_COMMAND && LOWORD(msg->msg.win.wParam) == 1) {
                exit(0);
            }
        }
#endif
    }
    uint8_t* vram = c->mem + 0xB8000;
    for (int y = 0; y < CGA_HEIGHT; ++y) {
        uint8_t* line = vram + ((y & 1) ? 0x2000 : 0) + (y >> 1) * 80;
        for (int x = 0; x < CGA_WIDTH; x += 4) {
            uint8_t b = line[x >> 2];
            c->pixels[y * CGA_WIDTH + x + 0] = cga_palette[(b >> 6) & 3];
            c->pixels[y * CGA_WIDTH + x + 1] = cga_palette[(b >> 4) & 3];
            c->pixels[y * CGA_WIDTH + x + 2] = cga_palette[(b >> 2) & 3];
            c->pixels[y * CGA_WIDTH + x + 3] = cga_palette[(b >> 0) & 3];
        }
    }
    SDL_UpdateTexture(c->texture, NULL, c->pixels, CGA_WIDTH * sizeof(uint32_t));
    SDL_RenderClear(c->renderer);
    SDL_RenderCopy(c->renderer, c->texture, NULL, NULL);
    SDL_RenderPresent(c->renderer);
}

