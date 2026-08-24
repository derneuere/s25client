// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "driver/VideoDriver.h"
#include "commonDefines.h"
#include "driver/VideoDriverLoaderInterface.h"
#include "helpers/mathFuncs.h"
#include <algorithm>
#include <stdexcept>

// Do not inline! That would break DLL compatibility:
// http://stackoverflow.com/questions/32444520/how-to-handle-destructors-in-dll-exported-interfaces
IVideoDriver::~IVideoDriver() = default;

/**
 *  Konstruktor von @p VideoDriver.
 *
 *  @param[in] CallBack DriverCallback für Rückmeldungen.
 */
VideoDriver::VideoDriver(VideoDriverLoaderInterface* CallBack)
    : CallBack(CallBack), initialized(false), displayMode_(DisplayMode::Windowed), renderSize_(0, 0),
      scaledRenderSize_(0, 0), dpiScale_(1.f), guiScale_(100), autoGuiScale_(false), uiReferenceHeight_(0)
{
    std::fill(keyboard.begin(), keyboard.end(), false);
}

Position VideoDriver::GetMousePos() const
{
    return mouse_xy.pos;
}

/**
 *  Funktion zum Auslesen ob die Linke Maustaste gedrückt ist.
 *
 *  @return @p true bei Gedrückt, @p false bei Losgelassen
 */
bool VideoDriver::GetMouseStateL() const
{
    return mouse_xy.ldown;
}

/**
 *  Funktion zum Auslesen ob die Rechte Maustaste gedrückt ist.
 *
 *  @return @p true bei Gedrückt, @p false bei Losgelassen
 */
bool VideoDriver::GetMouseStateR() const
{
    return mouse_xy.rdown;
}

/**
 * Function to check if at least 1 finger is on screen.
 *
 *  @return @p true at least 1 finger, @p false when mouse used
 */
bool VideoDriver::IsTouchEvent() const
{
    return mouse_xy.num_tfingers > 0;
}

VideoMode VideoDriver::FindClosestVideoMode(const VideoMode mode) const
{
    std::vector<VideoMode> avModes = ListVideoModes();
    if(avModes.empty())
        throw std::runtime_error("No supported video modes found!");
    unsigned minSizeDiff = std::numeric_limits<unsigned>::max();
    VideoMode best = avModes.front();
    for(const VideoMode& current : avModes)
    {
        const auto dw = absDiff(current.width, mode.width);
        const auto dh = absDiff(current.height, mode.height);
        unsigned sizeDiff = dw * dw + dh * dh;
        if(sizeDiff < minSizeDiff)
        {
            minSizeDiff = sizeDiff;
            best = current;
        }
    }
    return best;
}

void VideoDriver::SetNewSize(VideoMode windowSize, Extent renderSize)
{
    windowSize_ = windowSize;
    renderSize_ = renderSize;

    const auto ratioXY = PointF(renderSize_) / PointF(windowSize_.width, windowSize_.height);
    dpiScale_ = (ratioXY.x + ratioXY.y) / 2.f; // use the average ratio of both axes

    if(autoGuiScale_)
        guiScale_ = GuiScale(getGuiScaleRange().recommendedPercent);

    scaledRenderSize_ = guiScale_.screenToView<Extent>(renderSize);
}

void VideoDriver::setGuiScalePercent(unsigned percent)
{
    autoGuiScale_ = (percent == 0);
    if(autoGuiScale_)
        percent = getGuiScaleRange().recommendedPercent;

    if(guiScale_.percent() == percent)
        return;

    // translate current mouse position to screen space
    const auto screenPos = guiScale_.viewToScreen(mouse_xy.pos);

    guiScale_ = GuiScale(percent);
    scaledRenderSize_ = guiScale_.screenToView<Extent>(renderSize_);
    CallBack->WindowResized();

    // move cursor to new position in view space
    // must happen after window resize event to avoid drawing spurious hover events
    mouse_xy.pos = guiScale_.screenToView(screenPos);
    CallBack->Msg_MouseMove(mouse_xy);
}

