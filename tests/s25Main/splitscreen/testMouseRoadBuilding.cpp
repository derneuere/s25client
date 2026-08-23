// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

// Die harte Randbedingung dieser Phase: der Strassenbau des MAUSSPIELERS bleibt exakt der von
// heute. Der Umbau hat den Strassenbau von der Uebergangsreferenz `road` (= primary()) auf die
// handelnde Ansicht umgestellt; die spielerlosen Signaturen GI_StartRoadBuilding /
// BuildRoadPart / GetIdInCurBuildRoad / DemolishRoad / GI_CancelRoadBuilding sind seitdem die
// MAUSfassungen und muessen weiter auf primary() wirken.
//
// Genau diese Kette misst hier - und zwar in den Faellen, die die frueheren Runden
// durchrutschen liessen: nicht "eine Ansicht ohne Pad", sondern eine Ansicht MIT Pad, und
// nicht nur der Einzelspieler, sondern 1, 2 und 4 Ansichten als Datenpunkte derselben Aussage.
//
// Der Bestandswaechter ausserhalb dieser Datei ist BQWithVisualRoad
// (tests/s25Main/integration/testBuilding.cpp:217): er ruft dieselbe Kette und bleibt
// unveraendert stehen.

#include "GamePlayer.h"
#include "Loader.h"
#include "PointOutput.h"
#include "WindowManager.h"
#include "buildings/nobBaseWarehouse.h"
#include "desktops/PlayerView.h"
#include "driver/PadEvent.h"
#include "drivers/VideoDriverWrapper.h"
#include "nodeObjs/noFlag.h"
#include "pathfinding/FindPathForRoad.h"
#include "world/GameWorld.h"
#include "PadFixture.h"
#include "PadGameFixture.h"
#include "gameTypes/BuildingQuality.h"
#include "gameTypes/RoadBuildMode.h"
#include "gameData/const_gui_ids.h"
#include <boost/test/unit_test.hpp>
#include <vector>

using namespace rttr::test;

namespace {

/// Die vollstaendige Mauskette auf der Hauptansicht, mit allen Zwischenmessungen, die
/// BQWithVisualRoad auch macht - nur eben so verpackt, dass sie in beliebigem Padzustand und
/// bei beliebiger Ansichtszahl wiederholbar ist.
///
/// Gerufen werden ausschliesslich die SPIELERLOSEN Signaturen. Das ist hier kein Verstoss gegen
/// "nur ueber den Produktivweg", sondern der Punkt: sie sind der Mauspfad, und dass sie
/// unveraendert auf primary() wirken, ist die zu pruefende Aussage.
template<class T_Fixture>
void runMouseRoadChain(T_Fixture& f)
{
    dskGameInterface& dsk = *f.dsk;
    const GameWorldBase& world = f.worldFixture.world;
    PlayerView& main = f.view(0);
    const GameWorldViewer& gwv = main.GetViewer();

    const RoadSpot spot = findRoadSpotFromHQ(gwv, 4, 6);
    BOOST_TEST_REQUIRE(spot.isValid());
    const std::vector<MapPoint> pts = roadPoints(world, spot.start, spot.route);

    // Ausgangswerte der Bauqualitaet UND der gezeichneten Wege im Umfeld. Verglichen wird am
    // Ende gegen diese Aufnahme und nicht gegen absolute Werte: die Startflagge ist die Flagge
    // des HQ und haengt schon vor jedem Strassenbau an einem Wegstueck zum Gebaeude.
    const std::vector<MapPoint> around = world.GetPointsInRadiusWithCenter(spot.start, 8);
    std::vector<BuildingQuality> bqBefore;
    std::vector<bool> onRoadBefore;
    for(const MapPoint& pt : around)
    {
        bqBefore.push_back(gwv.GetBQ(pt));
        onRoadBefore.push_back(gwv.IsOnRoad(pt));
    }

    // 1. Baumodus an
    dsk.GI_StartRoadBuilding(spot.start, false);
    BOOST_TEST_REQUIRE((dsk.GetRoadMode() == RoadBuildMode::Normal));
    BOOST_TEST((main.GetRoad().start == spot.start));

    // 2. Den ganzen Weg in einem Zug - wie ein Mausklick auf den Zielknoten
    MapPoint target = spot.end;
    BOOST_TEST_REQUIRE(dsk.BuildRoadPart(target));
    BOOST_TEST((target == spot.end));
    BOOST_TEST((main.GetRoad().route == spot.route), boost::test_tools::per_element());
    BOOST_TEST(viewerDrawsRoad(gwv, spot.start, spot.route));
    for(const MapPoint& pt : pts)
        BOOST_TEST(gwv.IsOnRoad(pt));
    // Auf dem laufenden Weg ist hoechstens noch eine Flagge moeglich - ein Gebaeude nie.
    for(const MapPoint& pt : pts)
        BOOST_TEST_CONTEXT("bei " << pt)
        BOOST_TEST((gwv.GetBQ(pt) == BuildingQuality::Nothing || gwv.GetBQ(pt) == BuildingQuality::Flag));

    // 3. Die Nummer im laufenden Weg - der erste Punkt nach dem Start hat die 2
    BOOST_TEST(dsk.GetIdInCurBuildRoad(pts[1]) == 2u);
    BOOST_TEST(dsk.GetIdInCurBuildRoad(spot.start) == 1u);

    // 4. Teilrueckbau bis dorthin
    dsk.DemolishRoad(2);
    BOOST_TEST(gwv.IsOnRoad(pts[0]));
    BOOST_TEST(gwv.IsOnRoad(pts[1]));
    for(unsigned i = 2; i < pts.size(); ++i)
        BOOST_TEST(!gwv.IsOnRoad(pts[i]));
    BOOST_TEST(main.GetRoad().route.size() == 1u);

    // 5. Abbruch - und Bauqualitaet wie Wegbild sind vollstaendig wiederhergestellt
    dsk.GI_CancelRoadBuilding();
    BOOST_TEST((dsk.GetRoadMode() == RoadBuildMode::Disabled));
    BOOST_TEST(!viewerDrawsAnyOf(gwv, spot.start, spot.route));
    for(unsigned i = 0; i < around.size(); ++i)
        BOOST_TEST_CONTEXT("bei " << around[i])
        {
            BOOST_TEST((gwv.GetBQ(around[i]) == bqBefore[i]));
            BOOST_TEST(gwv.IsOnRoad(around[i]) == onRoadBefore[i]);
        }
}

} // namespace

