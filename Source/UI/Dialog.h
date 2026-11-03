#pragma once

#include <JuceHeader.h>
#include "Theme.h"

//==============================================================================
/** The highest ancestor of `from` that still uses the same look and feel: the scaled view of the app. A
    dialog added there grows and shrinks with the rest of the UI and keeps its look (an operating system
    window would not). */
inline juce::Component* scaledHostOf (juce::Component* from)
{
    juce::Component* host = nullptr;

    for (auto* c = from; c != nullptr && &c->getLookAndFeel() == &from->getLookAndFeel(); c = c->getParentComponent())
        host = c;

    if (host == nullptr || host == from)
        host = from != nullptr ? from->getTopLevelComponent() : nullptr;

    return host;
}

//==============================================================================
/** A button of a dialog: the main action is filled with the accent (or the danger colour), the other one
    is the quiet grey of the rest of the app. */
class DialogButton : public juce::Button
{
public:
    enum class Style { quiet, primary, danger };

    DialogButton (const juce::String& text, Style s) : juce::Button (text), style (s)
    {
        setButtonText (text);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        const auto fill = style == Style::danger ? Theme::danger : Theme::accent;

        if (style == Style::quiet)
            g.setColour (juce::Colours::white.withAlpha (down ? 0.14f : highlighted ? 0.10f : 0.06f));
        else
            g.setColour (down ? fill.darker (0.2f) : highlighted ? fill.brighter (0.12f) : fill);

        g.fillRoundedRectangle (r, 6.0f);

        g.setColour (style == Style::quiet ? (highlighted ? Theme::textHi : Theme::textMid) : Theme::bg);
        g.setFont (Theme::ui (12.5f, true));
        g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, false);
    }

private:
    Style style;
};

//==============================================================================
/** A row of chips where one value can be picked (and un-picked): the gear type or the tone type of a
    model. A value the list does not know (a capture with an unusual type) gets a chip of its own. */
class ChipRow : public juce::Component
{
public:
    struct Choice { juce::String value, label; };

    static constexpr int chipHeight = 26, gap = 6;

    ChipRow (std::vector<Choice> choicesToShow, const juce::String& selectedValue)
        : choices (std::move (choicesToShow)), selected (selectedValue.trim().toLowerCase())
    {
        if (selected.isNotEmpty())
        {
            const bool known = std::any_of (choices.begin(), choices.end(), [this] (const Choice& c) { return c.value == selected; });
            if (! known)
                choices.push_back ({ selected, selected.replaceCharacter ('_', ' ') });
        }

        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    /** The picked value, or an empty string when nothing is picked. */
    const juce::String& getValue() const { return selected; }

    void setValue (const juce::String& value)
    {
        selected = value;
        repaint();
    }

    /** The height this row needs at `width`. */
    int heightFor (int width) const
    {
        const auto cells = layout (width);
        return cells.empty() ? chipHeight : cells.back().getBottom();
    }

    void paint (juce::Graphics& g) override
    {
        const auto cells = layout (getWidth());

        for (size_t i = 0; i < cells.size(); ++i)
        {
            const auto r = cells[i].toFloat().reduced (0.5f);
            const bool on = choices[i].value == selected;
            const bool hot = (int) i == hovered;

            g.setColour (on ? Theme::accent.withAlpha (0.16f) : juce::Colours::white.withAlpha (hot ? 0.10f : 0.06f));
            g.fillRoundedRectangle (r, 6.0f);

            if (on)
            {
                g.setColour (Theme::accent.withAlpha (0.5f));
                g.drawRoundedRectangle (r, 6.0f, 1.0f);
            }

            g.setColour (on ? Theme::accent : hot ? Theme::textHi : Theme::textMid);
            g.setFont (Theme::ui (12.0f, true));
            g.drawText (choices[i].label, r, juce::Justification::centred, false);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { setHovered (indexAt (e.getPosition())); }
    void mouseExit (const juce::MouseEvent&) override   { setHovered (-1); }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const int i = indexAt (e.getPosition());
        if (i < 0 || ! e.mouseWasClicked())
            return;

        pick (i);
    }

    /** A click on a chip (the tests call it directly): picks it, or un-picks it when it was picked. */
    void pick (int index)
    {
        if (! juce::isPositiveAndBelow (index, (int) choices.size()))
            return;

        selected = choices[(size_t) index].value == selected ? juce::String() : choices[(size_t) index].value;
        repaint();
    }

    int getNumChoices() const { return (int) choices.size(); }

private:
    std::vector<juce::Rectangle<int>> layout (int width) const
    {
        std::vector<juce::Rectangle<int>> cells;
        int x = 0, y = 0;

        for (const auto& c : choices)
        {
            const int w = (int) std::ceil (Theme::ui (12.0f, true).getStringWidthFloat (c.label)) + 22;

            if (x > 0 && x + w > width)
            {
                x = 0;
                y += chipHeight + gap;
            }

            cells.push_back ({ x, y, w, chipHeight });
            x += w + gap;
        }

        return cells;
    }

    int indexAt (juce::Point<int> p) const
    {
        const auto cells = layout (getWidth());
        for (size_t i = 0; i < cells.size(); ++i)
            if (cells[i].contains (p))
                return (int) i;
        return -1;
    }

    void setHovered (int i)
    {
        if (i != hovered)
        {
            hovered = i;
            repaint();
        }
    }

    std::vector<Choice> choices;
    juce::String selected;
    int hovered = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChipRow)
};

//==============================================================================
/** A small dialog in the look of the app, used for renaming, editing what a model says about itself and
    for confirmations. It covers the scaled view with a dimmed backdrop and shows one card with a title,
    an optional message, text fields and chip rows, and two buttons (the main action and Cancel).

    Enter does the main action, Escape cancels. The main action calls onSubmit: an ok closes the dialog,
    a failure shows its message inside the card and keeps the dialog open. */
class FormDialog : public juce::Component
{
public:
    static constexpr int cardWidth = 420, margin = 24, fieldHeight = 32, labelHeight = 18, buttonHeight = 34;

