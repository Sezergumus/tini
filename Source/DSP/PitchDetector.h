#pragma once

#include <JuceHeader.h>

/** Pitch detection for the tuner: the YIN algorithm (de Cheveigne and Kawahara, 2002) on a block of
    mono samples. It has no state and does not touch the audio thread: the UI feeds it the samples it
    reads from the processor's tuner FIFO.

    The block is first reduced to about 24 kHz (averaging neighbouring samples), which keeps the cost at
    roughly a million multiply-adds per detection while the range stays 30 Hz .. 1.5 kHz. */
class PitchDetector
{
public:
    struct Result
    {
        float frequency  = 0.0f;   // Hz; 0 = no clear pitch
        float confidence = 0.0f;   // 0..1, how periodic the signal is

        bool valid() const { return frequency > 0.0f; }
    };

    /** A frequency as the tuner shows it. */
    struct Note
    {
        juce::String name;   // "E", "F#", ...
        int octave = 0;
        float cents = 0.0f;  // -50..+50 away from the nearest note
        int midi = 0;
    };

    static constexpr float minHz = 30.0f;
    static constexpr float maxHz = 1500.0f;

    /** Number of input samples detect() wants for a sample rate (more is ignored: the newest are used). */
    static int samplesNeeded (double sampleRate) { return windowSize * decimationFor (sampleRate); }

    /** `samples` holds the newest mono input, oldest first. Returns an invalid result if there are too
        few samples, the signal is too quiet or it has no clear pitch. */
    Result detect (const float* samples, int numSamples, double sampleRate, float silenceThreshold = 0.003f)
    {
        const int factor = decimationFor (sampleRate);
        const int needed = windowSize * factor;
        if (numSamples < needed || sampleRate <= 0.0)
            return {};

        // reduce to ~24 kHz, taking the newest samples
        const float* src = samples + (numSamples - needed);
        float energy = 0.0f;
        for (int i = 0; i < windowSize; ++i)
        {
            float sum = 0.0f;
            for (int k = 0; k < factor; ++k)
                sum += src[i * factor + k];

            x[(size_t) i] = sum / (float) factor;
            energy += x[(size_t) i] * x[(size_t) i];
        }

        if (std::sqrt (energy / (float) windowSize) < silenceThreshold)
            return {};

        const double fs = sampleRate / factor;
        const int tauMin = juce::jmax (2, (int) (fs / maxHz));
        const int tauMax = juce::jmin (window - 1, (int) (fs / minHz));

        // difference function d(tau) and its cumulative mean normalisation d'(tau)
        float running = 0.0f;
        d[0] = 1.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float sum = 0.0f;
            for (int j = 0; j < window; ++j)
            {
                const float diff = x[(size_t) j] - x[(size_t) (j + tau)];
                sum += diff * diff;
            }

            running += sum;
            d[(size_t) tau] = running > 0.0f ? sum * (float) tau / running : 1.0f;
        }

        // first dip under the threshold, followed down to its local minimum
        int best = -1;
        for (int tau = tauMin; tau <= tauMax; ++tau)
        {
            if (d[(size_t) tau] < threshold)
            {
                while (tau + 1 <= tauMax && d[(size_t) tau + 1] < d[(size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        }

        if (best < 0)
            return {};

        // Refine at the full sample rate: look for the best period around the coarse one and interpolate
        // a parabola through its neighbours. (At 24 kHz a 1 kHz tone has a period of only 24 samples,
        // too coarse for a tuner.)
        const int half = needed / 2;
        const int centre = (int) std::lround ((double) best * factor);
        const int lo = juce::jmax (2, centre - factor - 2);
        const int hi = juce::jmin (needed - half - 1, centre + factor + 2);

        auto fullDifference = [&] (int tau)
        {
            float sum = 0.0f;
            for (int j = 0; j < half; ++j)
            {
                const float diff = src[j] - src[j + tau];
                sum += diff * diff;
            }
            return sum;
        };

        int bestFull = lo;
        float bestValue = fullDifference (lo);
        for (int tau = lo + 1; tau <= hi; ++tau)
        {
            const float v = fullDifference (tau);
            if (v < bestValue) { bestValue = v; bestFull = tau; }
        }

        double fullPeriod = (double) bestFull;
        if (bestFull > 2 && bestFull < needed - half - 1)
        {
            const float a = fullDifference (bestFull - 1), c = fullDifference (bestFull + 1);
            const float denom = a - 2.0f * bestValue + c;
            if (std::abs (denom) > 1.0e-12f)
                fullPeriod += juce::jlimit (-1.0f, 1.0f, 0.5f * (a - c) / denom);
        }

        Result r;
        r.frequency = (float) (sampleRate / fullPeriod);
        r.confidence = juce::jlimit (0.0f, 1.0f, 1.0f - d[(size_t) best]);
        return r;
    }

    /** Nearest note of `frequency` with A4 = `a4` Hz, and how many cents the frequency is off it. */
    static Note describe (float frequency, float a4 = 440.0f)
    {
        static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

        const double exact = 69.0 + 12.0 * std::log2 ((double) frequency / (double) a4);
        const int nearest = (int) std::lround (exact);

        Note n;
        n.midi = nearest;
        n.name = names[((nearest % 12) + 12) % 12];
        n.octave = (int) std::floor ((double) nearest / 12.0) - 1;
        n.cents = (float) (100.0 * (exact - (double) nearest));
        return n;
    }

private:
    static constexpr int windowSize = 2048;   // samples after the reduction
    static constexpr int window = windowSize / 2;
    static constexpr float threshold = 0.15f;

    static int decimationFor (double sampleRate) { return sampleRate >= 88200.0 ? 4 : sampleRate >= 44100.0 ? 2 : 1; }

    std::array<float, windowSize> x {};
    std::array<float, window> d {};
};
