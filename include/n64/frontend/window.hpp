#pragma once

#include "n64/common/types.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
union SDL_Event;

namespace n64::frontend {

struct WindowConfig {
    std::string title = "n64emu";
    int width  = 640;
    int height = 480;
    bool resizable = true;
    bool vsync = true;
    /// If true (or N64EMU_HEADLESS), use SDL dummy video driver.
    bool headless = false;
};

/// Thin SDL2 window + renderer wrapper.
class Window {
public:
    using EventCallback = std::function<void(const SDL_Event&)>;

    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) noexcept;
    Window& operator=(Window&&) noexcept;

    /// Initialize SDL subsystems and create the window. Returns false on failure.
    [[nodiscard]] bool create(const WindowConfig& cfg = {});

    void destroy();

    /// Poll OS events. Returns false if the user requested quit.
    /// Optional callback receives every SDL_Event (for input mapping).
    [[nodiscard]] bool poll_events(const EventCallback& on_event = {});

    /// Clear to RGBA color and present.
    void clear(u8 r, u8 g, u8 b, u8 a = 255);
    void present();

    /// Upload a tightly packed RGBA8888 framebuffer and draw it stretched.
    void draw_framebuffer(const u8* rgba, int width, int height);

    /// Draw monochrome debug text (5x7 bitmap font) at pixel position.
    void draw_text(int x, int y, std::string_view text,
                   u8 r = 0, u8 g = 255, u8 b = 0, int scale = 1);

    /// Semi-transparent dark panel then text (for debug overlay).
    void draw_overlay_text(std::string_view text);

    [[nodiscard]] bool open() const noexcept { return window_ != nullptr; }
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] bool quit_requested() const noexcept { return quit_; }

    void set_title(std::string_view title);

private:
    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture*  fb_tex_   = nullptr;
    int width_  = 0;
    int height_ = 0;
    int fb_w_   = 0;
    int fb_h_   = 0;
    bool quit_  = false;
    bool sdl_owned_ = false; // true if we called SDL_Init
};

} // namespace n64::frontend
