#include "sample_bank.h"

#include "sample_loader.h"

bool SampleBank::load(const char* path, const int16_t*& data, uint32_t& length, std::string& error) {
    auto buf = std::make_unique<std::vector<int16_t>>();
    if (!load_wav_mono16(path, *buf, error)) return false;
    if (buf->empty()) {
        error = "fichier sans données audio";
        return false;
    }

    data   = buf->data();
    length = static_cast<uint32_t>(buf->size());
    buffers_.push_back(std::move(buf));
    return true;
}

void SampleBank::release(const int16_t* data) {
    for (auto it = buffers_.begin(); it != buffers_.end(); ++it) {
        if ((*it)->data() == data) {
            buffers_.erase(it);
            return;
        }
    }
}
