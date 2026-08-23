// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "WindowManager.h"
#include "RttrForeachPt.h"
#include "buildings/nobMilitary.h"
#include "desktops/PlayerView.h"
#include "desktops/dskGameInterface.h"
#include "factories/BuildingFactory.h"
#include "ingameWindows/IngameWindow.h"
#include "input/FocusPath.h"
#include "world/GameWorld.h"
#include "world/GameWorldViewer.h"
#include "world/MapBase.h"
#include "PadFixture.h"
#include "PointOutput.h"
#include "gameData/const_gui_ids.h"
#include "gameTypes/BuildingQuality.h"
#include "gameTypes/BuildingType.h"
#include "gameTypes/Nation.h"
#include <boost/test/unit_test.hpp>
#include <memory>

using rttr::test::PadViewFixture;

namespace {

/// Ein Punkt im Gebiet DIESES Spielers, auf dem ein Militaergebaeude Platz hat.
///
/// Bewusst ueber GetBQ(pt, player) gesucht und nicht geraten: dieselbe Pruefung, die auch das
/// Spiel anstellt, bevor dort gebaut werden darf.
MapPoint findOwnBuildingSpot(const GameWorld& world, const GameWorldViewer& viewer)
{
    RTTR_FOREACH_PT(MapPoint, world.GetSize())
    {
        if(!viewer.IsOwner(pt))
            continue;
        if(world.GetBQ(pt, static_cast<unsigned char>(viewer.GetPlayerId())) < BuildingQuality::Hut)
            continue;
        if(world.GetNO(pt)->GetType() != NodalObjectType::Nothing)
            continue;
        return pt;
    }
    return MapPoint::Invalid();
}

/// Setzt ein Militaergebaeude fuer diesen Spieler und liefert seinen Punkt.
MapPoint placeBarracksFor(GameWorld& world, const GameWorldViewer& viewer)
{
    const MapPoint pt = findOwnBuildingSpot(world, viewer);
    BOOST_TEST_REQUIRE(pt.isValid());
    BOOST_TEST_REQUIRE(BuildingFactory::CreateBuilding(world, BuildingType::Barracks, pt,
                                                       static_cast<unsigned char>(viewer.GetPlayerId()),
                                                       Nation::Romans)
                       != static_cast<noBuilding*>(nullptr));
    return pt;
}

unsigned buildingWndId(const MapPoint pt)
{
    return CGI_BUILDING + MapBase::CreateGUIID(pt);
}

} // namespace

BOOST_AUTO_TEST_SUITE(PadOpensWindowsTests)

/// DER fehlende Weg: ein Padspieler oeffnet das Fenster SEINES Gebaeudes.
///
/// Bis hierher konnte ein Padspieler ueberhaupt kein Fenster oeffnen - A setzte eine Flagge, Y
/// betrat ein vorhandenes. Der Fensterbesitz war damit gebaut, aber im Spiel nicht erreichbar:
/// jedes Fenster einer laufenden Partie trug Besitzer 0 oder SHARED_WINDOW_OWNER.
///
/// Gemessen wird ausdruecklich am PRODUKTIVEN Weg: der Test legt KEINE eigene Besitzklammer an.
/// Der Besitzer kann hier nur aus dskGameInterface::OnPadButton stammen.
BOOST_FIXTURE_TEST_CASE(APadPlayerOpensTheWindowOfHisOwnBuildingAndOwnsIt, PadViewFixture<2>)
{
    const MapPoint bldPt = placeBarracksFor(worldFixture.world, view(1).GetViewer());
    const unsigned wndId = buildingWndId(bldPt);
    BOOST_TEST_REQUIRE(WINDOWMANAGER.FindNonModalWindow(wndId, 1) == static_cast<IngameWindow*>(nullptr));

    // Pad 10 nimmt Ansicht 0, Pad 11 danach Ansicht 1 (Reihenfolge der Benutzung).
    pads.pickUp(10);
    step(16);
    aimPadAt(11, 1, bldPt);
    BOOST_TEST_REQUIRE(view(1).HasPadCursor());
    BOOST_TEST_REQUIRE((gwv(1).GetSelectedPt() == bldPt));

    pads.tap(11, PadButton::A);
    step(16);

    IngameWindow* wnd = WINDOWMANAGER.FindNonModalWindow(wndId, 1);
    BOOST_TEST_REQUIRE(wnd != static_cast<IngameWindow*>(nullptr));
    BOOST_TEST(wnd->GetOwner() == 1u);
    // ... und ausdruecklich NICHT der Hauptansicht.
    BOOST_TEST(WINDOWMANAGER.FindNonModalWindow(wndId, 0) == static_cast<IngameWindow*>(nullptr));

    // Solange die Welt noch lebt aufraeumen - die Fenster ueberleben sonst den WorldFixture.
    wnd->Close();
    WINDOWMANAGER.Draw();
}

