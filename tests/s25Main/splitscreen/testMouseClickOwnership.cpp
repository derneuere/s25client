// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

// BEFUND 2: der Mausklick wirkte auf den PADpunkt.
//
// dskGameInterface::ContextClick las durchgehend gwv.GetSelectedPt(), also den Zeiger der
// HAUPTansicht - und UpdateInput gibt einer Ansicht mit zugeordnetem Pad IMMER den Padzeiger.
// Hatte der Hauptspieler ein Pad in der Hand, baute ein Mausklick dort, wo die Maus NICHT war.
// Solange der Fehlschuss hoechstens ein Fenster oeffnete, war das laestig; mit dem Strassenbau
// am Pad schreibt der zweite Klick die Strasse ueber CommitRoad fest - ein echtes GameCommand.
//
// Die Loesung, die hier gemessen wird: der Mausklick wirkt auf die Ansicht, die den MAUSZEIGER
// haelt. Haelt keine Ansicht ihn (weil alle ein Pad haben), ist der Klick auf die Karte
// wirkungslos. Die Begruendung steht in dskGameInterface::ContextClick.

#include "GamePlayer.h"
#include "Loader.h"
#include "PointOutput.h"
#include "WindowManager.h"
#include "buildings/nobBaseWarehouse.h"
#include "desktops/PlayerView.h"
#include "driver/PadEvent.h"
#include "drivers/VideoDriverWrapper.h"
#include "nodeObjs/noFlag.h"
#include "world/GameWorld.h"
#include "PadFixture.h"
#include "ingameWindows/iwAction.h"
#include "NodalObjectTypes.h"
#include "gameTypes/RoadBuildMode.h"
#include "gameData/const_gui_ids.h"
#include <boost/test/unit_test.hpp>

using namespace rttr::test;

namespace {
/// Die HQ-Flagge eines Spielers - der einzige Punkt, von dem ohne weitere Annahme feststeht,
/// dass dort eine Flagge DIESES Spielers steht.
MapPoint hqFlagOf(const GameWorldBase& world, const unsigned char player)
{
    const MapPoint hqPos = world.GetPlayer(player).GetHQPos();
    const auto* hq = world.GetSpecObj<nobBaseWarehouse>(hqPos);
    BOOST_TEST_REQUIRE(hq != nullptr);
    return hq->GetFlagPos();
}
} // namespace

BOOST_AUTO_TEST_SUITE(MouseClickOwnershipTests)

// ============================================================================================
// 1. Der gemessene Fall aus dem Befund - jetzt als Nachweis
// ============================================================================================

