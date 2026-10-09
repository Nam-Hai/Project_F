#pragma once

// Sliders ImGui pilotables à la molette de la souris : à utiliser pour TOUS les sliders de
// l'émulateur (molette vers le haut = valeur qui augmente). Le survol suffit, pas de clic.
//
// La molette d'un trackpad envoie de petites valeurs fractionnaires : on les accumule par slider
// (dans le stockage d'état d'ImGui) pour ne perdre aucun mouvement.
#include <cmath>

#include "imgui.h"

// `step` = variation de la valeur par cran de molette.
inline bool SliderFloatWheel(const char* label, float* v, float v_min, float v_max, const char* format,
                             float step) {
    bool changed = ImGui::SliderFloat(label, v, v_min, v_max, format);

    if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
        const float next = *v + ImGui::GetIO().MouseWheel * step;
        const float clamped = next < v_min ? v_min : (next > v_max ? v_max : next);
        if (clamped != *v) {
            *v      = clamped;
            changed = true;
        }
    }
    return changed;
}

// `step` = variation (entière) de la valeur par cran de molette.
inline bool SliderIntWheel(const char* label, int* v, int v_min, int v_max, const char* format = "%d",
                           int step = 1) {
    bool changed = ImGui::SliderInt(label, v, v_min, v_max, format);

    float* acc = ImGui::GetStateStorage()->GetFloatRef(ImGui::GetItemID(), 0.0f);
    if (ImGui::IsItemHovered()) {
        *acc += ImGui::GetIO().MouseWheel;
        const int notches = static_cast<int>(*acc);  // crans entiers accumulés (vers zéro)
        if (notches != 0) {
            *acc -= static_cast<float>(notches);
            int next = *v + notches * step;
            if (next < v_min) next = v_min;
            if (next > v_max) next = v_max;
            if (next != *v) {
                *v      = next;
                changed = true;
            }
        }
    } else {
        *acc = 0.0f;
    }
    return changed;
}
