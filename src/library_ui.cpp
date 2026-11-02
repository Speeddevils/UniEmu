#include "library_ui.h"
#include <algorithm>
#include <cmath>
#include <regex>
#include "imgui.h"
#include "imgui_internal.h"
#include "ui_common.h"
#include "util.h"
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

ImVec4 systemColor(System s) {
    switch (s) {
    case System::Dreamcast: return ImVec4(1.00f, 0.48f, 0.10f, 1);   // laranja Dreamcast
    case System::PS1: return ImVec4(0.36f, 0.48f, 1.00f, 1);         // azul PlayStation
    case System::N64: return ImVec4(0.18f, 0.80f, 0.44f, 1);         // verde N64
    case System::SNES: return ImVec4(0.61f, 0.36f, 0.90f, 1);        // roxo Super Nintendo
    case System::Atari2600: return ImVec4(0.86f, 0.33f, 0.22f, 1);   // laranja/marrom Atari
    case System::MegaDrive: return ImVec4(0.20f, 0.55f, 0.95f, 1);   // azul Mega Drive
    case System::GameCube: return ImVec4(0.45f, 0.33f, 0.85f, 1);    // índigo GameCube
    case System::Wii: return ImVec4(0.30f, 0.78f, 0.95f, 1);         // azul claro Wii
    case System::PS2: return ImVec4(0.16f, 0.32f, 0.85f, 1);         // azul PS2
    case System::PS3: return ImVec4(0.62f, 0.66f, 0.72f, 1);         // prata PS3
    case System::PS4: return ImVec4(0.18f, 0.45f, 0.90f, 1);         // azul PS4
    case System::Switch: return ImVec4(0.90f, 0.05f, 0.10f, 1);      // vermelho Switch
    case System::Xbox: return ImVec4(0.38f, 0.80f, 0.10f, 1);        // verde Xbox
    case System::Xbox360: return ImVec4(0.55f, 0.90f, 0.25f, 1);     // verde-limão 360
    default: return ImVec4(0.6f, 0.6f, 0.6f, 1);
    }
}

std::string displayName(const std::string& raw) {
    static const std::regex ver("\\s+[vV]\\d+(\\.\\d+)*\\b");
    std::string s = raw;
    std::string lower = util::toLower(raw);
    int disc = 0;
    size_t dp = lower.find("disc ");
    if (dp != std::string::npos) disc = atoi(lower.c_str() + dp + 5);
    size_t cut = s.find_first_of("([");
    if (cut != std::string::npos && cut > 0) s = s.substr(0, cut);
    s = std::regex_replace(s, ver, "");
    while (!s.empty() && (s.back() == ' ' || s.back() == '-' || s.back() == '_')) s.pop_back();
    // "Legend of Zelda, The - ..." -> "The Legend of Zelda - ..."
    size_t the = s.find(", The");
    if (the != std::string::npos) {
        size_t end = the + 5;
        s = "The " + s.substr(0, the) + s.substr(end);
    }
    if (disc > 0) s += " (Disco " + std::to_string(disc) + ")";
    return s.empty() ? raw : s;
}

void LibraryUi::refreshCovers(const std::vector<GameEntry>& games, bool force) {
    Uint32 now = SDL_GetTicks();
    if (!force && dl.generation() == lastGen && games.size() == lastCount && now - lastRefresh < 4000) return;
    lastGen = dl.generation();
    lastCount = games.size();
    lastRefresh = now;
    for (auto& g : games) coverOf[g.path] = coverPathFor(g);
}

