#pragma once

#include "ModelList.h"
#include "Theme.h"

//==============================================================================
/** A small rounded button of the filter bar: a facet ("Make", "Make . 2" while two makes are wanted), the
    order ("Sort . Name") or a switch ("In use"). Lit in the accent colour while it narrows the list. */
class FilterButton : public juce::Button
{
public:
    explicit FilterButton (const juce::String& text, bool withChevron = true)
        : juce::Button (text), chevron (withChevron), caption (text)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setCaption (const juce::String& text, int count, bool isActive)
    {
        const auto sep = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));
        const auto shown = count > 0 ? text + sep + juce::String (count) : text;

        if (shown != caption || isActive != active)
        {
            caption = shown;
            active = isActive;
            repaint();
        }
    }

    int getIdealWidth() const
    {
        return (int) std::ceil (Theme::ui (12.0f, true).getStringWidthFloat (caption)) + 24 + (chevron ? 12 : 0);
    }

    bool isActive() const { return active; }
    const juce::String& getCaption() const { return caption; }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);

        g.setColour (active ? Theme::accent.withAlpha (down ? 0.26f : highlighted ? 0.21f : 0.16f)
                            : juce::Colours::white.withAlpha (down ? 0.14f : highlighted ? 0.10f : 0.06f));
        g.fillRoundedRectangle (r, 6.0f);

        if (active)
        {
            g.setColour (Theme::accent.withAlpha (0.5f));
            g.drawRoundedRectangle (r, 6.0f, 1.0f);
        }

        const auto colour = active ? Theme::accent : highlighted ? Theme::textHi : Theme::textMid;
        auto text = r.reduced (10.0f, 0.0f);

        if (chevron)
        {
            const auto c = juce::Point<float> (text.getRight() - 4.0f, r.getCentreY());
            juce::Path arrow;
            arrow.startNewSubPath (c.x - 3.5f, c.y - 1.5f);
            arrow.lineTo (c.x, c.y + 2.0f);
            arrow.lineTo (c.x + 3.5f, c.y - 1.5f);
            g.setColour (colour);
            g.strokePath (arrow, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            text.removeFromRight (12.0f);
        }

        g.setColour (colour);
        g.setFont (Theme::ui (12.0f, true));
        g.drawText (caption, text, juce::Justification::centred, false);
    }

private:
    bool chevron;
    bool active = false;
    juce::String caption;
};

//==============================================================================
/** What a facet button opens: the choices with a tick box and the number of files, in a scrolling list.
    A click ticks or un-ticks a choice at once (the panel stays open, so several can be ticked); the
    footer shows how many are ticked and clears them. */
class FilterOptionsPanel : public juce::Component, private juce::ListBoxModel
{
public:
    static constexpr int width = 260, rowHeight = 28, footerHeight = 40, maxRows = 9, listPadding = 4;

    FilterOptionsPanel (ModelList& l, LibraryFilter::Facet f)
        : list (&l), facet (f), options (l.getFacetOptions (f))
    {
        box.setModel (this);
        box.setRowHeight (rowHeight);
        box.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        box.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        box.getVerticalScrollBar().setAutoHide (true);
        addAndMakeVisible (box);

        clearButton.setButtonText ("Clear");
        clearButton.onClick = [this]
        {
            if (list != nullptr)
                list->setFilter (facet, {});
            changed();
        };
        addAndMakeVisible (clearButton);
        addMouseListener (this, true);   // the hover highlight follows the mouse over the rows

        const int rows = juce::jlimit (1, maxRows, (int) options.size());
        setSize (width, rows * rowHeight + 2 * listPadding + footerHeight);
        changed();
    }

    ~FilterOptionsPanel() override { box.setModel (nullptr); }

    int getNumOptions() const { return (int) options.size(); }
    const LibraryFilter::Option& getOption (int row) const { return options[(size_t) row]; }

    /** A click on a choice (the tests call it directly). */
    void toggleRow (int row)
    {
        if (list == nullptr || ! juce::isPositiveAndBelow (row, (int) options.size()))
            return;

        list->toggleFilter (facet, options[(size_t) row].key);
        changed();
    }

    void resized() override
    {
        auto b = getLocalBounds();
        auto footer = b.removeFromBottom (footerHeight);
        box.setBounds (b.reduced (4, listPadding));
        clearButton.setBounds (footer.removeFromRight (64 + 12).reduced (0, 7).withTrimmedRight (12));
    }

