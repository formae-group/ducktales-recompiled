// formae_ui.cpp - the Formae look, the in-game menu and the first-run ROM
// screen, drawn with Dear ImGui over SDL_Renderer (formae_ui.h).
//
// The menu is a modal loop: it runs inside the host's event callback, so no
// machine frame runs while it is open (the game is paused by construction)
// and nothing of it reaches the machine's picture, screenshots or hashes. It
// draws the last machine frame, dimmed, with its own texture, and gives the
// renderer back exactly as it found it. Events the host still needs (quit,
// resize, controllers) are pushed back when it closes.
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include "formae_app.h"
#include "formae_rom.h"
#include "formae_ui.h"
#include "game/enhancements.h"
#include "host/ducktales_host.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#endif

extern "C" const uint32_t *cyc_frame_argb(void);   // cyc_core.h (a C header)
extern "C" bool cyc_write_png(const char *path, const uint32_t *argb, int width, int height);   // cyc_png.h

namespace {

// ---- the look ----

const ImU32 ACCENT = IM_COL32(242, 179, 61, 255);
const ImU32 PANEL = IM_COL32(16, 19, 24, 244);
const ImU32 BACKDROP = IM_COL32(12, 14, 18, 255);
const ImU32 DIM = IM_COL32(5, 7, 10, 150);
const ImU32 TEXT = IM_COL32(236, 239, 243, 255);
const ImU32 TEXT_DIM = IM_COL32(138, 147, 160, 255);
const ImU32 TEXT_OFF = IM_COL32(90, 97, 108, 255);
const ImU32 ROW_FOCUS = IM_COL32(255, 255, 255, 20);
const ImU32 ROW_HOVER = IM_COL32(255, 255, 255, 10);
const ImU32 ERROR_TEXT = IM_COL32(239, 112, 100, 255);
const ImU32 OK_TEXT = IM_COL32(126, 205, 140, 255);

struct Fonts {
    ImFont *body = nullptr, *title = nullptr, *small = nullptr;
    float base = 18.0f;
    bool system = false;   // a system UI font: arrows and the like exist
};

Fonts load_fonts(float display_h)
{
    Fonts f;
    ImGuiIO &io = ImGui::GetIO();
    f.base = display_h / 34.0f;
    if (f.base < 15.0f) f.base = 15.0f;
    if (f.base > 30.0f) f.base = 30.0f;
    static ImVector<ImWchar> ranges;
    if (ranges.empty()) {
        ImFontGlyphRangesBuilder b;
        b.AddRanges(io.Fonts->GetGlyphRangesDefault());
        static const ImWchar extra[] = { 0x2013, 0x2014, 0x2026, 0x2026, 0x2039, 0x203A, 0x2190, 0x2193, 0x00D7, 0x00D7, 0 };
        b.AddRanges(extra);
        b.BuildRanges(&ranges);
    }
    std::string regular, semibold;
#ifdef _WIN32
    const char *windir = std::getenv("WINDIR");
    std::string dir = std::string(windir ? windir : "C:\\Windows") + "\\Fonts\\";
    regular = dir + "segoeui.ttf";
    semibold = dir + "seguisb.ttf";
#elif defined(__APPLE__)
    regular = semibold = "/System/Library/Fonts/SFNS.ttf";
#else
    regular = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    semibold = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf";
#endif
    auto exists = [](const std::string &p) { FILE *t = std::fopen(p.c_str(), "rb"); if (t) std::fclose(t); return t != nullptr; };
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.PixelSnapH = true;
    if (exists(regular)) {
        f.body = io.Fonts->AddFontFromFileTTF(regular.c_str(), f.base, &cfg, ranges.Data);
        f.small = io.Fonts->AddFontFromFileTTF(regular.c_str(), f.base * 0.78f, &cfg, ranges.Data);
        f.title = io.Fonts->AddFontFromFileTTF(exists(semibold) ? semibold.c_str() : regular.c_str(), f.base * 1.9f, &cfg, ranges.Data);
        f.system = f.body && f.small && f.title;
    }
    if (!f.system) {
        io.Fonts->Clear();
        ImFontConfig d;
        d.SizePixels = 13.0f * f.base / 15.0f;
        f.body = io.Fonts->AddFontDefault(&d);
        d.SizePixels = 13.0f;
        f.small = io.Fonts->AddFontDefault(&d);
        d.SizePixels = 13.0f * f.base / 9.0f;
        f.title = io.Fonts->AddFontDefault(&d);
    }
    return f;
}

void apply_style()
{
    ImGuiStyle &s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.WindowPadding = ImVec2(0, 0);
    s.WindowBorderSize = 0;
    s.ItemSpacing = ImVec2(0, 2);
    s.FrameRounding = 4;
    s.Colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_NavCursor] = ImVec4(0, 0, 0, 0);   // focus is drawn by the rows themselves
    s.Colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(TEXT);
}

