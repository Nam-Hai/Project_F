// Émulateur : la drum machine complète sur PC, en deux fenêtres indépendantes.
//
//   - Fenêtre « Ecran »    : l'OLED 128x64 (vue réelle). Affiche le Canvas, ne produit aucune entrée.
//   - Fenêtre « Controles »: le faux matériel (roue, boutons, potard). Ne produit que des InputEvent.
//
// Les deux fenêtres ont chacune leur contexte ImGui et ne se connaissent pas : elles parlent
// seulement à UiController (InputEvent en entrée, Canvas en sortie).
//
// Clavier (fenêtre Controles) : flèches haut/bas = roue, Entrée = Valider,
// Retour arrière / Échap = Back, M = Menu, Tab = Mode, Espace = Play/Stop, 1-8 = pas 1-8, Q W E R T Y U I = pas 9-16.
#include <SDL.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../../core/engine.h"
#include "../../core/ui/canvas.h"
#include "../../core/ui/keyboard_layout.h"
#include "../../core/ui/ui_controller.h"
#include "audio_miniaudio.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "profile_store_file.h"
#include "sample_bank.h"
#include "sample_library.h"
#include "wheel_slider.h"

namespace {

void render_cb(float* left, float* right, int frames, void* user) {
    static_cast<Engine*>(user)->render(left, right, frames);
}

constexpr ImGuiKey STEP_KEYS[16] = {
    ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5, ImGuiKey_6, ImGuiKey_7, ImGuiKey_8,
    ImGuiKey_Q, ImGuiKey_W, ImGuiKey_E, ImGuiKey_R, ImGuiKey_T, ImGuiKey_Y, ImGuiKey_U, ImGuiKey_I,
};

// Une fenêtre SDL avec son renderer et son propre contexte ImGui.
class HostWindow {
public:
    HostWindow(const char* title, int x, int y, int w, int h, bool vsync) {
        window_ = SDL_CreateWindow(title, x, y, w, h, SDL_WINDOW_RESIZABLE);
        Uint32 flags = SDL_RENDERER_ACCELERATED | (vsync ? SDL_RENDERER_PRESENTVSYNC : 0);
        renderer_    = SDL_CreateRenderer(window_, -1, flags);

        context_ = ImGui::CreateContext();
        ImGui::SetCurrentContext(context_);
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::StyleColorsDark();
        ImGui_ImplSDL2_InitForSDLRenderer(window_, renderer_);
        ImGui_ImplSDLRenderer2_Init(renderer_);
    }

    ~HostWindow() {
        ImGui::SetCurrentContext(context_);
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext(context_);
        SDL_DestroyRenderer(renderer_);
        SDL_DestroyWindow(window_);
    }

    bool ok() const { return window_ && renderer_; }
    SDL_Renderer* renderer() const { return renderer_; }

    // Chaque contexte ne réagit qu'aux événements de sa propre fenêtre.
    void process(SDL_Event& e) {
        ImGui::SetCurrentContext(context_);
        ImGui_ImplSDL2_ProcessEvent(&e);
    }

    // Ouvre une fenêtre ImGui qui remplit toute la fenêtre SDL.
    void begin_frame(const char* name) {
        ImGui::SetCurrentContext(context_);
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin(name, nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    }

    void end_frame() {
        ImGui::End();
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer_, 30, 30, 34, 255);
        SDL_RenderClear(renderer_);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer_);
        SDL_RenderPresent(renderer_);
    }

private:
    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    ImGuiContext* context_  = nullptr;
};

// Touches noires du clavier de piano (10) : Z X C V B N , . / ' (le M est déjà pris par Menu).
constexpr ImGuiKey BLACK_KEYS[10] = {
    ImGuiKey_Z, ImGuiKey_X, ImGuiKey_C, ImGuiKey_V, ImGuiKey_B,
    ImGuiKey_N, ImGuiKey_Comma, ImGuiKey_Period, ImGuiKey_Slash, ImGuiKey_Apostrophe,
};