namespace {

ImU32 col(ImVec4 v, float a = -1) { if (a >= 0) v.w = a; return ImGui::GetColorU32(v); }
ImVec4 mul(ImVec4 v, float k) { return ImVec4(v.x * k, v.y * k, v.z * k, v.w); }

// quebra o texto em até maxLines linhas que caibam em w (última com "...")
std::vector<std::string> wrapLines(const std::string& t, float w, int maxLines, ImFont* font, float size) {
    std::vector<std::string> out;
    std::string line, word;
    auto width = [&](const std::string& s) { return font->CalcTextSizeA(size, FLT_MAX, 0, s.c_str()).x; };
    std::vector<std::string> words;
    {
        std::string cur;
        for (char c : t) { if (c == ' ') { if (!cur.empty()) words.push_back(cur); cur.clear(); } else cur += c; }
        if (!cur.empty()) words.push_back(cur);
    }
    size_t i = 0;
    while (i < words.size() && (int)out.size() < maxLines) {
        std::string l = words[i++];
        while (i < words.size() && width(l + " " + words[i]) <= w) l += " " + words[i++];
        while (width(l) > w && l.size() > 1) l.pop_back();
        out.push_back(l);
    }
    if (i < words.size() && !out.empty()) {
        std::string& l = out.back();
        while (!l.empty() && width(l + "...") > w) l.pop_back();
        l += "...";
    }
    return out;
}

void drawTextCentered(ImDrawList* dl, ImFont* f, float size, ImVec2 center, ImU32 c, const std::string& s) {
    ImVec2 ts = f->CalcTextSizeA(size, FLT_MAX, 0, s.c_str());
    dl->AddText(f, size, ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), c, s.c_str());
}

#ifdef _WIN32
bool pickImageFile(std::string& out) {
    wchar_t buf[MAX_PATH * 4] = L"";
    OPENFILENAMEW of{};
    of.lStructSize = sizeof of;
    of.lpstrFilter = L"Imagens (PNG, JPG)\0*.png;*.jpg;*.jpeg\0";
    of.lpstrFile = buf;
    of.nMaxFile = MAX_PATH * 4;
    of.lpstrTitle = L"Escolha a imagem da capa";
    of.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&of)) return false;
    int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    out.assign(n ? n - 1 : 0, 0);
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, &out[0], n, nullptr, nullptr);
    return true;
}
#endif

// coloca o próximo item na mesma linha só se couber
void sameLineIfFits(float nextW) {
    ImGuiStyle& st = ImGui::GetStyle();
    float lastMax = ImGui::GetItemRectMax().x;
    float winMax = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    if (lastMax + st.ItemSpacing.x + nextW < winMax) ImGui::SameLine();
}
float buttonW(const char* l) { return ImGui::CalcTextSize(l).x + ImGui::GetStyle().FramePadding.x * 2; }

}  // namespace