/// Pad uebernimmt Ansicht 0, Baumodus an, Maus bewusst 120/90 Pixel neben dem Padzeiger, dann
/// ContextClick mit der MAUSposition. Vorher: route.size() == 1 und road.point == der Padpunkt.
BOOST_FIXTURE_TEST_CASE(AMouseClickOnAPadDrivenViewBuildsNothing, PadViewFixture<1>)
{
    const GameWorldBase& world = worldFixture.world;
    const RoadSpot spot = findRoadSpotFromHQ(view(0).GetViewer(), 2, 4);
    BOOST_TEST_REQUIRE(spot.isValid());
    BOOST_TEST_REQUIRE((spot.start == hqFlagOf(world, 0)));
    const std::vector<MapPoint> pts = roadPoints(world, spot.start, spot.route);

    // 1. Der Padspieler faengt auf seiner eigenen Flagge an - ueber den Padpfad.
    aimPadAt(10, 0, spot.start);
    press(10, padRoad::Begin);
    BOOST_TEST_REQUIRE((view(0).GetRoad().mode == RoadBuildMode::Normal));
    BOOST_TEST_REQUIRE(view(0).GetRoad().route.empty());

    // 2. Sein Zeiger steht jetzt auf dem NAECHSTEN Knoten der Strecke, aber er hat A noch nicht
    //    gedrueckt - es ist also noch nichts gebaut.
    aimAt(0, pts[1]);
    BOOST_TEST_REQUIRE(view(0).GetRoad().route.empty());

    // 3. Die Maus steht woanders. 120/90 Pixel - genau die Messung aus dem Befund.
    const Position padCursor = view(0).GetPadCursor();
    const Position mousePos = view(0).ClampToView(padCursor + Position(120, 90));
    BOOST_TEST_REQUIRE(view(0).ContainsViewPos(mousePos));
    BOOST_TEST_REQUIRE((mousePos != padCursor));
    step(16, mousePos);
    // Die Ansicht gehoert weiter dem Pad - das soll sich NICHT aendern.
    BOOST_TEST_REQUIRE(view(0).HasPadCursor());
    BOOST_TEST_REQUIRE((gwv(0).GetSelectedPt() == pts[1]));

    // 4. Der Klick. Er darf nichts bauen.
    const DrawPoint offsetBefore = gwv(0).GetOffset();
    dsk->ContextClick(MouseCoords(mousePos));

    BOOST_TEST_INFO("route nach dem Mausklick: " << view(0).GetRoad().route.size());
    BOOST_TEST(view(0).GetRoad().route.empty());
    BOOST_TEST((view(0).GetRoad().point == spot.start));
    BOOST_TEST(!viewerDrawsAnyOf(view(0).GetViewer(), spot.start, spot.route));
    // ... und der Klick verschiebt auch nichts anderes an dieser Ansicht.
    BOOST_TEST((gwv(0).GetOffset() == offsetBefore));
}

/// Dasselbe ausserhalb des Baumodus: der Klick oeffnet kein Fenster auf dem Padpunkt.
BOOST_FIXTURE_TEST_CASE(AMouseClickOnAPadDrivenViewOpensNothing, PadViewFixture<1>)
{
    pads.pickUp(10);
    step(0);
    BOOST_TEST_REQUIRE(view(0).HasPadCursor());
    const Position mousePos = view(0).ClampToView(view(0).GetPadCursor() + Position(120, 90));
    step(16, mousePos);

    const bool handled = dsk->ContextClick(MouseCoords(mousePos));
    BOOST_TEST(!handled);
    BOOST_TEST(view(0).actionwindow == nullptr);
}

// ============================================================================================
// 2. Der Mehransichtsfall: der Klick wirkt auf die Ansicht, die die MAUS haelt
// ============================================================================================

/// Ansicht 0 hat ein Pad, Ansicht 1 nicht. Die Maus steht ueber Ansicht 1 - dort und nur dort
/// darf der Klick wirken. Vorher wirkte er immer auf primary(), also auf die Ansicht mit dem
/// Pad, und zwar auf DEREN Padpunkt.
BOOST_FIXTURE_TEST_CASE(AMouseClickActsOnTheViewThatHoldsTheMouse, PadViewFixture<2>)
{
    pads.pickUp(10); // nimmt Ansicht 0
    step(0);
    BOOST_TEST_REQUIRE(view(0).HasPadCursor());
    BOOST_TEST_REQUIRE(!view(1).HasPadCursor());

    // Ein schlichter Knoten im Bild von Ansicht 1, weit genug vom HQ weg, damit dort kein
    // Gebaeude und keine Flagge steht - sonst oeffnete OpenObjectWindow ein Gebaeudefenster
    // statt eines Aktionsfensters, und die Messung traefe eine andere Aussage.
    const Position mousePos = view(1).GetViewCenter() + Position(140, -84);
    BOOST_TEST_REQUIRE(view(1).ContainsViewPos(mousePos));
    step(16, mousePos);
    const MapPoint mouseSel = gwv(1).GetSelectedPt();
    BOOST_TEST_REQUIRE(mouseSel.isValid());
    BOOST_TEST_REQUIRE((worldFixture.world.GetNO(mouseSel)->GetType() == NodalObjectType::Nothing));
    // Ansicht 0 zeigt woanders hin - sonst waere die Aussage unten nicht unterscheidbar.
    BOOST_TEST_REQUIRE((gwv(0).GetSelectedPt() != mouseSel));

    dsk->ContextClick(MouseCoords(mousePos));

    BOOST_TEST(view(1).actionwindow != nullptr);
    BOOST_TEST(view(0).actionwindow == nullptr);
    if(view(1).actionwindow)
        BOOST_TEST(view(1).actionwindow->GetOwner() == 1u);
}