// Vue « écran » : affiche le Canvas agrandi, tel qu'il sera envoyé à l'OLED. Aucune entrée.
class ScreenView {
public:
    explicit ScreenView(SDL_Renderer* renderer) : pixels_(Canvas::W * Canvas::H) {
        texture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                     Canvas::W, Canvas::H);
        SDL_SetTextureScaleMode(texture_, SDL_ScaleModeNearest);
    }
    ~ScreenView() { SDL_DestroyTexture(texture_); }

    // Zoom entier (pixels nets) qui remplit la fenêtre ; l'image est centrée.
    void draw(const Canvas& canvas) {
        for (int y = 0; y < Canvas::H; ++y)
            for (int x = 0; x < Canvas::W; ++x)
                pixels_[y * Canvas::W + x] = canvas.get_pixel(x, y) ? 0xFFB8E8FF : 0xFF04060A;
        SDL_UpdateTexture(texture_, nullptr, pixels_.data(), Canvas::W * sizeof(uint32_t));

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        int scale = static_cast<int>(std::fmin(avail.x / Canvas::W, avail.y / Canvas::H));
        if (scale < 1) scale = 1;

        const float w = static_cast<float>(Canvas::W * scale);
        const float h = static_cast<float>(Canvas::H * scale);
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + (avail.x - w) * 0.5f,
                                   ImGui::GetCursorPosY() + (avail.y - h) * 0.5f));
        ImGui::Image(reinterpret_cast<ImTextureID>(texture_), ImVec2(w, h));
    }

private:
    SDL_Texture*          texture_;
    std::vector<uint32_t> pixels_;
};

// Vue « matériel simulé » : faux boutons, roue et potard. Ne produit que des InputEvent.
class FakeHardwareView {
public:
    explicit FakeHardwareView(float initial_pot) : pot_(initial_pot) {}