struct Imgui {
    SDL_Window *win = nullptr;
    SDL_Renderer *ren = nullptr;
    ImGuiContext *ctx = nullptr;
    Fonts fonts;
    bool begin(SDL_Window *w, SDL_Renderer *r)
    {
        win = w;
        ren = r;
        IMGUI_CHECKVERSION();
        ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctx);
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // gamepad input reaches ImGui's keys; the windows take no nav
        if (!ImGui_ImplSDL2_InitForSDLRenderer(w, r)) { ImGui::DestroyContext(ctx); ctx = nullptr; return false; }
        if (!ImGui_ImplSDLRenderer2_Init(r)) { ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext(ctx); ctx = nullptr; return false; }
        int ww = 0, wh = 0;
        SDL_GetWindowSize(w, &ww, &wh);
        fonts = load_fonts((float)wh);
        apply_style();
        return true;
    }
    void end()
    {
        if (!ctx) return;
        ImGui::SetCurrentContext(ctx);
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext(ctx);
        ctx = nullptr;
    }
};

// ---- widgets ----
//
// The menu keeps its own cursor instead of ImGui's keyboard navigation, so
// the highlighted row is always the one the arrows, the D-pad and the mouse
// last chose, and a list behaves the same with a keyboard, a pad or a mouse.

struct Layout {
    float x = 0, width = 0, row_h = 0, pad = 0;
    int cursor = 0;           // the item with the focus
    int index = 0;            // items drawn so far this frame
    bool accept = false, left = false, right = false, mouse_moved = false;
};

enum RowResult { ROW_NONE, ROW_ACTIVATE, ROW_DEC, ROW_INC };

bool pressed(std::initializer_list<ImGuiKey> keys)
{
    for (ImGuiKey k : keys)
        if (ImGui::IsKeyPressed(k, true)) return true;
    return false;
}

void text_at(ImDrawList *dl, ImFont *font, float x, float y, ImU32 col, const char *t)
{
    dl->AddText(font, font->FontSize, ImVec2(x, y), col, t);
}

float text_w(ImFont *font, const char *t) { return font->CalcTextSizeA(font->FontSize, FLT_MAX, 0, t).x; }

// One focusable item: its index, whether it has the focus, and a click.
struct Item { int index; bool focused, hovered, clicked; ImVec2 p, q; };

Item item(Layout &L, ImVec2 p, ImVec2 size)
{
    Item it;
    it.index = L.index++;
    ImGui::PushID(it.index);
    ImGui::SetCursorScreenPos(p);
    it.clicked = ImGui::InvisibleButton("##item", size);
    it.hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    if (it.hovered && L.mouse_moved) L.cursor = it.index;
    it.focused = L.cursor == it.index;
    it.p = p;
    it.q = ImVec2(p.x + size.x, p.y + size.y);
    return it;
}

void item_back(ImDrawList *dl, const Item &it, bool lit)
{
    if (it.focused || it.hovered || lit) dl->AddRectFilled(it.p, it.q, it.focused || lit ? ROW_FOCUS : ROW_HOVER, 4.0f);
    if (it.focused || lit) dl->AddRectFilled(it.p, ImVec2(it.p.x + 3.0f, it.q.y), ACCENT, 1.5f);
}

RowResult row(Layout &L, const Fonts &F, const char *label, const char *value = nullptr, bool enabled = true)
{
    Item it = item(L, ImVec2(L.x, ImGui::GetCursorScreenPos().y), ImVec2(L.width, L.row_h));
    ImGui::SetCursorScreenPos(ImVec2(L.x, it.q.y + 2.0f));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    item_back(dl, it, false);
    float ty = it.p.y + (L.row_h - F.body->FontSize) * 0.5f;
    text_at(dl, F.body, it.p.x + L.pad, ty, enabled ? TEXT : TEXT_OFF, label);
    RowResult r = ROW_NONE;
    if (value) {
        std::string shown = value;
        if (it.focused && enabled) shown = (F.system ? "\u2039  " : "<  ") + shown + (F.system ? "  \u203A" : "  >");
        float w = text_w(F.body, shown.c_str());
        float vx = it.q.x - L.pad - w;
        text_at(dl, F.body, vx, ty, enabled ? (it.focused ? TEXT : TEXT_DIM) : TEXT_OFF, shown.c_str());
        if (enabled) {
            if (it.clicked) r = ImGui::GetIO().MousePos.x < vx + w * 0.5f ? ROW_DEC : ROW_INC;
            if (it.focused && L.accept) r = ROW_INC;
            if (it.focused && L.left) r = ROW_DEC;
            if (it.focused && L.right) r = ROW_INC;
        }
    } else if (enabled && (it.clicked || (it.focused && L.accept))) {
        r = ROW_ACTIVATE;
    }
    return r;
}

