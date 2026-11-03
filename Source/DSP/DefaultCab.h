#pragma once

#include <juce_dsp/juce_dsp.h>

//==============================================================================
/** The cabinet that is used while the user has not loaded an impulse response of their own.

    Without any IR the amp would go out unfiltered (fizzy and harsh), so a plain guitar-cab shaped
    response is generated in code: low cut around 80 Hz, a body bump near 110 Hz, a presence bump near
    2.5 kHz and a steep roll-off above 5 kHz. It is built by sending an impulse through a few biquads,
    so there is no asset file and no licence to worry about. The buffer is mono at 48 kHz; the
    convolution engine converts it to the host rate. */
namespace DefaultCab
{
    constexpr double sampleRate = 48000.0;
    constexpr int numSamples = 2048;

    inline juce::AudioBuffer<float> makeBuffer()
    {
        using Coeffs = juce::dsp::IIR::Coefficients<float>;
        using Filter = juce::dsp::IIR::Filter<float>;

        std::array<Filter, 7> chain;
        chain[0].coefficients = Coeffs::makeHighPass (sampleRate, 80.0f, 0.707f);
        chain[1].coefficients = Coeffs::makePeakFilter (sampleRate, 110.0f, 1.0f, juce::Decibels::decibelsToGain (3.0f));
        chain[2].coefficients = Coeffs::makePeakFilter (sampleRate, 400.0f, 0.8f, juce::Decibels::decibelsToGain (-2.0f));
        chain[3].coefficients = Coeffs::makePeakFilter (sampleRate, 2500.0f, 1.2f, juce::Decibels::decibelsToGain (5.0f));
        chain[4].coefficients = Coeffs::makeLowPass (sampleRate, 5200.0f, 0.707f);
        chain[5].coefficients = Coeffs::makeLowPass (sampleRate, 5200.0f, 0.707f);
        chain[6].coefficients = Coeffs::makeLowPass (sampleRate, 7500.0f, 0.707f);

        juce::AudioBuffer<float> buffer (1, numSamples);
        auto* data = buffer.getWritePointer (0);

        for (int i = 0; i < numSamples; ++i)
        {
            float x = i == 0 ? 1.0f : 0.0f;
            for (auto& f : chain)
                x = f.processSample (x);
            data[i] = x;
        }

        // The tail has rung out long before the end; a short fade makes the cut inaudible.
        constexpr int fade = 512;
        for (int i = 0; i < fade; ++i)
            data[numSamples - fade + i] *= 1.0f - (float) i / (float) fade;

        return buffer;
    }
}
