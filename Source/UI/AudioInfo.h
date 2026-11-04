#pragma once

#include "../PluginProcessor.h"
#include "../LatencyProbe.h"

/** One line about the audio that is running: the driver, the sample rate, the buffer size and (standalone
    only) the round-trip latency, e.g. "Windows Audio - 48.0 kHz - 512 smp - about 21.3 ms" (with middle dots).
    Inside a DAW only the buffer size is known, so no millisecond figure is shown. */
namespace AudioInfo
{
    inline juce::String describe (TiniAudioProcessor& processor)
    {
        const double sr = processor.getSampleRate();
        const int block = processor.getBlockSize();

        const auto device = LatencyProbe::query();
        const double roundTrip = LatencyProbe::roundTripMs (device, processor.getLatencySamples());

        juce::StringArray parts;
        if (device.valid && device.driverName.isNotEmpty())
            parts.add (device.driverName);

        parts.add (juce::String (sr / 1000.0, 1) + " kHz");

        if (block > 0 && sr > 0.0)
            parts.add (juce::String (block) + " smp");

        if (roundTrip >= 0.0)
            parts.add (juce::String::charToString (0x2248) + " " + juce::String (roundTrip, 1) + " ms");

        return parts.joinIntoString (juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")));
    }
}
