// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Window.h"
#include "driver/PadEvent.h"

class IngameWindow;
class glArchivItem_Bitmap;
struct ScreenResizeEvent;

/// Desktopklasse für Spielmenü-Haupthintergrundflächen.
class Desktop : public Window
{
public:
    Desktop(glArchivItem_Bitmap* background);
    ~Desktop();
    void Msg_ScreenResize(const ScreenResizeEvent& sr) override;
    /// Callback when a window was closed
    virtual void Msg_WindowClosed(IngameWindow&){};
    /// Show or hide the fps
    void SetFpsDisplay(bool show);

    // --- Gamepad -----------------------------------------------------------------------------
    // Vier Nahtstellen mit wirkungsloser Vorgabe. Ein Desktop, der keine davon ueberschreibt,
    // verhaelt sich exakt wie vor der Einfuehrung: der WindowManager holt fuer ihn gar keine
    // Padereignisse ab (siehe WindowManager::PumpPadInput).

    /// Soll der WindowManager fuer diesen Desktop Padereignisse abholen und zustellen?
    /// Ausdruecklich FALSE fuer dskGameInterface: die Partie holt selbst ab
    /// (dskGameInterface::UpdateInput), und zwei Abholer wuerden sich die Ereignisse wegnehmen.
    virtual bool WantsPadInput() const { return false; }
    /// Wie viele Pads duerfen hier GLEICHZEITIG navigieren?
    ///
    /// Vorgabe 1, und das ist keine Bequemlichkeit: zwei Fokusse auf einem Menuebildschirm
    /// hiessen, dass zwei Leute gleichzeitig verschiedene Knoepfe druecken und der zweite
    /// Desktopwechsel den ersten ueberholt. Ein neu hinzukommendes Pad darf einem aktiven
    /// Spieler nie die Kontrolle wegnehmen (XR-115, TV-RECHERCHE.md:167); mit einem einzigen
    /// Slot bleibt es schlicht unversorgt und wirkungslos, bis der Bildschirm mehr anbietet.
    /// Genau das tut der Zuordnungsbildschirm der Lobby - und nur er.
    virtual unsigned GetNumPadSlots() const { return 1; }
    /// Knopf, den die Fokusnavigation nicht verbraucht: B (zurueck) und Start (Vorgabeaktion).
    /// true = verbraucht.
    virtual bool Msg_PadCommand(unsigned /*slot*/, PadButton /*button*/) { return false; }
    /// Control, auf dem ein NEU hinzugekommenes Pad seinen Fokus beginnen soll.
    /// nullptr = das erste fokussierbare Control (Vorgabe). Siehe Window::GetPadEntryCtrl.
    Window* GetPadEntryCtrl(unsigned /*slot*/) override { return nullptr; }

    /// ID of the fps display text
    static const unsigned fpsDisplayId;

protected:
    void Draw_() override;

    glArchivItem_Bitmap* background;

private:
    void UpdateFps(unsigned newFps);

    unsigned lastFPS_;
};
