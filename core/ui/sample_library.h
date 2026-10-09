#pragma once

#include <cstdint>

// Les samples disponibles pour créer une piste (disque sur PC, carte SD sur Teensy).
// Interface implémentée par la plateforme ; l'UI ne connaît ni fichiers ni formats.
class SampleLibrary {
public:
    virtual ~SampleLibrary() = default;

    virtual int count() const = 0;

    // Nom d'affichage de l'échantillon `index` (par exemple « Kick1.wav »).
    virtual const char* name(int index) const = 0;

    // Charge l'échantillon en mémoire (hors callback audio). Le buffer reste la propriété de la
    // bibliothèque jusqu'à ce que le moteur le rende (Engine::pop_released). false si échec.
    virtual bool load(int index, const int16_t*& data, uint32_t& length) = 0;

    // Libère un buffer chargé par load() mais finalement pas transmis au moteur.
    virtual void discard(const int16_t* data) = 0;
};