BOOST_AUTO_TEST_SUITE(MouseRoadBuildingTests)

/// Der Name schliesst den kritischen Fall EIN: "whatever the pad state" heisst auch "mit einem
/// Pad, das diese Ansicht wirklich uebernommen hat". Genau der Fall ist frueher schon einmal
/// durchgerutscht, weil ein Testname ihn ausgeschlossen hatte
/// (SingleViewWithoutPadsBehavesExactlyAsBefore).
BOOST_FIXTURE_TEST_CASE(MouseRoadBuildingIsUnchangedWhateverThePadState, PadViewFixture<1>)
{
    struct Case
    {
        const char* name;
        bool connect;
        bool use;
    };
    for(const Case& c : {Case{"keinPad", false, false}, Case{"padGesteckt", true, false},
                         Case{"padBenutzt", true, true}})
    {
        BOOST_TEST_CONTEXT(c.name)
        {
            if(c.connect)
                pads.connect(10);
            if(c.use)
                pads.tap(10, PadButton::Start);
            step(16);
            BOOST_TEST_REQUIRE(view(0).HasPadCursor() == c.use);

            runMouseRoadChain(*this);
        }
    }
}

/// Der Einzelspieler ist ein Datenpunkt, kein Sonderfall: dieselbe Kette bei 1, 2 und 4
/// Ansichten. Faende der Umbau die Hauptansicht nur deshalb richtig, weil es genau eine gibt,
/// fiele das hier auf.
BOOST_FIXTURE_TEST_CASE(MouseRoadBuildingIsUnchangedWithOneView, PadViewFixture<1>)
{
    runMouseRoadChain(*this);
}
BOOST_FIXTURE_TEST_CASE(MouseRoadBuildingIsUnchangedWithTwoViews, PadViewFixture<2>)
{
    runMouseRoadChain(*this);
    // Die zweite Ansicht ist durch den Mausbau nicht angefasst worden.
    BOOST_TEST((view(1).GetRoad().mode == RoadBuildMode::Disabled));
    BOOST_TEST(view(1).GetRoad().route.empty());
}
BOOST_FIXTURE_TEST_CASE(MouseRoadBuildingIsUnchangedWithFourViews, PadViewFixture<4>)
{
    runMouseRoadChain(*this);
    for(unsigned v = 1; v < 4; ++v)
        BOOST_TEST((view(v).GetRoad().mode == RoadBuildMode::Disabled));
}

