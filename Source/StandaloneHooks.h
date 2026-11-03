#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <functional>

// What the UI may ask of the standalone application, without the shared plugin code having to know
// about the standalone wrapper. Source/StandaloneApp.cpp is compiled into the Standalone target only
// and fills this in; inside a DAW (VST3) it stays empty, so the UI shows no window buttons there and
// leaves the audio settings to the host. (Same idea as LatencyProbe.)
namespace StandaloneHooks
{
    struct Controller
    {
        std::function<void()> minimise;
        std::function<void()> toggleMaximise;   // fit the window to the screen / back to its old size
        std::function<void()> close;
        std::function<bool()> isMaximised;
        std::function<juce::AudioDeviceManager*()> deviceManager;

        bool available() const { return (bool) close; }
    };

    inline Controller& controller()
    {
        static Controller c;
        return c;
    }
}
