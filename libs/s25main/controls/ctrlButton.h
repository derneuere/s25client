// Copyright (C) 2005 - 2025 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "DrawPoint.h"
#include "Window.h"
#include "ctrlBaseTooltip.h"

#include <string>

struct MouseCoords;

/// Buttonklasse
class ctrlButton : public Window, public ctrlBaseTooltip
{
public:
    ctrlButton(Window* parent, unsigned id, const DrawPoint& pos, const Extent& size, TextureColor tc,
               const std::string& tooltip);
    ~ctrlButton() override;

    void SetEnabled(bool enable = true);
    bool GetEnabled() const { return isEnabled; }
    TextureColor GetTexture() const { return tc; }
    void SetTexture(TextureColor tc) { this->tc = tc; }
    void SetActive(bool activate = true) override;

    void SetChecked(bool checked) { this->isChecked = checked; }
    bool GetCheck() const { return isChecked; }
    void SetIlluminated(bool illuminated) { this->isIlluminated = illuminated; }
    bool GetIlluminated() const { return isIlluminated; }
    void SetBorder(bool hasBorder) { this->hasBorder = hasBorder; }

    bool Msg_MouseMove(const MouseCoords& mc) override;
    bool Msg_LeftDown(const MouseCoords& mc) override;
    bool Msg_LeftUp(const MouseCoords& mc) override;

    /// Fokusnavigation. Deckt ctrlTextButton, ctrlImageButton, ctrlColorButton und
    /// ctrlBuildingIcon mit ab - alle vier erben von hier.
    bool CanFocus() const override;
    bool Activate() override;
    /// Die Vorbedingung von Activate(), und zwar DIESELBE - Activate() steigt damit ein.
    ///
    /// Die letzte Frage ist seit Befund P3 dabei: ein Klick geht an den ELTERNTEIL
    /// (Msg_ButtonClick), und nur der weiss, ob er mit dieser Kennung ueberhaupt etwas anfaengt.
    /// Gemessen war das der bereits gewaehlte Reiterkopf - dort stand "A Waehlen", und der Druck
    /// setzte in ctrlTab::SetSelection Schritt fuer Schritt dieselben Werte noch einmal.
    /// Vorgabe von WouldChildClickDoAnything ist "ja"; fuer jeden Knopf ausser einem
    /// Reiterkopf aendert sich damit nichts.
    bool CanActivate() const override
    {
        return isEnabled && IsVisible() && GetParent() && GetParent()->WouldChildClickDoAnything(GetID());
    }

protected:
    /// Zeichnet Grundstruktur des Buttons
    void Draw_() override;
    /// Abgeleitete Klassen müssen erweiterten Button-Inhalt zeichnen
    virtual void DrawContent() const = 0;

    /// Texturfarbe des Buttons
    TextureColor tc;
    /// Status des Buttons (gedrückt, erhellt usw. durch Maus ausgelöst)
    ButtonState state;
    /// Hat der Button einen 3D-Rand?
    bool hasBorder;
    /// Button dauerhaft gedrückt?
    bool isChecked;
    /// Button "erleuchtet"?
    bool isIlluminated;
    /// Button angeschalten?
    bool isEnabled;
};
