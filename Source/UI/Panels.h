#pragma once

#include "../PluginProcessor.h"
#include "../PedalCatalog.h"
#include "Theme.h"
#include "Widgets.h"

//==============================================================================
/** The Cab is a block of the Rig like the amp and the pedals, but it is not part of the chain order:
    it always sits at the end. Its block id is the first number after the chain items. */
constexpr int cabBlock = PedalCatalog::numItems;

/** Colour of the page that belongs to a block: cyan for the amp and the cab, the soft colour of
    the category for a pedal. */
inline juce::Colour blockColour (int block)
{
    if (block == PedalCatalog::ampItem || block == cabBlock)
        return Theme::accent;

    return Theme::categoryColour (PedalCatalog::pedalOf (block).category);
}

//==============================================================================
/** A NAM pedal is named after its model: the file name without its extension. Without a model it is
    just "NAM Pedal". */
inline juce::String namPedalName (NeuralFlowAmpAudioProcessor& p, int copy)
{
    const auto file = p.pedalNam[(size_t) copy].getFile();
    return file == juce::File() ? juce::String ("NAM Pedal") : file.getFileNameWithoutExtension();
}

/** What the model of a NAM pedal says about itself (empty until the background read is done). */
inline NamMetadata::Info namPedalInfo (NeuralFlowAmpAudioProcessor& p, int copy)
{
    const auto file = p.pedalNam[(size_t) copy].getFile();
    return file.existsAsFile() ? p.metadata.get (file) : NamMetadata::Info();
}

//==============================================================================
/** Small "x" that removes a pedal from the chain. */
class RemoveButton : public juce::Button
{
public:
    RemoveButton() : juce::Button ("remove")
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Remove pedal");
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);

        if (highlighted || down)
        {
            g.setColour (Theme::danger.withAlpha (down ? 0.30f : 0.18f));
            g.fillEllipse (r);
        }

        auto c = r.getCentre();
        const float k = r.getWidth() * 0.22f;
        juce::Path x;
        x.startNewSubPath (c.x - k, c.y - k);  x.lineTo (c.x + k, c.y + k);
        x.startNewSubPath (c.x + k, c.y - k);  x.lineTo (c.x - k, c.y + k);

        g.setColour (highlighted ? Theme::danger : Theme::textMid);
        g.strokePath (x, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
};

//==============================================================================
/** The round dot of a tile. On a pedal it is the bypass switch (lit = active, hollow ring = bypassed);
    on the amp and the cab it only shows that the block is on. */
class TileDot : public juce::Button
{
public:
    TileDot() : juce::Button ("bypass") {}

    void setTint (juce::Colour c) { tint = c; }

    /** Pedals toggle through their attachment; the amp and the cab do not react to clicks. */
    void setSwitchable (bool canSwitch)
    {
        setClickingTogglesState (canSwitch);
        setMouseCursor (canSwitch ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::DraggingHandCursor);
        setInterceptsMouseClicks (canSwitch, false);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        const auto dotArea = juce::Rectangle<float> (10.0f, 10.0f).withCentre (getLocalBounds().toFloat().getCentre());
        const bool on = getToggleState();

        if (highlighted && getClickingTogglesState())
        {
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.fillEllipse (dotArea.expanded (6.0f));
        }

        if (on)
        {
            g.setColour (tint);
            g.fillEllipse (dotArea);
        }
        else
        {
            g.setColour (Theme::textLo);
            g.drawEllipse (dotArea.reduced (0.75f), 1.5f);
        }
    }

private:
    juce::Colour tint = Theme::accent;
};

//==============================================================================
/** One block in the chain strip: a pedal, the amp or the cab. It shows a dot in the colour of the
    block (the bypass switch on a pedal), the name and one line with the main value ("Time 380 ms",
    the model name, the IR name). Click it to open its page; pedals and the amp can be dragged. */
class BlockTile : public juce::Component
{
public:
    static constexpr int width = 132, height = 104;

