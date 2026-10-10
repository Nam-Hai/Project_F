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

    // Index de l'échantillon dont le nom de fichier est `file`, ou -1 s'il n'existe pas (plus).
    virtual int find(const char* file) const = 0;

    // Charge l'échantillon en mémoire (hors callback audio). Le buffer reste la propriété de la
    // bibliothèque jusqu'à ce que le moteur le rende (Engine::pop_released). false si échec.
    virtual bool load(int index, const int16_t*& data, uint32_t& length) = 0;

    // Nombre de canaux (1 mono, 2 stéréo entrelacé) d'un buffer chargé par load(). `length` de load() est
    // toujours un nombre d'images. Par défaut : mono.
    virtual int channels(const int16_t* data) const {
        (void)data;
        return 1;
    }

    // Libère un buffer chargé par load() mais finalement pas transmis au moteur.
    virtual void discard(const int16_t* data) = 0;

    // Taille de l'échantillon `index` une fois chargé (nombre d'images et de canaux), lue dans son en-tête sans charger
    // les données. false si elle ne peut pas être lue (la bibliothèque qui ne sait pas la donner garde ce défaut).
    virtual bool info(int index, uint32_t& frames, int& channels) const {
        (void)index;
        (void)frames;
        (void)channels;
        return false;
    }

    // Mémoire disponible pour les samples d'une loop, en octets (un échantillon de 16 bits en prend 2). 0 = pas de
    // limite connue : l'interface ne refuse alors aucune loop.
    virtual uint32_t memory_bytes() const { return 0; }
};
