// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Window.h"
#include <optional>
struct MouseCoords;

class ctrlScrollBar final : public Window
{
public:
    ctrlScrollBar(Window* parent, unsigned id, const DrawPoint& pos, const Extent& size, unsigned short button_height,
                  TextureColor tc, unsigned short pagesize);
    void Resize(const Extent& newSize) override;
    void SetScrollPos(unsigned short scroll_pos);
    void SetRange(unsigned short scroll_range);
    void SetPageSize(unsigned short pagesize);

    unsigned short GetPageSize() const { return pagesize; }
    unsigned short GetScrollPos() const { return scroll_pos; }
    /// Scrolls by the given distance (less than 0 = up, else down)
    void Scroll(int distance);

    bool Msg_LeftUp(const MouseCoords& mc) override;
    bool Msg_LeftDown(const MouseCoords& mc) override;
    bool Msg_MouseMove(const MouseCoords& mc) override;
    void Msg_ButtonClick(unsigned ctrl_id) override;

    /// Fokusnavigation: senkrechte Werteachse, waagerecht wandert der Fokus weiter.
    bool CanFocus() const override { return IsVisible() && scroll_range > pagesize; }
    std::optional<ValueRange> GetValueRange() const override
    {
        return ValueRange{scroll_pos, static_cast<unsigned>(scroll_range > pagesize ? scroll_range - pagesize : 0),
                          ValueAxis::Vertical};
    }
    /// Bewusst Scroll() und nicht SetScrollPos(): letzteres meldet NICHT nach oben, die
    /// angehaengte Liste wuerde nicht mitkommen.
    bool SetValue(unsigned value) override
    {
        Scroll(static_cast<int>(value) - static_cast<int>(scroll_pos));
        return true;
    }
    /// Senkrecht verbraucht, waagerecht nicht - dort wandert der Fokus weiter.
    bool CanStepValue(const Position& dir) const override { return dir.y != 0; }
    void DoStepValue(const Position& dir) override { Scroll(dir.y); }

protected:
    void Draw_() override;

private:
    void UpdatePosFromSlider();
    void UpdateSliderFromPos();
    void RecalculateSizes();

    unsigned short button_height;
    TextureColor tc;
    /// (max) Number of elements/lines visible
    unsigned short pagesize;
    /// Total number of elements
    unsigned short scroll_range;
    /// Current element at top
    unsigned short scroll_pos;
    /// Height of the actual area in which we can move the slider
    unsigned short scroll_height;
    /// Size of the slider
    unsigned short sliderHeight;
    /// Position of the slider in the scroll area (in [0, scroll_height - sliderHeight] )
    unsigned short sliderPos;

    bool isMouseScrolling;
    int last_y;
};