    FormDialog (juce::String titleText, juce::String messageText, const juce::String& okText,
                DialogButton::Style okStyle = DialogButton::Style::primary, bool withCancel = true)
        : title (std::move (titleText)), message (std::move (messageText)),
          okButton (okText, okStyle), cancelButton ("Cancel", DialogButton::Style::quiet)
    {
        okButton.onClick = [this] { submit(); };
        cancelButton.onClick = [this] { close(); };
        addAndMakeVisible (okButton);
        addChildComponent (cancelButton);
        cancelButton.setVisible (withCancel);
        setWantsKeyboardFocus (true);
    }

    //==========================================================================
    /** Called by the main action. An ok closes the dialog; a failure shows its message and keeps it open. */
    std::function<juce::Result (FormDialog&)> onSubmit;

    juce::TextEditor& addText (const juce::String& id, const juce::String& label, const juce::String& value,
                               const juce::String& hint = {})
    {
        auto& field = *fields.add (new Field());
        field.id = id;
        field.label = label;
        field.editor.setOpaque (false);   // rounded corners
        field.editor.setFont (Theme::ui (13.0f));
        field.editor.setIndents (10, 7);
        field.editor.setText (value, false);
        field.editor.setTextToShowWhenEmpty (hint, Theme::textLo);
        field.editor.setSelectAllWhenFocused (true);
        field.editor.onReturnKey = [this] { submit(); };
        field.editor.onEscapeKey = [this] { close(); };
        addAndMakeVisible (field.editor);
        return field.editor;
    }

    ChipRow& addChoices (const juce::String& id, const juce::String& label, std::vector<ChipRow::Choice> choices,
                         const juce::String& selected)
    {
        auto& field = *fields.add (new Field());
        field.id = id;
        field.label = label;
        field.chips = std::make_unique<ChipRow> (std::move (choices), selected);
        addAndMakeVisible (*field.chips);
        return *field.chips;
    }

    juce::String getText (const juce::String& id) const
    {
        for (auto* f : fields)
            if (f->id == id)
                return f->editor.getText();
        return {};
    }

    juce::String getChoice (const juce::String& id) const
    {
        for (auto* f : fields)
            if (f->id == id && f->chips != nullptr)
                return f->chips->getValue();
        return {};
    }

    juce::TextEditor* getEditor (const juce::String& id)
    {
        for (auto* f : fields)
            if (f->id == id && f->chips == nullptr)
                return &f->editor;
        return nullptr;
    }

    ChipRow* getChips (const juce::String& id)
    {
        for (auto* f : fields)
            if (f->id == id)
                return f->chips.get();
        return nullptr;
    }

    DialogButton& getOkButton()     { return okButton; }
    DialogButton& getCancelButton() { return cancelButton; }
    const juce::String& getError() const { return error; }

    void setError (const juce::String& text)
    {
        error = text;
        resized();
        repaint();
    }

    //==========================================================================
    /** Puts the dialog over the scaled view that contains `from` and takes the keyboard. The dialog owns
        itself from here on: it deletes itself when it is closed. */
    static FormDialog* show (std::unique_ptr<FormDialog> dialog, juce::Component* from)
    {
        auto* host = scaledHostOf (from);
        if (host == nullptr)
            return nullptr;

        auto* raw = dialog.release();
        host->addAndMakeVisible (raw);
        raw->setBounds (host->getLocalBounds());
        raw->toFront (true);

        if (! raw->fields.isEmpty() && raw->fields.getFirst()->chips == nullptr)
            raw->fields.getFirst()->editor.grabKeyboardFocus();
        else
            raw->grabKeyboardFocus();

        return raw;
    }

    /** Runs the main action: calls onSubmit and closes the dialog if it says ok. */
    void submit()
    {
        if (closing)
            return;

        const auto outcome = onSubmit ? onSubmit (*this) : juce::Result::ok();

        if (outcome.failed())
            setError (outcome.getErrorMessage());
        else
            close();
    }