void VideoDriver::setUiReferenceHeight(unsigned referenceHeight)
{
    if(uiReferenceHeight_ == referenceHeight)
        return;
    uiReferenceHeight_ = referenceHeight;
    // Steht die Skalierung auf "automatisch", muss die neue Empfehlung SOFORT wirken - sonst
    // bliebe der Fernsehmodus bis zum naechsten Fenstergroessenwechsel folgenlos.
    if(autoGuiScale_)
        setGuiScalePercent(0);
}

unsigned VideoDriver::largestScaleFittingTheUi() const
{
    constexpr Extent minUiSize(800, 600);
    // Die einzige Probe, die zaehlt: dieselbe Umrechnung, die spaeter auch scaledRenderSize_
    // erzeugt (SetNewSize/setGuiScalePercent). Sie SCHNEIDET AB - Extent ist Point<unsigned>
    // und wird aus PointF konstruiert -, deshalb darf hier nicht gerundet werden.
    const auto fits = [&](const unsigned percent) {
        const Extent viewSize = GuiScale(percent).screenToView<Extent>(renderSize_);
        return viewSize.x >= minUiSize.x && viewSize.y >= minUiSize.y;
    };
    // Startwert aus der reinen Rechnung, ABGERUNDET. Genau hier stand vorher iround, und das
    // war die falsche Richtung: bei 804x1920 lieferte es 101 %, davon blieben 804/1.01 = 796
    // View-Einheiten Breite - unter den 800, die die Kappung gerade sichern sollte. Von 601
    // geprueften Breiten (800..1400) fielen so 296 durch.
    const auto ratio = PointF(renderSize_) / PointF(minUiSize);
    auto percent = static_cast<unsigned>(std::min(ratio.x, ratio.y) * 100.f); // truncate = floor
    // Der abgerundete Startwert wird nicht geglaubt, sondern nachgeprueft. Grund: die Kette
    // percent -> 1.f/(percent/100.f) -> Multiplikation -> Abschneiden laeuft komplett in float,
    // und eine Abweichung in der letzten Stelle entscheidet hier ueber 800 oder 799. Die
    // Schleife kostet nichts (sie laeuft, wenn ueberhaupt, einen Schritt) und macht die
    // Zusicherung unabhaengig davon, wie eine Plattform rundet.
    while(percent > 100u && !fits(percent))
        --percent;
    return percent;
}

GuiScaleRange VideoDriver::getGuiScaleRange() const
{
    constexpr auto min = 100u;
    const auto maxScaleXY = renderSize_ / PointF(800.f, 600.f);
    const auto maxScale = std::min(maxScaleXY.x, maxScaleXY.y);

    // Zwei Quellen fuer die Empfehlung, und die Reihenfolge ist die Zusicherung:
    // ohne gesetzte Referenzhoehe (Auslieferungszustand) wird EXAKT wie bisher gerechnet.
    unsigned recommended;
    if(uiReferenceHeight_ > 0)
    {
        recommended = std::max(min, helpers::iround<unsigned>(renderSize_.y * 100.f / uiReferenceHeight_));
        // Die Referenzhoehe kennt nur ZEILEN. Auf einem hochkanten Fenster (1080x1920) verlangt
        // sie 178 %, wovon die Renderflaeche 607 View-Einheiten breit wuerde - unter den 800,
        // auf die die Oberflaeche ausgelegt ist. Die Empfehlung wird deshalb auf das gekappt,
        // was in BEIDE Achsen NACHWEISLICH passt. Nur hier, nicht im DPI-Zweig: dort bliebe
        // sonst nicht "exakt wie bisher" stehen.
        recommended = std::min(recommended, std::max(min, largestScaleFittingTheUi()));
    } else
        recommended = std::max(min, helpers::iround<unsigned>(dpiScale_ * 100.f));

    // if the window shrinks below its minimum size of 800x600, max can be smaller than recommended
    // Bewusst weiter mit iround und nicht mit largestScaleFittingTheUi(): die OBERGRENZE der
    // Auswahlliste ist Auslieferungsverhalten und darf sich nicht verschieben.
    const auto max = std::max(helpers::iround<unsigned>(maxScale * 100.f), recommended);

    return GuiScaleRange{min, max, recommended};
}