    /** `block` is a pedal item (see PedalCatalog), PedalCatalog::ampItem or cabBlock. */
    BlockTile (int blockId, juce::AudioProcessorValueTreeState& state)
        : block (blockId), apvts (state), tint (blockColour (blockId))
    {
        const bool isPedal = block != PedalCatalog::ampItem && block != cabBlock;

        dot.setTint (tint);
        dot.setSwitchable (isPedal);
        dot.setToggleState (true, juce::dontSendNotification);
        addAndMakeVisible (dot);

        if (isPedal)
        {
            title = PedalCatalog::typeOf (block) == PedalCatalog::captureType ? juce::String ("NAM Pedal")
                                                                              : Theme::titleCase (PedalCatalog::displayName (block));
            activeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, PedalCatalog::onId (block), dot);
            lastActive = isActive();
            updateDetail();
        }
        else
        {
            title = block == cabBlock ? "Cab" : "Amp";
        }

        if (isPedal && PedalCatalog::isRemovable (block))
        {
            removeButton.onClick = [this] { if (onRemove) onRemove(); };
            addChildComponent (removeButton);
        }

        addMouseListener (this, true);   // hover over the dot or the remove button counts as hover over the tile
        setMouseCursor (block == cabBlock ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::DraggingHandCursor);
    }

    int getBlock() const { return block; }
    int getItem() const { return block; }
    juce::Colour getColour() const { return tint; }
    bool isActive() const { return block == PedalCatalog::ampItem || block == cabBlock || dot.getToggleState(); }

    /** The bypass dot of a pedal tile (tests click it). */
    juce::Button& getDot() { return dot; }
    const juce::String& getDetail() const { return detail; }

    void setSelected (bool shouldBeSelected)
    {
        if (selected == shouldBeSelected)
            return;
        selected = shouldBeSelected;
        repaint();
    }

    /** The value line of the amp and the cab tile (a pedal tile reads its own first knob). */
    void setDetail (const juce::String& text)
    {
        if (detail == text)
            return;
        detail = text;
        repaint();
    }

    /** A pedal that is named after something else than its type (the NAM pedal: its model). */
    void setTitle (const juce::String& text)
    {
        if (title == text)
            return;
        title = text;
        repaint();
    }

    const juce::String& getTitle() const { return title; }

    /** A pedal whose value line is not its first knob (a NAM pedal shows what its model is). */
    void setCustomDetail (const juce::String& text)
    {
        customDetail = true;
        setDetail (text);
    }

    std::function<void()> onRemove;
    std::function<void()> onClick;   // set for tiles that handle their own click (the cab)

    /** Called from the UI timer so host automation of the bypass state and the knob is reflected too. */
    bool refresh()
    {
        if (isPedalBlock() && ! customDetail)
            updateDetail();

        const bool now = isActive();
        if (now == lastActive)
            return false;

        lastActive = now;
        repaint();
        return true;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const bool on = isActive();

        g.setColour (selected ? Theme::surfaceHi : hover ? Theme::surfaceHi.interpolatedWith (Theme::surface, 0.5f) : Theme::surface);
        g.fillRoundedRectangle (r, 10.0f);

        if (selected)
        {
            // underline in the colour of the block
            g.setColour (tint);
            g.fillRoundedRectangle (r.getX() + 16.0f, r.getBottom() - 4.0f, r.getWidth() - 32.0f, 2.0f, 1.0f);
        }

        g.setColour (on ? Theme::textHi : Theme::textMid);
        g.setFont (Theme::ui (14.0f, true));
        g.drawText (title, juce::Rectangle<float> (r.getX() + 14.0f, r.getY() + 42.0f, r.getWidth() - 28.0f, 20.0f),
                    juce::Justification::centredLeft, true);

        if (detail.isNotEmpty())
        {
            g.setColour (on ? Theme::textMid : Theme::textLo);
            g.setFont (Theme::ui (11.5f));
            g.drawText (detail, juce::Rectangle<float> (r.getX() + 14.0f, r.getY() + 64.0f, r.getWidth() - 28.0f, 16.0f),
                        juce::Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        dot.setBounds (4, 4, 32, 32);
        removeButton.setBounds (getWidth() - 30, 6, 24, 24);
    }

    void mouseEnter (const juce::MouseEvent&) override { updateHover(); }
    void mouseExit  (const juce::MouseEvent&) override { updateHover(); }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.originalComponent == this && e.mouseWasClicked() && onClick)
            onClick();
    }

