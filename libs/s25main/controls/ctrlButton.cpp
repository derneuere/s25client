// Copyright (C) 2005 - 2025 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ctrlButton.h"
#include "CollisionDetection.h"
#include "driver/MouseCoords.h"
#include "drivers/VideoDriverWrapper.h"

ctrlButton::ctrlButton(Window* parent, unsigned id, const DrawPoint& pos, const Extent& size, TextureColor tc,
                       const std::string& tooltip)
    : Window(parent, id, pos, size), ctrlBaseTooltip(tooltip), tc(tc), state(ButtonState::Up), hasBorder(true),
      isChecked(false), isIlluminated(false), isEnabled(true)
{}

ctrlButton::~ctrlButton() = default;

void ctrlButton::SetEnabled(bool enable /*= true*/)
{
    isEnabled = enable;
    state = ButtonState::Up;
}

void ctrlButton::SetActive(bool activate)
{
    Window::SetActive(activate);
    if(!activate)
        state = ButtonState::Up;
    else if(IsMouseOver())
        state = ButtonState::Hover;
}

bool ctrlButton::Msg_MouseMove(const MouseCoords& mc)
{
    if(isEnabled && IsMouseOver(mc))
    {
        if(state != ButtonState::Pressed)
            state = ButtonState::Hover;

        ShowTooltip();
        return true;
    } else
    {
        state = ButtonState::Up;
        HideTooltip();
        return false;
    }
}

bool ctrlButton::Msg_LeftDown(const MouseCoords& mc)
{
    if(isEnabled && IsMouseOver(mc))
    {
        state = ButtonState::Pressed;
        return true;
    }

    return false;
}

bool ctrlButton::Msg_LeftUp(const MouseCoords& mc)
{
    if(state == ButtonState::Pressed)
    {
        if(isEnabled && IsMouseOver(mc))
        {
            state = ButtonState::Hover;
            GetParent()->Msg_ButtonClick(GetID());
            return true;
        } else
            state = ButtonState::Up;
    }

    return false;
}

bool ctrlButton::CanFocus() const
{
    return isEnabled && IsVisible();
}

bool ctrlButton::Activate()
{
    if(!isEnabled || !IsVisible() || !GetParent())
        return false;
    // `state` wird NICHT angefasst. Es ist der Zustand DER MAUS auf diesem Knopf und traegt
    // deren halben Klick zwischen Msg_LeftDown und Msg_LeftUp. Wer ihn hier setzt, verschluckt
    // genau diesen Klick (Befund B2), denn Msg_LeftUp loest nur bei ButtonState::Pressed aus.
    // Ein Haengenbleiben kann daraus nicht entstehen: Activate() setzt Pressed nie, und ohne
    // Maus auf dem Knopf ist `state` ohnehin Up - der Knopf wird also auch nach einem Padklick
    // nicht erhellt gezeichnet (siehe Draw_()).
    GetParent()->Msg_ButtonClick(GetID()); // identisch zum Mauspfad in Msg_LeftUp
    return true;
}

/**
 *  zeichnet das Fenster.
 */
void ctrlButton::Draw_()
{
    if(GetSize().x == 0 || GetSize().y == 0)
        return;

    if(tc != TextureColor::Invisible)
    {
        unsigned color = isEnabled ? COLOR_WHITE : 0xFFBBBBBB;
        bool isCurIlluminated = isIlluminated || (!isEnabled && isChecked);
        bool isElevated = !isChecked && state != ButtonState::Pressed;
        bool isHighlighted = isEnabled && !isChecked && state == ButtonState::Hover;
        if(hasBorder)
            Draw3D(GetDrawRect(), tc, isElevated, isHighlighted, isCurIlluminated, color);
        else
            Draw3DContent(GetDrawRect(), tc, isElevated, isHighlighted, isCurIlluminated, color);
    }

    /// Inhalt malen (Text, Bilder usw.)
    DrawContent();
}