void draw_wrapped(ImDrawList *dl, ImFont *font, float x, float y, float width, ImU32 col, const char *t)
{
    dl->AddText(font, font->FontSize, ImVec2(x, y), col, t, nullptr, width);
}

float wrapped_h(ImFont *font, float width, const char *t)
{
    return font->CalcTextSizeA(font->FontSize, FLT_MAX, width, t).y;
}

// ---- the menu ----

enum Page { PAGE_MAIN, PAGE_SETTINGS, PAGE_CONTROLS, PAGE_ENHANCEMENTS, PAGE_RESET };

struct Menu {
    Page page = PAGE_MAIN;
    int cursor = 0;
    int count = 1;            // items on the page last frame
    bool resume = false, quit = false;
    int capture_row = -1;
    bool capture_pad = false;
    std::string notice;
    Uint64 notice_until = 0;
    std::vector<SDL_Event> for_host;   // events the host must still see
};

const char *page_title(Page p)
{
    switch (p) {
    case PAGE_MAIN: return "Paused";
    case PAGE_SETTINGS: return "Settings";
    case PAGE_CONTROLS: return "Controls";
    case PAGE_ENHANCEMENTS: return "Enhancements";
    case PAGE_RESET: return "Reset";
    }
    return "";
}

void go(Menu &m, Page p)
{
    m.page = p;
    m.cursor = 0;
    m.capture_row = -1;
    m.notice.clear();
}

void back(Menu &m)
{
    if (m.capture_row >= 0) { m.capture_row = -1; return; }
    if (m.page == PAGE_MAIN) m.resume = true;
    else go(m, PAGE_MAIN);
}

void say(Menu &m, const char *text)
{
    m.notice = text ? text : "";
    m.notice_until = SDL_GetTicks64() + 4000;
}

void enh_change(DtEnh e, int dir)
{
    const DtEnhInfo *in = dt_enh_info(e);
    int v = dt_enh_get(e);
    if (in->kind == DT_ENH_KIND_TOGGLE) v = !v;
    else {
        v += dir * in->step;
        if (v < in->min) v = in->min;
        if (v > in->max) v = in->max;
    }
    if (v == dt_enh_get(e) || !dt_enh_set(e, v)) return;
    ducktales_host_set_saved_value(e, v);
    formae_settings_changed();
}

std::string enh_value(DtEnh e)
{
    const DtEnhInfo *in = dt_enh_info(e);
    int v = dt_enh_get(e);
    char buf[48];
    if (in->kind == DT_ENH_KIND_TOGGLE) return v ? "On" : "Off";
    if (in->kind == DT_ENH_KIND_COUNT_OR_OFF) {
        if (v == in->stock) return "Off";
        std::snprintf(buf, sizeof buf, "%d", v);
        return buf;
    }
    std::snprintf(buf, sizeof buf, v == in->stock ? "%d%% (original)" : "%d%%", v);
    return buf;
}

void page_main(Menu &m, Layout &L, const Fonts &F)
{
    if (row(L, F, "Resume") == ROW_ACTIVATE) m.resume = true;
    if (row(L, F, "Reset") == ROW_ACTIVATE) go(m, PAGE_RESET);
    if (row(L, F, "Settings") == ROW_ACTIVATE) go(m, PAGE_SETTINGS);
    if (row(L, F, "Controls") == ROW_ACTIVATE) go(m, PAGE_CONTROLS);
    if (row(L, F, "Enhancements") == ROW_ACTIVATE) go(m, PAGE_ENHANCEMENTS);
    if (row(L, F, "Quit") == ROW_ACTIVATE) m.quit = true;
}

void page_reset(Menu &m, Layout &L, const Fonts &F)
{
    if (row(L, F, "Reset the game") == ROW_ACTIVATE) {
        dt_soft_reset();
        m.resume = true;
    }
    if (row(L, F, "Cancel") == ROW_ACTIVATE) go(m, PAGE_MAIN);
}