private:
    bool isPedalBlock() const { return block != PedalCatalog::ampItem && block != cabBlock; }

    void updateHover()
    {
        const bool now = isMouseOver (true);
        if (now == hover)
            return;

        hover = now;
        removeButton.setVisible (hover && PedalCatalog::isRemovable (block) && isPedalBlock());
        repaint();
    }

    /** First knob of the pedal and its value, e.g. "Time 380 ms". */
    void updateDetail()
    {
        const auto& knobs = PedalCatalog::pedalOf (block).knobs;
        if (knobs.empty())
            return;

        const auto& k = knobs.front();
        const auto* value = apvts.getRawParameterValue (PedalCatalog::knobId (block, 0));
        if (value == nullptr)
            return;

        setDetail (Theme::titleCase (k.label) + " " + juce::String (value->load(), k.decimals) + k.suffix);
    }

    int block;
    juce::AudioProcessorValueTreeState& apvts;
    juce::Colour tint;
    juce::String title, detail;
    bool selected = false, hover = false, lastActive = true, customDetail = false;

    TileDot dot;
    RemoveButton removeButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> activeAttachment;
};

//==============================================================================
/** The scrolling row of chain blocks (pedals and the amp, in signal order). Blocks can be dragged
    sideways to change their place in the chain; a click without a drag selects the block. */
class ChainStrip : public juce::Component, private juce::Timer
{
public:
    /** One thing in the chain: a catalog pedal item, or PedalCatalog::ampItem. */
    struct Item
    {
        int id;
        BlockTile* comp;
    };

    explicit ChainStrip (NeuralFlowAmpAudioProcessor& p)
        : processor (p), apvts (p.apvts), ampTile (PedalCatalog::ampItem, p.apvts)
    {
        addAndMakeVisible (ampTile);
        ampTile.addMouseListener (this, false);
    }

    /** True if this pedal item (type + copy) is part of the signal chain. */
    static bool isInChain (juce::AudioProcessorValueTreeState& state, int item)
    {
        const auto added = PedalCatalog::addedId (item);
        return added.isEmpty() || state.getRawParameterValue (added)->load() > 0.5f;
    }

    /** First copy of this pedal type that is not in the chain yet, as an item number; -1 if all are used. */
    static int firstFreeCopy (juce::AudioProcessorValueTreeState& state, int type)
    {
        for (int copy = 0; copy < PedalCatalog::copiesOfType (type); ++copy)
            if (! isInChain (state, PedalCatalog::makeItem (type, copy)))
                return PedalCatalog::makeItem (type, copy);
        return -1;
    }

    static int copiesInChain (juce::AudioProcessorValueTreeState& state, int type)
    {
        int n = 0;
        for (int copy = 0; copy < PedalCatalog::copiesOfType (type); ++copy)
            n += isInChain (state, PedalCatalog::makeItem (type, copy)) ? 1 : 0;
        return n;
    }

    /** Called after the user dropped an item in a new place, with the item ids in their new order. */
    std::function<void (const std::vector<int>&)> onOrderChanged;

    /** Called when a block was clicked (pressed and released without dragging it). */
    std::function<void (int)> onItemClicked;

    /** Recreates the pedals from the current parameter state and the processor's chain order. */
    void rebuild()
    {
        endDrag();
        animator.cancelAllAnimations (false);

        items.clear();
        pedals.clear();

        for (int id : processor.getChainOrder())
        {
            if (id == PedalCatalog::ampItem)
            {
                items.push_back ({ id, &ampTile });
                continue;
            }

            if (! isInChain (apvts, id))
                continue;

            auto* tile = pedals.add (new BlockTile (id, apvts));
            addAndMakeVisible (tile);
            tile->addMouseListener (this, false);
            items.push_back ({ id, tile });
        }

        applySelection();
    }

    void setSelected (int block)
    {
        selectedBlock = block;
        applySelection();
    }

    BlockTile& getAmpTile() { return ampTile; }