// ============================================================================================
// 2b. BEFUND B: die Maus steht MITTEN IN einer Ansicht, die ein Pad haelt
// ============================================================================================

/// Gemessen war: zwei Ansichten, Pad auf Ansicht 0, Maus mitten in Ansicht 0. GetMouseView()
/// lieferte Ansicht 1 - den Rueckfall auf "die erste padlose Ansicht". Ansicht 1 bekam damit
/// cursorPos_ auf einem Punkt gesetzt, der ausserhalb IHRES Viewports liegt, UpdateSelection
/// waehlte daraus einen Randknoten, und ein echter Mausklick oeffnete dort ein Aktionsfenster -
/// auf einem Knoten, ueber dem die Maus sichtbar nicht steht.
///
/// Die Begruendung an GetMouseView() lautet "der Klick trifft die Ansicht, ueber der die Maus
/// steht". Fuer diesen Fall galt sie gerade NICHT.
///
/// Der Klick nimmt hier den ECHTEN Weg (Msg_LeftDown), nicht ContextClick direkt.
BOOST_FIXTURE_TEST_CASE(AMouseInsideAPadDrivenViewClicksNothingAtAll, PadViewFixture<2>)
{
    pads.pickUp(10); // Ansicht 0 nimmt ein Pad, Ansicht 1 bleibt padlos
    step(0);
    BOOST_TEST_REQUIRE(view(0).HasPadCursor());
    BOOST_TEST_REQUIRE(!view(1).HasPadCursor());

    // Die Maus steht MITTEN IN Ansicht 0 - und damit nachweislich nicht in Ansicht 1.
    const Position mousePos = view(0).GetViewCenter() + Position(11, -7);
    BOOST_TEST_REQUIRE(view(0).ContainsViewPos(mousePos));
    BOOST_TEST_REQUIRE(!view(1).ContainsViewPos(mousePos));
    step(16, mousePos);

    // 1. Es gibt keine Ansicht, die diesen Klick bekommen duerfte.
    BOOST_TEST(dsk->GetMouseView() == static_cast<PlayerView*>(nullptr));
    // 2. Der Nachbar bekommt KEINEN Zeiger, der ausserhalb seines eigenen Viewports liegt.
    BOOST_TEST(!gwv(1).GetCursorPos().has_value());
    BOOST_TEST(!gwv(1).GetSelectedPt().isValid());
    // 3. Und die padbesetzte Ansicht behaelt ihren Padzeiger - die Maus stiehlt ihn nicht.
    BOOST_TEST_REQUIRE(gwv(0).GetCursorPos().has_value());
    BOOST_TEST((*gwv(0).GetCursorPos() == view(0).GetPadCursor()));

    // 4. Der echte Klick oeffnet nirgends etwas.
    const bool handled = dsk->Msg_LeftDown(MouseCoords(mousePos));
    BOOST_TEST(!handled);
    BOOST_TEST(view(0).actionwindow == static_cast<iwAction*>(nullptr));
    BOOST_TEST(view(1).actionwindow == static_cast<iwAction*>(nullptr));
}

