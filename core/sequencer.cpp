#include "sequencer.h"

#include <cmath>

void Sequencer::set_bpm(float bpm) {
    if (bpm < 20.0f) bpm = 20.0f;
    if (bpm > 300.0f) bpm = 300.0f;
    bpm_ = bpm;  // pris en compte à partir du prochain pas
}

void Sequencer::play() {
    playing_   = true;
    step_      = 0;
    countdown_ = 0.0;
}

void Sequencer::stop() {
    playing_ = false;
}

int32_t Sequencer::frames_until_due() const {
    if (!playing_) return INT32_MAX;
    if (countdown_ <= 0.0) return 0;
    return static_cast<int32_t>(std::ceil(countdown_));
}

int Sequencer::fire() {
    const int fired = step_;
    step_ = (step_ + 1) % cfg::MAX_STEPS;
    countdown_ += step_length();  // on garde le reste (négatif ou fractionnaire) : pas de dérive
    return fired;
}
