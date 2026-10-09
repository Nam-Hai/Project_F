#include "mixer.h"

#include "dsp_utils.h"

namespace mixer {

void accumulate(float* dst, const float* src, int n, float gain) {
    for (int i = 0; i < n; ++i) dst[i] += src[i] * gain;
}

void master(float* buf, int n, float gain) {
    for (int i = 0; i < n; ++i) buf[i] = dsp::soft_clip(buf[i] * gain);
}

}  // namespace mixer