/// Die Gegenprobe, ohne die die Aussage oben nichts wert waere: A wirkt auf den Knoten unter
/// SEINEM Zeiger. Zeigt Ansicht 1 auf das Gebaeude von Spieler 0, entsteht kein Fenster - dort
/// ist er nicht Eigentuemer.
BOOST_FIXTURE_TEST_CASE(APadPlayerDoesNotOpenTheWindowOfSomebodyElsesBuilding, PadViewFixture<2>)
{
    const MapPoint foreignPt = placeBarracksFor(worldFixture.world, view(0).GetViewer());
    const unsigned wndId = buildingWndId(foreignPt);

    pads.pickUp(10);
    step(16);
    aimPadAt(11, 1, foreignPt);
    BOOST_TEST_REQUIRE((gwv(1).GetSelectedPt() == foreignPt));

    pads.tap(11, PadButton::A);
    step(16);

    BOOST_TEST(WINDOWMANAGER.FindNonModalWindow(wndId, 1) == static_cast<IngameWindow*>(nullptr));
    BOOST_TEST(WINDOWMANAGER.GetTopMostWindow() == static_cast<IngameWindow*>(nullptr));
}

/// Die Y-VERSCHLECHTERUNG: EnterTopMostWindow filtert seit dem Fensterbesitz auf die eigene
/// Ansicht. Solange Ansicht 1 gar kein Fenster besitzen KONNTE, betrat Spieler 1 mit Y nur noch
/// besitzerlose Fenster - Y war fuer ihn faktisch wirkungslos.
///
/// Der Nachweis muss deshalb ueber den produktiven Weg laufen: Spieler 1 oeffnet sein Fenster
/// selbst (A) und betritt es dann (Y). Ein Test, der sein Fenster selbst mit einer Klammer
/// erzeugt, misst genau die Luecke NICHT.
BOOST_FIXTURE_TEST_CASE(YEntersTheWindowThePadPlayerJustOpenedHimself, PadViewFixture<2>)
{
    const MapPoint myPt = placeBarracksFor(worldFixture.world, view(1).GetViewer());
    const MapPoint foreignPt = placeBarracksFor(worldFixture.world, view(0).GetViewer());
    BOOST_TEST_REQUIRE((myPt != foreignPt));

    aimPadAt(10, 0, foreignPt);
    aimPadAt(11, 1, myPt);

    // Spieler 0 oeffnet zuerst SEIN Fenster - es liegt danach oben.
    pads.tap(10, PadButton::A);
    step(16);
    IngameWindow* wnd0 = WINDOWMANAGER.FindNonModalWindow(buildingWndId(foreignPt), 0);
    BOOST_TEST_REQUIRE(wnd0 != static_cast<IngameWindow*>(nullptr));

    // Dann Spieler 1 seines.
    pads.tap(11, PadButton::A);
    step(16);
    IngameWindow* wnd1 = WINDOWMANAGER.FindNonModalWindow(buildingWndId(myPt), 1);
    BOOST_TEST_REQUIRE(wnd1 != static_cast<IngameWindow*>(nullptr));

    // Y bringt Spieler 1 in SEIN Fenster.
    pads.tap(11, PadButton::Y);
    step(16);
    BOOST_TEST_REQUIRE(view(1).GetFocus().IsActive());
    BOOST_TEST(view(1).GetFocus().GetRoot() == static_cast<Window*>(wnd1));

    // Und Spieler 0 in seines - keiner landet im Fenster des anderen.
    pads.tap(10, PadButton::Y);
    step(16);
    BOOST_TEST_REQUIRE(view(0).GetFocus().IsActive());
    BOOST_TEST(view(0).GetFocus().GetRoot() == static_cast<Window*>(wnd0));

    wnd0->Close();
    wnd1->Close();
    WINDOWMANAGER.Draw();
}

/// Zweimal A auf dasselbe Gebaeude darf kein zweites Fenster erzeugen - genau wie der zweite
/// Mausklick des Mausspielers das vorhandene nur nach vorn holt.
BOOST_FIXTURE_TEST_CASE(PressingOpenTwiceKeepsExactlyOneWindow, PadViewFixture<2>)
{
    const MapPoint bldPt = placeBarracksFor(worldFixture.world, view(1).GetViewer());
    const unsigned wndId = buildingWndId(bldPt);

    pads.pickUp(10);
    step(16);
    aimPadAt(11, 1, bldPt);

    pads.tap(11, PadButton::A);
    step(16);
    IngameWindow* first = WINDOWMANAGER.FindNonModalWindow(wndId, 1);
    BOOST_TEST_REQUIRE(first != static_cast<IngameWindow*>(nullptr));

    pads.tap(11, PadButton::A);
    step(16);
    BOOST_TEST(WINDOWMANAGER.FindNonModalWindow(wndId, 1) == first);
    BOOST_TEST(!first->ShouldBeClosed());

    first->Close();
    WINDOWMANAGER.Draw();
}

BOOST_AUTO_TEST_SUITE_END()
