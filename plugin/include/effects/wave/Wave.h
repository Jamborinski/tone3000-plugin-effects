// Wave — test-tone generator.
//
// A source block: replaces the buffer content with a fixed-frequency tone
// (440 Hz) of a selectable shape. Not a processor-of-input: the dry copy
// made by the block's In/Out/Mix machinery is the incoming signal, and at
// full mix the output is purity of the generated wave, which is the point
// (a reference tone for testing).
//
// Zero-latency (running phase, no look-ahead), zero-allocation after
// prepare(), no state beyond the phase + the shape crossfade. Shape
// switches click-free via an 8 ms smoothstep crossfade between the old and
// new shape (same mechanism as the tremolo's LFO) — no clicks when the
// shape knob is turned at any rate.
//
// The five shapes are the shared test-tone set (index 0..4):
//   0 sine, 1 triangle, 2 saw (up-ramp), 3 saw (down-ramp), 4 square.
// Square/saw carry hard edges at the full-scale level; the crossfade is
// what keeps the transitions silent, not a slew on the waveform itself
// (a slew would round the very edges the user selected).

#pragma once

#include <algorithm>
#include <cmath>

#include "juce_audio_processors/juce_audio_processors.h"


class Wave {
public:
    // Bounds + the fixed tone frequency. Shared with the UI scales.
    static constexpr double kMinLevel = 0.0;
    static constexpr double kMaxLevel = 1.0;
    static constexpr double kFreqHz = 440.0;
    static constexpr int kMinShape = 0;
    static constexpr int kMaxShape = 4;
    static constexpr int kNumShapes = 5;

    struct Params {
        double level = 0.25;   // 0..1 output level (-12 dBFS; 0.5 read as "really loud")
        int shape = 0;         // kMinShape..kMaxShape (0 = sine)
    };

    void prepare(double sampleRate) {
        if (sampleRate <= 0.0)
            return;
        rate_ = static_cast<float>(sampleRate);
        // Idempotent: prepare() must NOT reset shape_ (the restore funnel
        // runs setParams THEN prepareBlockForChainRate; resetting here
        // would silently drop the restored shape back to sine) or
        // phase_/level_ (a re-prepare would bump the running tone).
        shapeCoef_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.008 * sampleRate)));
    }

    void setParams(const Params& p) {
        level_ = static_cast<float>(juce::jlimit(kMinLevel, kMaxLevel, p.level));
        const int s = static_cast<int>(juce::jlimit(kMinShape, kMaxShape, p.shape));
        if (s != shape_) {
            prevShape_ = shape_;
            shape_ = s;
            shapeMix_ = 0.0f;
        }
    }

    // The five test-tone waveforms on phase [0,1). Shared shape set with
    // the tremolo LFO (our own implementation in both places).
    static float shape(int index, float phase, float twoPi) {
        switch (index) {
        case 1:  // triangle
            return (phase < 0.25f) ? phase * 4.0f
                 : (phase < 0.75f) ? (2.0f - phase * 4.0f)
                                   : (phase * 4.0f - 4.0f);
        case 2:  // saw, up-ramp
            return phase * 2.0f - 1.0f;
        case 3:  // saw, down-ramp
            return 1.0f - 2.0f * phase;
        case 4:  // square
            return (phase < 0.5f) ? -1.0f : 1.0f;
        default: // sine
            return std::sin(twoPi * phase);
        }
    }

    void process(juce::AudioBuffer<float>& buffer) {
        const int n = buffer.getNumSamples();
        const float inc = static_cast<float>(kFreqHz / rate_);
        const float twoPi = static_cast<float>(2.0 * juce::MathConstants<double>::pi);
        const float gain = level_;

        for (int i = 0; i < n; ++i) {
            phase_ += inc;
            if (phase_ >= 1.0f)
                phase_ -= 1.0f;

            float w;
            if (shapeMix_ < 1.0f) {
                shapeMix_ += shapeCoef_;
                if (shapeMix_ > 1.0f)
                    shapeMix_ = 1.0f;
                const float m = shapeMix_ * shapeMix_ * (3.0f - 2.0f * shapeMix_);
                w = shape(prevShape_, phase_, twoPi)
                    + (shape(shape_, phase_, twoPi) - shape(prevShape_, phase_, twoPi)) * m;
            } else {
                w = shape(shape_, phase_, twoPi);
            }

            const float s = w * gain;
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                buffer.getWritePointer(c)[i] = s;
        }
    }

private:
    float rate_ = 48000.0f;
    float phase_ = 0.0f;
    float level_ = 0.5f;
    int shape_ = 0;
    int prevShape_ = 0;
    float shapeMix_ = 1.0f;
    float shapeCoef_ = 0.0f;
};
