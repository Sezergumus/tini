#pragma once

#include <juce_core/juce_core.h>
#include <functional>

// Lets the UI ask the audio device how much latency it really has, without the shared plugin
// code having to know about the standalone wrapper. Source/StandaloneBridge.cpp is compiled into
// the Standalone target only and registers the provider; in a DAW (VST3) none is registered and
// the UI falls back to showing the buffer size alone.
namespace LatencyProbe
{
    struct DeviceInfo
    {
        bool         valid = false;
        double       sampleRate = 0.0;
        int          bufferSamples = 0;
        int          inputLatencySamples = 0;    // as reported by the driver (includes its buffers)
        int          outputLatencySamples = 0;
        juce::String driverName;                  // "ASIO", "Windows Audio", "DirectSound", ...
    };

    using Provider = std::function<DeviceInfo()>;

    inline Provider& provider()
    {
        static Provider p;
        return p;
    }

    inline DeviceInfo query()
    {
        if (auto& p = provider())
            return p();
        return {};
    }

    // Estimated round trip (input -> processing -> output) in milliseconds, or < 0 if unknown.
    inline double roundTripMs (const DeviceInfo& d, int pluginLatencySamples)
    {
        if (! d.valid || d.sampleRate <= 0.0)
            return -1.0;

        const int total = d.inputLatencySamples + d.outputLatencySamples + pluginLatencySamples;
        return 1000.0 * (double) total / d.sampleRate;
    }
}