void page_settings(Menu &m, Layout &L, const Fonts &F, SDL_Window *win)
{
    (void)m;
    FormaeDisplay d;
    formae_display_get(&d);
    FormaeDisplay was = d;
    static const char *MODES[] = { "Windowed", "Borderless full screen", "Exclusive full screen" };
    RowResult r = row(L, F, "Display", MODES[d.fullscreen < 0 || d.fullscreen > 2 ? 0 : d.fullscreen]);
    if (r == ROW_DEC) d.fullscreen = (d.fullscreen + 2) % 3;
    if (r == ROW_INC || r == ROW_ACTIVATE) d.fullscreen = (d.fullscreen + 1) % 3;
    char scale[16];
    std::snprintf(scale, sizeof scale, F.system ? "%d\u00D7" : "%dx", d.window_scale);
    r = row(L, F, "Window size", d.fullscreen ? (F.system ? "\u2014" : "-") : scale, d.fullscreen == 0);
    if (r == ROW_DEC && d.window_scale > 1) d.window_scale--;
    if ((r == ROW_INC || r == ROW_ACTIVATE) && d.window_scale < 8) d.window_scale++;
    r = row(L, F, "Integer scaling", d.integer_scale ? "On" : "Off");
    if (r != ROW_NONE) d.integer_scale = !d.integer_scale;
    r = row(L, F, "VSync", d.vsync ? "On" : "Off");
    if (r != ROW_NONE) d.vsync = !d.vsync;
    char vol[16];
    std::snprintf(vol, sizeof vol, "%d%%", d.volume);
    r = row(L, F, "Volume", vol);
    if (r == ROW_DEC) d.volume = d.volume >= 5 ? d.volume - 5 : 0;
    if (r == ROW_INC || r == ROW_ACTIVATE) d.volume = d.volume <= 95 ? d.volume + 5 : 100;
    if (std::memcmp(&d, &was, sizeof d)) formae_display_set(win, &d);
}

void page_controls(Menu &m, Layout &L, const Fonts &F)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float label_w = L.width * 0.34f, cell_w = (L.width - label_w) * 0.5f;
    ImVec2 head = ImGui::GetCursorScreenPos();
    text_at(dl, F.small, L.x + label_w + L.pad, head.y, TEXT_DIM, "Keyboard");
    text_at(dl, F.small, L.x + label_w + cell_w + L.pad, head.y, TEXT_DIM, "Controller");
    ImGui::Dummy(ImVec2(L.width, F.small->FontSize * 1.4f));
    for (int b = 0; b < FORMAE_BIND_COUNT; b++) {
        ImVec2 p(L.x, ImGui::GetCursorScreenPos().y);
        text_at(dl, F.body, p.x + L.pad, p.y + (L.row_h - F.body->FontSize) * 0.5f, TEXT, formae_bind_label(b));
        for (int c = 0; c < 2; c++) {
            Item it = item(L, ImVec2(p.x + label_w + cell_w * c, p.y), ImVec2(cell_w - 4.0f, L.row_h));
            bool capturing = m.capture_row == b && m.capture_pad == (c == 1);
            item_back(dl, it, capturing);
            char text[64];
            if (capturing) std::snprintf(text, sizeof text, "%s", c ? "Press a button" : "Press a key");
            else formae_bind_text(b, c == 1, text, sizeof text);
            text_at(dl, F.body, it.p.x + L.pad, it.p.y + (L.row_h - F.body->FontSize) * 0.5f,
                    capturing ? ACCENT : !std::strcmp(text, "Unbound") ? TEXT_OFF : it.focused ? TEXT : TEXT_DIM, text);
            if ((it.clicked || (it.focused && L.accept)) && m.capture_row < 0) {
                m.capture_row = b;
                m.capture_pad = c == 1;
            }
        }
        ImGui::SetCursorScreenPos(ImVec2(L.x, p.y + L.row_h + 2.0f));
    }
    ImGui::Dummy(ImVec2(L.width, L.row_h * 0.3f));
    if (row(L, F, "Restore defaults") == ROW_ACTIVATE) {
        formae_bind_defaults();
        say(m, "Default controls restored.");
    }
}

void page_enhancements(Menu &m, Layout &L, const Fonts &F)
{
    for (int e = 0; e < DT_ENH_COUNT; e++) {
        const DtEnhInfo *in = dt_enh_info((DtEnh)e);
        std::string v = enh_value((DtEnh)e);
        RowResult r = row(L, F, in->label, v.c_str());
        if (r == ROW_DEC) enh_change((DtEnh)e, -1);
        if (r == ROW_INC || r == ROW_ACTIVATE) enh_change((DtEnh)e, +1);
    }
    ImGui::Dummy(ImVec2(L.width, L.row_h * 0.3f));
    if (row(L, F, "Restore the original game") == ROW_ACTIVATE) {
        for (int e = 0; e < DT_ENH_COUNT; e++) {
            const DtEnhInfo *in = dt_enh_info((DtEnh)e);
            if (dt_enh_get((DtEnh)e) == in->stock) continue;
            dt_enh_set((DtEnh)e, in->stock);
            ducktales_host_set_saved_value((DtEnh)e, in->stock);
            formae_settings_changed();
        }
        say(m, "Every enhancement is off.");
    }
}

