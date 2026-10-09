// Fenêtre d'aperçu du Canvas (écran OLED 128x64) : Dear ImGui + SDL2.
// On tape du texte dans le panneau, l'écran se met à jour en direct.
// Premier jalon de l'émulateur : l'écran réel est déjà affiché tel qu'il le sera sur Teensy.
#include <SDL.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "../../core/ui/canvas.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "wheel_slider.h"

namespace {

constexpr int LINES = Canvas::H / Canvas::CHAR_H;  // 8 lignes

struct Line {
    char text[32]  = "";
    char right[16] = "";  // aligné à droite
    bool inverted  = false;
};

void draw_line(Canvas& c, int row, const Line& line) {
    const int y = row * Canvas::CHAR_H;
    if (line.inverted) c.rect(0, y, Canvas::W, Canvas::CHAR_H, true);
    c.text(0, y, line.text, line.inverted);
    if (line.right[0]) c.text(Canvas::W - Canvas::text_width(line.right), y, line.right, line.inverted);
}

void set(Line& l, const char* text, const char* right = "", bool inverted = false) {
    std::snprintf(l.text, sizeof l.text, "%s", text);
    std::snprintf(l.right, sizeof l.right, "%s", right);
    l.inverted = inverted;
}

}  // namespace

int main() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init : %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");  // pixels nets quand l'écran est agrandi

    SDL_Window* window = SDL_CreateWindow("Drum machine - emulateur", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, 960, 640, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer =
        SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    if (!window || !renderer) {
        std::fprintf(stderr, "SDL : %s\n", SDL_GetError());
        return 1;
    }

    SDL_Texture* screen = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                            SDL_TEXTUREACCESS_STREAMING, Canvas::W, Canvas::H);
    SDL_SetTextureScaleMode(screen, SDL_ScaleModeNearest);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;  // pas de fichier imgui.ini dans le projet
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    Line lines[LINES];
    set(lines[0], "Pistes", "120 >");
    set(lines[1], "> Kick1", "", true);
    set(lines[2], "  Snare1");
    set(lines[3], "  HHcl");
    set(lines[4], "  + Ajouter piste");

    int scale = 6;

    std::vector<uint32_t> pixels(Canvas::W * Canvas::H);
    const uint32_t        on_color  = 0xFFB8E8FF;  // bleu-blanc d'un OLED
    const uint32_t        off_color = 0xFF04060A;

    for (bool running = true; running;) {
        for (SDL_Event e; SDL_PollEvent(&e);) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) running = false;
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Panneau d'édition : une ligne de l'écran par rangée.
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::Begin("Texte de l'ecran (8 lignes de 21 caracteres)");
        SliderIntWheel("Zoom", &scale, 2, 8);
        ImGui::Separator();
        ImGui::TextUnformatted("Ligne                          Droite       Inverse");
        for (int i = 0; i < LINES; ++i) {
            ImGui::PushID(i);
            ImGui::SetNextItemWidth(260);
            ImGui::InputText("##text", lines[i].text, sizeof lines[i].text);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            ImGui::InputText("##right", lines[i].right, sizeof lines[i].right);
            ImGui::SameLine();
            ImGui::Checkbox("##inv", &lines[i].inverted);
            ImGui::PopID();
        }
        ImGui::End();

        // Rendu du Canvas : exactement ce qui partira vers l'écran sur Teensy.
        Canvas canvas;
        for (int i = 0; i < LINES; ++i) draw_line(canvas, i, lines[i]);

        for (int y = 0; y < Canvas::H; ++y)
            for (int x = 0; x < Canvas::W; ++x) pixels[y * Canvas::W + x] = canvas.get_pixel(x, y) ? on_color : off_color;
        SDL_UpdateTexture(screen, nullptr, pixels.data(), Canvas::W * sizeof(uint32_t));

        ImGui::SetNextWindowPos(ImVec2(10, 220), ImGuiCond_FirstUseEver);
        ImGui::Begin("Ecran OLED 128x64", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Image(reinterpret_cast<ImTextureID>(screen),
                     ImVec2(static_cast<float>(Canvas::W * scale), static_cast<float>(Canvas::H * scale)));
        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 30, 30, 34, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyTexture(screen);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