    void paint (juce::Graphics& g) override
    {
        auto footer = getLocalBounds().removeFromBottom (footerHeight);
        g.setColour (Theme::divider);
        g.fillRect (footer.removeFromTop (1));

        const int n = list != nullptr ? list->getFilter().count (facet) : 0;
        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (12.0f));
        g.drawText (options.empty() ? juce::String ("Nothing to choose from")
                                    : n > 0 ? juce::String (n) + " selected" : juce::String ("Pick one or more"),
                    footer.reduced (12, 0), juce::Justification::centredLeft, false);
    }

private:
    int getNumRows() override { return (int) options.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
    {
        if (! juce::isPositiveAndBelow (row, (int) options.size()) || list == nullptr)
            return;

        const auto& option = options[(size_t) row];
        const bool on = list->getFilter().has (facet, option.key);

        if (row == hoverRow)
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 1.0f, (float) w, (float) h - 2.0f), 6.0f);
        }

        const auto tick = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ 18.0f, (float) h * 0.5f });
        if (on)
        {
            g.setColour (Theme::accent);
            g.fillRoundedRectangle (tick, 3.5f);

            juce::Path check;
            check.startNewSubPath (tick.getX() + 3.5f, tick.getCentreY());
            check.lineTo (tick.getX() + 6.0f, tick.getCentreY() + 2.8f);
            check.lineTo (tick.getRight() - 3.2f, tick.getCentreY() - 2.8f);
            g.setColour (Theme::bg);
            g.strokePath (check, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            g.setColour (Theme::textLo);
            g.drawRoundedRectangle (tick.reduced (0.5f), 3.5f, 1.0f);
        }

        auto text = juce::Rectangle<float> (34.0f, 0.0f, (float) w - 34.0f - 10.0f, (float) h);
        auto countArea = text.removeFromRight (36.0f);

        g.setColour (on ? Theme::textHi : Theme::textMid);
        g.setFont (Theme::ui (13.0f, on));
        g.drawText (option.label, text, juce::Justification::centredLeft, true);

        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (11.5f));
        g.drawText (juce::String (option.count), countArea, juce::Justification::centredRight, false);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        toggleRow (row);
        box.deselectAllRows();
    }

    void backgroundClicked (const juce::MouseEvent&) override {}

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto local = e.getEventRelativeTo (&box);
        const int row = box.getRowContainingPosition (local.x, local.y);

        if (row != hoverRow)
        {
            hoverRow = row;
            box.repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hoverRow != -1)
        {
            hoverRow = -1;
            box.repaint();
        }
    }

    void changed()
    {
        clearButton.setEnabled (list != nullptr && list->getFilter().count (facet) > 0);
        box.repaint();
        repaint();
    }

    juce::Component::SafePointer<ModelList> list;
    LibraryFilter::Facet facet;
    std::vector<LibraryFilter::Option> options;
    juce::ListBox box;
    juce::TextButton clearButton;
    int hoverRow = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterOptionsPanel)
};

//==============================================================================
/** The row of filters under the header of a file list: one button per facet of the list's kind (each opens
    its choices), the order, optionally an "In use" switch, and Clear while something is filtered.

    The bar follows the list: when the list changes its kind (a section of the Library) the buttons are
    rebuilt for the new facets. */
class FilterBar : public juce::Component
{
public:
    static constexpr int height = 36, buttonHeight = 28;

    FilterBar (ModelList& modelList, bool offerInUse)
        : list (modelList), inUseOffered (offerInUse), sortButton ("Sort"), inUseButton ("In use", false)
    {
        sortButton.onClick = [this] { showSortMenu(); };
        addAndMakeVisible (sortButton);

        inUseButton.setTooltip ("Only the files that are loaded right now");
        inUseButton.onClick = [this] { list.setOnlyInUse (! list.getFilter().onlyInUse); };
        addChildComponent (inUseButton);
        inUseButton.setVisible (inUseOffered);

        clearButton.setButtonText ("Clear");
        clearButton.setTooltip ("Remove all filters");
        clearButton.onClick = [this] { list.clearFilters(); };
        addChildComponent (clearButton);

        list.onFiltersChanged = [this] { refresh(); };
        refresh();
    }

    ~FilterBar() override { list.onFiltersChanged = nullptr; }

