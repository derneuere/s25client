// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

// Mauseingaben muessen auf die Ansicht wirken, UEBER DER DIE MAUS STEHT.
//
// Diese Datei fasst die drei Stellen zusammen, an denen das zuletzt noch nicht galt:
//
//  BEFUND 1 - Die Zeigerregel in dskGameInterface::UpdateInput fiel auf "die erste padlose
//             Ansicht" zurueck, sobald die Maus ueber GAR KEINER Ansicht stand. Die Begruendung
//             dafuer war "die Viewports decken die Renderflaeche lueckenlos ab, also heisst das
//             ausserhalb des Bildschirms". Bei DREI Ansichten stimmte das nicht: das
//             Quadrantenlayout liess die vierte Zelle frei, und die liegt MITTEN AUF DEM
//             BILDSCHIRM. Dort griff der Rueckfall, und ein Klick wirkte auf eine Ansicht, ueber
//             der die Maus nicht stand.
//
//  BEFUND 2 - Mausrad (WheelZoom) und Kartenzug (Msg_RightDown/Msg_MouseMove) lasen gwv, also
//             primary(). Stand die Maus ueber Ansicht 1, zoomte und scrollte Ansicht 0.
//
// Alle Nachweise nehmen die produktiven Eingaenge: dskGameInterface::Msg_LeftDown,
// Msg_WheelUp/-Down, Msg_RightDown, Msg_MouseMove - genau die Methoden, die der WindowManager
// beim Weiterreichen eines Treiberereignisses aufruft (WindowManager::RelayMouseMessage).

#include "PointOutput.h"
#include "desktops/PlayerView.h"
#include "drivers/VideoDriverWrapper.h"
#include "world/GameWorldView.h"
#include "world/ViewportLayout.h"
#include "PadFixture.h"
#include "Settings.h"
#include "ingameWindows/iwAction.h"
#include <boost/test/unit_test.hpp>

using namespace rttr::test;

namespace {
/// Setzt den Kartenzug-Modus fuer die Dauer eines Nachweises und stellt ihn danach zurueck.
/// Beide Modi nehmen im Produktivcode UNTERSCHIEDLICHE Zweige (GrabAndDrag rechnet ueber
/// ViewPosToMap, ScrollOpposite/ScrollSame rechnet in Bildschirmpixeln) - beide lasen bisher
/// primary(), beide muessen umgestellt werden, also wird auch beides gemessen.
struct ScopedScrollMode
{
    MapScrollMode old_;
    explicit ScopedScrollMode(MapScrollMode mode) : old_(SETTINGS.interface.mapScrollMode)
    {
        SETTINGS.interface.mapScrollMode = mode;
    }
    ~ScopedScrollMode() { SETTINGS.interface.mapScrollMode = old_; }
};
} // namespace

BOOST_AUTO_TEST_SUITE(MouseViewTargetTests)

// ============================================================================================
// BEFUND 1 - die Luecke im Dreier-Layout
// ============================================================================================

