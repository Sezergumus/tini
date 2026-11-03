#pragma once

#include "../PluginProcessor.h"
#include "Theme.h"
#include "PresetList.h"

//==============================================================================
/** Contents of the call-out that opens from SAVE: asks for a name and writes the preset. */
class SavePresetPanel : public juce::Component
{
public:
    static constexpr int width = 320, height = 146;

    /** Called with the preset's index after it was written. */
    std::function<void (int)> onSaved;

    SavePresetPanel (NeuralFlowAmpAudioProcessor& p, const juce::String& suggestedName)
        : processor (p)
    {
        nameEditor.setFont (Theme::ui (14.0f));
        nameEditor.setJustification (juce::Justification::centredLeft);
        nameEditor.setIndents (10, 0);
        nameEditor.setInputRestrictions (PresetManager::maxNameLength);
        nameEditor.setSelectAllWhenFocused (true);
        nameEditor.setText (suggestedName, false);
        nameEditor.onTextChange = [this] { updateState(); };
        nameEditor.onReturnKey  = [this] { save(); };
        nameEditor.onEscapeKey  = [this] { close(); };
        addAndMakeVisible (nameEditor);

        saveButton.setButtonText ("Save");
        saveButton.onClick = [this] { save(); };
        addAndMakeVisible (saveButton);

        cancelButton.setButtonText ("Cancel");
        cancelButton.onClick = [this] { close(); };
        addAndMakeVisible (cancelButton);

        setSize (width, height);
        updateState();
    }

    void setName (const juce::String& name)
    {
        nameEditor.setText (name, false);
        updateState();
    }

    bool isSaveEnabled() const { return saveButton.isEnabled(); }
    const juce::String& getHint() const { return hint; }

    /** Writes the preset if the name is allowed. Returns true if it was saved. */
    bool save()
    {
        updateState();   // the editor reports text changes asynchronously: never trust a stale state

        if (! saveButton.isEnabled())
            return false;

        const int index = processor.saveCurrentAsPreset (nameEditor.getText());
        if (index < 0)
        {
            hint = "Could not write the preset file";
            hintColour = Theme::danger;
            repaint();
            return false;
        }

        if (onSaved)
            onSaved (index);

        close();
        return true;
    }

    void focusEditor()
    {
        nameEditor.grabKeyboardFocus();
        nameEditor.selectAll();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().reduced (padding, 0);

        g.setColour (Theme::textHi);
        g.setFont (Theme::ui (14.0f, true));
        g.drawText ("Save preset", b.withTrimmedTop (padding).removeFromTop (18), juce::Justification::centredLeft, false);

        g.setColour (hintColour);
        g.setFont (Theme::ui (11.5f));
        g.drawText (hint, hintArea, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (padding, 0);
        b.removeFromTop (padding + 18 + 8);

        nameEditor.setBounds (b.removeFromTop (32));
        b.removeFromTop (6);
        hintArea = b.removeFromTop (16);
        b.removeFromTop (8);

        auto buttons = b.removeFromTop (30);
        saveButton.setBounds (buttons.removeFromRight (92));
        buttons.removeFromRight (8);
        cancelButton.setBounds (buttons.removeFromRight (84));
    }

private:
    static constexpr int padding = 14;

    void updateState()
    {
        const auto& presets = processor.presets;
        const auto name = nameEditor.getText();
        const auto valid = presets.validateName (name);
        const bool overwrite = valid.wasOk() && presets.wouldOverwrite (name);

        saveButton.setEnabled (valid.wasOk());
        saveButton.setButtonText (overwrite ? "Replace" : "Save");

        if (valid.failed())
        {
            hint = name.trim().isEmpty() ? "Type a name for this setup" : valid.getErrorMessage();
            hintColour = name.trim().isEmpty() ? Theme::textLo : Theme::danger;
        }
        else if (overwrite)
        {
            hint = "Replaces your preset with the same name";
            hintColour = Theme::textMid;
        }
        else
        {
            hint = "Saves knobs, pedals, chain order and IR";
            hintColour = Theme::textLo;
        }

        repaint();
    }

    void close()
    {
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    }

    NeuralFlowAmpAudioProcessor& processor;
    juce::TextEditor nameEditor;
    juce::TextButton saveButton, cancelButton;
    juce::String hint;
    juce::Colour hintColour = Theme::textLo;
    juce::Rectangle<int> hintArea;
};

//==============================================================================
/** Previous / next arrow. */
class StepButton : public juce::Button
{
public:
    explicit StepButton (bool pointsRight) : juce::Button (pointsRight ? "preset_next" : "preset_prev"), right (pointsRight)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);

        if (highlighted || down)
        {
            g.setColour (juce::Colours::white.withAlpha (down ? 0.12f : 0.07f));
            g.fillRoundedRectangle (r, 6.0f);
        }

        const auto c = r.getCentre();
        const float dx = right ? 2.5f : -2.5f;
        juce::Path chevron;
        chevron.startNewSubPath (c.x - dx, c.y - 5.0f);
        chevron.lineTo (c.x + dx, c.y);
        chevron.lineTo (c.x - dx, c.y + 5.0f);

        g.setColour (highlighted ? Theme::textHi : Theme::textMid);
        g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    bool right;
};

/** The preset name: plain text in the header. Click it to open the list; it lights up on hover. */
class PresetNameButton : public juce::Button
{
public:
    PresetNameButton() : juce::Button ("preset_name")
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setContent (const juce::String& newName, bool isEdited)
    {
        if (name == newName && edited == isEdited)
            return;

        name = newName;
        edited = isEdited;
        repaint();
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);