/// Der Mausspieler steht MITTEN im Baumodus, waehrend ein Padspieler in Ansicht 1 eine ganze
/// Strasse baut und wieder abbricht. Danach muss der Zustand des Mausspielers bitgleich sein.
///
/// Die Zuordnung des Pads auf Ansicht 1 laeuft ueber PadRouter::AssignSlot - die Produktivstelle
/// fuer eine feste Zuordnung. Damit bleibt Ansicht 0 padlos und ist wirklich der Mausspieler.
BOOST_FIXTURE_TEST_CASE(TheMousePlayersRoadIsUnaffectedWhileAPadPlayerBuildsHisOwn, PadViewFixture<2>)
{
    const GameWorldBase& world = worldFixture.world;
    const RoadSpot sMouse = findRoadSpotFromHQ(view(0).GetViewer(), 3, 6);
    const RoadSpot sPad = findRoadSpotFromHQ(view(1).GetViewer(), 3, 6);
    BOOST_TEST_REQUIRE(sMouse.isValid());
    BOOST_TEST_REQUIRE(sPad.isValid());
    const std::vector<MapPoint> ptsMouse = roadPoints(world, sMouse.start, sMouse.route);
    const std::vector<MapPoint> ptsPad = roadPoints(world, sPad.start, sPad.route);

    // Pad 11 fest auf Ansicht 1. Ansicht 0 bleibt ohne Pad - der Mausspieler.
    pads.connect(11);
    step(16);
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(11, 1));
    step(16);
    BOOST_TEST_REQUIRE(!view(0).HasPadCursor());
    BOOST_TEST_REQUIRE(view(1).HasPadCursor());
    // Zielen ueber den Padpfad - AssignSlot hat den Zeiger bereits gesetzt, gefahren wird ab
    // hier nur noch mit den Sticks.
    padSteerTo(11, 1, sPad.start);
    BOOST_TEST_REQUIRE((gwv(1).GetSelectedPt() == sPad.start));

    // Der Mausspieler steht mitten im Bau.
    dsk->GI_StartRoadBuilding(sMouse.start, false);
    MapPoint target = ptsMouse[2];
    BOOST_TEST_REQUIRE(dsk->BuildRoadPart(target));
    const auto mouseRouteBefore = view(0).GetRoad().route;
    const MapPoint mousePointBefore = view(0).GetRoad().point;
    const MapPoint mouseStartBefore = view(0).GetRoad().start;

    // Der Padspieler baut seine eigene Strasse - und nimmt sie wieder zurueck.
    press(11, padRoad::Begin);
    for(unsigned i = 1; i < ptsPad.size(); ++i)
    {
        aimAt(1, ptsPad[i]);
        press(11, padRoad::Extend);
    }
    BOOST_TEST_REQUIRE(view(1).GetRoad().route.size() == sPad.route.size());
    for(unsigned i = 0; i <= sPad.route.size(); ++i)
        press(11, padRoad::StepBack);
    BOOST_TEST_REQUIRE((view(1).GetRoad().mode == RoadBuildMode::Disabled));

    // Der Mausspieler ist bitgleich geblieben.
    BOOST_TEST((view(0).GetRoad().mode == RoadBuildMode::Normal));
    BOOST_TEST((view(0).GetRoad().start == mouseStartBefore));
    BOOST_TEST((view(0).GetRoad().point == mousePointBefore));
    BOOST_TEST((view(0).GetRoad().route == mouseRouteBefore), boost::test_tools::per_element());
    for(unsigned i = 0; i < 3; ++i)
        BOOST_TEST(view(0).GetViewer().IsOnRoad(ptsMouse[i]));
    // Und die Vorschau des Padspielers war im Bild des Mausspielers nie zu sehen.
    BOOST_TEST(!viewerDrawsAnyOf(view(0).GetViewer(), sPad.start, sPad.route));

    // Der Mausspieler kann seinen Bau ganz normal zu Ende bringen.
    dsk->GI_CancelRoadBuilding();
    BOOST_TEST((dsk->GetRoadMode() == RoadBuildMode::Disabled));
}