    /** Positions every item for the given height and resizes the strip to fit. */
    void layoutItems (int height, bool animate = false)
    {
        stripHeight = height;
        gaps.clear();
        targets.assign (items.size(), {});

        const int gap = 24;
        const int y = (height - BlockTile::height) / 2;
        int x = 0;

        for (size_t i = 0; i < items.size(); ++i)
        {
            if (i > 0)
            {
                gaps.push_back ({ x, x + gap });
                x += gap;
            }
            targets[i] = { x, y, BlockTile::width, BlockTile::height };
            x += BlockTile::width;
        }
        itemsRight = x;

        setSize (x, height);

        for (size_t i = 0; i < items.size(); ++i)
            if (! (dragging && (int) i == dragIndex))
                moveTo (*items[i].comp, targets[i], animate);
    }

    bool isDragging() const { return dragging; }

    /** Ids of the items in their current order. */
    std::vector<int> getItemOrder() const
    {
        std::vector<int> ids;
        for (const auto& item : items)
            ids.push_back (item.id);
        return ids;
    }

    //==========================================================================
    // Drag and drop. The mouse handlers below call these; they are public so tests can drive them.

    /** Arms a drag on `comp`; nothing moves until the pointer travels a few pixels. */
    void startDrag (juce::Component& comp, juce::Point<int> stripPos)
    {
        endDrag();

        for (size_t i = 0; i < items.size(); ++i)
        {
            if (items[i].comp != &comp)
                continue;

            dragIndex = (int) i;
            downPos = stripPos;
            grabOffsetX = stripPos.x - comp.getX();
            orderAtStart = getItemOrder();
            armed = true;
            return;
        }
    }

    void dragTo (juce::Point<int> stripPos)
    {
        if (! armed)
            return;

        auto& comp = *items[(size_t) dragIndex].comp;

        if (! dragging)
        {
            if (stripPos.getDistanceFrom (downPos) < 6)
                return;

            dragging = true;
            animator.cancelAnimation (&comp, false);
            comp.toFront (false);
            comp.setAlpha (0.96f);
            startTimerHz (60);
        }

        const int x = juce::jlimit (0, juce::jmax (0, itemsRight - comp.getWidth()), stripPos.x - grabOffsetX);
        comp.setTopLeftPosition (x, targets[(size_t) dragIndex].getY());

        // Swap with a neighbour once the dragged item's centre reaches the neighbour's centre. (At the
        // ends the item is clamped, so its centre can only ever become equal to the end slot's centre.)
        const int centre = x + comp.getWidth() / 2;

        while (dragIndex + 1 < (int) items.size() && centre >= targets[(size_t) dragIndex + 1].getCentreX())
        {
            std::swap (items[(size_t) dragIndex], items[(size_t) dragIndex + 1]);
            ++dragIndex;
            layoutItems (stripHeight, true);
        }
        while (dragIndex > 0 && centre <= targets[(size_t) dragIndex - 1].getCentreX())
        {
            std::swap (items[(size_t) dragIndex], items[(size_t) dragIndex - 1]);
            --dragIndex;
            layoutItems (stripHeight, true);
        }

        repaint();
    }

    void endDrag()
    {
        if (! armed)
            return;

        const bool wasDragging = dragging;
        auto* comp = items[(size_t) dragIndex].comp;

        armed = dragging = false;
        stopTimer();

        if (wasDragging)
        {
            comp->setAlpha (1.0f);
            animator.animateComponent (comp, targets[(size_t) dragIndex], 1.0f, 140, false, 1.0, 1.0);

            if (getItemOrder() != orderAtStart && onOrderChanged)
                onOrderChanged (getItemOrder());
        }

        dragIndex = -1;
        repaint();
    }

    //==========================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        pressedItem = -1;

        if (auto* comp = findItemComponent (e.originalComponent))
        {
            pressedItem = comp->getBlock();
            startDrag (*comp, e.getEventRelativeTo (this).getPosition());
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override { dragTo (e.getEventRelativeTo (this).getPosition()); }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const bool wasDragging = dragging;
        endDrag();

        if (! wasDragging && pressedItem >= 0 && e.mouseWasClicked() && onItemClicked)
            onItemClicked (pressedItem);

        pressedItem = -1;
    }

