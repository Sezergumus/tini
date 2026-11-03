// Compiled into the Standalone target only (see CMakeLists.txt). Needs JucePlugin_Build_Standalone,
// which JUCE defines for the format targets but not for the shared plugin code.
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include "LatencyProbe.h"

namespace
{
    LatencyProbe::DeviceInfo queryStandaloneDevice()
    {
        LatencyProbe::DeviceInfo info;

        if (auto* holder = juce::StandalonePluginHolder::getInstance())
        {
            if (auto* device = holder->deviceManager.getCurrentAudioDevice())
            {
                info.valid                = true;
                info.sampleRate           = device->getCurrentSampleRate();
                info.bufferSamples        = device->getCurrentBufferSizeSamples();
                info.inputLatencySamples  = device->getInputLatencyInSamples();
                info.outputLatencySamples = device->getOutputLatencyInSamples();
                info.driverName           = device->getTypeName();
            }
        }

        return info;
    }

    struct Registrar
    {
        Registrar() { LatencyProbe::provider() = queryStandaloneDevice; }
    } registrar;
}