/// Nicht Kosmetik, sondern die Begruendung dafuer, dass der Padpfad das Strassenfenster gar
/// nicht erst benutzt: iwRoadWindow warpt im KONSTRUKTOR die eine echte Maus auf seinen
/// Vorgabeknopf (VIDEODRIVER.SetMousePos) und beim Klick ein zweites Mal zurueck. Oeffnete ein
/// Padspieler es, risse er dem Mausspieler den Zeiger weg.
///
/// Der Padpfad hat deshalb X und B statt der beiden Knoepfe dieses Fensters. Hier wird gemessen,
/// dass er dabei bleibt.
BOOST_FIXTURE_TEST_CASE(PadRoadBuildingMovesNoMousePointerAndOpensNoRoadWindow, PadViewFixture<2>)
{
    const GameWorldBase& world = worldFixture.world;
    const RoadSpot spot = findRoadSpotFromHQ(view(1).GetViewer(), 3, 6);
    BOOST_TEST_REQUIRE(spot.isValid());
    const std::vector<MapPoint> pts = roadPoints(world, spot.start, spot.route);

    pads.connect(11);
    step(16);
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(11, 1));
    step(16);
    padSteerTo(11, 1, spot.start);

    const Position mouseBefore = VIDEODRIVER.GetMousePos();

    press(11, padRoad::Begin);
    for(unsigned i = 1; i < pts.size(); ++i)
    {
        aimAt(1, pts[i]);
        press(11, padRoad::Extend);
    }
    // Auch der Versuch, auf dem eigenen Wegende zu bestaetigen und zurueckzugehen, oeffnet
    // nichts: beim Mausspieler waere genau das der Weg ins Strassenfenster.
    press(11, padRoad::Extend);
    press(11, padRoad::StepBack);
    press(11, padRoad::Commit);

    BOOST_TEST((VIDEODRIVER.GetMousePos() == mouseBefore));
    for(unsigned owner = 0; owner < 2; ++owner)
        BOOST_TEST(WINDOWMANAGER.FindNonModalWindow(CGI_ROADWINDOW, owner) == nullptr);
    BOOST_TEST(WINDOWMANAGER.FindNonModalWindow(CGI_ROADWINDOW, SHARED_WINDOW_OWNER) == nullptr);
    BOOST_TEST(view(0).roadwindow == nullptr);
    BOOST_TEST(view(1).roadwindow == nullptr);
    // Der Fokus des Padspielers ist nie irgendwo hineingesprungen.
    BOOST_TEST(!view(1).GetFocus().IsActive());
}

// ============================================================================================
// BEFUND C: die beiden Knoepfe des Strassenfensters wirken auf DESSEN Ansicht
// ============================================================================================

// iwRoadWindow ruft gi.GI_BuildRoad() bzw. gi.GI_CancelRoadBuilding() - ohne jeden Spielerbezug
// (GameInterface.h kennt keine Ansichten). dskGameInterface::RoadWindowOwner() sucht deshalb die
// Ansicht, deren roadwindow gerade offen ist, und faellt nur sonst auf primary() zurueck.
//
// Ehrlich dazugesagt: der Zustand "eine ANDERE Ansicht als primary() haelt ein Strassenfenster"
// ist aus der Bedienoberflaeche heraus derzeit nicht erreichbar. ShowRoadWindow oeffnet das
// Fenster ausschliesslich fuer GetMouseView() (ContextClick), und in den Baumodus kommt eine
// Ansicht ausser primary() nur ueber das Pad (PadStartRoad) - eine padbesetzte Ansicht bekommt
// die Maus aber nie. Der Zustand wird hier deshalb mit den PRODUKTIVEN Funktionen aufgebaut
// (StartRoadBuilding und ShowRoadWindow, dieselben Aufrufe, die der Padpfad und ContextClick
// benutzen) statt ueber eine Bedienfolge. Gedeckt ist damit die Entscheidung, nicht ihr Weg -
// und genau die faellt um, wenn RoadWindowOwner() wieder primary() liefert.