/// Die reine Aufteilungsfunktion, ohne Videotreiber und ohne Spiel.
///
/// Die Renderflaeche muss bei JEDER Ansichtszahl lueckenlos abgedeckt sein. Genau darauf beruft
/// sich die Zeigerregel in UpdateInput; solange bei drei Ansichten eine Zelle frei bleibt, ist
/// die Berufung falsch - und zwar an einer Stelle mitten auf dem Bildschirm, die der Spieler
/// jederzeit mit der Maus erreicht.
BOOST_AUTO_TEST_CASE(ViewportsCoverTheWholeRenderAreaForEveryViewCount)
{
    for(const Extent renderSize : {Extent(1920, 1080), Extent(800, 600), Extent(1281, 801), Extent(600, 1000)})
    {
        BOOST_TEST_CONTEXT("renderSize " << renderSize.x << "x" << renderSize.y)
        {
            for(const unsigned numViews : {1u, 2u, 3u, 4u})
            {
                BOOST_TEST_CONTEXT("numViews " << numViews)
                {
                    const std::vector<Viewport> vps = CalcViewports(renderSize, numViews);
                    BOOST_TEST_REQUIRE(vps.size() == numViews);
                    // Punktweise: jeder Pixel der Renderflaeche gehoert GENAU EINER Ansicht.
                    // Punktweise und nicht ueber die Flaechensumme, weil erst das die Luecke
                    // auch ORTET statt sie nur zu zaehlen.
                    unsigned uncovered = 0;
                    Position firstUncovered(-1, -1);
                    for(unsigned y = 0; y < renderSize.y; y += 7)
                    {
                        for(unsigned x = 0; x < renderSize.x; x += 11)
                        {
                            const Position p(static_cast<int>(x), static_cast<int>(y));
                            unsigned owners = 0;
                            for(const Viewport& vp : vps)
                            {
                                if(p.x >= vp.origin.x && p.y >= vp.origin.y
                                   && p.x < vp.origin.x + static_cast<int>(vp.size.x)
                                   && p.y < vp.origin.y + static_cast<int>(vp.size.y))
                                    ++owners;
                            }
                            if(owners != 1u)
                            {
                                if(uncovered == 0)
                                    firstUncovered = p;
                                ++uncovered;
                            }
                        }
                    }
                    BOOST_TEST_INFO("erster nicht genau einfach abgedeckter Punkt " << firstUncovered);
                    BOOST_TEST(uncovered == 0u);
                }
            }
        }
    }
}

/// Dieselbe Aussage am laufenden Desktop, ueber die ECHTE Zeigerzuordnung: keine Ansicht darf
/// je einen Zeiger bekommen, der ausserhalb ihres eigenen Viewports liegt. Geprueft ueber die
/// gesamte Renderflaeche, mit drei Ansichten und ohne jedes Pad.
///
/// Vorher rot in der freien vierten Zelle: dort stand die Maus ueber keiner Ansicht, der
/// Rueckfall gab sie Ansicht 0, und Ansicht 0 bekam einen Zeiger mitten im Bild des Nachbarn.
BOOST_FIXTURE_TEST_CASE(WithThreeViewsNoViewEverHoldsACursorOutsideItsOwnViewport, PadViewFixture<3>)
{
    const Extent renderSize = VIDEODRIVER.GetRenderSize();
    unsigned violations = 0;
    Position firstBad(-1, -1);
    for(unsigned y = 0; y < renderSize.y; y += 13)
    {
        for(unsigned x = 0; x < renderSize.x; x += 17)
        {
            const Position p(static_cast<int>(x), static_cast<int>(y));
            step(16, p);
            for(unsigned i = 0; i < 3u; ++i)
            {
                const auto& cursor = gwv(i).GetCursorPos();
                if(!cursor)
                    continue;
                if(!view(i).ContainsViewPos(*cursor))
                {
                    if(violations == 0)
                        firstBad = p;
                    ++violations;
                }
            }
        }
    }
    BOOST_TEST_INFO("erste Verletzung bei Mausposition " << firstBad);
    BOOST_TEST(violations == 0u);
}

/// Der greifbare Schaden: ein ECHTER Klick in die Bildmitte. Er darf kein Fenster in einer
/// Ansicht oeffnen, ueber der die Maus nicht steht.
///
/// 800x600, drei Ansichten: die freie Zelle des Quadrantenlayouts war (400,300)-(800,600). Ein
/// Klick auf (600,450) oeffnete ein Aktionsfenster in Ansicht 0 - deren Viewport bei
/// (0,0)-(400,300) liegt.
BOOST_FIXTURE_TEST_CASE(AClickInTheMiddleOfAThreeViewScreenOnlyEverHitsTheViewUnderIt, PadViewFixture<3>)
{
    const Extent renderSize = VIDEODRIVER.GetRenderSize();
    const Position mousePos(static_cast<int>(renderSize.x) * 3 / 4, static_cast<int>(renderSize.y) * 3 / 4);
    step(16, mousePos);

    dsk->Msg_LeftDown(MouseCoords(mousePos));

    for(unsigned i = 0; i < 3u; ++i)
    {
        BOOST_TEST_CONTEXT("Ansicht " << i)
        {
            if(view(i).actionwindow != nullptr)
                BOOST_TEST(view(i).ContainsViewPos(mousePos));
        }
    }
}

