// Compiled into the Standalone target only (see CMakeLists.txt, which defines
// JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP for it). This is JUCE's own StandaloneFilterApp with one
// difference: its window has no title bar. The header of the UI is the title bar now (it drags the
// window and carries the minimise / fit-to-screen / close buttons, see StandaloneHooks.h).
//
// The window still derives from juce::StandaloneFilterWindow, so the plugin holder, the saved window
// position, the audio device handling and StandalonePluginHolder::getInstance() keep working.
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include "StandaloneHooks.h"

namespace
{
    class TiniWindow final : public juce::StandaloneFilterWindow
    {
    public:
        TiniWindow (const juce::String& title, juce::Colour background, juce::PropertySet* settings,
                    const juce::Array<PluginInOuts>& channels)
            : juce::StandaloneFilterWindow (title, background, settings, false, {}, nullptr, channels)
        {
            // JUCE's "Options" button lives in the title bar: audio settings are on the Settings page now.
            for (auto* child : getChildren())
                if (auto* button = dynamic_cast<juce::TextButton*> (child))
                    if (button->getButtonText() == "Options")
                        button->setVisible (false);

            // No title bar: the window keeps the size of its content, minus the bar that is gone.
            const int bar = getTitleBarHeight();
            setTitleBarHeight (0);
            setSize (getWidth(), getHeight() - bar);
        }

        /** Fits the window to the screen (as large as the locked aspect ratio allows) or puts it back. */
        void toggleFitToScreen()
        {
            if (fitted)
            {
                setBoundsConstrained (restoreBounds);
                fitted = false;
                return;
            }

            restoreBounds = getBounds();

            const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds());
            if (display == nullptr)
                return;

            const auto area = display->userArea;
            setBoundsConstrained (area);
            setCentrePosition (area.getCentre());
            fitted = true;
        }

        bool isFitted() const { return fitted; }

    private:
        juce::Rectangle<int> restoreBounds;
        bool fitted = false;
    };

    //==========================================================================
    class TiniApp final : public juce::JUCEApplication
    {
    public:
        TiniApp()
        {
            juce::PropertiesFile::Options options;

            options.applicationName     = appName;
            options.filenameSuffix      = ".settings";
            options.osxLibrarySubFolder = "Application Support";
           #if JUCE_LINUX || JUCE_BSD
            options.folderName          = "~/.config";
           #else
            options.folderName          = "";
           #endif

            appProperties.setStorageParameters (options);
        }

        const juce::String getApplicationName() override           { return appName; }
        const juce::String getApplicationVersion() override        { return JucePlugin_VersionString; }
        bool moreThanOneInstanceAllowed() override                 { return true; }
        void anotherInstanceStarted (const juce::String&) override {}

        void initialise (const juce::String&) override
        {
           #ifdef JucePlugin_PreferredChannelConfigurations
            juce::StandalonePluginHolder::PluginInOuts channels[] = { JucePlugin_PreferredChannelConfigurations };
            const juce::Array<juce::StandalonePluginHolder::PluginInOuts> config (channels, juce::numElementsInArray (channels));
           #else
            const juce::Array<juce::StandalonePluginHolder::PluginInOuts> config;
           #endif

            // The hooks come first: the UI looks at them while the window builds its content.
            auto& hooks = StandaloneHooks::controller();
            hooks.minimise       = [this] { if (mainWindow != nullptr) mainWindow->setMinimised (true); };
            hooks.toggleMaximise = [this] { if (mainWindow != nullptr) mainWindow->toggleFitToScreen(); };
            hooks.close          = [this] { systemRequestedQuit(); };
            hooks.isMaximised    = [this] { return mainWindow != nullptr && mainWindow->isFitted(); };
            hooks.deviceManager  = [this]() -> juce::AudioDeviceManager* { return mainWindow != nullptr ? &mainWindow->getDeviceManager() : nullptr; };

            mainWindow = std::make_unique<TiniWindow> (getApplicationName(), juce::Colour (0xff0f1113),
                                                       appProperties.getUserSettings(), config);

            mainWindow->setVisible (true);
        }

        void shutdown() override
        {
            StandaloneHooks::controller() = {};
            mainWindow = nullptr;
            appProperties.saveIfNeeded();
        }

        void systemRequestedQuit() override
        {
            if (mainWindow != nullptr)
                mainWindow->pluginHolder->savePluginState();

            if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
            {
                juce::Timer::callAfterDelay (100, []
                {
                    if (auto* app = juce::JUCEApplicationBase::getInstance())
                        app->systemRequestedQuit();
                });
            }
            else
            {
                quit();
            }
        }

    private:
        juce::ApplicationProperties appProperties;
        std::unique_ptr<TiniWindow> mainWindow;
        const juce::String appName { juce::CharPointer_UTF8 (JucePlugin_Name) };
    };
}

START_JUCE_APPLICATION (TiniApp)
