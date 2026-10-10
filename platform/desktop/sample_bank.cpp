#include "sample_bank.h"

#include "sample_loader.h"

bool SampleBank::load(const char* path, const int16_t*& data, uint32_t& length, std::string& error) {
    auto buf = std::make_unique<Buffer>();
    if (!load_wav(path, buf->samples, buf->channels, error)) return false;
    if (buf->samples.empty()) {
        error = "fichier sans données audio";
        return false;
    }

    data   = buf->samples.data();
    length = static_cast<uint32_t>(buf->samples.size() / static_cast<std::size_t>(buf->channels));
    buffers_.push_back(std::move(buf));
    return true;
}

int SampleBank::channels(const int16_t* data) const {
    for (const auto& b : buffers_)
        if (b->samples.data() == data) return b->channels;
    return 1;
}

void SampleBank::release(const int16_t* data) {
    for (auto it = buffers_.begin(); it != buffers_.end(); ++it) {
        if ((*it)->samples.data() == data) {
            buffers_.erase(it);
            return;
        }
    }
}