    void paint (juce::Graphics& g) override
    {
        const float y = (float) stripHeight * 0.5f;

        for (const auto& gap : gaps)
        {
            const float cx = (float) (gap.first + gap.second) * 0.5f;

            juce::Path chevron;
            chevron.startNewSubPath (cx - 2.5f, y - 4.5f);
            chevron.lineTo (cx + 2.5f, y);
            chevron.lineTo (cx - 2.5f, y + 4.5f);
            g.setColour (Theme::textLo);
            g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        if (dragging && juce::isPositiveAndBelow (dragIndex, (int) items.size()))
        {
            // where the item will land
            auto slot = targets[(size_t) dragIndex].toFloat();
            juce::Path outline, dashed;
            outline.addRoundedRectangle (slot.reduced (0.5f), 8.0f);
            const float dashes[] = { 4.0f, 4.0f };
            juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (Theme::textLo);
            g.fillPath (dashed);

            // lift: shadow under the item being carried
            juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 16, { 0, 6 })
                .drawForRectangle (g, items[(size_t) dragIndex].comp->getBounds());
        }
    }

    juce::OwnedArray<BlockTile> pedals;

private:
    void applySelection()
    {
        for (auto& item : items)
            item.comp->setSelected (item.id == selectedBlock);
    }

    void timerCallback() override
    {
        auto mouse = juce::Desktop::getInstance().getMainMouseSource();
        if (! dragging || ! mouse.isDragging())
            return;

        auto* viewport = findParentComponentOfClass<juce::Viewport>();
        if (viewport == nullptr)
            return;

        // scroll the strip when an item is held near either edge
        const auto screenPos = mouse.getScreenPosition();
        const auto inViewport = viewport->getLocalPoint (nullptr, screenPos).roundToInt();

        const int edge = 56, visible = viewport->getMaximumVisibleWidth();
        int dx = 0;
        if (inViewport.x < edge)                dx = -(2 + (edge - inViewport.x) / 4);
        else if (inViewport.x > visible - edge) dx = 2 + (inViewport.x - (visible - edge)) / 4;

        if (dx != 0)
        {
            viewport->setViewPosition (juce::jmax (0, viewport->getViewPositionX() + dx), 0);
            dragTo (getLocalPoint (nullptr, screenPos).roundToInt());
        }
    }

    BlockTile* findItemComponent (juce::Component* c) const
    {
        for (; c != nullptr && c != this; c = c->getParentComponent())
            for (const auto& item : items)
                if (item.comp == c)
                    return item.comp;
        return nullptr;
    }

    void moveTo (juce::Component& comp, juce::Rectangle<int> bounds, bool animate)
    {
        if (comp.getBounds() == bounds)
            return;

        if (animate && comp.isVisible() && comp.getWidth() > 0)
        {
            animator.animateComponent (&comp, bounds, 1.0f, 140, false, 1.0, 1.0);
        }
        else
        {
            animator.cancelAnimation (&comp, false);
            comp.setBounds (bounds);
        }
    }

    NeuralFlowAmpAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;
    BlockTile ampTile;
    juce::ComponentAnimator animator;

    std::vector<Item> items;
    std::vector<juce::Rectangle<int>> targets;
    std::vector<std::pair<int, int>> gaps;
    int stripHeight = 0, itemsRight = 0, selectedBlock = PedalCatalog::ampItem, pressedItem = -1;

    bool armed = false, dragging = false;
    int dragIndex = -1, grabOffsetX = 0;
    juce::Point<int> downPos;
    std::vector<int> orderAtStart;
};

//==============================================================================
/** Round "+" at the end of the chain strip: opens the pedal picker (lit while it is open). */
class AddButton : public juce::Button
{
public:
    static constexpr int size = 36;

    AddButton() : juce::Button ("add")
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Add pedal");
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto r = getLocalBounds().toFloat().withSizeKeepingCentre ((float) size, (float) size);
        const bool usable = isEnabled();

        const bool open = getToggleState();

        g.setColour (down ? Theme::surfaceHi.brighter (0.1f) : (highlighted && usable) || open ? Theme::surfaceHi : Theme::surface);
        g.fillEllipse (r);

