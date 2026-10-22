// Volume — output gain.
//
// The simplest block in the suite: a single knob, ±24 dB, applied to every
// channel of the input. No state beyond the smoothed gain, zero-latency,
// zero-allocation after prepare(). The stored value is normalised 0..1 with
// 0.5 = 0 dB (the same mapping as the per-block In/Out gain knobs —
// scales::gainDb()), so the block field, the chain state, and the UI agree
// on one number.
//
// The gain steps are smoothed with the block's own audio-rate attack law
// (a fast one-pole, ~5 ms) so dragging the knob never introduces clicks of
// the gain itself — the tone/content passes through unchanged.

#pragma once

#include <cmath>

#include "juce_audio_processors/juce_audio_processors.h"

class Volume {
public:
    // The knob's real range, in dB. Shared with the UI scale (gainDb()).
    static constexpr double kMinDb = -24.0;
    static constexpr double kMaxDb = 24.0;

    struct Params {
        double gainDb = 0.0;  // kMinDb..kMaxDb
    };

    // Idempotent: a re-prepare must NOT reset the smoothed gain (the
    // restore funnel runs setParams THEN prepareBlockForChainRate, exactly
    // like the wave generator's).
    void prepare(double sampleRate) {
        if (sampleRate <= 0.0)
            return;
        rate_ = static_cast<float>(sampleRate);
        coef_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.005 * sampleRate)));
    }

    void setParams(const Params& p) {
        const double db = juce::jlimit(kMinDb, kMaxDb, p.gainDb);
        target_ = static_cast<float>(std::pow(10.0, db / 20.0));
    }

    void process(juce::AudioBuffer<float>& buffer) {
        // The (smoothed) gain is applied to EVERY sample of EVERY channel.
        // During a knob step g ramps (click-free); once settled g == target_
        // and the block is a plain, honest gain. There is no compounding in
        // the real audio path: each block of audio is fresh input, so it is
        // multiplied by the gain exactly once.
        float g = gain_;
        const int n = buffer.getNumSamples();
        for (int i = 0; i < n; ++i) {
            g += (target_ - g) * coef_;
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                buffer.getWritePointer(c)[i] *= g;
        }
        gain_ = g;
    }

private:
    float rate_ = 48000.0f;
    float coef_ = 0.0f;
    float target_ = 1.0f;
    float gain_ = 1.0f;
};
