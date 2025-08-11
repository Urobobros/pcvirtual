#include "codex_cga.h"

#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>

struct CodexCga {
    uint8_t* mem;
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    uint32_t pixels[320 * 200];
};

static const uint32_t cga_palette[16] = {
    0xFF000000, 0xFF0000AA, 0xFF00AA00, 0xFF00AAAA,
    0xFFAA0000, 0xFFAA00AA, 0xFFAA5500, 0xFFAAAAAA,
    0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF,
    0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF
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
    c->window = SDL_CreateWindow("CGA", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 320, 200, 0);
    if (!c->window) {
        codex_cga_destroy(c);
        return NULL;
    }
    c->renderer = SDL_CreateRenderer(c->window, -1, SDL_RENDERER_ACCELERATED);
    c->texture = SDL_CreateTexture(c->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 200);
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
    }
    uint8_t* vram = c->mem + 0xB8000;
    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 320; x += 2) {
            int offset = y * 160 + (x / 2);
            uint8_t byte = vram[offset];
            uint32_t left = cga_palette[byte >> 4];
            uint32_t right = cga_palette[byte & 0x0F];
            c->pixels[y * 320 + x] = left;
            c->pixels[y * 320 + x + 1] = right;
        }
    }
    SDL_UpdateTexture(c->texture, NULL, c->pixels, 320 * sizeof(uint32_t));
    SDL_RenderClear(c->renderer);
    SDL_RenderCopy(c->renderer, c->texture, NULL, NULL);
    SDL_RenderPresent(c->renderer);
}