    // Les LED des boutons de pas lisent l'état du moteur et l'UI, comme le feraient de vraies LED.
    void draw(UiController& ui, const EngineState& state) {
        // Roue : un slider rotatif. Le glisser émet la différence en crans ; la molette de la souris
        // (ou les flèches) le fait tourner en continu, avec retour au début en bout de course.
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Roue");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(300);
        int pos = wheel_pos_;
        if (ImGui::SliderInt("##wheel", &pos, 0, WHEEL_STEPS - 1, "%d")) {
            const int delta = pos - wheel_pos_;
            wheel_pos_      = pos;
            if (delta) ui.handle(InputEvent::wheel(delta));
        }
        if (ImGui::IsItemHovered()) {
            wheel_acc_ += ImGui::GetIO().MouseWheel;
            while (wheel_acc_ >= 1.0f) { turn(ui, -1); wheel_acc_ -= 1.0f; }  // molette vers le haut = monter
            while (wheel_acc_ <= -1.0f) { turn(ui, 1); wheel_acc_ += 1.0f; }
        } else {
            wheel_acc_ = 0.0f;
        }
        ImGui::SameLine(0, 30);
        if (ImGui::Button("Valider", ImVec2(80, 30))) ui.handle(InputEvent::ok());
        ImGui::SameLine();
        if (ImGui::Button("Back", ImVec2(80, 30))) ui.handle(InputEvent::back());
        ImGui::SameLine();
        if (ImGui::Button("Menu", ImVec2(80, 30))) ui.handle(InputEvent::menu());
        ImGui::SameLine();
        if (ImGui::Button("Mode", ImVec2(80, 30))) ui.handle(InputEvent::mode());
        ImGui::SameLine(0, 30);
        if (ImGui::Button(ui.playing() ? "Stop" : "Play", ImVec2(80, 30))) ui.handle(InputEvent::play());

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("BPM ");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(420);
        // Un cran de molette = 1 BPM.
        constexpr float bpm_notch = 1.0f / static_cast<float>(UiController::BPM_MAX - UiController::BPM_MIN);
        if (SliderFloatWheel("##bpm", &pot_, 0.0f, 1.0f, "", bpm_notch)) ui.handle(InputEvent::pot_bpm(pot_));
        ImGui::SameLine();
        ImGui::Text("%d", ui.bpm());

        ImGui::Spacing();

        // 10 touches noires du clavier de piano, au-dessus des 16 boutons de pas, décalées entre deux
        // blanches. Hors de la vue Clavier, seule la première est active : elle ouvre le clavier.
        const bool  in_keyboard = ui.view() == UiController::View::Keyboard;
        const float spacing     = ImGui::GetStyle().ItemSpacing.x;
        const float white_w     = 44.0f;
        const float black_w     = 30.0f;
        const float black_h     = 34.0f;
        const float x0          = ImGui::GetCursorPosX();
        const float y0          = ImGui::GetCursorPosY();

        ImGui::SetCursorPos(ImVec2(x0 + 4.0f, y0));
        ImGui::TextDisabled("Clavier");  // étiquette de la première touche noire
        const float black_y = y0 + ImGui::GetTextLineHeightWithSpacing();

        static const char* const black_names[10] = {"C#", "D#", "F#", "G#", "A#", "C#", "D#", "F#", "G#", "A#"};
        for (int j = 0; j < keyboard::BLACK_KEYS; ++j) {
            const float center = x0 + keyboard::BLACK_LEFT_WHITE[j] * (white_w + spacing) + white_w + spacing * 0.5f;
            ImGui::SetCursorPos(ImVec2(center - black_w * 0.5f, black_y));

            ImGui::BeginDisabled(!in_keyboard && j != 0);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.07f, 0.07f, 0.09f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.20f, 0.26f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.35f, 0.45f, 1.0f));
            // Une bordure claire : sans elle, la touche noire se confond avec le fond du panneau.
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.75f, 0.75f, 0.82f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
            char label[24];
            std::snprintf(label, sizeof label, "%s##black%d", black_names[j], j);
            if (ImGui::Button(label, ImVec2(black_w, black_h))) ui.handle(InputEvent::black(j));
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(4);
            ImGui::EndDisabled();
        }

        // 16 boutons de pas, avec une LED simulée : allumée si le pas est actif (vue Pattern), plus claire sur
        // le pas en cours de lecture. En vue Clavier ce sont les touches blanches : nom de la note, les deux
        // dernières grisées.
        ImGui::SetCursorPos(ImVec2(x0, black_y + black_h + 6.0f));

        const bool     in_pattern = ui.view() == UiController::View::Pattern;
        const uint16_t bits       = in_pattern ? ui.pattern_bits(ui.selected_track()) : 0;
        const int      play_step  = state.playing.load() ? state.step.load() : -1;

        static const char* const white_names[14] = {"C", "D", "E", "F", "G", "A", "B", "C", "D", "E", "F", "G", "A", "B"};
        for (int i = 0; i < cfg::MAX_STEPS; ++i) {
            const bool on      = (bits >> i) & 1u;
            const bool playing = (i == play_step);

            ImVec4 color = on ? ImVec4(0.85f, 0.30f, 0.10f, 1.0f) : ImVec4(0.22f, 0.22f, 0.25f, 1.0f);
            if (playing) color = on ? ImVec4(1.0f, 0.85f, 0.3f, 1.0f) : ImVec4(0.45f, 0.55f, 0.65f, 1.0f);

            char label[24];
            if (in_keyboard) {
                // Touches blanches : claires, texte sombre ; le Do central (note de base) est souligné par son nom.
                color = ImVec4(0.82f, 0.82f, 0.86f, 1.0f);
                std::snprintf(label, sizeof label, "%s##step%d", i < keyboard::WHITE_KEYS ? white_names[i] : "-", i);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.05f, 0.08f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.95f, 1.0f, 1.0f));
            } else {
                std::snprintf(label, sizeof label, "%d##step%d", i + 1, i);
            }
            ImGui::BeginDisabled(in_keyboard && i >= keyboard::WHITE_KEYS);
            ImGui::PushStyleColor(ImGuiCol_Button, color);
            if (ImGui::Button(label, ImVec2(white_w, 44))) ui.handle(InputEvent::step(i));
            ImGui::PopStyleColor();
            ImGui::EndDisabled();
            if (in_keyboard) ImGui::PopStyleColor(2);
            if (i != cfg::MAX_STEPS - 1) ImGui::SameLine();
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Molette de la souris sur la roue, ou fleches haut/bas.");
        ImGui::TextDisabled("Entree = Valider, Retour/Echap = Back, M = Menu, Tab = Mode, Espace = Play/Stop,");
        ImGui::TextDisabled("1-8 et Q W E R T Y U I = pas 1 a 16 (touches blanches du clavier).");
        ImGui::TextDisabled("Z X C V B N , . / ' = touches noires 1 a 10 (la 1re ouvre le clavier).");
    }

    void keyboard(UiController& ui) {
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) turn(ui, -1);
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) turn(ui, 1);
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) ui.handle(InputEvent::ok());
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            ui.handle(InputEvent::back());
        if (ImGui::IsKeyPressed(ImGuiKey_M, false)) ui.handle(InputEvent::menu());
        if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) ui.handle(InputEvent::mode());
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) ui.handle(InputEvent::play());
        for (int i = 0; i < 16; ++i)
            if (ImGui::IsKeyPressed(STEP_KEYS[i], false)) ui.handle(InputEvent::step(i));
        for (int j = 0; j < 10; ++j)
            if (ImGui::IsKeyPressed(BLACK_KEYS[j], false)) ui.handle(InputEvent::black(j));
    }