// Raw events before ImGui: back, capture, and what the host must see later.
bool menu_event(Menu &m, const SDL_Event &ev)
{
    switch (ev.type) {
    case SDL_QUIT:
        m.quit = true;
        return true;
    case SDL_WINDOWEVENT:
        if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) m.for_host.push_back(ev);
        return false;
    case SDL_CONTROLLERDEVICEADDED:
    case SDL_CONTROLLERDEVICEREMOVED:
        m.for_host.push_back(ev);
        return false;
    default:
        break;
    }
    if (m.capture_row >= 0) {
        if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
            SDL_Scancode sc = ev.key.keysym.scancode;
            if (sc == SDL_SCANCODE_ESCAPE) m.capture_row = -1;
            else if (sc == SDL_SCANCODE_DELETE || sc == SDL_SCANCODE_BACKSPACE) {
                formae_bind_set(m.capture_row, m.capture_pad, 0);
                m.capture_row = -1;
            } else if (!m.capture_pad) {
                say(m, formae_bind_set(m.capture_row, false, sc));
                m.capture_row = -1;
            }
            return true;
        }
        if (m.capture_pad && ev.type == SDL_CONTROLLERBUTTONDOWN) {
            say(m, formae_bind_set(m.capture_row, true, 1 + ev.cbutton.button));
            m.capture_row = -1;
            return true;
        }
        if (m.capture_pad && ev.type == SDL_CONTROLLERAXISMOTION && (ev.caxis.value > 20000 || ev.caxis.value < -20000)) {
            say(m, formae_bind_set(m.capture_row, true, 100 + ev.caxis.axis * 2 + (ev.caxis.value > 0 ? 1 : 0)));
            m.capture_row = -1;
            return true;
        }
        return ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP || ev.type == SDL_CONTROLLERBUTTONDOWN ||
               ev.type == SDL_CONTROLLERBUTTONUP || ev.type == SDL_CONTROLLERAXISMOTION;
    }
    if (ev.type == SDL_KEYDOWN && !ev.key.repeat &&
        (ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE || formae_is_menu_key(ev.key.keysym.scancode))) {
        if (formae_is_menu_key(ev.key.keysym.scancode) && ev.key.keysym.scancode != SDL_SCANCODE_ESCAPE) m.resume = true;
        else back(m);
        return true;
    }
    if (ev.type == SDL_CONTROLLERBUTTONDOWN) {
        if (formae_is_menu_button(ev.cbutton.button)) { m.resume = true; return true; }
        if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_B) { back(m); return true; }
    }
    return false;
}

// FORMAE_UI_SELFTEST=<dir> (QA only): the menu walks its pages and the first
// run screen shows its states by themselves, each written to <dir> as a PNG
// of what the window draws, so a hidden window can still be looked at.
const char *selftest_dir()
{
    const char *d = std::getenv("FORMAE_UI_SELFTEST");
    return d && *d ? d : nullptr;
}

void selftest_shot(SDL_Renderer *ren, const char *name)
{
    int ow = 0, oh = 0;
    SDL_GetRendererOutputSize(ren, &ow, &oh);
    std::vector<uint32_t> px((size_t)ow * (size_t)oh);
    std::string path = std::string(selftest_dir()) + "/" + name + ".png";
    bool ok = ow > 0 && SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, px.data(), ow * 4) == 0 &&
              cyc_write_png(path.c_str(), px.data(), ow, oh);
    std::printf("[formae selftest] %s %s (%dx%d)\n", ok ? "wrote" : "could not write", path.c_str(), ow, oh);
    std::fflush(stdout);
}

void draw_picture(SDL_Renderer *ren, SDL_Texture *tex, int ow, int oh)
{
    if (!tex) return;
    float s = (float)ow / 256.0f < (float)oh / 240.0f ? (float)ow / 256.0f : (float)oh / 240.0f;
    SDL_Rect dst = { (int)((ow - 256 * s) / 2), (int)((oh - 240 * s) / 2), (int)(256 * s), (int)(240 * s) };
    SDL_RenderCopy(ren, tex, nullptr, &dst);
}

}   // namespace

