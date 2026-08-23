// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "world/ViewportLayout.h"
#include <RTTR_Assert.h>
#include <algorithm>

namespace {
/// Zerlegt eine Laenge in numParts Teile, ohne dass ein Pixel verlorengeht: alle Teile bekommen
/// die abgerundete Groesse, der Rest wird auf die letzten Teile verteilt. Die Summe ist exakt len.
std::vector<unsigned> splitLength(const unsigned len, const unsigned numParts)
{
    RTTR_Assert(numParts > 0);
    std::vector<unsigned> result(numParts, len / numParts);
    const unsigned remainder = len % numParts;
    for(unsigned i = 0; i < remainder; ++i)
        result[numParts - 1 - i] += 1;
    return result;
}
} // namespace

std::vector<Viewport> CalcViewports(const Extent& renderSize, unsigned numViews)
{
    if(numViews == 0)
        return {};
    RTTR_Assert(numViews <= MAX_VIEWPORTS);
    numViews = std::min(numViews, MAX_VIEWPORTS);

    // Ein Spieler: exakt der heutige Zustand, ohne jede Rechnung. Das ist die Zusicherung, dass
    // Einzelspieler, Replay und Savegame nicht regressieren koennen.
    if(numViews == 1)
        return {Viewport{Position(0, 0), renderSize}};

    // Wie viele Ansichten stehen in JEDER Zeile? Die Laenge der Liste ist die Zahl der Zeilen,
    // ihre Summe ist numViews - damit KANN keine Zelle frei bleiben.
    //
    // Frueher stand hier ein festes numCols x numRows-Raster, aus dem die ueberzaehligen Zellen
    // einfach nicht befuellt wurden. Bei DREI Ansichten liess das die vierte Zelle des
    // 2x2-Rasters leer, und die liegt mitten auf dem Bildschirm: unbemalte Flaeche, ueber der
    // die Maus ueber GAR KEINER Ansicht steht. Genau darauf beruft sich aber die Zeigerregel in
    // dskGameInterface::UpdateInput ("ueber gar keiner Ansicht heisst ausserhalb der
    // Renderflaeche") - die Berufung war damit falsch, und ein Klick in die Bildmitte wirkte auf
    // eine Ansicht, ueber der die Maus nicht stand.
    //
    // Drei Ansichten bekommen deshalb zwei oben und EINE ueber die volle Breite darunter. Der
    // dritte Spieler bekommt dabei mehr Bild statt Loch daneben; die beiden oberen behalten
    // exakt die Geometrie, die sie im Vierer-Layout haetten.
    std::vector<unsigned> viewsPerRow;
    if(numViews == 2)
    {
        // Entlang der laengeren Achse teilen: auf 16:9 links|rechts, hochkant oben|unten.
        if(renderSize.x >= renderSize.y)
            viewsPerRow = {2};
        else
            viewsPerRow = {1, 1};
    } else if(numViews == 3)
        viewsPerRow = {2, 1};
    else
        viewsPerRow = {2, 2};

    const std::vector<unsigned> rowHeights =
      splitLength(renderSize.y, static_cast<unsigned>(viewsPerRow.size()));

    std::vector<Viewport> result;
    result.reserve(numViews);
    unsigned y = 0;
    for(unsigned row = 0; row < viewsPerRow.size(); ++row)
    {
        // Je Zeile neu: die volle Breite wird auf die Ansichten DIESER Zeile aufgeteilt.
        const std::vector<unsigned> colWidths = splitLength(renderSize.x, viewsPerRow[row]);
        unsigned x = 0;
        for(unsigned col = 0; col < viewsPerRow[row]; ++col)
        {
            result.push_back(Viewport{Position(static_cast<int>(x), static_cast<int>(y)),
                                      Extent(colWidths[col], rowHeights[row])});
            x += colWidths[col];
        }
        y += rowHeights[row];
    }
    RTTR_Assert(result.size() == numViews);
    return result;
}
