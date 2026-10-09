#pragma once

#include "../../core/command.h"
#include "../../core/ui/ui_controller.h"
#include "sample_bank.h"

// Envoie une commande en réessayant tant que la file est pleine (elle ne fait que 64 entrées).
// Le thread audio doit tourner, sinon l'appel ne se termine pas.
void send_command(CommandSink& sink, const Command& cmd);

// Charge la loop de default_pattern.h dans le moteur, par commandes (comme le fera l'UI) :
// BPM, puis pour chaque piste son sample, son pitch, son gain et son pattern.
// Si `ui` n'est pas nul, les pistes y sont aussi déclarées. Ne lance pas la lecture.
// Le thread audio doit déjà tourner. Retourne false (et affiche l'erreur) si un sample échoue.
bool load_default_loop(CommandSink& sink, SampleBank& bank, UiController* ui);