/// Die harte Randbedingung der letzten Runde, ausdruecklich festgenagelt: steht die Maus
/// AUSSERHALB der Renderflaeche, bleibt der Rueckfall auf die erste padlose Ansicht bestehen.
/// Daran haengt der Einzelspieler (SingleViewFollowsTheMouseWhetherOrNotAPadIsPlugged), und hier
/// wird gezeigt, dass er auch mit drei Ansichten steht.
BOOST_FIXTURE_TEST_CASE(AMouseOutsideTheRenderAreaStillFallsBackToTheFirstPadlessView, PadViewFixture<3>)
{
    const Extent renderSize = VIDEODRIVER.GetRenderSize();
    for(const Position p : {Position(-500, -500), Position(static_cast<int>(renderSize.x) + 99, 42),
                            Position(17, static_cast<int>(renderSize.y) + 5)})
    {
        BOOST_TEST_INFO("Maus bei " << p);
        step(16, p);
        BOOST_TEST(dsk->GetMouseView() == &view(0));
        BOOST_TEST_REQUIRE(gwv(0).GetCursorPos().has_value());
        BOOST_TEST((*gwv(0).GetCursorPos() == p));
    }
}

// ============================================================================================
// BEFUND 2 - Mausrad und Kartenzug
// ============================================================================================

/// Das Rad zoomt die Ansicht unter der Maus. Vorher zoomte es immer primary().
BOOST_FIXTURE_TEST_CASE(TheWheelZoomsTheViewUnderTheMouse, PadViewFixture<2>)
{
    const Position mousePos = view(1).GetViewCenter();
    BOOST_TEST_REQUIRE(view(1).ContainsViewPos(mousePos));
    BOOST_TEST_REQUIRE(!view(0).ContainsViewPos(mousePos));
    step(16, mousePos);

    const float zoom0 = gwv(0).GetCurrentTargetZoomFactor();
    const float zoom1 = gwv(1).GetCurrentTargetZoomFactor();

    BOOST_TEST(dsk->Msg_WheelUp(MouseCoords(mousePos)));
    BOOST_TEST_INFO("Ansicht 0: " << zoom0 << " -> " << gwv(0).GetCurrentTargetZoomFactor());
    BOOST_TEST_INFO("Ansicht 1: " << zoom1 << " -> " << gwv(1).GetCurrentTargetZoomFactor());
    BOOST_TEST(gwv(0).GetCurrentTargetZoomFactor() == zoom0);
    BOOST_TEST(gwv(1).GetCurrentTargetZoomFactor() != zoom1);

    // und wieder heraus, ebenfalls nur dort
    const float zoomedIn = gwv(1).GetCurrentTargetZoomFactor();
    BOOST_TEST(dsk->Msg_WheelDown(MouseCoords(mousePos)));
    BOOST_TEST(gwv(0).GetCurrentTargetZoomFactor() == zoom0);
    BOOST_TEST(gwv(1).GetCurrentTargetZoomFactor() != zoomedIn);
}

