// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "IngameWindow.h"

class dskGameInterface;
class PlayerView;

/// Das Systemmenue EINES Padspielers - sein Ersatz fuer die Knopfleiste am unteren Bildrand.
///
/// WARUM EIN EIGENES FENSTER UND NICHT DIE VORHANDENE LEISTE:
///
///  1. Es gibt genau EINE Leiste, und sie wird gegen den GANZEN Bildschirm gerechnet
///     (dskGameInterface::CalcButtonBarOrigin -> tv::ScreenChromeRect). Bei vier Ansichten
///     liegt sie mittig unten, also auf der senkrechten Naht zwischen den beiden unteren
///     Viewports. Ein Fokusrahmen, den der Spieler unten links setzt, waere zur Haelfte im
///     Bild seines Nachbarn. Das laesst sich nicht wegkonfigurieren, ohne Leiste und
///     Bildschirmrahmen zu trennen - die sitzen seit Phase 7 ausdruecklich in EINEM Kasten.
///  2. Die vier Leistenknoepfe sind unbeschriftete Symbole von 1996 (io 50/192/83/62), 37x32
///     Einheiten gross, und ihre einzige Erklaerung ist ein MAUS-Tooltip. Aus drei Metern vor
///     einem Fernseher ist das nicht lesbar und mit dem Pad gar nicht abrufbar. Phase 9 hat
///     denselben Fehler schon einmal gemacht und selbst korrigiert: Symbole allein genuegen
///     einem Anfaenger nicht, der Klartext muss danebenstehen.
///
/// Dieses Fenster ist deshalb Klartext, gehoert dem druckenden Spieler (die Besitzklammer in
/// dskGameInterface::OnPadButton ist beim Bau offen) und erscheint an SEINEM Padzeiger, also in
/// SEINEM Viewport. Vier Spieler koennen es gleichzeitig offen haben; es gibt darin keinen
/// geteilten Zustand.
///
/// Es SCHREIBT NICHTS NEU: jeder Knopf ruft dieselbe benannte Methode auf dskGameInterface, die
/// auch der Mausknopf der Leiste ruft (OpenMinimapFor, OpenMainMenuFor, OpenPostOfficeFor,
/// ToggleConstructionAidFor, ToggleNamesAndProductivityFor). Zwei Regelwerke fuer dieselbe
/// Handlung laufen ab dem ersten Zusatz auseinander - derselbe Grund, aus dem Phase 4f
/// OpenObjectWindow aus ContextClick herausgezogen hat.
class iwPadSystemMenu : public IngameWindow
{
public:
    /// Die Knopfnummern sind oeffentlich, weil die Nachweise sie brauchen: ein Fokusnachweis
    /// darf nicht auf uebersetzten Text zielen (in frueheren Phasen sind dreimal Tests an einer
    /// fremden Sprache zerbrochen), sondern auf die Kennung.
    enum ButtonId
    {
        ID_MINIMAP,
        ID_POST,
        ID_CONSTRUCTION_AID,
        /// PHASE 13, GETRENNT: bis hierher kippte EIN Knopf beide Anzeigen zusammen
        /// (ToggleShowNamesAndProductivity). Das war ein Platzkompromiss der Knopfliste und
        /// keine Entscheidung - der MAUSspieler hat die Trennung seit jeher (Taste c und
        /// Taste s), und fuer einen Anfaenger ist sie die richtige: NAMEN sind Lernstoff
        /// ("welches Gebaeude ist das?" - woertlich der Phase-9-Befund mit Steinbruch und
        /// Holzfaeller), AUSLASTUNG ist eine Expertenzahl. Wer beides zusammen einschaltet,
        /// bekommt ueber jedem Haus zwei Zeilen Text.
        ///
        /// Der Ring hat den Platz, den die Liste nicht hatte. ToggleShowNames und
        /// ToggleShowProductivity gibt es laengst und sie sind laengst je Ansicht - es entsteht
        /// KEIN neuer Mechanismus.
        ID_NAMES,
        ID_PRODUCTIVITY,
        /// PHASE 13, NEU: der Sammelschalter, nach dem der Auftraggeber woertlich gefragt hat
        /// ("wenn ich einfach nur ein wenig zuschauen will").
        ID_WATCH_ONLY,
        ID_MAIN_SELECTION
    };

    iwPadSystemMenu(dskGameInterface& dsk, PlayerView& view, const DrawPoint& pos);

    void Msg_ButtonClick(unsigned ctrl_id) override;
    void Msg_PaintBefore() override;
    brief::Brief GetPadBrief(const Window* focused) const override;

private:
    /// Schreibt die beiden Umschalter neu, damit die Beschriftung den ZUSTAND nennt und nicht
    /// die Handlung. "Bauhilfe: an" sagt einem Anfaenger, was er sieht; "Bauhilfe umschalten"
    /// laesst ihn raten, was gerade gilt.
    void UpdateToggleLabels();

    dskGameInterface& dsk_;
    PlayerView& view_;
    /// Zuletzt geschriebener Zustand der beiden Umschalter. Nur, damit Msg_PaintBefore nicht
    /// jeden Frame Zeichenketten neu baut.
    bool lastShowBQ_;
    bool lastShowNames_;
    bool lastShowProductivity_;
    bool lastWatchOnly_;
};