/// Die Invariante, aus der der Befund folgt: eine Ansicht bekommt nie einen Zeiger, der im
/// Viewport einer ANDEREN Ansicht liegt. Geprueft ueber die gesamte Renderflaeche.
///
/// Der Rueckfall auf die erste padlose Ansicht bleibt ausdruecklich bestehen, solange die Maus
/// ueber GAR KEINER Ansicht steht (also ausserhalb der Renderflaeche): daran haengt der
/// Einzelspielernachweis SingleViewFollowsTheMouseWhetherOrNotAPadIsPlugged, und dort gibt es
/// keine fremde Ansicht, deren Punkt faelschlich getroffen werden koennte.
BOOST_FIXTURE_TEST_CASE(NoViewEverHoldsACursorThatLiesInsideAnotherViewsViewport, PadViewFixture<2>)
{
    pads.pickUp(10);
    step(0);
    BOOST_TEST_REQUIRE(view(0).HasPadCursor());

    const Extent renderSize = VIDEODRIVER.GetRenderSize();
    unsigned violations = 0;
    Position firstBad(-1, -1);
    for(unsigned y = 0; y < renderSize.y; y += 37)
    {
        for(unsigned x = 0; x < renderSize.x; x += 41)
        {
            const Position p(static_cast<int>(x), static_cast<int>(y));
            step(16, p);
            for(unsigned i = 0; i < 2u; ++i)
            {
                const auto& cursor = gwv(i).GetCursorPos();
                if(!cursor)
                    continue;
                if(view(1u - i).ContainsViewPos(*cursor))
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

// ============================================================================================
// 3. Die harte Randbedingung: OHNE Pad aendert sich am Mausspieler nichts
// ============================================================================================

/// Einzelspieler mit Maus, kein Pad im Spiel: der Klick baut wie immer. Dieser Fall war vorher
/// gruen und muss es bleiben.
BOOST_FIXTURE_TEST_CASE(WithoutAnyPadTheMouseClickBuildsExactlyAsBefore, PadViewFixture<1>)
{
    const GameWorldBase& world = worldFixture.world;
    const RoadSpot spot = findRoadSpotFromHQ(view(0).GetViewer(), 2, 4);
    BOOST_TEST_REQUIRE(spot.isValid());
    const std::vector<MapPoint> pts = roadPoints(world, spot.start, spot.route);

    dsk->GI_StartRoadBuilding(spot.start, false);
    BOOST_TEST_REQUIRE((view(0).GetRoad().mode == RoadBuildMode::Normal));

    const Position mousePos = nodeViewPos(0, spot.end);
    BOOST_TEST_REQUIRE(view(0).ContainsViewPos(mousePos));
    step(16, mousePos);
    BOOST_TEST_REQUIRE(!view(0).HasPadCursor());
    BOOST_TEST_REQUIRE((gwv(0).GetSelectedPt() == spot.end));

    BOOST_TEST(dsk->ContextClick(MouseCoords(mousePos)));
    BOOST_TEST((view(0).GetRoad().route == spot.route), boost::test_tools::per_element());
    BOOST_TEST((view(0).GetRoad().point == spot.end));
    BOOST_TEST(viewerDrawsRoad(view(0).GetViewer(), spot.start, spot.route));
}

/// Und ein Pad, das bloss ANGESTECKT ist, aendert daran nichts - es hat keine Ansicht.
BOOST_FIXTURE_TEST_CASE(AnIdlePluggedPadLeavesTheMouseClickAlone, PadViewFixture<1>)
{
    pads.connect(10);
    step(16);
    BOOST_TEST_REQUIRE(!view(0).HasPadCursor());

    const RoadSpot spot = findRoadSpotFromHQ(view(0).GetViewer(), 2, 4);
    BOOST_TEST_REQUIRE(spot.isValid());
    dsk->GI_StartRoadBuilding(spot.start, false);

    const Position mousePos = nodeViewPos(0, spot.end);
    step(16, mousePos);
    BOOST_TEST_REQUIRE((gwv(0).GetSelectedPt() == spot.end));
    BOOST_TEST(dsk->ContextClick(MouseCoords(mousePos)));
    BOOST_TEST((view(0).GetRoad().route == spot.route), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_SUITE_END()
