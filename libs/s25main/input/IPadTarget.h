// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Point.h"
#include "driver/PadEvent.h"

/// Empfaenger fertig zugeordneter Gamepad-Wirkungen.
///
/// Der Empfaenger sieht KEINE Geraetekennung, kein SDL und keine Rohachse - nur eine
/// Slotnummer, die bereits < GetNumSlots() ist. Der Slot ist die Nummer der Ansicht
/// (dskGameInterface::GetPlayerView) und bewusst weder Spieler-ID noch PlayerView-Zeiger:
///  - Ein PlayerView* haengt, sobald der WindowManager den Desktop austauscht
///    (WindowManager.h:157, curDesktop ist ein unique_ptr<Desktop>).
///  - Eine Spieler-ID gibt es im Hauptmenue noch nicht, ein Pad kann aber schon stecken.
class IPadTarget
{
public:
    virtual ~IPadTarget() = default;

    /// Ein Geraet hat den Slot bekommen (assigned=true) oder verloren (assigned=false).
    virtual void OnPadAssigned(unsigned slot, bool assigned) = 0;
    /// Zeigerbewegung dieses Slots in View-Pixeln seit dem letzten Schritt. Nie (0,0).
    virtual void OnPadMove(unsigned slot, const Position& delta) = 0;
    /// KAMERAbewegung dieses Slots in View-Pixeln seit dem letzten Schritt. Nie (0,0).
    ///
    /// Bewusst getrennt von OnPadMove und nicht als zweiter Parameter: Zeiger und Ausschnitt
    /// sind zwei Groessen mit zwei Geschwindigkeiten, zwei Grenzen (der Zeiger ist auf sein
    /// Viewport geklemmt, die Kamera laeuft ueber die ganze Karte) und zwei Empfaengern (den
    /// Zeiger verbraucht die Fensternavigation, die Kamera nie).
    virtual void OnPadCamera(unsigned slot, const Position& delta) = 0;
    /// Zoomschritt dieses Slots: RELATIVE Aenderung, wie ZOOM_WHEEL_INCREMENT beim Mausrad.
    /// Positiv = naeher heran. Nie 0.
    ///
    /// Relativ und nicht absolut, damit der Router die Zoomstufen nicht kennen muss: die
    /// Grenzen (ZOOM_FACTORS) gehoeren der Ansicht, die Kennlinie dem Router.
    virtual void OnPadZoom(unsigned slot, float step) = 0;
    /// Flanke eines Knopfes. Wird pro tatsaechlicher Zustandsaenderung genau einmal gerufen.
    virtual void OnPadButton(unsigned slot, PadButton button, bool down) = 0;
};
