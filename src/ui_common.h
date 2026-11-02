// UniEmu - tema visual "neon retrô" (Dear ImGui)
#pragma once
#include "imgui.h"
#include "util.h"

// Paleta
namespace theme {
const ImVec4 bg0 = ImVec4(0.043f, 0.031f, 0.110f, 1.0f);      // #0B081C
const ImVec4 bg1 = ImVec4(0.086f, 0.063f, 0.200f, 1.0f);      // #161033
const ImVec4 panel = ImVec4(0.075f, 0.059f, 0.165f, 0.92f);
const ImVec4 card = ImVec4(0.110f, 0.086f, 0.239f, 1.0f);
const ImVec4 magenta = ImVec4(1.00f, 0.180f, 0.533f, 1.0f);   // #FF2E88
const ImVec4 cyan = ImVec4(0.000f, 0.898f, 1.000f, 1.0f);     // #00E5FF
const ImVec4 yellow = ImVec4(1.000f, 0.824f, 0.247f, 1.0f);   // #FFD23F
const ImVec4 text = ImVec4(0.93f, 0.92f, 1.0f, 1.0f);
const ImVec4 dim = ImVec4(0.60f, 0.57f, 0.78f, 1.0f);
}

inline ImFont*& uiPixelFont() { static ImFont* f = nullptr; return f; }
inline ImFont*& uiPixelFontSmall() { static ImFont* f = nullptr; return f; }

inline void uiSetup(float fontPx) {
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    const char* fonts[] = {
#ifdef _WIN32
        "C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\arial.ttf",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
#endif
    };
    bool loaded = false;
    for (auto f : fonts)
        if (util::fileExists(f) && io.Fonts->AddFontFromFileTTF(f, fontPx, nullptr, io.Fonts->GetGlyphRangesDefault())) {
            loaded = true;
            break;
        }
    if (!loaded) {
        ImFontConfig cfg;
        cfg.SizePixels = fontPx;
        io.Fonts->AddFontDefault(&cfg);
    }
    // fonte "pixel" para títulos (Press Start 2P, licença OFL)
    std::string pix = util::joinPath(util::joinPath(util::baseDir(), "fonts"), "PressStart2P-Regular.ttf");
    if (util::fileExists(pix)) {
        uiPixelFont() = io.Fonts->AddFontFromFileTTF(pix.c_str(), fontPx * 1.05f, nullptr, io.Fonts->GetGlyphRangesDefault());
        uiPixelFontSmall() = io.Fonts->AddFontFromFileTTF(pix.c_str(), fontPx * 0.55f, nullptr, io.Fonts->GetGlyphRangesDefault());
    }

    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 10.0f;
    s.ChildRounding = 10.0f;
    s.FrameRounding = 8.0f;
    s.PopupRounding = 8.0f;
    s.GrabRounding = 8.0f;
    s.TabRounding = 8.0f;
    s.ScrollbarRounding = 8.0f;
    s.FramePadding = ImVec2(12, 7);
    s.ItemSpacing = ImVec2(10, 9);
    s.WindowPadding = ImVec2(16, 16);
    s.ScrollbarSize = 12.0f;
    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    ImVec4* c = s.Colors;
    auto A = [](ImVec4 v, float a) { v.w = a; return v; };
    c[ImGuiCol_Text] = theme::text;
    c[ImGuiCol_TextDisabled] = theme::dim;
    c[ImGuiCol_WindowBg] = A(theme::bg0, 1.0f);
    c[ImGuiCol_ChildBg] = A(theme::panel, 0.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.07f, 0.20f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(1.0f, 0.18f, 0.53f, 0.22f);
    c[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.10f, 0.28f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.14f, 0.38f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.17f, 0.46f, 1.0f);
    c[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.07f, 0.22f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.30f, 0.07f, 0.30f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.20f, 0.12f, 0.40f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.75f, 0.13f, 0.45f, 1.0f);
    c[ImGuiCol_ButtonActive] = theme::magenta;
    c[ImGuiCol_Header] = ImVec4(0.55f, 0.10f, 0.40f, 0.55f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.75f, 0.13f, 0.45f, 0.75f);
    c[ImGuiCol_HeaderActive] = theme::magenta;
    c[ImGuiCol_CheckMark] = theme::cyan;
    c[ImGuiCol_SliderGrab] = theme::cyan;
    c[ImGuiCol_SliderGrabActive] = theme::magenta;
    c[ImGuiCol_Separator] = ImVec4(1.0f, 0.18f, 0.53f, 0.35f);
    c[ImGuiCol_Tab] = ImVec4(0.16f, 0.11f, 0.32f, 1.0f);
    c[ImGuiCol_TabHovered] = ImVec4(0.75f, 0.13f, 0.45f, 1.0f);
    c[ImGuiCol_TabActive] = ImVec4(0.55f, 0.10f, 0.40f, 1.0f);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.16f, 0.11f, 0.32f, 1.0f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.35f, 0.20f, 0.55f, 0.8f);
    c[ImGuiCol_ScrollbarGrabHovered] = theme::magenta;
    c[ImGuiCol_NavHighlight] = theme::cyan;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.02f, 0.01f, 0.06f, 0.7f);
}

// Fundo "synthwave": degradê, grade em perspectiva e scanlines
inline void uiDrawBackground() {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImVec2 sz = ImGui::GetIO().DisplaySize;
    dl->AddRectFilledMultiColor(ImVec2(0, 0), sz, ImGui::GetColorU32(theme::bg0), ImGui::GetColorU32(theme::bg0),
                                ImGui::GetColorU32(theme::bg1), ImGui::GetColorU32(theme::bg1));
    float horizon = sz.y * 0.62f;
    // brilho do horizonte
    dl->AddRectFilledMultiColor(ImVec2(0, horizon - 60), ImVec2(sz.x, horizon), IM_COL32(255, 46, 136, 0),
                                IM_COL32(255, 46, 136, 0), IM_COL32(255, 46, 136, 38), IM_COL32(255, 46, 136, 38));
    ImU32 grid = IM_COL32(255, 46, 136, 34);
    for (int i = 1; i < 14; i++) {   // linhas horizontais cada vez mais espaçadas
        float t = i / 14.0f;
        float y = horizon + (sz.y - horizon) * t * t;
        dl->AddLine(ImVec2(0, y), ImVec2(sz.x, y), grid, 1.0f);
    }
    float cx = sz.x * 0.5f;
    for (int i = -18; i <= 18; i++) {   // linhas que convergem no horizonte
        float xb = cx + i * sz.x * 0.09f;
        dl->AddLine(ImVec2(cx + i * sz.x * 0.012f, horizon), ImVec2(xb, sz.y), grid, 1.0f);
    }
    for (float y = 0; y < sz.y; y += 3.0f) dl->AddLine(ImVec2(0, y), ImVec2(sz.x, y), IM_COL32(0, 0, 0, 22), 1.0f);
}