        g.setColour (! usable ? Theme::textLo.withAlpha (0.5f) : highlighted || open ? Theme::accent : Theme::textMid);
        g.fillRoundedRectangle (r.withSizeKeepingCentre (14.0f, 2.0f), 1.0f);
        g.fillRoundedRectangle (r.withSizeKeepingCentre (2.0f, 14.0f), 1.0f);
    }
};

//==============================================================================
/** One card of the pedal picker: a built-in pedal that can be added. */
class PedalRow : public juce::Button
{
public:
    static constexpr int height = 48;

    /** `type` is the pedal type; `copiesUsed` how many copies are in the chain already.
        The row is only disabled once every copy is in use. */
    PedalRow (int pedalType, int copiesUsed)
        : juce::Button (juce::String ("row_") + PedalCatalog::all()[(size_t) pedalType].name),
          type (pedalType), info (PedalCatalog::all()[(size_t) pedalType]),
          tint (Theme::categoryColour (info.category)),
          used (copiesUsed), inChain (copiesUsed >= PedalCatalog::copiesOfType (pedalType))
    {
        setEnabled (! inChain);
        setMouseCursor (inChain ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);

        for (const auto& k : info.knobs)
            knobNames += (knobNames.isEmpty() ? juce::String() : juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))) + Theme::titleCase (k.label);
    }

    const PedalCatalog::Pedal& getInfo() const { return info; }
    int getType() const { return type; }
    int getCopiesUsed() const { return used; }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        const float alpha = inChain ? 0.45f : 1.0f;

        if (isEnabled() && (highlighted || down))
        {
            g.setColour (juce::Colours::white.withAlpha (down ? 0.10f : 0.06f));
            g.fillRoundedRectangle (r, 8.0f);
        }

        g.setColour (tint.withAlpha (alpha));
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ r.getX() + 16.0f, r.getCentreY() }));

        const float textX = r.getX() + 32.0f;
        const float textW = r.getWidth() - textX - 90.0f;

        g.setColour (Theme::textHi.withAlpha (alpha));
        g.setFont (Theme::ui (13.0f, true));
        g.drawText (Theme::titleCase (info.name), juce::Rectangle<float> (textX, r.getCentreY() - 17.0f, textW, 18.0f),
                    juce::Justification::centredLeft, true);

        g.setColour (Theme::textLo.withAlpha (juce::jmin (1.0f, alpha + 0.2f)));
        g.setFont (Theme::ui (11.0f));
        g.drawText (knobNames, juce::Rectangle<float> (textX, r.getCentreY() + 1.0f, textW, 15.0f),
                    juce::Justification::centredLeft, true);

        // right side: how many copies are in the chain
        const auto right = juce::Rectangle<float> (r.getRight() - 92.0f, r.getY(), 80.0f, r.getHeight());
        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (11.0f));

        if (inChain)
            g.drawText ("All in use", right, juce::Justification::centredRight, false);
        else if (used > 0)
            g.drawText (juce::String (used) + " in chain", right, juce::Justification::centredRight, false);
    }

private:
    int type;
    const PedalCatalog::Pedal& info;
    juce::Colour tint;
    int used;
    bool inChain;   // every copy is in the chain
    juce::String knobNames;
};

//==============================================================================
/** The top of the Rig: the chain of pedals and the amp, the "+" button and the fixed cab block.
    Clicking a block selects it (onSelect), the page below shows it. */
class ChainBar : public juce::Component
{
public:
    explicit ChainBar (NeuralFlowAmpAudioProcessor& p)
        : processor (p), apvts (p.apvts), strip (p), cabTile (cabBlock, p.apvts)
    {
        viewport.setViewedComponent (&strip, false);
        viewport.setScrollBarsShown (false, true);
        viewport.setScrollBarThickness (6);
        addAndMakeVisible (viewport);

        addAndMakeVisible (addButton);
        addAndMakeVisible (cabTile);

        strip.onOrderChanged = [this] (const std::vector<int>& visible) { applyVisibleOrder (visible); };
        strip.onItemClicked  = [this] (int block) { select (block); };
        addButton.onClick    = [this] { if (onAddRequested) onAddRequested(); };
        cabTile.onClick      = [this] { select (cabBlock); };

        rebuild();
        select (PedalCatalog::ampItem);
    }