        if (highlighted || down)
        {
            g.setColour (juce::Colours::white.withAlpha (down ? 0.10f : 0.06f));
            g.fillRoundedRectangle (r, 6.0f);
        }

        g.setColour (Theme::textHi);
        g.setFont (Theme::ui (14.0f, true));
        g.drawText (name, r.reduced (10.0f, 0.0f), juce::Justification::centred, true);
    }

    const juce::String& getDisplayedName() const { return name; }
    bool isEdited() const { return edited; }

private:
    juce::String name;
    bool edited = false;
};

//==============================================================================
/** Header control for presets:  < name >  [Save].
    The arrows step through factory and user presets, the name opens the list. The Save button is only
    there while the preset was changed (or is still untitled), and says "Saved" for a moment afterwards. */
class PresetBar : public juce::Component
{
public:
    static constexpr int width = 460, height = 30;      // the arrows and the name sit in the middle,
    static constexpr int navWidth = 300;                // Save appears to the right of them

    explicit PresetBar (NeuralFlowAmpAudioProcessor& p)
        : processor (p), prevButton (false), nextButton (true)
    {
        prevButton.onClick = [this] { step (-1); };
        nextButton.onClick = [this] { step (+1); };
        nameButton.onClick = [this] { showList(); };

        saveButton.setButtonText ("Save");
        saveButton.onClick = [this] { showSaveDialog(); };

        for (auto* c : std::initializer_list<juce::Component*> { &prevButton, &nameButton, &nextButton })
            addAndMakeVisible (c);
        addChildComponent (saveButton);

        refresh();
    }

    /** Called from the UI timer: follows preset changes made elsewhere (host, session restore, edits). */
    void refresh()
    {
        const int index = processor.getCurrentPresetIndex();

        juce::String name = "Untitled";
        if (index >= 0)
            name = processor.presets.get (index).name;

        const bool modified = index < 0 || processor.isPresetModified();
        nameButton.setContent (name, index >= 0 && modified);

        if (savedFrames > 0)
        {
            --savedFrames;
            saveButton.setButtonText ("Saved");
            saveButton.setEnabled (false);
            saveButton.setVisible (true);
        }
        else
        {
            saveButton.setButtonText ("Save");
            saveButton.setEnabled (true);
            saveButton.setVisible (modified);
        }
    }

    /** Loads the preset `delta` places away (wrapping around). */
    void step (int delta)
    {
        const int n = processor.presets.size();
        if (n == 0)
            return;

        const int current = processor.getCurrentPresetIndex();
        const int target = current < 0 ? (delta > 0 ? 0 : n - 1)
                                       : (current + delta + n) % n;
        select (target);
    }

    void select (int index)
    {
        processor.loadPreset (index);
        refresh();
    }

    /** Opens the preset list below the name field. */
    void showList()
    {
        auto* parent = getParentComponent();
        if (parent == nullptr)
            return;

        auto panel = std::make_unique<PresetListPanel> (processor);
        panel->onPicked  = [safe = juce::Component::SafePointer<PresetBar> (this)] (int index) { if (safe != nullptr) safe->select (index); };
        panel->onChanged = [safe = juce::Component::SafePointer<PresetBar> (this)] { if (safe != nullptr) safe->refresh(); };

        auto& box = juce::CallOutBox::launchAsynchronously (std::move (panel),
                                                            parent->getLocalArea (this, nameButton.getBounds()),
                                                            parent);
        box.setArrowSize (10.0f);
    }

    /** Opens the "save preset" call-out below the SAVE button. */
    void showSaveDialog()
    {
        auto* parent = getParentComponent();
        if (parent == nullptr)
            return;

        // Suggest the current name for your own presets, "My <name>" for a factory one.
        juce::String suggestion = "My Preset";
        const int index = processor.getCurrentPresetIndex();
        if (index >= 0)
        {
            const auto& preset = processor.presets.get (index);
            suggestion = preset.factory ? "My " + preset.name : preset.name;
        }

        auto panel = std::make_unique<SavePresetPanel> (processor, suggestion);
        auto* panelPtr = panel.get();
        panel->onSaved = [safe = juce::Component::SafePointer<PresetBar> (this)] (int)
        {
            if (safe != nullptr)
            {
                safe->savedFrames = 90;   // ~1.5 s of "Saved"
                safe->refresh();
            }
        };

        auto& box = juce::CallOutBox::launchAsynchronously (std::move (panel),
                                                            parent->getLocalArea (this, saveButton.getBounds()),
                                                            parent);
        box.setArrowSize (10.0f);

        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SavePresetPanel> (panelPtr)]
        {
            if (safe != nullptr)
                safe->focusEditor();
        });
    }

    void resized() override
    {
        auto b = getLocalBounds();
        auto nav = b.withSizeKeepingCentre (navWidth, b.getHeight());

        saveButton.setBounds (juce::Rectangle<int> (64, b.getHeight()).withX (nav.getRight() + 10));

        prevButton.setBounds (nav.removeFromLeft (28));
        nav.removeFromLeft (6);
        nextButton.setBounds (nav.removeFromRight (28));
        nav.removeFromRight (6);
        nameButton.setBounds (nav);
    }

    PresetNameButton& getNameButton() { return nameButton; }
    juce::TextButton& getSaveButton() { return saveButton; }
    StepButton& getPrevButton() { return prevButton; }
    StepButton& getNextButton() { return nextButton; }

private:
    NeuralFlowAmpAudioProcessor& processor;
    StepButton prevButton, nextButton;
    PresetNameButton nameButton;
    juce::TextButton saveButton;
    int savedFrames = 0;
};