void LibraryUi::draw(Settings& st, const std::vector<GameEntry>& games, int sysFilter, bool scanning, int scanProgress,
                     const std::function<void(const GameEntry&)>& launch, const std::function<void()>& rescan,
                     const std::string& status) {
    refreshCovers(games, false);
    tex.pump();
    ImFont* body = ImGui::GetFont();
    ImFont* pix = uiPixelFont() ? uiPixelFont() : body;
    ImFont* pixS = uiPixelFontSmall() ? uiPixelFontSmall() : body;
    float fs = ImGui::GetFontSize();

    // ---- cabeçalho ----
    auto upper = [](std::string x) { for (auto& c : x) c = (char)toupper((unsigned char)c); return x; };
    std::string pageTitle = sysFilter ? upper(systemName((System)sysFilter)) : "TODOS OS JOGOS";
    std::vector<int> view;
    std::string q = util::toLower(search);
    int missing = 0;
    for (int i = 0; i < (int)games.size(); i++) {
        if (sysFilter && (int)games[i].sys != sysFilter) continue;
        if (!q.empty() && util::toLower(games[i].name).find(q) == std::string::npos) continue;
        view.push_back(i);
        if (coverOf[games[i].path].empty()) missing++;
    }
    ImVec4 accent = sysFilter ? systemColor((System)sysFilter) : theme::magenta;
    ImGui::PushFont(pix);
    ImGui::TextColored(accent, "%s", pageTitle.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("  %d jogo%s", (int)view.size(), view.size() == 1 ? "" : "s");

    // ---- barra de ferramentas ----
    ImGui::SetNextItemWidth(fs * 15);
    ImGui::InputTextWithHint("##busca", "Buscar jogo...", search, sizeof search);
    ImGui::SameLine();
    auto toggle = [&](const char* label, bool on) {
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, theme::magenta);
        bool r = ImGui::Button(label);
        if (on) ImGui::PopStyleColor();
        return r;
    };
    if (toggle("Capas", st.viewMode == 0)) { st.viewMode = 0; st.save(); }
    ImGui::SameLine(0, 2);
    if (toggle("Lista", st.viewMode == 1)) { st.viewMode = 1; st.save(); }
    if (st.viewMode == 0) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs * 7);
        if (ImGui::SliderInt("##tam", &st.coverSize, 110, 320, "Tamanho")) {}
        if (ImGui::IsItemDeactivatedAfterEdit()) st.save();
    }
    char b[64];
    snprintf(b, sizeof b, "Baixar capas (%d faltando)", missing);
    sameLineIfFits(buttonW(b));
    if (dl.busy()) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::cyan);
        ImGui::Text("Baixando capas %d/%d...", dl.done(), dl.total());
        ImGui::PopStyleColor();
    } else {
        if (missing == 0) ImGui::BeginDisabled();
        if (ImGui::Button(b)) {
            std::vector<GameEntry> v;
            for (int i : view) v.push_back(games[i]);
            dl.enqueue(v, true);
        }
        if (missing == 0) ImGui::EndDisabled();
    }
    sameLineIfFits(buttonW("Automático") + fs * 1.5f);
    if (ImGui::Checkbox("Automático", &st.coversAuto)) st.save();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Baixa sozinho as capas dos jogos novos (acervo libretro-thumbnails)");
    sameLineIfFits(buttonW("Procurando jogos... 0000"));
    if (scanning) ImGui::TextDisabled("Procurando jogos... %d", scanProgress);
    else if (ImGui::Button("Atualizar")) rescan();
    if (!status.empty()) ImGui::TextColored(theme::yellow, "%s", status.c_str());

    if (games.empty() && !scanning) {
        ImGui::Spacing();
        ImGui::TextWrapped("Nenhum jogo encontrado. Vá em \"Pastas de jogos\" e adicione as pastas onde ficam seus jogos "
                           "(.gdi, .cdi, .chd, .cue, .pbp, .z64, .n64, .v64, .sfc, .smc, .zip). Também dá para arrastar "
                           "um jogo ou uma pasta para esta janela.");
        return;
    }

    // ---- lista ----
    if (st.viewMode == 1) {
        if (ImGui::BeginTable("games", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Jogo", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Console", ImGuiTableColumnFlags_WidthFixed, fs * 9);
            ImGui::TableSetupColumn("Formato", ImGuiTableColumnFlags_WidthFixed, fs * 5);
            ImGui::TableHeadersRow();
            ImGuiListClipper clip;
            clip.Begin((int)view.size());
            while (clip.Step())
                for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++) {
                    const GameEntry& g = games[view[r]];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::PushID(view[r]);
                    if (ImGui::Selectable(displayName(g.name).c_str(), selected == g.path,
                                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                        selected = g.path;
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || ImGui::GetIO().NavVisible) launch(g);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", g.path.c_str());
                    ImGui::PopID();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(systemColor(g.sys), "%s", systemName(g.sys));
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", util::extLower(g.path).c_str() + 1);
                }
            ImGui::EndTable();
        }
        return;
    }

    // ---- grade de capas ----
    ImGui::BeginChild("grid", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NavFlattened);
    float W = (float)st.coverSize;
    float stripH = std::max(14.0f, fs * 0.95f);
    float imgH = W;   // área quadrada: serve para caixas de CD (quadradas) e de cartucho (deitadas)
    float titleH = fs * 2.5f;
    float H = stripH + imgH + titleH;
    float gap = std::max(12.0f, W * 0.09f);
    float avail = ImGui::GetContentRegionAvail().x;
    int cols = std::max(1, (int)((avail + gap) / (W + gap)));
    float extra = (avail - (cols * W + (cols - 1) * gap)) / 2;   // centraliza a grade
    int rows = ((int)view.size() + cols - 1) / cols;
    ImDrawList* dl2 = ImGui::GetWindowDrawList();
    double t = ImGui::GetTime();
    ImGuiListClipper clip;
    clip.Begin(rows, H + gap);
    while (clip.Step()) {
        for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++) {
            for (int c = 0; c < cols; c++) {
                int idx = r * cols + c;
                if (idx >= (int)view.size()) break;
                const GameEntry& g = games[view[idx]];
                ImGui::SetCursorPos(ImVec2(extra + c * (W + gap), r * (H + gap) + gap * 0.5f));
                ImGui::PushID(view[idx]);
                ImVec2 p0 = ImGui::GetCursorScreenPos();
                bool clicked = ImGui::InvisibleButton("card", ImVec2(W, H));
                bool hov = ImGui::IsItemHovered();
                bool foc = ImGui::IsItemFocused() && ImGui::GetIO().NavVisible;
                if (clicked) { selected = g.path; launch(g); }
                std::string cover = coverOf[g.path];
                if (ImGui::BeginPopupContextItem("ctx")) {
                    ImGui::TextDisabled("%s", displayName(g.name).c_str());
                    ImGui::Separator();
                    if (ImGui::MenuItem("Jogar")) launch(g);
                    if (ImGui::MenuItem("Baixar capa da internet")) {
                        if (!cover.empty()) { tex.invalidate(cover); util::removeFile(cover); }
                        dl.enqueue({g}, true);
                        lastRefresh = 0;
                    }
#ifdef _WIN32
                    if (ImGui::MenuItem("Escolher imagem da capa...")) {
                        std::string f;
                        if (pickImageFile(f)) {
                            std::vector<uint8_t> d;
                            if (util::readFile(f, d)) {
                                std::string out = coverSavePath(g);
                                if (util::extLower(f) != ".png") out = out.substr(0, out.size() - 4) + ".jpg";
                                if (!cover.empty()) { tex.invalidate(cover); util::removeFile(cover); }
                                util::writeFile(out, d.data(), d.size());
                                tex.invalidate(out);
                                lastRefresh = 0;
                            }
                        }
                    }
#endif
                    if (!cover.empty() && ImGui::MenuItem("Remover capa")) {
                        tex.invalidate(cover);
                        util::removeFile(cover);
                        lastRefresh = 0;
                    }
                    ImGui::Separator();
                    ImGui::TextDisabled("%s", g.path.c_str());
                    ImGui::EndPopup();
                }
                if (hov) ImGui::SetTooltip("%s\n%s", displayName(g.name).c_str(), systemName(g.sys));
                ImGui::PopID();

                // ---- desenho do cartão ----
                float lift = (hov || foc) ? 4.0f : 0.0f;
                ImVec2 a(p0.x, p0.y - lift), b(p0.x + W, p0.y + H - lift);
                ImVec4 sc = systemColor(g.sys);
                float rnd = 8.0f;
                if (hov || foc) {   // brilho neon
                    ImVec4 glow = foc ? theme::magenta : theme::cyan;
                    for (int k = 1; k <= 4; k++)
                        dl2->AddRect(ImVec2(a.x - k * 2, a.y - k * 2), ImVec2(b.x + k * 2, b.y + k * 2), col(glow, 0.16f / k),
                                     rnd + k * 2, 0, 3.0f);
                } else {
                    dl2->AddRectFilled(ImVec2(a.x + 4, a.y + 6), ImVec2(b.x + 4, b.y + 6), IM_COL32(0, 0, 0, 90), rnd);
                }
                dl2->AddRectFilled(a, b, col(theme::card), rnd);
                // faixa do console (como a faixa "PlayStation 2" das capas)
                dl2->AddRectFilled(a, ImVec2(b.x, a.y + stripH), col(mul(sc, 0.85f)), rnd, ImDrawFlags_RoundCornersTop);
                {
                    std::string lbl = upper(systemName(g.sys));
                    float ps = pixS->FontSize;
                    dl2->AddText(pixS, ps, ImVec2(a.x + 7, a.y + (stripH - ps) * 0.5f + 1), IM_COL32(255, 255, 255, 235), lbl.c_str());
                }
                ImVec2 ia(a.x + 6, a.y + stripH + 6), ib(b.x - 6, a.y + stripH + imgH - 6);
                const CoverTex* ct = cover.empty() ? nullptr : tex.get(cover);
                if (ct) {
                    float aw = ib.x - ia.x, ah = ib.y - ia.y;
                    float s = std::min(aw / ct->w, ah / ct->h);
                    float dw = ct->w * s, dh = ct->h * s;
                    ImVec2 q0(ia.x + (aw - dw) / 2, ia.y + (ah - dh) / 2);
                    dl2->AddRectFilled(ia, ib, IM_COL32(8, 6, 20, 255), 4.0f);
                    dl2->AddImageRounded((ImTextureID)(intptr_t)ct->tex, q0, ImVec2(q0.x + dw, q0.y + dh), ImVec2(0, 0),
                                         ImVec2(1, 1), IM_COL32_WHITE, 4.0f);
                } else {
                    // capa provisória: degradê da cor do console + nome do jogo
                    dl2->AddRectFilledMultiColor(ia, ib, col(mul(sc, 0.55f)), col(mul(sc, 0.35f)), col(mul(sc, 0.12f)),
                                                 col(mul(sc, 0.22f)));
                    for (float y = ia.y; y < ib.y; y += 3) dl2->AddLine(ImVec2(ia.x, y), ImVec2(ib.x, y), IM_COL32(0, 0, 0, 40));
                    auto lines = wrapLines(displayName(g.name), (ib.x - ia.x) - 14, 4, body, fs * 1.05f);
                    float lh = fs * 1.15f, y0 = (ia.y + ib.y) * 0.5f - lines.size() * lh * 0.5f;
                    for (size_t i = 0; i < lines.size(); i++)
                        drawTextCentered(dl2, body, fs * 1.05f, ImVec2((ia.x + ib.x) * 0.5f, y0 + lh * (i + 0.5f)),
                                         IM_COL32(255, 255, 255, 230), lines[i]);
                    bool loading = !cover.empty() || dl.busy();
                    const char* tag = loading ? ((int)(t * 2) % 2 ? "CARREGANDO" : "") : "SEM CAPA";
                    if (*tag) drawTextCentered(dl2, pixS, pixS->FontSize, ImVec2((ia.x + ib.x) * 0.5f, ib.y - pixS->FontSize * 1.6f),
                                               col(theme::yellow, 0.85f), tag);
                }
                // título
                {
                    auto lines = wrapLines(displayName(g.name), W - 12, 2, body, fs * 0.9f);
                    float ty = a.y + stripH + imgH + 4;
                    for (size_t i = 0; i < lines.size(); i++)
                        drawTextCentered(dl2, body, fs * 0.9f, ImVec2((a.x + b.x) * 0.5f, ty + fs * 0.55f + i * fs * 1.0f),
                                         (hov || foc) ? IM_COL32(255, 255, 255, 255) : col(theme::text, 0.85f), lines[i]);
                }
                ImU32 border = foc ? col(theme::magenta) : hov ? col(theme::cyan) : col(sc, 0.35f);
                dl2->AddRect(a, b, border, rnd, 0, (hov || foc) ? 2.5f : 1.0f);
            }
        }
    }
    ImGui::SetCursorPos(ImVec2(0, rows * (H + gap) + gap));
    ImGui::Dummy(ImVec2(1, 1));
    ImGui::EndChild();
}