    /** Called when the selected block changes. */
    std::function<void (int)> onSelect;

    /** Called when the "+" button was clicked: the owner shows or hides the pedal picker. */
    std::function<void()> onAddRequested;

    /** Lights the "+" button while the picker is open. */
    void setAddOpen (bool open) { addButton.setToggleState (open, juce::dontSendNotification); }

    int getSelected() const { return selected; }

    void select (int block)
    {
        selected = block;
        strip.setSelected (block);
        cabTile.setSelected (block == cabBlock);

        if (onSelect)
            onSelect (block);
    }

    /** Called from the UI timer: reflects automation and state changes. */
    void refresh()
    {
        if (! strip.isDragging() && (currentMask() != shownMask || processor.getChainOrder() != shownOrder))
            rebuild();

        for (auto* p : strip.pedals)
            p->refresh();

        updateDetails();
    }

    void resized() override
    {
        auto b = getLocalBounds();
        const int h = b.getHeight();

        // same row as the tiles of the strip (the strip leaves 8 px for its scroll bar)
        cabTile.setBounds (juce::Rectangle<int> (b.getRight() - BlockTile::width, (h - 8 - BlockTile::height) / 2,
                                                 BlockTile::width, BlockTile::height));
        b.removeFromRight (BlockTile::width);
        b.removeFromRight (24);
        // on the signal line: the centre of the tiles, which leave 8 px for the scroll bar
        addButton.setBounds (juce::Rectangle<int> (b.removeFromRight (AddButton::size).getX(), (h - 8 - AddButton::size) / 2,
                                                   AddButton::size, AddButton::size));
        b.removeFromRight (16);

        viewport.setBounds (b);
        strip.layoutItems (juce::jmax (BlockTile::height, h - 8));
    }

    void paint (juce::Graphics& g) override
    {
        // the signal line runs behind everything, from the first block to the cab
        const float y = (float) getHeight() * 0.5f - 4.0f;
        g.setColour (Theme::trackBg);
        g.fillRect ((float) viewport.getX(), y - 0.75f, (float) (cabTile.getX() - viewport.getX()), 1.5f);
    }

    ChainStrip& getStrip() { return strip; }
    BlockTile& getCabTile() { return cabTile; }
    AddButton& getAddButton() { return addButton; }

    /** Puts the first unused copy of a built-in pedal type into the chain and opens its page. */
    void addPedal (int type)
    {
        const int item = ChainStrip::firstFreeCopy (apvts, type);
        if (item < 0)
            return;

        setAdded (item, true);
        showNewItem (item);
    }

    /** Puts a NAM pedal with `model` into the chain and opens its page. Returns false if every NAM pedal
        slot is already in the chain or the file is missing. */
    bool addNamPedal (const juce::File& model)
    {
        const int item = ChainStrip::firstFreeCopy (apvts, PedalCatalog::captureType);
        if (item < 0 || ! model.existsAsFile())
            return false;

        processor.loadPedalModel (PedalCatalog::copyOf (item), model);
        setAdded (item, true);
        showNewItem (item);
        return true;
    }

private:
    juce::uint64 currentMask() const
    {
        juce::uint64 mask = 0;
        for (int item = 0; item < PedalCatalog::numItems; ++item)
            if (! PedalCatalog::isAmp (item) && ChainStrip::isInChain (apvts, item))
                mask |= (juce::uint64) 1 << item;
        return mask;
    }

