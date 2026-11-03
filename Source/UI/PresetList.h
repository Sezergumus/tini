#pragma once

#include "../PluginProcessor.h"
#include "Theme.h"

//==============================================================================
/** One line of the preset list. User presets show a pencil (rename) and a bin (delete) on hover;
    both work in place: renaming turns the line into a text field, deleting asks "sure?" first. */
class PresetRow : public juce::Component
{
public:
    static constexpr int height = 38;
    enum class Mode { normal, renaming, confirmingDelete };

    std::function<void()> onPick;                                 // the line itself was clicked
    std::function<juce::Result (const juce::String&)> onRename;   // commit a new name
    std::function<juce::Result (const juce::String&)> checkName;  // live check while typing
    std::function<void()> onDeleteConfirmed;
    std::function<void (const juce::String&)> onHint;             // message for the list footer ("" = none)

    PresetRow (const juce::String& presetName, bool isFactory, bool isCurrent)
        : name (presetName), factory (isFactory), current (isCurrent)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);

        nameEditor.setFont (Theme::ui (13.0f, true));
        nameEditor.setIndents (8, 0);
        nameEditor.setJustification (juce::Justification::centredLeft);
        nameEditor.setInputRestrictions (PresetManager::maxNameLength);
        nameEditor.onTextChange = [this] { showCheck(); };
        nameEditor.onReturnKey  = [this] { commitRename(); };
        nameEditor.onEscapeKey  = [this] { cancel(); };
        nameEditor.onFocusLost  = [this] { if (mode == Mode::renaming) cancel(); };
        addChildComponent (nameEditor);

        confirmButton.setButtonText ("Delete");
        confirmButton.onClick = [this] { if (onDeleteConfirmed) onDeleteConfirmed(); };
        keepButton.setButtonText ("Keep");
        keepButton.onClick = [this] { cancel(); };
        addChildComponent (confirmButton);
        addChildComponent (keepButton);
    }

    const juce::String& getPresetName() const { return name; }
    bool isFactory() const { return factory; }
    Mode getMode() const { return mode; }

    //==========================================================================
    void beginRename()
    {
        if (factory)
            return;

        mode = Mode::renaming;
        nameEditor.setText (name, false);
        nameEditor.setVisible (true);
        nameEditor.grabKeyboardFocus();
        nameEditor.selectAll();
        repaint();
    }

    void setRenameText (const juce::String& text)
    {
        nameEditor.setText (text, false);
        showCheck();
    }

    /** Applies the typed name. Stays in rename mode (with a hint) if the name is not allowed. */
    juce::Result commitRename()
    {
        const auto text = nameEditor.getText().trim();

        if (text == name)
        {
            cancel();
            return juce::Result::ok();
        }

        auto result = onRename ? onRename (text) : juce::Result::fail ("Cannot rename");
        if (result.failed() && onHint)
            onHint (result.getErrorMessage());

        return result;
    }

    void beginDelete()
    {
        if (factory)
            return;

        mode = Mode::confirmingDelete;
        confirmButton.setVisible (true);
        keepButton.setVisible (true);
        repaint();
    }

    void confirmDelete()
    {
        if (mode == Mode::confirmingDelete && onDeleteConfirmed)
            onDeleteConfirmed();
    }

    void cancel()
    {
        mode = Mode::normal;
        nameEditor.setVisible (false);
        confirmButton.setVisible (false);
        keepButton.setVisible (false);
        if (onHint)
            onHint ({});
        repaint();
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (6.0f, 2.0f);

        if (mode == Mode::confirmingDelete)
        {
            g.setColour (Theme::danger.withAlpha (0.10f));
            g.fillRoundedRectangle (r, 7.0f);
            g.setColour (Theme::danger.withAlpha (0.45f));
            g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);

            g.setColour (Theme::textHi);
            g.setFont (Theme::ui (12.0f, true));
            g.drawText ("Delete \"" + name + "\"?", r.reduced (14.0f, 0.0f).withTrimmedRight (132.0f),
                        juce::Justification::centredLeft, true);
            return;
        }

        if (mode == Mode::renaming)
        {
            g.setColour (Theme::accent.withAlpha (0.07f));
            g.fillRoundedRectangle (r, 7.0f);
            return;
        }

        if (current || hover)
        {
            g.setColour (current ? Theme::accent.withAlpha (hover ? 0.18f : 0.12f) : juce::Colours::white.withAlpha (0.055f));
            g.fillRoundedRectangle (r, 7.0f);
        }

        if (current)
        {
            g.setColour (Theme::accent);
            g.fillRoundedRectangle (r.getX(), r.getY() + 9.0f, 3.0f, r.getHeight() - 18.0f, 1.5f);

            juce::Path tick;
            const float tx = r.getX() + 15.0f, ty = r.getCentreY();
            tick.startNewSubPath (tx, ty);
            tick.lineTo (tx + 3.0f, ty + 3.5f);
            tick.lineTo (tx + 9.0f, ty - 3.5f);
            g.strokePath (tick, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        auto text = r.withTrimmedLeft (34.0f).withTrimmedRight (hover && ! factory ? 70.0f : 14.0f);
        g.setColour (current ? Theme::accent.brighter (0.25f) : Theme::textHi);
        g.setFont (Theme::ui (13.0f, current));
        g.drawText (name, text, juce::Justification::centredLeft, true);

        if (hover && ! factory)
        {
            drawPencil (g, renameRect().getCentre(), hoverIcon == 1 ? Theme::accent : Theme::textMid);
            drawBin (g, deleteRect().getCentre(), hoverIcon == 2 ? Theme::danger : Theme::textMid);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 2);
        nameEditor.setBounds (r.reduced (6, 3));

        auto b = r.reduced (8, 0);
        confirmButton.setBounds (b.removeFromRight (66).withSizeKeepingCentre (66, 26));
        b.removeFromRight (6);
        keepButton.setBounds (b.removeFromRight (56).withSizeKeepingCentre (56, 26));
    }

    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hover = false; hoverIcon = 0; repaint(); }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int icon = iconAt (e.position);
        if (icon != hoverIcon)
        {
            hoverIcon = icon;
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (mode != Mode::normal || ! e.mouseWasClicked())
            return;

        switch (iconAt (e.position))
        {
            case 1:  beginRename(); break;
            case 2:  beginDelete(); break;
            default: if (onPick) onPick(); break;
        }
    }

