// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TvDisplay.h"
#include "Settings.h"
#include "helpers/mathFuncs.h"
#include "gameData/GuiConsts.h"
#include <algorithm>

namespace tv {

unsigned RecommendedGuiScalePercent(const unsigned renderHeight)
{
    if(renderHeight <= UI_REFERENCE_HEIGHT)
        return 100u;
    return std::max(100u, helpers::iround<unsigned>(renderHeight * 100.f / UI_REFERENCE_HEIGHT));
}

float RecommendedZoomFactor(const unsigned renderHeight)
{
    const float wanted = static_cast<float>(renderHeight) / UI_REFERENCE_HEIGHT;
    // Groesster ANGEBOTENER Faktor, der nicht groesser ist als gewuenscht. Bewusst keine neue
    // Zwischenstufe: der Spieler soll mit dem Pad (dskGameInterface::OnPadZoom) und dem Mausrad
    // durch dieselbe Liste laufen, in der er gestartet ist.
    float result = ZOOM_FACTORS[ZOOM_DEFAULT_INDEX];
    for(const float f : ZOOM_FACTORS)
    {
        if(f <= wanted && f > result)
            result = f;
    }
    return result;
}

Rect SafeAreaRect(const Extent& renderSize, const unsigned percentPerSide)
{
    if(percentPerSide == 0)
        return Rect(Position(0, 0), renderSize);
    const unsigned percent = std::min(percentPerSide, SAFE_AREA_PERCENT_MAX);
    const auto marginX = static_cast<int>(renderSize.x * percent / 100u);
    const auto marginY = static_cast<int>(renderSize.y * percent / 100u);
    return Rect(marginX, marginY, renderSize.x - 2u * marginX, renderSize.y - 2u * marginY);
}

unsigned SanitizeSafeAreaPercent(const int rawPercent)
{
    if(rawPercent < 0)
        return SAFE_AREA_PERCENT_DEFAULT;
    return std::min(static_cast<unsigned>(rawPercent), SAFE_AREA_PERCENT_MAX);
}

bool IsTvModeEnabled()
{
    return SETTINGS.video.tvMode;
}

unsigned ActiveSafeAreaPercent()
{
    return SETTINGS.video.tvMode ? std::min(SETTINGS.video.tvSafeAreaPercent, SAFE_AREA_PERCENT_MAX) : 0u;
}

Rect ActiveSafeAreaRect(const Extent& renderSize)
{
    return SafeAreaRect(renderSize, ActiveSafeAreaPercent());
}

Rect WindowBoundsRect(const Extent& renderSize, const Extent& windowSize)
{
    const Rect full(Position(0, 0), renderSize);
    Rect result = ActiveSafeAreaRect(renderSize);
    // Je Achse GETRENNT: ein Fenster, das zu hoch fuer den Kasten ist, soll trotzdem den
    // seitlichen Rand einhalten. Siehe die Begruendung im Kopf - der Rand gibt nach, nicht das
    // Fenster.
    const Extent safeSize = result.getSize();
    if(safeSize.x < windowSize.x)
    {
        result.left = full.left;
        result.right = full.right;
    }
    if(safeSize.y < windowSize.y)
    {
        result.top = full.top;
        result.bottom = full.bottom;
    }
    return result;
}

Rect ScreenChromeRect(const Extent& renderSize)
{
    // Dieselbe Regel, dasselbe Nachgeben je Achse - nur ist der Mindestbedarf hier nicht ein
    // Fenster, sondern das kleinste, was der Rahmenbauer noch zusammensetzen kann.
    return WindowBoundsRect(renderSize, SCREEN_CHROME_MIN_SIZE);
}

} // namespace tv