    void close()
    {
        if (closing)
            return;

        closing = true;
        setVisible (false);
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<FormDialog> (this)] { delete safe.getComponent(); });
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.55f));

        const auto card = cardBounds();
        juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 24, { 0, 8 }).drawForRectangle (g, card);

        g.setColour (Theme::surface);
        g.fillRoundedRectangle (card.toFloat(), 12.0f);
        g.setColour (Theme::panelBorder.brighter (0.2f));
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 12.0f, 1.0f);

        auto inner = card.reduced (margin);
        g.setColour (Theme::textHi);
        g.setFont (Theme::ui (17.0f, true));
        g.drawText (title, inner.removeFromTop (26), juce::Justification::centredLeft, true);

        if (message.isNotEmpty())
        {
            inner.removeFromTop (6);
            g.setColour (Theme::textMid);
            messageLayout (inner.getWidth()).draw (g, inner.removeFromTop (messageHeight (inner.getWidth())).toFloat());
        }

        for (auto* f : fields)
        {
            g.setColour (Theme::textLo);
            g.setFont (Theme::ui (11.5f, true));
            g.drawText (f->label, f->labelBounds, juce::Justification::centredLeft, false);
        }

        if (error.isNotEmpty())
        {
            g.setColour (Theme::danger);
            g.setFont (Theme::ui (12.0f));
            g.drawFittedText (error, errorBounds, juce::Justification::centredLeft, 3);
        }
    }

    void resized() override
    {
        const auto card = cardBounds();
        auto inner = card.reduced (margin);

        inner.removeFromTop (26);
        if (message.isNotEmpty())
            inner.removeFromTop (6 + messageHeight (inner.getWidth()));

        if (! fields.isEmpty())
            inner.removeFromTop (14);

        for (auto* f : fields)
        {
            f->labelBounds = inner.removeFromTop (labelHeight);
            inner.removeFromTop (3);

            if (f->chips != nullptr)
                f->chips->setBounds (inner.removeFromTop (f->chips->heightFor (inner.getWidth())));
            else
                f->editor.setBounds (inner.removeFromTop (fieldHeight));

            inner.removeFromTop (12);
        }

        errorBounds = {};
        if (error.isNotEmpty())
        {
            errorBounds = inner.removeFromTop (errorHeight());
            inner.removeFromTop (6);
        }

        inner.removeFromTop (fields.isEmpty() && error.isEmpty() ? 18 : 6);
        auto buttons = inner.removeFromTop (buttonHeight);
        okButton.setBounds (buttons.removeFromRight (96));

        if (cancelButton.isVisible())
        {
            buttons.removeFromRight (8);
            cancelButton.setBounds (buttons.removeFromRight (84));
        }
    }

    void parentSizeChanged() override
    {
        if (auto* parent = getParentComponent())
            setBounds (parent->getLocalBounds());
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            close();
            return true;
        }

        if (key == juce::KeyPress::returnKey)
        {
            submit();
            return true;
        }

        return true;   // the dialog is modal: the keys do not reach the page behind it
    }

private:
    struct Field
    {
        Field()
        {
            editor.setSelectAllWhenFocused (true);
        }

        juce::String id, label;
        juce::TextEditor editor;
        std::unique_ptr<ChipRow> chips;
        juce::Rectangle<int> labelBounds;
    };

    juce::AttributedString attributed (const juce::String& text) const
    {
        juce::AttributedString s;
        s.append (text, Theme::ui (13.0f), Theme::textMid);
        s.setLineSpacing (2.0f);
        return s;
    }

    juce::TextLayout messageLayout (int width) const
    {
        juce::TextLayout layout;
        layout.createLayout (attributed (message), (float) width);
        return layout;
    }

    int messageHeight (int width) const { return (int) std::ceil (messageLayout (width).getHeight()) + 2; }

    int errorHeight() const
    {
        juce::TextLayout layout;
        auto s = attributed (error);
        layout.createLayout (s, (float) (cardWidth - 2 * margin));
        return juce::jlimit (18, 54, (int) std::ceil (layout.getHeight()));
    }

    int contentHeight() const
    {
        const int width = cardWidth - 2 * margin;
        int h = 26;

        if (message.isNotEmpty())
            h += 6 + messageHeight (width);

        if (! fields.isEmpty())
            h += 14;

        for (auto* f : fields)
            h += labelHeight + 3 + (f->chips != nullptr ? f->chips->heightFor (width) : fieldHeight) + 12;

        if (error.isNotEmpty())
            h += errorHeight() + 6;

        h += (fields.isEmpty() && error.isEmpty() ? 18 : 6) + buttonHeight;
        return h;
    }

    juce::Rectangle<int> cardBounds() const
    {
        const int w = juce::jmin (cardWidth, getWidth() - 40);
        return juce::Rectangle<int> (w, contentHeight() + 2 * margin).withCentre (getLocalBounds().getCentre());
    }

    juce::String title, message, error;
    juce::OwnedArray<Field> fields;
    DialogButton okButton, cancelButton;
    juce::Rectangle<int> errorBounds;
    bool closing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FormDialog)
};
