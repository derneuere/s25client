// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Point.h"
#include <vector>

/// Ein rechteckiger Ausschnitt der Renderflaeche fuer genau eine GameWorldView.
/// Koordinaten sind View-Koordinaten - derselbe Raum, in dem GameWorldView::origin_/size_ und
/// VIDEODRIVER.GetRenderSize() leben. Die Umrechnung nach Fensterpixeln macht erst
/// GameWorldView::GetScissorRect().
struct Viewport
{
    Position origin;
    Extent size;

    bool operator==(const Viewport& o) const { return origin == o.origin && size == o.size; }
    bool operator!=(const Viewport& o) const { return !(*this == o); }
};

/// Teilt die Renderflaeche auf numViews Ansichten auf.
///
/// Bewusst eine freie, GL- und singletonfreie Funktion: sie ist damit ohne Videotreiber und ohne
/// laufendes Spiel testbar.
///
/// Regeln:
///  - numViews == 0: leeres Ergebnis.
///  - numViews == 1: exakt Position(0,0) + renderSize. Bit-identisch zu dem, was
///    dskGameInterface heute baut - der Einzelspielerfall darf sich nicht veraendern.
///  - numViews == 2: Teilung entlang der LAENGEREN Achse. Auf 16:9 also links|rechts, damit jede
///    Haelfte quadratischer wird statt extrem breit; auf einem hochkanten Fenster automatisch
///    oben|unten.
///  - numViews == 3: zwei Ansichten oben nebeneinander, die dritte ueber die VOLLE Breite
///    darunter. Frueher war es das 2x2-Raster mit freier vierter Zelle; die freie Zelle lag
///    mitten auf dem Bildschirm und war damit eine Stelle, an der die Maus ueber GAR KEINER
///    Ansicht steht - siehe die Invariante unten, an der die Zeigerregel in
///    dskGameInterface::UpdateInput haengt.
///  - numViews == 4: 2x2-Raster in Leserichtung.
///  - Mehr als 4 lokale Spieler sind nicht vorgesehen; die Anzahl wird auf 4 begrenzt.
///
/// Zugesicherte Invarianten (im Test festgenagelt):
///  - Die Ansichten ueberlappen sich nicht.
///  - Ihre Vereinigung ist EXAKT renderSize, es geht also kein Pixel verloren - bei JEDER
///    Ansichtszahl. Rundungsreste landen jeweils in der letzten Spalte/Zeile.
///  - Jede Ansicht liegt vollstaendig innerhalb der Renderflaeche.
///  - Daraus folgt die Aussage, auf die sich der Eingabepfad beruft: JEDER Punkt der
///    Renderflaeche gehoert GENAU EINER Ansicht.
std::vector<Viewport> CalcViewports(const Extent& renderSize, unsigned numViews);

/// Hoechstzahl gleichzeitig dargestellter Ansichten (= lokale Spieler am Fernseher).
constexpr unsigned MAX_VIEWPORTS = 4;
