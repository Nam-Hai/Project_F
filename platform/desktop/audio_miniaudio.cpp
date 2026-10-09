#define MINIAUDIO_IMPLEMENTATION
#include "../../third_party/miniaudio.h"

#include "audio_miniaudio.h"

#include <cstdio>

#include "../../config.h"

struct DesktopAudio::Impl {
    ma_device  device;
    bool       running = false;
    RenderFn   fn      = nullptr;
    void*      user    = nullptr;
    float      left[cfg::BLOCK];
    float      right[cfg::BLOCK];
};

static void data_callback(ma_device* device, void* output, const void*, ma_uint32 frame_count) {
    auto* impl = static_cast<DesktopAudio::Impl*>(device->pUserData);
    auto* out  = static_cast<float*>(output);

    // miniaudio fournit des tailles variables : on découpe en blocs de cfg::BLOCK.
    for (ma_uint32 done = 0; done < frame_count;) {
        const int n = (frame_count - done < static_cast<ma_uint32>(cfg::BLOCK))
                          ? static_cast<int>(frame_count - done)
                          : cfg::BLOCK;

        impl->fn(impl->left, impl->right, n, impl->user);

        for (int i = 0; i < n; ++i) {
            out[(done + i) * 2]     = impl->left[i];
            out[(done + i) * 2 + 1] = impl->right[i];
        }
        done += n;
    }
}

DesktopAudio::DesktopAudio() : impl_(new Impl) {}

DesktopAudio::~DesktopAudio() {
    stop();
    delete impl_;
}

bool DesktopAudio::start(RenderFn fn, void* user) {
    impl_->fn   = fn;
    impl_->user = user;

    ma_device_config config         = ma_device_config_init(ma_device_type_playback);
    config.playback.format          = ma_format_f32;
    config.playback.channels        = 2;
    config.sampleRate               = cfg::SAMPLE_RATE;
    config.periodSizeInFrames       = cfg::BLOCK;
    config.dataCallback             = data_callback;
    config.pUserData                = impl_;

    if (ma_device_init(nullptr, &config, &impl_->device) != MA_SUCCESS) {
        std::fprintf(stderr, "miniaudio: échec d'ouverture du périphérique audio\n");
        return false;
    }
    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        std::fprintf(stderr, "miniaudio: échec du démarrage du périphérique\n");
        ma_device_uninit(&impl_->device);
        return false;
    }
    impl_->running = true;
    return true;
}

void DesktopAudio::stop() {
    if (!impl_->running) return;
    ma_device_uninit(&impl_->device);
    impl_->running = false;
}