extern "C" bool formae_menu_run(SDL_Window *win)
{
    SDL_Renderer *ren = SDL_GetRenderer(win);
    if (!ren) return false;
    int lw = 0, lh = 0;
    SDL_RenderGetLogicalSize(ren, &lw, &lh);
    SDL_Texture *target = SDL_GetRenderTarget(ren);
    SDL_SetRenderTarget(ren, nullptr);
    Imgui ui;
    if (!ui.begin(win, ren)) {
        SDL_SetRenderTarget(ren, target);
        return false;
    }
    SDL_Texture *pic = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 256, 240);
    if (pic) {
        SDL_UpdateTexture(pic, nullptr, cyc_frame_argb(), 256 * 4);
        SDL_SetTextureScaleMode(pic, SDL_ScaleModeNearest);
    }
    std::printf("[formae] menu open\n");
    std::fflush(stdout);
    Menu m;
    Uint64 last = SDL_GetTicks64();
    struct Step { Page page; int focus; const char *name; };
    static const Step SCRIPT[] = {
        { PAGE_MAIN, 0, "menu_1_paused" }, { PAGE_SETTINGS, 4, "menu_2_settings" }, { PAGE_CONTROLS, 0, "menu_3_controls" },
        { PAGE_ENHANCEMENTS, 1, "menu_4_enhancements" }, { PAGE_RESET, 0, "menu_5_reset" },
    };
    const bool selftest = selftest_dir() != nullptr;
    size_t step = 0;
    int step_frames = 0;
    if (selftest) { go(m, SCRIPT[0].page); m.cursor = SCRIPT[0].focus; }
    while (!m.resume && !m.quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (menu_event(m, ev)) continue;
            ImGui_ImplSDL2_ProcessEvent(&ev);
        }
        if (m.resume || m.quit) break;
        SDL_RenderSetLogicalSize(ren, 0, 0);
        SDL_RenderSetViewport(ren, nullptr);
        int ow = 0, oh = 0;
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        draw_picture(ren, pic, ow, oh);

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        const ImVec2 ds = ImGui::GetIO().DisplaySize;
        const Fonts &F = ui.fonts;
        ImDrawList *bg = ImGui::GetBackgroundDrawList();
        bg->AddRectFilled(ImVec2(0, 0), ds, DIM);
        float panel_w = ds.x * 0.42f;
        if (panel_w < 360.0f) panel_w = ds.x < 360.0f ? ds.x : 360.0f;
        if (panel_w > 600.0f) panel_w = 600.0f;
        bg->AddRectFilled(ImVec2(0, 0), ImVec2(panel_w, ds.y), PANEL);
        bg->AddRectFilled(ImVec2(panel_w - 1.0f, 0), ImVec2(panel_w, ds.y), IM_COL32(255, 255, 255, 14));

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(panel_w, ds.y));
        ImGui::Begin("##formae-menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoNav);
        Layout L;
        L.pad = F.base * 0.9f;
        L.x = F.base * 1.2f;
        L.width = panel_w - L.x * 2.0f;
        L.row_h = F.base * (m.page == PAGE_CONTROLS ? 1.65f : 2.1f);
        {
            // the cursor: Up/Down move it, Left/Right change a value (or the
            // column on the Controls page), Enter / Space / A choose
            const bool capturing = m.capture_row >= 0;
            const bool up = !capturing && pressed({ ImGuiKey_UpArrow, ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadLStickUp });
            const bool down = !capturing && pressed({ ImGuiKey_DownArrow, ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadLStickDown });
            bool left = !capturing && pressed({ ImGuiKey_LeftArrow, ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadLStickLeft });
            bool right = !capturing && pressed({ ImGuiKey_RightArrow, ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickRight });
            const int grid = m.page == PAGE_CONTROLS ? 2 * FORMAE_BIND_COUNT : 0;
            if (m.cursor < grid) {
                if (up && m.cursor >= 2) m.cursor -= 2;
                if (down) m.cursor = m.cursor + 2 < grid ? m.cursor + 2 : grid;
                if (left || right) m.cursor ^= 1;
                left = right = false;
            } else {
                if (up && m.cursor > 0) m.cursor = grid && m.cursor == grid ? grid - 2 : m.cursor - 1;
                if (down && m.cursor + 1 < m.count) m.cursor++;
            }
            if (m.cursor >= m.count) m.cursor = m.count - 1;
            if (m.cursor < 0) m.cursor = 0;
            L.cursor = m.cursor;
            L.left = left;
            L.right = right;
            L.accept = !capturing && pressed({ ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_Space, ImGuiKey_GamepadFaceDown });
            const ImVec2 md = ImGui::GetIO().MouseDelta;
            L.mouse_moved = md.x != 0.0f || md.y != 0.0f;
        }
        ImDrawList *dl = ImGui::GetWindowDrawList();
        float y = F.base * 2.0f;
        text_at(dl, F.small, L.x + L.pad, y, ACCENT, "FORMAE");
        y += F.small->FontSize * 1.3f;
        text_at(dl, F.title, L.x + L.pad, y, TEXT, "DuckTales");
        y += F.title->FontSize * 1.15f;
        text_at(dl, F.body, L.x + L.pad, y, TEXT_DIM, page_title(m.page));
        y += F.body->FontSize * 2.4f;
        ImGui::SetCursorScreenPos(ImVec2(L.x, y));
        const Page drawn_page = m.page;
        switch (m.page) {
        case PAGE_MAIN: page_main(m, L, F); break;
        case PAGE_SETTINGS: page_settings(m, L, F, win); break;
        case PAGE_CONTROLS: page_controls(m, L, F); break;
        case PAGE_ENHANCEMENTS: page_enhancements(m, L, F); break;
        case PAGE_RESET: page_reset(m, L, F); break;
        }
        if (m.page == drawn_page) {   // a page change resets the cursor; otherwise keep the mouse's choice
            m.count = L.index > 0 ? L.index : 1;
            m.cursor = L.cursor;
        }
        if (!m.notice.empty() && SDL_GetTicks64() < m.notice_until)
            draw_wrapped(dl, F.small, L.x + L.pad, ImGui::GetCursorScreenPos().y + F.base * 0.8f, L.width - L.pad * 2.0f, ACCENT,
                         m.notice.c_str());
        ImGui::End();
        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), ren);
        if (selftest && ++step_frames == 6) {
            selftest_shot(ren, SCRIPT[step].name);
            step_frames = 0;
            if (++step == sizeof SCRIPT / sizeof SCRIPT[0]) m.resume = true;
            else { go(m, SCRIPT[step].page); m.cursor = SCRIPT[step].focus; }
        }
        SDL_RenderPresent(ren);
        Uint64 now = SDL_GetTicks64();
        if (now - last < 16) SDL_Delay((Uint32)(16 - (now - last)));
        last = SDL_GetTicks64();
    }
    if (pic) SDL_DestroyTexture(pic);
    ui.end();
    SDL_SetRenderTarget(ren, target);
    SDL_RenderSetLogicalSize(ren, lw, lh);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    for (SDL_Event &ev : m.for_host) SDL_PushEvent(&ev);
    if (m.quit) {
        SDL_Event q;
        std::memset(&q, 0, sizeof q);
        q.type = SDL_QUIT;
        SDL_PushEvent(&q);
    }
    std::printf("[formae] menu closed%s\n", m.quit ? " (quit)" : "");
    std::fflush(stdout);
    return true;
}