/// Das Rad zoomt auch eine PADGESTEUERTE Ansicht, solange die Maus ueber ihr steht - und
/// wieder ausdruecklich nicht primary().
///
/// Das ist bewusst NICHT dieselbe Regel wie beim Klick
/// (AMouseInsideAPadDrivenViewClicksNothingAtAll, wo gar nichts passiert). Der Klick waehlt
/// einen KNOTEN aus und handelt auf ihm; steht der Zeiger der Ansicht beim Pad, waere das ein
/// Knoten, den mit der Maus niemand gemeint hat. Das Rad waehlt keinen Knoten aus, es
/// skaliert nur ein Bild. Bindet man beides an dieselbe Padpruefung, verliert der einzelne
/// Spieler mit Pad UND Maus sein Mausrad, sobald er das Pad anfasst - der Regelfall am
/// Fernseher. Siehe dskGameInterface::CameraViewUnderMouse.
BOOST_FIXTURE_TEST_CASE(TheWheelZoomsThePadDrivenViewUnderTheMouseAndNotTheMainView, PadViewFixture<2>)
{
    pads.pickUp(10);
    step(0);
    // Das Pad nimmt Slot 0. Fuer diesen Nachweis muss es die Ansicht sein, ueber der die Maus
    // steht - und die darf nicht primary() sein, sonst ist die Aussage nicht unterscheidbar.
    // Also: zweites Pad auf Ansicht 1.
    pads.pickUp(11);
    step(0);
    BOOST_TEST_REQUIRE(view(1).HasPadCursor());

    const Position mousePos = view(1).GetViewCenter();
    BOOST_TEST_REQUIRE(view(1).ContainsViewPos(mousePos));
    step(16, mousePos);
    // Kein Zeiger fuer die Maus - beide Ansichten haben ein Pad. Das Rad wirkt trotzdem.
    BOOST_TEST_REQUIRE(dsk->GetMouseView() == static_cast<PlayerView*>(nullptr));

    const float zoom0 = gwv(0).GetCurrentTargetZoomFactor();
    const float zoom1 = gwv(1).GetCurrentTargetZoomFactor();

    dsk->Msg_WheelUp(MouseCoords(mousePos));
    BOOST_TEST_INFO("Ansicht 0: " << zoom0 << " -> " << gwv(0).GetCurrentTargetZoomFactor());
    BOOST_TEST_INFO("Ansicht 1: " << zoom1 << " -> " << gwv(1).GetCurrentTargetZoomFactor());
    BOOST_TEST(gwv(0).GetCurrentTargetZoomFactor() == zoom0);
    BOOST_TEST(gwv(1).GetCurrentTargetZoomFactor() != zoom1);
}

/// Die Randbedingung dazu, ausdruecklich festgenagelt: EIN lokaler Spieler mit Pad UND Maus
/// behaelt Rad und Kartenzug, auch waehrend das Pad seine Ansicht steuert.
BOOST_FIXTURE_TEST_CASE(ASingleViewWithAPadKeepsWheelAndDrag, PadViewFixture<1>)
{
    pads.pickUp(10);
    step(0);
    BOOST_TEST_REQUIRE(view(0).HasPadCursor());

    const Position mousePos = view(0).GetViewCenter();
    step(16, mousePos);

    const float zoom = gwv(0).GetCurrentTargetZoomFactor();
    BOOST_TEST(dsk->Msg_WheelUp(MouseCoords(mousePos)));
    BOOST_TEST(gwv(0).GetCurrentTargetZoomFactor() != zoom);

    ScopedScrollMode scoped(MapScrollMode::ScrollOpposite);
    const DrawPoint offset = gwv(0).GetOffset();
    dsk->Msg_RightDown(MouseCoords(mousePos));
    dsk->Msg_MouseMove(MouseCoords(mousePos + Position(40, 25)));
    dsk->Msg_RightUp(MouseCoords(mousePos + Position(40, 25)));
    BOOST_TEST((gwv(0).GetOffset() != offset));
}

