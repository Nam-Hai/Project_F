#pragma once

#include <cstdint>

// Action de l'utilisateur sur un contrôle. Produit par le panneau simulé sur PC et par les
// vrais boutons / roue / potard sur Teensy ; consommé par UiController.
struct InputEvent {
    enum Type : uint8_t {
        Wheel,   // delta : crans de rotation (+ = bas / suivant)
        Ok,      // bouton Valider
        Back,    // bouton Back
        Menu,    // bouton Menu : ouvre le menu contextuel de la vue courante
        Mode,    // bouton Mode : vue Pattern, passe au mode suivant (Edit, Velocity, Pitch)
        Play,    // bouton Play/Stop
        Step,    // index : bouton de pas 0..15
        PotBpm,  // value : position du potard, 0..1
    };

    Type    type  = Ok;
    int     delta = 0;
    uint8_t index = 0;
    float   value = 0.0f;

    static InputEvent wheel(int delta) { return {Wheel, delta, 0, 0.0f}; }
    static InputEvent ok() { return {Ok, 0, 0, 0.0f}; }
    static InputEvent back() { return {Back, 0, 0, 0.0f}; }
    static InputEvent menu() { return {Menu, 0, 0, 0.0f}; }
    static InputEvent mode() { return {Mode, 0, 0, 0.0f}; }
    static InputEvent play() { return {Play, 0, 0, 0.0f}; }
    static InputEvent step(int index) { return {Step, 0, static_cast<uint8_t>(index), 0.0f}; }
    static InputEvent pot_bpm(float value) { return {PotBpm, 0, 0, value}; }
};
