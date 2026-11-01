#pragma once

#include <JuceHeader.h>

/** Feedback delay with a smoothed delay time and a darkening low-pass in the feedback path,
    so repeats get softer the way analog / tape delays do. The dry signal is always passed at unity. */
class StereoDelay
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        maxDelaySamples = (float) (sampleRate * maxDelaySeconds);

        line.setMaximumDelayInSamples ((int) maxDelaySamples + 2);
        line.prepare (spec);
        line.reset();

        feedbackLowpass.assign (spec.numChannels, 0.0f);
        timeSmoothed.reset (sampleRate, 0.08);
        timeSmoothed.setCurrentAndTargetValue (timeMs * 0.001f * (float) sampleRate);
    }

    void reset()
    {
        line.reset();
        std::fill (feedbackLowpass.begin(), feedbackLowpass.end(), 0.0f);
    }

    void setParameters (float newTimeMs, float newFeedback, float newMix)
    {
        timeMs = newTimeMs;
        feedback = juce::jlimit (0.0f, 0.95f, newFeedback);
        mix = juce::jlimit (0.0f, 1.0f, newMix);
        timeSmoothed.setTargetValue (juce::jlimit (1.0f, maxDelaySamples, timeMs * 0.001f * (float) sampleRate));
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int numCh = juce::jmin (buffer.getNumChannels(), (int) feedbackLowpass.size());
        const int numSamples = buffer.getNumSamples();

        for (int i = 0; i < numSamples; ++i)
        {
            const float delaySamples = timeSmoothed.getNextValue();

            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* data = buffer.getWritePointer (ch);
                const float in = data[i];
                const float delayed = line.popSample (ch, delaySamples);

                auto& lp = feedbackLowpass[(size_t) ch];
                lp += 0.45f * (delayed - lp);

                line.pushSample (ch, in + lp * feedback);
                data[i] = in + delayed * mix;
            }
        }
    }

private:
    static constexpr double maxDelaySeconds = 1.1;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> line { 96000 };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> timeSmoothed;
    std::vector<float> feedbackLowpass;

    double sampleRate = 44100.0;
    float maxDelaySamples = 48000.0f;
    float timeMs = 380.0f, feedback = 0.35f, mix = 0.3f;
};