    /** Brings the buttons in line with the list (its facets, what is wanted, the order). */
    void refresh()
    {
        bool layoutChanged = false;
        const auto facets = list.facets();

        if (facets != shown)
        {
            shown = facets;
            buttons.clear();

            for (auto facet : shown)
            {
                auto* button = buttons.add (new FilterButton (LibraryFilter::facetName (facet)));
                button->onClick = [this, facet, button] { showOptions (facet, *button); };
                addAndMakeVisible (button);
            }

            layoutChanged = true;
        }

        const auto& state = list.getFilter();

        for (int i = 0; i < buttons.size(); ++i)
        {
            const int n = state.count (shown[(size_t) i]);
            const auto before = buttons[i]->getIdealWidth();
            buttons[i]->setCaption (LibraryFilter::facetName (shown[(size_t) i]), n, n > 0);
            layoutChanged = layoutChanged || before != buttons[i]->getIdealWidth();
        }

        {
            const auto before = sortButton.getIdealWidth();
            sortButton.setCaption ("Sort" + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + LibraryFilter::sortName (state.sort), 0, false);
            layoutChanged = layoutChanged || before != sortButton.getIdealWidth();
        }

        inUseButton.setCaption ("In use", 0, state.onlyInUse);

        const bool wantClear = state.isActive();
        if (wantClear != clearButton.isVisible())
        {
            clearButton.setVisible (wantClear);
            layoutChanged = true;
        }

        if (layoutChanged)
            resized();
    }

    void resized() override
    {
        auto row = getLocalBounds().withSizeKeepingCentre (getWidth(), buttonHeight);
        const int gap = 6;

        for (auto* button : buttons)
        {
            button->setBounds (row.removeFromLeft (button->getIdealWidth()));
            row.removeFromLeft (gap);
        }

        row.removeFromLeft (6);   // a little air before the order and the switches
        sortButton.setBounds (row.removeFromLeft (sortButton.getIdealWidth()));
        row.removeFromLeft (gap);

        if (inUseOffered)
        {
            inUseButton.setBounds (row.removeFromLeft (inUseButton.getIdealWidth()));
            row.removeFromLeft (gap);
        }

        clearButton.setBounds (row.removeFromLeft (56));
    }

    // exposed so tests can drive them
    const juce::OwnedArray<FilterButton>& getButtons() const { return buttons; }
    FilterButton& getSortButton() { return sortButton; }
    FilterButton& getInUseButton() { return inUseButton; }
    juce::TextButton& getClearButton() { return clearButton; }

    /** Opens the choices of a facet below its button. */
    void showOptions (LibraryFilter::Facet facet, FilterButton& button)
    {
        auto panel = std::make_unique<FilterOptionsPanel> (list, facet);
        launch (std::move (panel), button);
    }

    /** Opens the list of orders below the sort button. */
    void showSortMenu()
    {
        juce::PopupMenu menu;
        const auto orders = list.sorts();

        for (int i = 0; i < (int) orders.size(); ++i)
            menu.addItem (i + 1, LibraryFilter::sortName (orders[(size_t) i]), true, orders[(size_t) i] == list.getFilter().sort);

        Theme::styleMenu (menu, *this);
        menu.showMenuAsync (Theme::menuBelow (sortButton),
                            [safe = juce::Component::SafePointer<FilterBar> (this), orders] (int choice)
        {
            if (safe != nullptr && choice > 0)
                safe->list.setSort (orders[(size_t) choice - 1]);
        });
    }

private:
    /** The call-out lives in the scaled view (the plugin scales its whole UI with a transform), not in the
        window: it grows and shrinks with the rest and keeps the look of the app. That view is the highest
        ancestor that still uses the same look and feel as this bar. */
    void launch (std::unique_ptr<juce::Component> panel, FilterButton& button)
    {
        juce::Component* host = nullptr;
        for (auto* c = getParentComponent(); c != nullptr && &c->getLookAndFeel() == &getLookAndFeel(); c = c->getParentComponent())
            host = c;

        if (host == nullptr)
            host = getTopLevelComponent();

        if (host == nullptr)
            return;

        auto& box = juce::CallOutBox::launchAsynchronously (std::move (panel), host->getLocalArea (this, button.getBounds()), host);
        box.setArrowSize (10.0f);
    }

    ModelList& list;
    bool inUseOffered;
    std::vector<LibraryFilter::Facet> shown;
    juce::OwnedArray<FilterButton> buttons;
    FilterButton sortButton, inUseButton;
    juce::TextButton clearButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterBar)
};