    void rebuild()
    {
        shownMask = currentMask();
        shownOrder = processor.getChainOrder();
        strip.rebuild();

        for (auto* p : strip.pedals)
        {
            if (PedalCatalog::isRemovable (p->getItem()))
            {
                const int item = p->getItem();
                p->onRemove = [self = this, item]
                {
                    self->setAdded (item, false);

                    // a NAM pedal that leaves the chain lets go of its model
                    if (PedalCatalog::typeOf (item) == PedalCatalog::captureType)
                        self->processor.unloadPedalModel (PedalCatalog::copyOf (item));

                    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ChainBar> (self)]
                    {
                        if (safe != nullptr)
                            safe->refresh();
                    });
                };
            }
        }

        strip.layoutItems (juce::jmax (BlockTile::height, getHeight() - 8));
        strip.setSelected (selected);

        // the selected pedal left the chain: fall back to the amp
        if (selected != PedalCatalog::ampItem && selected != cabBlock && ! ChainStrip::isInChain (apvts, selected))
            select (PedalCatalog::ampItem);

        addButton.setEnabled (anyPedalLeft());
        updateDetails();
        repaint();
    }

    bool anyPedalLeft() const
    {
        for (int type = 0; type < PedalCatalog::count; ++type)
            if (ChainStrip::firstFreeCopy (apvts, type) >= 0)
                return true;
        return false;
    }

    /** The amp tile shows "Drive 50" for the built-in amp and the model name for a NAM model, the cab
        tile the loaded IR. */
    void updateDetails()
    {
        juce::String amp;
        if (processor.apvts.getRawParameterValue ("AMP_TYPE")->load() > 0.5f)
        {
            amp = processor.nam.getStatus().state == NamEngine::Status::State::ready ? processor.nam.getModelInfo().name
                                                                                    : juce::String ("No model");
        }
        else
        {
            amp = "Drive " + juce::String (processor.apvts.getRawParameterValue ("DRIVE")->load(), 0);
        }

        strip.getAmpTile().setDetail (amp);

        // a NAM pedal carries the name of its model; under it, what the model is
        for (auto* tile : strip.pedals)
        {
            if (PedalCatalog::typeOf (tile->getItem()) != PedalCatalog::captureType)
                continue;

            const int copy = PedalCatalog::copyOf (tile->getItem());
            using State = NamEngine::Status::State;

            tile->setTitle (namPedalName (processor, copy));

            switch (processor.pedalNam[(size_t) copy].getStatus().state)
            {
                case State::ready:
                {
                    const auto line = namPedalInfo (processor, copy).shortLine();
                    tile->setCustomDetail (line.isNotEmpty() ? line : "Input " + juce::String (apvts.getRawParameterValue (PedalCatalog::knobId (tile->getItem(), 0))->load(), 1) + " dB");
                    break;
                }
                case State::loading: tile->setCustomDetail ("Loading..."); break;
                case State::failed:  tile->setCustomDetail ("Could not load"); break;
                case State::empty:   tile->setCustomDetail ("No model"); break;
            }
        }

        cabTile.setDetail (processor.loadedIrFile.existsAsFile() ? processor.loadedIrFile.getFileNameWithoutExtension()
                                                                 : juce::String ("No IR"));
    }

    void setAdded (int item, bool added)
    {
        if (auto* param = apvts.getParameter (PedalCatalog::addedId (item)))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (added ? 1.0f : 0.0f);
            param->endChangeGesture();
        }
    }

    void showNewItem (int item)
    {
        rebuild();
        select (item);

        // bring the new pedal into view
        for (auto* p : strip.pedals)
            if (p->getItem() == item)
                viewport.setViewPosition (juce::jmax (0, p->getRight() - viewport.getMaximumVisibleWidth() + 40), 0);
    }

    /** The strip only knows the items that are on screen. Pedals that are not in the chain keep
        their slot in the processor's order; the visible items are poured into the slots that the
        visible items used to occupy. */
    void applyVisibleOrder (const std::vector<int>& visible)
    {
        auto full = processor.getChainOrder();
        auto isVisible = [&] (int id) { return std::find (visible.begin(), visible.end(), id) != visible.end(); };

        size_t next = 0;
        for (auto& slot : full)
            if (isVisible (slot))
                slot = visible[next++];

        processor.setChainOrder (full);
        shownOrder = processor.getChainOrder();
    }

    NeuralFlowAmpAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;
    ChainStrip strip;
    juce::Viewport viewport;
    AddButton addButton;
    BlockTile cabTile;
    int selected = PedalCatalog::ampItem;
    juce::uint64 shownMask = 0;
    PedalCatalog::ChainOrder shownOrder {};
};