// ---- first run ----

namespace {

#ifdef _WIN32
std::string pick_file(SDL_Window *win)
{
    wchar_t path[2048] = L"";
    OPENFILENAMEW ofn;
    std::memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    (void)win;
    ofn.lpstrFilter = L"NES ROM (*.nes)\0*.nes\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = 2048;
    ofn.lpstrTitle = L"Choose your DuckTales (USA) ROM";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
    std::string out((size_t)(n > 0 ? n : 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, path, -1, &out[0], n, nullptr, nullptr);
    out.resize(std::strlen(out.c_str()));
    return out;
}
#else
std::string pick_file(SDL_Window *) { return std::string(); }
#endif

bool button(const Fonts &F, const char *label, bool primary, float w, bool focused)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = F.base * 2.2f;
    bool clicked = ImGui::InvisibleButton(label, ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 q(p.x + w, p.y + h);
    if (primary) dl->AddRectFilled(p, q, hovered || focused ? IM_COL32(250, 194, 86, 255) : ACCENT, 5.0f);
    else dl->AddRect(p, q, hovered || focused ? TEXT : IM_COL32(255, 255, 255, 60), 5.0f, 0, 1.5f);
    if (focused && !primary) dl->AddRectFilled(p, q, ROW_FOCUS, 5.0f);
    float tw = text_w(F.body, label);
    text_at(dl, F.body, p.x + (w - tw) * 0.5f, p.y + (h - F.body->FontSize) * 0.5f, primary ? IM_COL32(20, 16, 8, 255) : TEXT, label);
    return clicked;
}

}   // namespace

extern "C" int formae_ui_first_run(const char *expected, char *rom_out, size_t cap)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) return 2;
    const bool selftest = selftest_dir() != nullptr;
    SDL_Window *win = SDL_CreateWindow("DuckTales", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 640,
                                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (selftest ? SDL_WINDOW_HIDDEN : 0));
    int selftest_frames = 0;
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if (win && !ren) ren = SDL_CreateRenderer(win, -1, 0);
    Imgui ui;
    if (!ren || !ui.begin(win, ren)) {
        if (ren) SDL_DestroyRenderer(ren);
        if (win) SDL_DestroyWindow(win);
        SDL_Quit();
        return 2;
    }
    char data_dir[1024] = "";
    formae_data_dir(data_dir, sizeof data_dir);
    std::string error, detail;
    int result = -1;
    bool focus_quit = false;
    Uint64 done_at = 0;
    std::string chosen;
    while (result < 0) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) result = 1;
            if (ev.type == SDL_DROPFILE) {
                chosen = ev.drop.file;
                SDL_free(ev.drop.file);
                continue;
            }
            ImGui_ImplSDL2_ProcessEvent(&ev);
        }
        if (!chosen.empty() && !done_at) {
            char actual[65] = "";
            FormaeRomStatus s = formae_rom_import(chosen.c_str(), expected, rom_out, cap, actual);
            if (s == FORMAE_ROM_OK) {
                error.clear();
                done_at = SDL_GetTicks64();
            } else {
                error = formae_rom_status_text(s);
                detail = s == FORMAE_ROM_MISMATCH ? std::string("SHA-256 of this file: ") + actual + "\nExpected: " + expected : std::string();
            }
            std::fprintf(stderr, "[formae] %s: %s\n", chosen.c_str(), formae_rom_status_text(s));
            chosen.clear();
        }
        if (done_at && SDL_GetTicks64() - done_at > 900) result = 0;

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        const Fonts &F = ui.fonts;
        const ImVec2 ds = ImGui::GetIO().DisplaySize;
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ds, BACKDROP);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ds);
        ImGui::Begin("##formae-first-run", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        float col_w = ds.x - F.base * 6.0f;
        if (col_w > F.base * 30.0f) col_w = F.base * 30.0f;
        float x = (ds.x - col_w) * 0.5f, y = ds.y * 0.16f;
        text_at(dl, F.small, x, y, ACCENT, "FORMAE");
        y += F.small->FontSize * 1.4f;
        text_at(dl, F.title, x, y, TEXT, "DuckTales");
        y += F.title->FontSize * 1.5f;
        const char *intro = "This port runs on your own copy of the game. Choose your DuckTales (USA) cartridge "
                            "image (.nes). It is checked against the original, then kept in your user folder, so "
                            "the game starts directly next time.";
        draw_wrapped(dl, F.body, x, y, col_w, TEXT_DIM, intro);
        y += wrapped_h(F.body, col_w, intro) + F.base * 1.6f;
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        bool busy = done_at != 0;
        // Left/Right (or Tab) move between the two buttons; Enter / A choose, Esc / B quit.
        if (pressed({ ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_Tab, ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight }))
            focus_quit = !focus_quit;
        bool accept = pressed({ ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_Space, ImGuiKey_GamepadFaceDown });
        if (pressed({ ImGuiKey_Escape, ImGuiKey_GamepadFaceRight })) result = 1;
        if (!busy && (button(F, F.system ? "Choose ROM file\u2026" : "Choose ROM file...", true, F.base * 11.0f, !focus_quit) ||
                      (accept && !focus_quit))) {
            chosen = pick_file(win);
        }
        ImGui::SetCursorScreenPos(ImVec2(x + F.base * 12.0f, y));
        if (!busy && (button(F, "Quit", false, F.base * 6.0f, focus_quit) || (accept && focus_quit))) result = 1;
        y += F.base * 3.4f;
        if (busy) {
            text_at(dl, F.body, x, y, OK_TEXT, "Verified. Starting the game.");
        } else if (!error.empty()) {
            draw_wrapped(dl, F.body, x, y, col_w, ERROR_TEXT, error.c_str());
            y += wrapped_h(F.body, col_w, error.c_str()) + F.base * 0.5f;
            if (!detail.empty()) draw_wrapped(dl, F.small, x, y, col_w, TEXT_DIM, detail.c_str());
        } else {
            text_at(dl, F.small, x, y, TEXT_DIM, "You can also drop the file onto this window.");
        }
        std::string where = std::string("Your copy is stored in ") + (data_dir[0] ? data_dir : "your user data folder") + ".";
        draw_wrapped(dl, F.small, x, ds.y - F.base * 3.0f, col_w, TEXT_OFF, where.c_str());
        ImGui::End();
        ImGui::Render();
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), ren);
        if (selftest) {
            ++selftest_frames;
            if (selftest_frames == 6) {
                selftest_shot(ren, "first_run_1_welcome");
                const char *pick = std::getenv("FORMAE_UI_SELFTEST_PICK");
                if (pick && *pick) chosen = pick;
                else result = 1;
            }
            if (selftest_frames == 12) {
                selftest_shot(ren, "first_run_2_result");
                if (!done_at) result = 1;
            }
        }
        SDL_RenderPresent(ren);
        SDL_Delay(1);
    }
    ui.end();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return result;
}