/// Der Abbruchknopf. Beide Ansichten bauen, das Fenster gehoert Ansicht 1 - abgebrochen wird
/// Ansicht 1, und Ansicht 0 merkt nichts davon.
BOOST_FIXTURE_TEST_CASE(TheRoadWindowCancelButtonActsOnTheViewThatOwnsTheWindow, PadViewFixture<2>)
{
    const RoadSpot spot0 = findRoadSpotFromHQ(view(0).GetViewer(), 2, 4);
    const RoadSpot spot1 = findRoadSpotFromHQ(view(1).GetViewer(), 2, 4);
    BOOST_TEST_REQUIRE(spot0.isValid());
    BOOST_TEST_REQUIRE(spot1.isValid());

    dsk->StartRoadBuilding(view(0), spot0.start, false);
    dsk->StartRoadBuilding(view(1), spot1.start, false);
    MapPoint t0 = spot0.end;
    MapPoint t1 = spot1.end;
    BOOST_TEST_REQUIRE((dsk->BuildRoadPart(view(0), t0) == dskGameInterface::RoadPartResult::Built));
    BOOST_TEST_REQUIRE((dsk->BuildRoadPart(view(1), t1) == dskGameInterface::RoadPartResult::Built));

    dsk->ShowRoadWindow(view(1), Position(10, 10));
    BOOST_TEST_REQUIRE(view(1).roadwindow != static_cast<IngameWindow*>(nullptr));
    BOOST_TEST_REQUIRE(view(0).roadwindow == static_cast<IngameWindow*>(nullptr));

    dsk->GI_CancelRoadBuilding();

    BOOST_TEST((view(1).GetRoad().mode == RoadBuildMode::Disabled));
    BOOST_TEST(!viewerDrawsAnyOf(view(1).GetViewer(), spot1.start, spot1.route));
    // Ansicht 0 baut unbeirrt weiter.
    BOOST_TEST((view(0).GetRoad().mode == RoadBuildMode::Normal));
    BOOST_TEST((view(0).GetRoad().route == spot0.route), boost::test_tools::per_element());
    BOOST_TEST(viewerDrawsRoad(view(0).GetViewer(), spot0.start, spot0.route));
}

/// Der Bauknopf, in einer LAUFENDEN Partie: die Strasse muss auf dem Spieler der Ansicht landen,
/// der das Fenster gehoert - gezaehlt im Replay, also an dem, was wirklich vom Server zurueckkam.
BOOST_FIXTURE_TEST_CASE(TheRoadWindowBuildButtonBooksTheRoadOnTheOwningViewsPlayer, rttr::test::PadGameFixture)
{
    setUpTwoLocalPlayers();

    const RoadSpot spot0 = findRoadSpotFromHQ(dsk->GetPlayerView(0).GetViewer(), 2, 4);
    const RoadSpot spot1 = findRoadSpotFromHQ(dsk->GetPlayerView(1).GetViewer(), 2, 4);
    BOOST_TEST_REQUIRE(spot0.isValid());
    BOOST_TEST_REQUIRE(spot1.isValid());

    dsk->StartRoadBuilding(dsk->GetPlayerView(0), spot0.start, false);
    dsk->StartRoadBuilding(dsk->GetPlayerView(1), spot1.start, false);
    MapPoint t0 = spot0.end;
    MapPoint t1 = spot1.end;
    BOOST_TEST_REQUIRE(
      (dsk->BuildRoadPart(dsk->GetPlayerView(0), t0) == dskGameInterface::RoadPartResult::Built));
    BOOST_TEST_REQUIRE(
      (dsk->BuildRoadPart(dsk->GetPlayerView(1), t1) == dskGameInterface::RoadPartResult::Built));

    dsk->ShowRoadWindow(dsk->GetPlayerView(1), Position(10, 10));
    BOOST_TEST_REQUIRE(dsk->GetPlayerView(1).roadwindow != static_cast<IngameWindow*>(nullptr));

    const unsigned startGF = GAMECLIENT.GetGFNumber();
    dsk->GI_BuildRoad();
    BOOST_TEST((dsk->GetPlayerView(1).GetRoad().mode == RoadBuildMode::Disabled));
    BOOST_TEST((dsk->GetPlayerView(0).GetRoad().mode == RoadBuildMode::Normal));

    pumpUntilGF(startGF + 40);
    BOOST_TEST(worldHasRoad(world(), spot1.start, spot1.route));
    BOOST_TEST(!worldHasRoad(world(), spot0.start, spot0.route));
    BOOST_TEST(ci().numErrors == 0u);
    BOOST_TEST(ci().numAsync == 0u);

    tearDownDesktop();
    const auto replayPath = stopAndGetReplay();
    BOOST_TEST(numGCsForPlayer(replayPath, 1) == 1u);
    BOOST_TEST(numGCsForPlayer(replayPath, 0) == 0u);
}

BOOST_AUTO_TEST_SUITE_END()