/// Der Kartenzug mit der rechten Taste verschiebt die Ansicht unter der Maus - in BEIDEN
/// Scrollmodi. Vorher verschob er immer primary().
BOOST_FIXTURE_TEST_CASE(DraggingTheMapMovesTheViewUnderTheMouse, PadViewFixture<2>)
{
    for(const MapScrollMode mode : {MapScrollMode::ScrollOpposite, MapScrollMode::GrabAndDrag})
    {
        BOOST_TEST_CONTEXT("Scrollmodus " << static_cast<int>(mode))
        {
            ScopedScrollMode scoped(mode);
            const Position start = view(1).GetViewCenter();
            BOOST_TEST_REQUIRE(view(1).ContainsViewPos(start));
            step(16, start);

            const DrawPoint offset0 = gwv(0).GetOffset();
            const DrawPoint offset1 = gwv(1).GetOffset();

            dsk->Msg_RightDown(MouseCoords(start));
            dsk->Msg_MouseMove(MouseCoords(start + Position(40, 25)));
            dsk->Msg_RightUp(MouseCoords(start + Position(40, 25)));

            BOOST_TEST_INFO("Ansicht 0 " << offset0 << " -> " << gwv(0).GetOffset());
            BOOST_TEST_INFO("Ansicht 1 " << offset1 << " -> " << gwv(1).GetOffset());
            BOOST_TEST((gwv(0).GetOffset() == offset0));
            BOOST_TEST((gwv(1).GetOffset() != offset1));
        }
    }
}

/// Ein laufender Zug bleibt bei der Ansicht, in der er ANGEFANGEN hat - auch wenn der Zeiger
/// dabei ueber die Viewportgrenze wandert. Sonst risse ein Zug ueber die Bildmitte die Karte
/// des Nachbarn mit.
BOOST_FIXTURE_TEST_CASE(ADragStaysWithTheViewItStartedIn, PadViewFixture<2>)
{
    ScopedScrollMode scoped(MapScrollMode::ScrollOpposite);
    const Position start = view(1).GetViewCenter();
    step(16, start);
    const DrawPoint offset0 = gwv(0).GetOffset();

    dsk->Msg_RightDown(MouseCoords(start));
    // Mitten im Zug ueber die Grenze in Ansicht 0 hinein
    const Position acrossTheBorder = view(0).GetViewCenter();
    BOOST_TEST_REQUIRE(view(0).ContainsViewPos(acrossTheBorder));
    dsk->Msg_MouseMove(MouseCoords(acrossTheBorder));
    dsk->Msg_RightUp(MouseCoords(acrossTheBorder));

    BOOST_TEST_INFO("Ansicht 0 " << offset0 << " -> " << gwv(0).GetOffset());
    BOOST_TEST((gwv(0).GetOffset() == offset0));
}

// ============================================================================================
// Die harte Randbedingung: der Einzelspieler bleibt exakt, wie er ist
// ============================================================================================

/// Eine Ansicht, kein Pad: Rad und Kartenzug wirken wie immer auf sie - egal, wo die Maus steht,
/// auch ausserhalb der Renderflaeche.
BOOST_FIXTURE_TEST_CASE(ASingleViewStillZoomsAndScrollsFromAnyMousePosition, PadViewFixture<1>)
{
    const Extent renderSize = VIDEODRIVER.GetRenderSize();
    for(const Position mousePos :
        {Position(static_cast<int>(renderSize.x) / 2, static_cast<int>(renderSize.y) / 2), Position(0, 0),
         Position(-500, -500), Position(static_cast<int>(renderSize.x) + 99, 42)})
    {
        BOOST_TEST_CONTEXT("Maus bei " << mousePos)
        {
            step(16, mousePos);
            const float zoom = gwv(0).GetCurrentTargetZoomFactor();
            BOOST_TEST(dsk->Msg_WheelUp(MouseCoords(mousePos)));
            BOOST_TEST(gwv(0).GetCurrentTargetZoomFactor() != zoom);
            gwv(0).SetZoomFactor(zoom, false);

            for(const MapScrollMode mode : {MapScrollMode::ScrollOpposite, MapScrollMode::GrabAndDrag})
            {
                ScopedScrollMode scoped(mode);
                const DrawPoint offset = gwv(0).GetOffset();
                dsk->Msg_RightDown(MouseCoords(mousePos));
                dsk->Msg_MouseMove(MouseCoords(mousePos + Position(40, 25)));
                dsk->Msg_RightUp(MouseCoords(mousePos + Position(40, 25)));
                BOOST_TEST((gwv(0).GetOffset() != offset));
            }
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
