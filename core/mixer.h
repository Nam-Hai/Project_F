#pragma once

namespace mixer {

// dst[i] += src[i] * gain
void accumulate(float* dst, const float* src, int n, float gain);

// buf[i] = tanh(buf[i] * gain) : gain master + soft clipping.
void master(float* buf, int n, float gain);

}  // namespace mixer