private:
    juce::Rectangle<float> deleteRect() const
    {
        return juce::Rectangle<float> (26.0f, 26.0f).withCentre ({ (float) getWidth() - 6.0f - 8.0f - 13.0f, (float) getHeight() * 0.5f });
    }
    juce::Rectangle<float> renameRect() const { return deleteRect().translated (-28.0f, 0.0f); }

    int iconAt (juce::Point<float> p) const
    {
        if (factory || mode != Mode::normal)  return 0;
        if (deleteRect().contains (p))        return 2;
        if (renameRect().contains (p))        return 1;
        return 0;
    }

    void showCheck()
    {
        if (! checkName || ! onHint)
            return;

        const auto text = nameEditor.getText().trim();
        auto result = checkName (text);
        onHint (result.failed() && text != name && text.isNotEmpty() ? result.getErrorMessage() : juce::String());
    }

    static void drawPencil (juce::Graphics& g, juce::Point<float> c, juce::Colour colour)
    {
        juce::Path p;
        p.addRoundedRectangle (-1.7f, -6.5f, 3.4f, 10.0f, 0.7f);
        p.addTriangle (-1.7f, 4.0f, 1.7f, 4.0f, 0.0f, 7.0f);
        p.applyTransform (juce::AffineTransform::rotation (juce::degreesToRadians (40.0f)).translated (c.x, c.y));
        g.setColour (colour);
        g.fillPath (p);
    }

    static void drawBin (juce::Graphics& g, juce::Point<float> c, juce::Colour colour)
    {
        g.setColour (colour);
        g.fillRoundedRectangle (c.x - 5.5f, c.y - 4.8f, 11.0f, 1.8f, 0.9f);   // lid
        g.fillRoundedRectangle (c.x - 2.0f, c.y - 7.0f, 4.0f, 2.0f, 0.8f);    // handle

        juce::Path body;
        body.startNewSubPath (c.x - 4.2f, c.y - 1.8f);
        body.lineTo (c.x + 4.2f, c.y - 1.8f);
        body.lineTo (c.x + 3.4f, c.y + 6.5f);
        body.lineTo (c.x - 3.4f, c.y + 6.5f);
        body.closeSubPath();
        g.strokePath (body, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::String name;
    bool factory, current, hover = false;
    int hoverIcon = 0;   // 0 none, 1 pencil, 2 bin
    Mode mode = Mode::normal;

    juce::TextEditor nameEditor;
    juce::TextButton confirmButton, keepButton;
};

//==============================================================================
/** Contents of the call-out that opens from the preset name: a scrolling list in two sections. */
class PresetListPanel : public juce::Component
{
public:
    static constexpr int width = 344, maxHeight = 480;

    std::function<void (int)> onPicked;   // index in PresetManager
    std::function<void()> onChanged;      // something was renamed / deleted

    explicit PresetListPanel (NeuralFlowAmpAudioProcessor& p) : processor (p)
    {
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (8);
        addAndMakeVisible (viewport);

        rebuild();

        const int natural = padding + headerHeight + content.getHeight() + footerHeight + padding;
        setSize (width, juce::jmin (maxHeight, natural));

        // open with the current preset in view
        const int current = processor.getCurrentPresetIndex();
        if (current >= 0)
            viewport.setViewPosition (0, juce::jmax (0, rows[current]->getY() - 120));
    }

    PresetRow* findRow (const juce::String& name) const
    {
        for (auto* row : rows)
            if (row->getPresetName() == name)
                return row;
        return nullptr;
    }

    int getNumRows() const { return rows.size(); }
    const juce::String& getHint() const { return hint; }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().reduced (padding, 0);
        auto header = b.withTrimmedTop (padding).removeFromTop (headerHeight);

        g.setColour (Theme::textHi);
        g.setFont (Theme::ui (14.0f, true));
        g.drawText ("Presets", header.withTrimmedBottom (4), juce::Justification::centredLeft, false);

        const auto& pm = processor.presets;
        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (11.0f));
        g.drawText (juce::String (pm.getNumFactory()) + " factory" + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + juce::String (pm.size() - pm.getNumFactory()) + " user",
                    header.withTrimmedBottom (4), juce::Justification::centredRight, false);

        g.setColour (Theme::divider.withAlpha (0.10f));
        g.fillRect (b.getX(), header.getBottom() - 4, b.getWidth(), 1);

        auto footer = getLocalBounds().removeFromBottom (footerHeight + padding).reduced (padding + 6, 0).withHeight (footerHeight);
        footer.setY (getHeight() - padding - footerHeight);
        g.setFont (Theme::ui (11.0f));
        g.setColour (hint.isNotEmpty() ? Theme::danger : Theme::textLo);
        g.drawText ((hint.isNotEmpty() ? hint : juce::String ("Hover one of your presets to rename or delete it")),
                    footer, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (padding, 0);
        b.removeFromTop (padding + headerHeight);
        b.removeFromBottom (footerHeight + padding);
        viewport.setBounds (b);
        content.setSize (juce::jmax (1, viewport.getMaximumVisibleWidth()), content.getHeight());
        content.layout();
    }

    /** Rebuilds the lines from the processor's preset list, keeping the scroll position. */
    void rebuild()
    {
        const int scroll = viewport.getViewPositionY();
        const auto& pm = processor.presets;
        const int current = processor.getCurrentPresetIndex();

        rows.clear();
        hint = {};

        for (int i = 0; i < pm.size(); ++i)
        {
            const auto presetName = pm.get (i).name;
            auto* row = rows.add (new PresetRow (presetName, pm.get (i).factory, i == current));

            row->onPick = [this, presetName] { pick (presetName); };
            row->checkName = [this, presetName] (const juce::String& text)
            {
                return processor.presets.validateRename (processor.presets.indexOf (presetName), text);
            };
            row->onRename = [this, presetName] (const juce::String& text)
            {
                auto result = processor.renamePreset (processor.presets.indexOf (presetName), text);
                if (result.wasOk())
                    changedLater();
                return result;
            };
            row->onDeleteConfirmed = [this, presetName]
            {
                processor.deletePreset (processor.presets.indexOf (presetName));
                changedLater();
            };
            row->onHint = [this] (const juce::String& text) { setHint (text); };
            content.addAndMakeVisible (row);
        }

        content.setRows (rows, pm.getNumFactory());
        content.setSize (juce::jmax (1, viewport.getMaximumVisibleWidth()), content.getHeight());
        content.layout();

        viewport.setViewPosition (0, scroll);

        repaint();
    }

private:
    static constexpr int padding = 10, headerHeight = 34, footerHeight = 18;

    /** The scrolled surface: section titles and the rows. */
    class Content : public juce::Component
    {
    public:
        void setRows (const juce::OwnedArray<PresetRow>& newRows, int numFactoryRows)
        {
            rowList.clear();
            for (auto* r : newRows)
                rowList.add (r);
            numFactory = numFactoryRows;

            const int numUser = rowList.size() - numFactory;
            const int y = sectionHeight + numFactory * PresetRow::height
                        + sectionHeight + juce::jmax (1, numUser) * PresetRow::height;
            setSize (getWidth(), y + 6);
        }

        void layout()
        {
            int y = 0;
            factoryTitleY = y;
            y += sectionHeight;

            for (int i = 0; i < rowList.size(); ++i)
            {
                if (i == numFactory)
                {
                    userTitleY = y;
                    y += sectionHeight;
                }
                rowList[i]->setBounds (0, y, getWidth(), PresetRow::height);
                y += PresetRow::height;
            }

            if (rowList.size() == numFactory)
            {
                userTitleY = y;
                emptyY = y + sectionHeight;
            }
            else
            {
                emptyY = -1;
            }
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            auto title = [&] (const juce::String& text, int y)
            {
                const auto font = Theme::ui (11.0f);
                g.setColour (Theme::textLo);
                g.setFont (font);
                g.drawText (text, 16, y + 8, 120, sectionHeight - 8, juce::Justification::centredLeft, false);

                const float tw = font.getStringWidthFloat (text);
                g.setColour (Theme::divider.withAlpha (0.10f));
                g.fillRect (16.0f + tw + 10.0f, (float) y + (float) sectionHeight * 0.5f + 4.0f, (float) getWidth() - tw - 40.0f, 1.0f);
            };

            title ("Factory", factoryTitleY);
            title ("User", userTitleY);

            if (emptyY >= 0)
            {
                g.setColour (Theme::textLo);
                g.setFont (Theme::ui (11.5f));
                g.drawText ("Nothing saved yet - press Save to keep your setup", 16, emptyY, getWidth() - 32, PresetRow::height,
                            juce::Justification::centredLeft, true);
            }
        }

    private:
        static constexpr int sectionHeight = 28;
        juce::Array<PresetRow*> rowList;
        int numFactory = 0, factoryTitleY = 0, userTitleY = 0, emptyY = -1;
    };

    void pick (const juce::String& presetName)
    {
        const int index = processor.presets.indexOf (presetName);
        if (index < 0)
            return;

        if (onPicked)
            onPicked (index);

        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    }

    /** The row that triggered the change is destroyed by the rebuild, so do it after its handler returned. */
    void changedLater()
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PresetListPanel> (this)]
        {
            if (safe == nullptr)
                return;

            safe->rebuild();
            if (safe->onChanged)
                safe->onChanged();
        });
    }

    void setHint (const juce::String& text)
    {
        if (text == hint)
            return;

        hint = text;
        repaint();
    }

    NeuralFlowAmpAudioProcessor& processor;
    juce::Viewport viewport;
    Content content;
    juce::OwnedArray<PresetRow> rows;
    juce::String hint;
};
