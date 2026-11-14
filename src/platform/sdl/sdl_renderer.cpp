/**
 * @file sdl_renderer.cpp
 * @brief Renderer SDL2.
 */
#include "platform/sdl/sdl_platform.hpp"

#include <iostream>

namespace rea::platform::sdl {

SdlRenderer::SdlRenderer(const char* title, int scale) {
    window_ = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               kScreenW * scale, kScreenH * scale, SDL_WINDOW_RESIZABLE);
    if (!window_) {
        std::cerr << "[SDL] SDL_CreateWindow: " << SDL_GetError() << '\n';
        return;
    }
    // Sem VSYNC: a cadência vem do GameLoop (taxa do arcade), não do monitor.
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer_) {
        std::cerr << "[SDL] SDL_CreateRenderer: " << SDL_GetError() << '\n';
        return;
    }
    SDL_RenderSetLogicalSize(renderer_, kScreenW, kScreenH);
}

SdlRenderer::~SdlRenderer() {
    if (frame_) SDL_DestroyTexture(frame_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
}

void SdlRenderer::setTitle(const char* title) {
    if (window_) SDL_SetWindowTitle(window_, title);
}

void SdlRenderer::toggleFullscreen() {
    if (!window_) return;
    const bool full = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    if (SDL_SetWindowFullscreen(window_, full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)
        std::cerr << "[SDL] SDL_SetWindowFullscreen: " << SDL_GetError() << '\n';
}

void SdlRenderer::beginFrame() {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
}

void SdlRenderer::fillRect(int x, int y, int w, int h, uint32_t rgba) {
    SDL_SetRenderDrawColor(renderer_, static_cast<Uint8>(rgba >> 24), static_cast<Uint8>(rgba >> 16),
                           static_cast<Uint8>(rgba >> 8), static_cast<Uint8>(rgba));
    const SDL_Rect r{x, y, w, h};
    SDL_RenderFillRect(renderer_, &r);
}

void SdlRenderer::blit(const uint32_t* rgba, int w, int h) {
    if (!frame_ || w != frame_w_ || h != frame_h_) {
        if (frame_) SDL_DestroyTexture(frame_);
        frame_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        frame_w_ = w;
        frame_h_ = h;
        if (!frame_) return;
    }
    SDL_UpdateTexture(frame_, nullptr, rgba, w * static_cast<int>(sizeof(uint32_t)));
    SDL_RenderCopy(renderer_, frame_, nullptr, nullptr);
}

void SdlRenderer::present() { SDL_RenderPresent(renderer_); }

} // namespace rea::platform::sdl