private:
    static constexpr int WHEEL_STEPS = 40;

    // Un cran de roue : le slider avance (en rebouclant) et l'événement part vers l'UI.
    void turn(UiController& ui, int delta) {
        wheel_pos_ = (wheel_pos_ + delta + WHEEL_STEPS) % WHEEL_STEPS;
        ui.handle(InputEvent::wheel(delta));
    }

    float pot_;
    int   wheel_pos_ = WHEEL_STEPS / 2;
    float wheel_acc_ = 0.0f;
};

}  // namespace

int main() {
    Engine               engine;
    SampleBank           bank;
    DesktopSampleLibrary library(SAMPLES_DIR, bank);
    FileProfileStore     store(std::string(PROFILES_DIR) + "/default.json");
    UiController         ui(engine, &library, &store);
    DesktopAudio         audio;  // détruit avant le moteur et les buffers

    // La machine démarre sur une loop vide ; les loops sauvegardées sont dans le profil.
    ui.load_profile();
    if (!audio.start(render_cb, &engine)) return 1;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init : %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    {
        // Le vsync d'une seule fenêtre cadence la boucle (deux vsync de suite la diviseraient par deux).
        HostWindow screen_window("Drum machine - Ecran OLED", 60, 60, 800, 420, true);
        HostWindow controls_window("Drum machine - Controles (materiel simule)", 60, 490, 940, 400, false);
        if (!screen_window.ok() || !controls_window.ok()) {
            std::fprintf(stderr, "SDL : %s\n", SDL_GetError());
            return 1;
        }

        ScreenView screen(screen_window.renderer());
        // Le potard démarre à la position qui correspond au BPM de départ : pas de saut de tempo.
        FakeHardwareView hardware(static_cast<float>(ui.bpm() - UiController::BPM_MIN) /
                                  static_cast<float>(UiController::BPM_MAX - UiController::BPM_MIN));
        Canvas canvas;

        for (bool running = true; running;) {
            for (SDL_Event e; SDL_PollEvent(&e);) {
                if (e.type == SDL_QUIT) running = false;
                if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) running = false;
                screen_window.process(e);
                controls_window.process(e);
            }

            // Buffers de samples que le moteur n'utilise plus.
            for (const int16_t* p; engine.pop_released(p);) bank.release(p);

            // 1. Les contrôles produisent des événements...
            controls_window.begin_frame("controles");
            hardware.keyboard(ui);
            hardware.draw(ui, engine.state());
            controls_window.end_frame();

            // 2. ...l'UI se met à jour et dessine...
            ui.update(engine.state(), SDL_GetTicks());
            ui.render(canvas);

            // 3. ...et l'écran affiche le résultat.
            screen_window.begin_frame("ecran");
            screen.draw(canvas);
            screen_window.end_frame();
        }
    }

    SDL_Quit();
    audio.stop();
    return 0;
}
