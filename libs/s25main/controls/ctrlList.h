// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Window.h"
#include "controls/ctrlBaseTooltip.h"
#include <optional>
#include <string>
#include <vector>

struct MouseCoords;
class glFont;

class ctrlList : public Window
{
public:
    ctrlList(Window* parent, unsigned id, const DrawPoint& pos, const Extent& size, TextureColor tc,
             const glFont* font);
    ~ctrlList() override;

    /// Change size
    void Resize(const Extent& newSize) override;

    /// Add item to listbox.
    void AddItem(const std::string& text);
    /// Change text of an item.
    void SetItemText(unsigned id, const std::string& text);
    void DeleteAllItems();
    const std::string& GetItemText(unsigned line) const;
    /// Get the value of the currently selected item
    const std::string& GetSelItemText() const;
    /// Exchange the text of 2 items
    void Swap(unsigned first, unsigned second);
    /// Deletes an item. If the deleted item is selected then the selection is cleared.
    void Remove(unsigned index);

    unsigned GetNumLines() const { return static_cast<unsigned>(lines.size()); }
    const std::optional<unsigned>& GetSelection() const { return selection_; };
    void SetSelection(const std::optional<unsigned>& selection);
    /// Scrollt so, dass die aktuelle Auswahl sichtbar ist. Ohne Auswahl passiert nichts.
    /// Meldet NICHTS nach oben - es aendert sich nur, welcher Ausschnitt gezeichnet wird.
    void ScrollToSelection();

    bool Msg_MouseMove(const MouseCoords& mc) override;
    bool Msg_LeftDown(const MouseCoords& mc) override;
    bool Msg_RightDown(const MouseCoords& mc) override;
    bool Msg_LeftUp(const MouseCoords& mc) override;
    bool Msg_WheelUp(const MouseCoords& mc) override;
    bool Msg_WheelDown(const MouseCoords& mc) override;

    /// Fokusnavigation: Zeilenauswahl ist der Rasterschritt auf der Y-Achse (CanStepValue/
    /// DoStepValue), Bestaetigen ist Activate.
    /// Dieselbe Trennung, die es beim Mauspfad zwischen Klick (Msg_ListSelectItem) und
    /// Doppelklick (Msg_ListChooseItem) schon gibt.
    bool CanFocus() const override { return !lines.empty() && IsVisible(); }
    bool Activate() override;
    /// Dieselbe Vorbedingung, die Activate() prueft - siehe ctrlButton::CanActivate.
    bool CanActivate() const override { return !lines.empty() && IsVisible() && GetParent() && selection_; }
    bool CanStepValue(const Position& dir) const override;
    void DoStepValue(const Position& dir) override;
    std::optional<ValueRange> GetValueRange() const override;

protected:
    void Draw_() override;

private:
    std::optional<unsigned> GetItemFromPos(const Position& pos) const;
    Rect GetFullDrawArea() const;
    Rect GetListDrawArea() const;

    TextureColor tc;
    const glFont* font;
    ctrlBaseTooltip tooltip_;

    std::vector<std::string> lines;

    std::optional<unsigned> selection_;
    std::optional<unsigned> mouseover_;
    unsigned pagesize;
};
