#pragma once

#include "core/Types.h"

#include <algorithm>
#include <cmath>

namespace guiro::dsp {

constexpr float kPi = 3.14159265358979f;

inline float dbToGain(float db) { return std::pow(10.f, db / 20.f); }

// Constant-power pan law (-3 dB at center): returns L/R gains for pan in [-1, 1].
inline void panGains(float pan, float& l, float& r)
{
    const float a = (pan + 1.f) * 0.25f * kPi;
    l = std::cos(a);
    r = std::sin(a);
}

// Zavalishin topology-preserving-transform state variable filter (stereo).
struct Svf {
    float ic1[2] = {0, 0};
    float ic2[2] = {0, 0};
    float a1 = 0, a2 = 0, a3 = 0, k = 2;
    float curCutoff = -1.f;
    float curRes = -1.f;

    void reset()
    {
        ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.f;
    }

    void setup(float cutoff, float res, float sr)
    {
        if (cutoff == curCutoff && res == curRes)
            return;
        curCutoff = cutoff;
        curRes = res;
        const float fc = std::clamp(cutoff, 10.f, sr * 0.49f);
        const float g = std::tan(kPi * fc / sr);
        k = 2.f - 1.96f * std::clamp(res, 0.f, 1.f);
        a1 = 1.f / (1.f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    inline float tick(int c, float v0, FilterType type)
    {
        const float v3 = v0 - ic2[c];
        const float v1 = a1 * ic1[c] + a2 * v3;
        const float v2 = ic2[c] + a2 * ic1[c] + a3 * v3;
        ic1[c] = 2.f * v1 - ic1[c];
        ic2[c] = 2.f * v2 - ic2[c];
        switch (type) {
        case FilterType::LowPass: return v2;
        case FilterType::HighPass: return v0 - k * v1 - v2;
        case FilterType::BandPass: return v1 * k;
        default: return v0;
        }
    }

    // Process a stereo block, gliding the cutoff (log domain) from `from` to `to`.
    void process(float* l, float* r, int n, FilterType type, float from, float to, float res, float sr)
    {
        if (type == FilterType::Off) {
            reset();
            curCutoff = to;
            return;
        }
        constexpr int kSeg = 16;
        const float lf = std::log(std::max(from, 10.f));
        const float lt = std::log(std::max(to, 10.f));
        for (int s = 0; s < n; s += kSeg) {
            const int e = std::min(n, s + kSeg);
            const float t = n > 0 ? float(e) / float(n) : 1.f;
            setup(std::exp(lf + (lt - lf) * t), res, sr);
            for (int i = s; i < e; ++i) {
                l[i] = tick(0, l[i], type);
                r[i] = tick(1, r[i], type);
            }
        }
        // Guard against denormals / blow-ups.
        for (int c = 0; c < 2; ++c) {
            if (!std::isfinite(ic1[c]) || !std::isfinite(ic2[c]))
                reset();
        }
    }
};

inline float peakOf(const float* b, int n)
{
    float p = 0.f;
    for (int i = 0; i < n; ++i)
        p = std::max(p, std::abs(b[i]));
    return p;
}

// Apply a gain ramp to a stereo block.
inline void applyGainRamp(float* l, float* r, int n, float l0, float r0, float l1, float r1)
{
    if (n <= 0)
        return;
    const float dl = (l1 - l0) / float(n);
    const float dr = (r1 - r0) / float(n);
    for (int i = 0; i < n; ++i) {
        l[i] *= l0 + dl * float(i + 1);
        r[i] *= r0 + dr * float(i + 1);
    }
}

} // namespace guiro::dsp
