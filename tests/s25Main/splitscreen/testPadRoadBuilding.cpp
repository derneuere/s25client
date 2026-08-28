// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

// Strassenbau am Gamepad - die erste vollstaendig spielbare Kernschleife.
//
// Jeder Fall hier nimmt AUSSCHLIESSLICH den Produktivweg: PadEvent in die Warteschlange des
// MockupVideoDriver, dskGameInterface::UpdateInput holt sie mit demselben Aufruf ab wie vom
// SDL2-Treiber (IVideoDriver::FetchPadEvents), PadRouter verteilt sie, OnPadButton entscheidet.
//
// Was in dieser Datei mechanisch NICHT vorkommen darf - und wodurch die frueheren Runden
// gescheitert sind:
//   * keine ViewScope und keine ScopedActingPlayer: der Besitzer muss aus OnPadButton kommen,
//     sonst prueft der Test seine eigene Klammer statt der des Spiels.
//   * kein Aufruf von GI_StartRoadBuilding / BuildRoadPart / CommitRoad / DemolishRoad /
//     GI_CancelRoadBuilding: das sind die MAUSfassungen, sie gehoeren nach
//     testMouseRoadBuilding.cpp.
//   * kein GetGCFactory zum AUSLOESEN einer Handlung.
//   * kein SetCursorPos / SetPadCursor: gezielt wird ueber aimPadAt/aimAt, mit
//     BOOST_TEST_REQUIRE(GetSelectedPt() == pt) als Quittung.
// Beides ist mit grep nachpruefbar.

#include "GamePlayer.h"
#include "Loader.h"
#include "PointOutput.h"
#include "RttrConfig.h"
#include "WindowManager.h"
#include "buildings/nobBaseWarehouse.h"
#include "controls/ctrlTextButton.h"
#include "desktops/PlayerView.h"
#include "driver/PadEvent.h"
#include "files.h"
#include "ingameWindows/IngameWindow.h"
#include "input/FocusPath.h"
#include "network/GameClient.h"
#include "nodeObjs/noFlag.h"
#include "pathfinding/FindPathForRoad.h"
#include "world/GameWorld.h"
#include "PadFixture.h"
#include "PadGameFixture.h"
#include "gameTypes/BuildingQuality.h"
#include "gameTypes/RoadBuildMode.h"
#include "gameData/const_gui_ids.h"
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <vector>

using namespace rttr::test;

namespace {
/// Ein Fenster mit einem Knopf, wie es der Padspieler mit Y betreten kann. Ohne Besitzer, also
/// fuer jede Ansicht betretbar (WindowManager::GetTopMostWindow).
struct PlainButtonWnd : IngameWindow
{
    PlainButtonWnd()
        : IngameWindow(CGI_HELP, DrawPoint(0, 0), Extent(200, 120), "", nullptr, false, CloseBehavior::Regular)
    {
        AddTextButton(1, DrawPoint(10, 10), Extent(80, 20), TextureColor::Green1, "A", NormalFont);
    }
    unsigned clicks = 0;
    void Msg_ButtonClick(unsigned) override { ++clicks; }
};

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

BOOST_AUTO_TEST_SUITE(PadRoadBuildingTests)

// ============================================================================================
// 1. Der Einstieg: A auf der EIGENEN Flagge - und auf nichts sonst
// ============================================================================================

/// Der Name schliesst den kritischen Fall ein: es wird nicht nur geprueft, dass es auf der
/// eigenen Flagge geht, sondern auch, dass es auf einem leeren Knoten und auf der Flagge eines
/// ANDEREN Spielers nicht geht (Negativkontrolle N3).
///
/// Warum das im Produktivcode noetig ist: GI_StartRoadBuilding prueft den Startpunkt NICHT.
/// Im Mauspfad stellt allein die Bedienoberflaeche sicher, dass der Knopf nur auf einer eigenen
/// Flagge existiert (iwAction-Flaggenreiter, gesetzt aus IsOwner + NodalObjectType::Flag).
/// Der Padspieler hat kein iwAction - die Pruefung muss er selbst mitbringen.
BOOST_FIXTURE_TEST_CASE(APadPlayerStartsRoadBuildingOnHisOwnFlagButNotOnAnyOtherNode, PadViewFixture<2>)
{
    const GameWorldBase& world = worldFixture.world;
    const MapPoint ownFlag = hqFlagOf(world, 0);
    const MapPoint foreignFlag = hqFlagOf(world, 1);

    aimPadAt(10, 0, ownFlag);

    // (a) leerer Knoten: A oeffnet dort kein Fenster und faengt auch keinen Strassenbau an
    const MapPoint emptyPt = world.GetNeighbour(world.GetNeighbour(ownFlag, Direction::East), Direction::East);
    BOOST_TEST_REQUIRE((world.GetNO(emptyPt)->GetType() != NodalObjectType::Flag));
    aimAt(0, emptyPt);
    press(10, padRoad::Begin);
    BOOST_TEST((view(0).GetRoad().mode == RoadBuildMode::Disabled));
    // PHASE 13: auf BAUBAREM leerem Land ist A die dritte Stufe der Kaskade und oeffnet das
    // Kreismenue (frueher: das Aktionsfenster, ohne den Fokus zu setzen). Der Strassenbau faengt
    // dort weiterhin NICHT an - das ist die Zusicherung dieses Falls. Der Ring muss aber weg,
    // bevor weitergezielt wird: solange er offen ist, gehoert der linke Stick ihm.
    if(view(0).GetRing().IsOpen())
        press(10, PadButton::B);
    BOOST_TEST_REQUIRE(!view(0).GetRing().IsOpen());

    // (b) fremde Flagge: der Startpunkt gehoert Spieler 1, also passiert nichts
    aimAt(0, foreignFlag);
    press(10, padRoad::Begin);
    BOOST_TEST((view(0).GetRoad().mode == RoadBuildMode::Disabled));

    // (c) eigene Flagge: jetzt - und nur jetzt - beginnt der Bau
    aimAt(0, ownFlag);
    press(10, padRoad::Begin);
    BOOST_TEST((view(0).GetRoad().mode == RoadBuildMode::Normal));
    BOOST_TEST((view(0).GetRoad().start == ownFlag));
    BOOST_TEST((view(0).GetRoad().point == ownFlag));
    BOOST_TEST(view(0).GetRoad().route.empty());
    // Die andere Ansicht ist davon voellig unberuehrt.
    BOOST_TEST((view(1).GetRoad().mode == RoadBuildMode::Disabled));
}

/// Der Wasserweg hat einen eigenen Knopf und eine strengere Bedingung: er gibt es nur an einer
/// eigenen WASSERflagge (dieselbe Bedingung, unter der iwAction den zweiten Knopf anbietet).
/// Auf einer gewoehnlichen Landflagge - und das ist die HQ-Flagge in der leeren Testwelt -
/// passiert nichts.
BOOST_FIXTURE_TEST_CASE(TheWaterwayButtonDoesNothingOnAPlainLandFlag, PadViewFixture<1>)
{
    const MapPoint ownFlag = hqFlagOf(worldFixture.world, 0);
    BOOST_TEST_REQUIRE((worldFixture.world.GetSpecObj<noFlag>(ownFlag)->GetFlagType() != FlagType::Water));

    aimPadAt(10, 0, ownFlag);
    press(10, padRoad::BeginWater);
    BOOST_TEST((view(0).GetRoad().mode == RoadBuildMode::Disabled));

    // Derselbe Knoten, der normale Knopf: der faengt sehr wohl an. Damit ist ausgeschlossen,
    // dass oben bloss die Vorbedingung des Punktes nicht stimmte.
    press(10, padRoad::Begin);
    BOOST_TEST((view(0).GetRoad().mode == RoadBuildMode::Normal));
}

// ============================================================================================
// 2. Verlaengern - und die Invariante, dass A nie etwas festschreibt
// ============================================================================================

/// Der Name schliesst den kritischen Fall ein: geprueft wird ausdruecklich AUCH, dass ein
/// A-Druck auf das eigene Wegende nichts tut. Genau dort steht der Zeiger nach jedem Wegstueck
/// (GameWorldView::DrawGUI hebt diesen Punkt hervor), und genau dort haelt FindPathForRoad im
/// Debugbau an: RTTR_Assert(startPt != endPt), pathfinding/FindPathForRoad.cpp:36. Ohne die
/// Vorbedingung koennte ein Padspieler das Programm mit einem einzigen Knopfdruck anhalten.
BOOST_FIXTURE_TEST_CASE(ExtendingStepByStepGrowsTheRouteAndPressingOnTheOwnEndPointIsHarmless,
                        PadViewFixture<1>)
{
    const GameWorldBase& world = worldFixture.world;
    const RoadSpot spot = findRoadSpotFromHQ(view(0).GetViewer(), 4, 6);
    BOOST_TEST_REQUIRE(spot.isValid());
    const std::vector<MapPoint> pts = roadPoints(world, spot.start, spot.route);

    aimPadAt(10, 0, spot.start);
    press(10, padRoad::Begin);
    BOOST_TEST_REQUIRE((view(0).GetRoad().mode == RoadBuildMode::Normal));

    // Der Zeiger steht auf dem Wegende (= Startflagge). Ein A-Druck hier ist der gefaehrliche
    // Fall - er darf schlicht nichts tun.
    press(10, padRoad::Extend);
    BOOST_TEST(view(0).GetRoad().route.empty());
    BOOST_TEST((view(0).GetRoad().point == spot.start));

    // Jetzt knotenweise vorwaerts: ein Druck, ein Wegstueck.
    for(unsigned i = 1; i < pts.size(); ++i)
    {
        aimAt(0, pts[i]);
        press(10, padRoad::Extend);
        BOOST_TEST_CONTEXT("Schritt " << i)
        {
            BOOST_TEST(view(0).GetRoad().route.size() == i);
            BOOST_TEST((view(0).GetRoad().point == pts[i]));
            BOOST_TEST(view(0).GetViewer().IsOnRoad(pts[i]));
        }
        // Und noch einmal derselbe Punkt - jetzt IST er das Wegende.
        press(10, padRoad::Extend);
        BOOST_TEST(view(0).GetRoad().route.size() == i);
    }
    BOOST_TEST((view(0).GetRoad().route == spot.route), boost::test_tools::per_element());
}

/// N5, die Flankenprobe: ein GEHALTENER Knopf haengt nicht Frame fuer Frame ein Wegstueck an.
/// Erst ein Loslassen macht das naechste Druecken wieder zu einer Flanke.
BOOST_FIXTURE_TEST_CASE(HoldingTheExtendButtonAddsNoFurtherSegmentUntilItIsReleased, PadViewFixture<1>)
{
    const RoadSpot spot = findRoadSpotFromHQ(view(0).GetViewer(), 3, 6);
    BOOST_TEST_REQUIRE(spot.isValid());
    const std::vector<MapPoint> pts = roadPoints(worldFixture.world, spot.start, spot.route);

    aimPadAt(10, 0, spot.start);
    press(10, padRoad::Begin);

    // Erstes Druecken -> Flanke -> genau ein Wegstueck
    aimAt(0, pts[1]);
    pads.button(10, padRoad::Extend, true);
    step(16);
    BOOST_TEST_REQUIRE(view(0).GetRoad().route.size() == 1u);

    // Knopf bleibt unten, Zeiger wandert weiter: KEINE neue Flanke, kein neues Wegstueck
    aimAt(0, pts[2]);
    pads.button(10, padRoad::Extend, true);
    step(16);
    BOOST_TEST(view(0).GetRoad().route.size() == 1u);

    // Erst das Loslassen macht das naechste Druecken wieder zu einer Flanke
    pads.button(10, padRoad::Extend, false);
    pads.button(10, padRoad::Extend, true);
    step(16);
    BOOST_TEST(view(0).GetRoad().route.size() == 2u);
}

/// N4: ein Ziel, zu dem es keinen Weg gibt (hier: jenseits des eigenen Gebiets - die
/// Wegbedingung verlangt IsPlayerTerritory fuer jeden Zwischenknoten). Route und Modus bleiben
/// unveraendert; es darf weder still eine falsche Route entstehen noch der Bau abbrechen.
BOOST_FIXTURE_TEST_CASE(AnUnreachableTargetLeavesTheRouteAndTheBuildModeUntouched, PadViewFixture<2>)
{
    const MapPoint ownFlag = hqFlagOf(worldFixture.world, 0);
    const MapPoint faraway = hqFlagOf(worldFixture.world, 1); // im Gebiet des ANDEREN Spielers

    aimPadAt(10, 0, ownFlag);
    press(10, padRoad::Begin);
    const RoadSpot spot = findRoadSpotFromHQ(view(0).GetViewer(), 2, 5);
    BOOST_TEST_REQUIRE(spot.isValid());
    const std::vector<MapPoint> pts = roadPoints(worldFixture.world, spot.start, spot.route);
    aimAt(0, pts[1]);
    press(10, padRoad::Extend);
    BOOST_TEST_REQUIRE(view(0).GetRoad().route.size() == 1u);

    aimAt(0, faraway);
    press(10, padRoad::Extend);
    BOOST_TEST(view(0).GetRoad().route.size() == 1u);
    BOOST_TEST((view(0).GetRoad().point == pts[1]));
    BOOST_TEST((view(0).GetRoad().mode == RoadBuildMode::Normal));
}

// ============================================================================================
// 3. Zurueck und Abbruch
// ============================================================================================

/// Ein Druck auf B nimmt GENAU EIN Wegstueck zurueck - nicht null, nicht alle - und stellt die
/// Bauqualitaet des freigewordenen Knotens wieder her. Vorbild fuer die Erwartungswerte ist der
/// bestehende Mausnachweis BQWithVisualRoad (tests/s25Main/integration/testBuilding.cpp:217).
BOOST_FIXTURE_TEST_CASE(SteppingBackRemovesExactlyOneSegmentAndRestoresItsBuildingQuality, PadViewFixture<1>)
{
    const RoadSpot spot = findRoadSpotFromHQ(view(0).GetViewer(), 4, 6);
    BOOST_TEST_REQUIRE(spot.isValid());
    const std::vector<MapPoint> pts = roadPoints(worldFixture.world, spot.start, spot.route);
    const BuildingQuality bqBefore = view(0).GetViewer().GetBQ(pts.back());

    aimPadAt(10, 0, spot.start);
    press(10, padRoad::Begin);
    for(unsigned i = 1; i < pts.size(); ++i)
    {
        aimAt(0, pts[i]);
        press(10, padRoad::Extend);
    }
    const unsigned full = static_cast<unsigned>(view(0).GetRoad().route.size());
    BOOST_TEST_REQUIRE(full == spot.route.size());
    BOOST_TEST_REQUIRE(view(0).GetViewer().IsOnRoad(pts.back()));

    press(10, padRoad::StepBack);

    BOOST_TEST(view(0).GetRoad().route.size() == full - 1u);
    BOOST_TEST((view(0).GetRoad().point == pts[pts.size() - 2]));
    BOOST_TEST(!view(0).GetViewer().IsOnRoad(pts.back()));
    BOOST_TEST((view(0).GetViewer().GetBQ(pts.back()) == bqBefore));
    // Alles davor steht noch.
    for(unsigned i = 0; i + 1 < pts.size(); ++i)
        BOOST_TEST(view(0).GetViewer().IsOnRoad(pts[i]));
    BOOST_TEST((view(0).GetRoad().mode == RoadBuildMode::Normal));
}

/// Der kritische Fall im Namen: der Schritt zurueck UEBER den Anfang hinaus. DemolishRoad laeuft
/// rueckwaerts mit einem unsigned-Zaehler (dskGameInterface.cpp, `for(unsigned i = route.size();
/// i >= start_id; --i)`); mit einer leeren Route liefe er in den Unterlauf und griffe ueber den
/// Anfang des Vektors hinaus. Statt dessen ist der Schritt dort der Abbruch.
BOOST_FIXTURE_TEST_CASE(SteppingBackPastTheStartCancelsInsteadOfUnderflowing, PadViewFixture<1>)
{
    const RoadSpot spot = findRoadSpotFromHQ(view(0).GetViewer(), 3, 6);
    BOOST_TEST_REQUIRE(spot.isValid());
    const std::vector<MapPoint> pts = roadPoints(worldFixture.world, spot.start, spot.route);
    // Ausgangswerte der Bauqualitaet im ganzen Umfeld merken - sie muessen am Ende wieder da
    // sein. Verglichen wird gegen die Aufnahme und nicht gegen absolute Werte: die Startflagge
    // haengt als Gebaeudeflagge schon vorher an einem Wegstueck.
    const std::vector<MapPoint> around = worldFixture.world.GetPointsInRadiusWithCenter(spot.start, 8);
    std::vector<BuildingQuality> bqBefore;
    std::vector<bool> onRoadBefore;
    for(const MapPoint& pt : around)
    {
        bqBefore.push_back(view(0).GetViewer().GetBQ(pt));
        onRoadBefore.push_back(view(0).GetViewer().IsOnRoad(pt));
    }

    aimPadAt(10, 0, spot.start);
    press(10, padRoad::Begin);
    for(unsigned i = 1; i < pts.size(); ++i)
    {
        aimAt(0, pts[i]);
        press(10, padRoad::Extend);
    }
    BOOST_TEST_REQUIRE(!view(0).GetRoad().route.empty());

    // So oft zurueck, bis die Strecke leer ist - und dann EIN Mal mehr.
    for(unsigned i = 0; i < spot.route.size(); ++i)
        press(10, padRoad::StepBack);
    BOOST_TEST_REQUIRE(view(0).GetRoad().route.empty());
    BOOST_TEST_REQUIRE((view(0).GetRoad().mode == RoadBuildMode::Normal));

    press(10, padRoad::StepBack); // <- der Schritt ueber den Anfang hinaus

    BOOST_TEST((view(0).GetRoad().mode == RoadBuildMode::Disabled));
    BOOST_TEST(view(0).GetRoad().route.empty());
    BOOST_TEST(!viewerDrawsAnyOf(view(0).GetViewer(), spot.start, spot.route));
    for(unsigned i = 0; i < around.size(); ++i)
        BOOST_TEST_CONTEXT("bei " << around[i])
        {
            BOOST_TEST((view(0).GetViewer().GetBQ(around[i]) == bqBefore[i]));
            BOOST_TEST(view(0).GetViewer().IsOnRoad(around[i]) == onRoadBefore[i]);
        }

    // Und noch einer, auf schon abgeschaltetem Modus: nichts passiert, nichts bricht.
    press(10, padRoad::StepBack);
    BOOST_TEST((view(0).GetRoad().mode == RoadBuildMode::Disabled));
}

/// N7: das Pad wird mitten im Bau abgezogen. Ohne Behandlung bliebe die visuelle Strasse dieses
/// Spielers fuer immer stehen und seine Bauqualitaet dauerhaft falsch - niemand koennte den Bau
/// noch abschliessen oder abbrechen, weil die Ansicht kein Eingabegeraet mehr hat.
BOOST_FIXTURE_TEST_CASE(UnpluggingThePadDuringRoadBuildingLeavesNoGhostRoad, PadViewFixture<2>)
{
    const RoadSpot spot0 = findRoadSpotFromHQ(view(0).GetViewer(), 2, 5);
    const RoadSpot spot1 = findRoadSpotFromHQ(view(1).GetViewer(), 2, 5);
    BOOST_TEST_REQUIRE(spot0.isValid());
    BOOST_TEST_REQUIRE(spot1.isValid());
    const std::vector<MapPoint> pts0 = roadPoints(worldFixture.world, spot0.start, spot0.route);
    const std::vector<MapPoint> pts1 = roadPoints(worldFixture.world, spot1.start, spot1.route);

    aimPadAt(10, 0, spot0.start);
    aimPadAt(11, 1, spot1.start);
    press(10, padRoad::Begin);
    press(11, padRoad::Begin);
    aimAt(0, pts0[1]);
    press(10, padRoad::Extend);
    aimAt(1, pts1[1]);
    press(11, padRoad::Extend);
    BOOST_TEST_REQUIRE(view(0).GetRoad().route.size() == 1u);
    BOOST_TEST_REQUIRE(view(1).GetRoad().route.size() == 1u);

    pads.disconnect(10);
    step(16);

    BOOST_TEST((view(0).GetRoad().mode == RoadBuildMode::Disabled));
    BOOST_TEST(!viewerDrawsAnyOf(view(0).GetViewer(), spot0.start, spot0.route));
    // Der Nachbar hat sein Pad noch und baut unbeirrt weiter.
    BOOST_TEST((view(1).GetRoad().mode == RoadBuildMode::Normal));
    BOOST_TEST(view(1).GetRoad().route.size() == 1u);
    BOOST_TEST(view(1).GetViewer().IsOnRoad(pts1[1]));
}

/// N8: die Startflagge faellt weg. GI_FlagDestroyed muss den Bau DER BETROFFENEN Ansicht
/// vollstaendig abraeumen - Modus AUS und visuelle Strasse weg - und die anderen in Ruhe lassen.
/// Frueher wurde ausserhalb von primary() nur der Modus abgeschaltet; die visuelle Strasse blieb
/// fuer immer stehen.
BOOST_FIXTURE_TEST_CASE(DestroyingTheStartFlagClearsThatViewsRoadCompletelyAndLeavesTheOtherAlone,
                        PadViewFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const RoadSpot spot0 = findRoadSpotFromHQ(view(0).GetViewer(), 2, 5);
    const RoadSpot spot1 = findRoadSpotFromHQ(view(1).GetViewer(), 2, 5);
    BOOST_TEST_REQUIRE(spot0.isValid());
    BOOST_TEST_REQUIRE(spot1.isValid());
    const std::vector<MapPoint> pts0 = roadPoints(world, spot0.start, spot0.route);
    const std::vector<MapPoint> pts1 = roadPoints(world, spot1.start, spot1.route);

    // Eine eigene Flagge, die NICHT am HQ haengt - sonst risse ihre Zerstoerung das HQ mit.
    const MapPoint flag0 = pts0.back();
    world.SetFlag(flag0, 0);
    BOOST_TEST_REQUIRE((world.GetNO(flag0)->GetType() == NodalObjectType::Flag));
    const RoadSpot fromFlag0 = [&] {
        RoadSpot s;
        s.start = flag0;
        for(const MapPoint& pt : world.GetPointsInRadiusWithCenter(flag0, 6))
        {
            if(pt == flag0 || world.GetNode(pt).obj || world.IsFlagAround(pt))
                continue;
            if(world.GetBQ(pt, 0) == BuildingQuality::Nothing)
                continue;
            auto route = FindPathForRoad(view(0).GetViewer(), flag0, pt, false, 100);
            if(route.size() < 2 || route.size() > 4)
                continue;
            s.end = pt;
            s.route = std::move(route);
            break;
        }
        return s;
    }();
    BOOST_TEST_REQUIRE(fromFlag0.isValid());
    const std::vector<MapPoint> ptsFrom0 = roadPoints(world, fromFlag0.start, fromFlag0.route);

    aimPadAt(10, 0, fromFlag0.start);
    aimPadAt(11, 1, spot1.start);
    press(10, padRoad::Begin);
    press(11, padRoad::Begin);
    for(unsigned i = 1; i < ptsFrom0.size(); ++i)
    {
        aimAt(0, ptsFrom0[i]);
        press(10, padRoad::Extend);
    }
    aimAt(1, pts1[1]);
    press(11, padRoad::Extend);
    BOOST_TEST_REQUIRE(!view(0).GetRoad().route.empty());
    BOOST_TEST_REQUIRE(view(1).GetRoad().route.size() == 1u);

    world.DestroyFlag(flag0, 0);

    BOOST_TEST((view(0).GetRoad().mode == RoadBuildMode::Disabled));
    BOOST_TEST(!viewerDrawsAnyOf(view(0).GetViewer(), fromFlag0.start, fromFlag0.route));
    BOOST_TEST((view(1).GetRoad().mode == RoadBuildMode::Normal));
    BOOST_TEST(view(1).GetRoad().route.size() == 1u);
    BOOST_TEST(view(1).GetViewer().IsOnRoad(pts1[1]));
}

// ============================================================================================
// 4. Der Fokus verbraucht die Flanke (N6)
// ============================================================================================

/// Steht der Padspieler in einem Fenster, sieht die WELT seine Knopfflanken nicht - auch dann
/// nicht, wenn er gerade eine Strasse baut. Andernfalls legte ein Knopfdruck im Fenster
/// gleichzeitig Weltzustand um.
BOOST_FIXTURE_TEST_CASE(APadPlayerInsideAWindowChangesNoRoadStateAtAll, PadViewFixture<1>)
{
    const RoadSpot spot = findRoadSpotFromHQ(view(0).GetViewer(), 2, 5);
    BOOST_TEST_REQUIRE(spot.isValid());
    const std::vector<MapPoint> pts = roadPoints(worldFixture.world, spot.start, spot.route);

    auto& wnd = static_cast<PlainButtonWnd&>(WINDOWMANAGER.Show(std::make_unique<PlainButtonWnd>()));

    aimPadAt(10, 0, spot.start);
    press(10, padRoad::Begin);
    aimAt(0, pts[1]);
    press(10, padRoad::Extend);
    const auto routeBefore = view(0).GetRoad().route;
    const MapPoint pointBefore = view(0).GetRoad().point;

    // Den Zeiger auf einen gueltigen naechsten Wegpunkt stellen, SOLANGE der Spieler noch in
    // der Welt steht: sobald er ein Fenster betreten hat, gehoert der linke Stick dem Fokus
    // (FocusPath::OnPadMove) und der Weltzeiger bewegt sich nicht mehr. Er bleibt dabei genau
    // dort stehen, wo er jetzt hingesetzt wird - das ist die Vorbedingung dieses Nachweises.
    aimAt(0, pts.back());

    press(10, PadButton::Y); // Fenster betreten
    BOOST_TEST_REQUIRE(view(0).GetFocus().IsActive());
    BOOST_TEST_REQUIRE((gwv(0).GetSelectedPt() == pts.back()));

    // Der Zeiger steht weiter auf einem gueltigen naechsten Wegpunkt - waere die Flanke nicht
    // verbraucht, wuerde A hier verlaengern und X festschreiben.
    press(10, padRoad::Commit);
    press(10, padRoad::Extend);
    BOOST_TEST((view(0).GetRoad().mode == RoadBuildMode::Normal));
    BOOST_TEST((view(0).GetRoad().route == routeBefore), boost::test_tools::per_element());
    BOOST_TEST((view(0).GetRoad().point == pointBefore));
    BOOST_TEST(wnd.clicks == 1u); // der A-Druck ist im Fenster angekommen, nicht in der Welt

    // B loest den Fokus auf (FocusPath::OnPadButton) - erst danach hoert die Welt wieder zu.
    press(10, padRoad::StepBack);
    BOOST_TEST(!view(0).GetFocus().IsActive());
    BOOST_TEST((view(0).GetRoad().route == routeBefore), boost::test_tools::per_element());
    press(10, padRoad::StepBack);
    BOOST_TEST(view(0).GetRoad().route.size() == routeBefore.size() - 1u);

    wnd.Close();
    WINDOWMANAGER.Draw();
}

// ============================================================================================
// 5. Zwei und vier Spieler gleichzeitig
// ============================================================================================

/// Der Kern der Phase, und zwar VERSCHRAENKT statt nacheinander: sonst misst der Fall nichts.
/// Hier faellt der Zustand vor diesem Umbau um - beide Ansichten lasen denselben
/// RoadBuildState, weil dskGameInterface::road auf primary() zeigte.
BOOST_FIXTURE_TEST_CASE(TwoPadPlayersBuildTwoRoadsAtTheSameTimeWithoutTouchingEachOthersState,
                        PadViewFixture<2>)
{
    const GameWorldBase& world = worldFixture.world;
    // Die Vorbedingung, ohne die alles Weitere bedeutungslos waere.
    BOOST_TEST_REQUIRE((&view(0).GetRoad() != &view(1).GetRoad()));

    const RoadSpot s0 = findRoadSpotFromHQ(view(0).GetViewer(), 3, 6);
    const RoadSpot s1 = findRoadSpotFromHQ(view(1).GetViewer(), 3, 6);
    BOOST_TEST_REQUIRE(s0.isValid());
    BOOST_TEST_REQUIRE(s1.isValid());
    const std::vector<MapPoint> p0 = roadPoints(world, s0.start, s0.route);
    const std::vector<MapPoint> p1 = roadPoints(world, s1.start, s1.route);

    aimPadAt(10, 0, s0.start);
    aimPadAt(11, 1, s1.start);

    press(10, padRoad::Begin); // p0 faengt an
    BOOST_TEST((view(1).GetRoad().mode == RoadBuildMode::Disabled));
    press(11, padRoad::Begin); // p1 faengt an
    BOOST_TEST((view(0).GetRoad().start == s0.start));
    BOOST_TEST((view(1).GetRoad().start == s1.start));

    aimAt(0, p0[1]);
    press(10, padRoad::Extend); // p0 ein Stueck
    BOOST_TEST(view(0).GetRoad().route.size() == 1u);
    BOOST_TEST(view(1).GetRoad().route.empty());

    aimAt(1, p1[1]);
    press(11, padRoad::Extend); // p1 ein Stueck
    aimAt(1, p1[2]);
    press(11, padRoad::Extend); // p1 noch eins
    BOOST_TEST(view(0).GetRoad().route.size() == 1u); // p0 unveraendert
    BOOST_TEST(view(1).GetRoad().route.size() == 2u);

    aimAt(0, p0[2]);
    press(10, padRoad::Extend);
    BOOST_TEST(view(0).GetRoad().route.size() == 2u);

    // Die schaerfste Einzelmessung: der Schritt zurueck von p0 verkuerzt NUR p0. DemolishRoad
    // schrumpft die Route in-place - liefe sie ueber primary(), stuende hier 1/1 statt 1/2.
    press(10, padRoad::StepBack);
    BOOST_TEST(view(0).GetRoad().route.size() == 1u);
    BOOST_TEST(view(1).GetRoad().route.size() == 2u);
    BOOST_TEST((view(0).GetRoad().point == p0[1]));
    BOOST_TEST((view(1).GetRoad().point == p1[2]));

    // Und die Abbruchseite: p1 bricht ab, p0 baut ungestoert weiter.
    press(11, padRoad::StepBack);
    press(11, padRoad::StepBack);
    press(11, padRoad::StepBack);
    BOOST_TEST((view(1).GetRoad().mode == RoadBuildMode::Disabled));
    BOOST_TEST((view(0).GetRoad().mode == RoadBuildMode::Normal));
    BOOST_TEST(view(0).GetRoad().route.size() == 1u);
    BOOST_TEST(view(0).GetViewer().IsOnRoad(p0[1]));
}

/// Die visuelle Vorschau liegt je Viewer (GameWorldViewer::visualNodes). Ein Weg im Bau darf im
/// Bild des Nachbarn nicht auftauchen - und seine Bauqualitaet dort nicht verstellen.
BOOST_FIXTURE_TEST_CASE(ARoadUnderConstructionIsInvisibleInTheOtherPlayersView, PadViewFixture<2>)
{
    const GameWorldBase& world = worldFixture.world;
    const RoadSpot s0 = findRoadSpotFromHQ(view(0).GetViewer(), 3, 6);
    BOOST_TEST_REQUIRE(s0.isValid());
    const std::vector<MapPoint> p0 = roadPoints(world, s0.start, s0.route);
    std::vector<BuildingQuality> bqInOther;
    for(const MapPoint& pt : p0)
        bqInOther.push_back(view(1).GetViewer().GetBQ(pt));

    aimPadAt(10, 0, s0.start);
    aimPadAt(11, 1, hqFlagOf(world, 1));
    press(10, padRoad::Begin);
    for(unsigned i = 1; i < p0.size(); ++i)
    {
        aimAt(0, p0[i]);
        press(10, padRoad::Extend);
    }
    BOOST_TEST_REQUIRE(view(0).GetRoad().route.size() == s0.route.size());

    BOOST_TEST(viewerDrawsAnyOf(view(0).GetViewer(), s0.start, s0.route));
    BOOST_TEST(!viewerDrawsAnyOf(view(1).GetViewer(), s0.start, s0.route));
    for(unsigned i = 0; i < p0.size(); ++i)
        BOOST_TEST_CONTEXT("bei " << p0[i])
        BOOST_TEST((view(1).GetViewer().GetBQ(p0[i]) == bqInOther[i]));
}

/// Skalierung: der Schluessel ist wirklich die Ansicht und nicht bloss "Haupt- oder
/// Nichthauptspieler". Vorbild: FourViewsHoldFourWindowsOfTheSameTypeOpen.
BOOST_FIXTURE_TEST_CASE(FourViewsBuildFourRoadsIndependently, PadViewFixture<4>)
{
    const GameWorldBase& world = worldFixture.world;
    std::vector<RoadSpot> spots;
    std::vector<std::vector<MapPoint>> pts;
    for(unsigned v = 0; v < 4; ++v)
    {
        spots.push_back(findRoadSpotFromHQ(view(v).GetViewer(), 2, 6));
        BOOST_TEST_REQUIRE(spots.back().isValid());
        pts.push_back(roadPoints(world, spots[v].start, spots[v].route));
        aimPadAt(static_cast<PadDeviceId>(10 + v), v, spots[v].start);
    }
    for(unsigned v = 0; v < 4; ++v)
        press(static_cast<PadDeviceId>(10 + v), padRoad::Begin);

    // Reihum ein Wegstueck, damit die Bearbeitungen sich wirklich verschraenken.
    for(unsigned step_ = 1; step_ < 3; ++step_)
    {
        for(unsigned v = 0; v < 4; ++v)
        {
            aimAt(v, pts[v][step_]);
            press(static_cast<PadDeviceId>(10 + v), padRoad::Extend);
        }
        for(unsigned v = 0; v < 4; ++v)
            BOOST_TEST(view(v).GetRoad().route.size() == step_);
    }

    // Ansicht 2 nimmt einen Schritt zurueck - und NUR sie.
    press(12, padRoad::StepBack);
    BOOST_TEST(view(0).GetRoad().route.size() == 2u);
    BOOST_TEST(view(1).GetRoad().route.size() == 2u);
    BOOST_TEST(view(2).GetRoad().route.size() == 1u);
    BOOST_TEST(view(3).GetRoad().route.size() == 2u);
    for(unsigned v = 0; v < 4; ++v)
        BOOST_TEST((view(v).GetRoad().start == spots[v].start));
}

// ============================================================================================
// 6. Die Buchhaltung: in einer ECHTEN Partie, gemessen am Replay
// ============================================================================================

/// DER Nachweis dieser Phase. Der Name schliesst den kritischen Fall ein: nicht nur, dass die
/// Strasse entsteht, sondern dass sie auf IHN gebucht wird und auf niemanden sonst.
///
/// Er traegt doppelt: die Strasse kann ueberhaupt nur existieren, wenn auf Spieler 1 gebucht
/// wurde (GameWorld::BuildRoad lehnt eine fremde Startflagge ab), und das Replay sagt davon
/// unabhaengig dasselbe.
///
/// Gleichzeitig die Zahlenprobe auf die Invariante "A schreibt nichts fest": gebaut wird mit
/// EINEM X und mehreren A. Kaeme aus einem A ein Kommando, stuende hier eine zu grosse Zahl.
/// Und Negativkontrolle N1: Pad 10 ist angesteckt und benutzt, rueht sich aber nie.
BOOST_FIXTURE_TEST_CASE(PadPlayerBuildsARoadFromHisOwnFlagAndOnlyHeIsCharged, PadGameFixture)
{
    setUpTwoLocalPlayers();

    GameWorld& world_ = world();
    const GameWorldViewer& viewer = dsk->GetPlayerView(1).GetViewer();
    const RoadSpot spot = findRoadSpotFromHQ(viewer, 2, 5);
    BOOST_TEST_REQUIRE(spot.isValid());
    const std::vector<MapPoint> pts = roadPoints(world_, spot.start, spot.route);

    // Pad 10 nimmt Ansicht 0 in Besitz und rueht sich danach nie (N1). Es muss zuerst kommen:
    // ein Pad bekommt den niedrigsten freien Slot (PadRouter, Uebernahme durch Benutzung).
    aimPadAt(10, 0, world_.GetPlayer(0).GetHQPos());
    aimPadAt(11, 1, spot.end);

    const unsigned startGF = GAMECLIENT.GetGFNumber();

    // Erst eine Zielflagge setzen - ueber den Padpfad, nicht ueber eine Abkuerzung im Aufbau.
    press(11, padRoad::Commit); // ausserhalb des Baumodus ist X der Flaggenknopf
    pumpUntilGF(startGF + 20);
    BOOST_TEST_REQUIRE((world_.GetNO(spot.end)->GetType() == NodalObjectType::Flag));

    // Die Route, die BuildRoadPart gleich berechnen wird - mit demselben Produktivaufruf und
    // demselben Viewer, im selben Weltzustand.
    const std::vector<Direction> expected = FindPathForRoad(viewer, spot.start, spot.end, false, 100);
    BOOST_TEST_REQUIRE(expected.size() >= 2u);

    aimAt(1, spot.start);
    press(11, padRoad::Begin);
    BOOST_TEST_REQUIRE((dsk->GetPlayerView(1).GetRoad().mode == RoadBuildMode::Normal));

    aimAt(1, spot.end);
    press(11, padRoad::Extend);
    BOOST_TEST_REQUIRE((dsk->GetPlayerView(1).GetRoad().route == expected), boost::test_tools::per_element());

    press(11, padRoad::Commit);
    pumpUntilGF(GAMECLIENT.GetGFNumber() + 30);

    // --- Z1: der Spielzustand. Gemessen an World::GetPointRoad, also an der Simulation. -----
    BOOST_TEST(worldHasRoad(world_, spot.start, expected));
    MapPoint cur = spot.start;
    for(const Direction d : expected)
        cur = world_.GetNeighbour(cur, d);
    BOOST_TEST((cur == spot.end));
    BOOST_TEST((world_.GetPointRoad(cur, expected.back()) == PointRoad::None)); // Ende ist Ende
    // Die Strasse haengt an einer Flagge von Spieler 1.
    BOOST_TEST(world_.GetSpecObj<noFlag>(spot.start)->GetPlayer() == 1u);
    BOOST_TEST(world_.GetSpecObj<noFlag>(spot.end)->GetPlayer() == 1u);
    // Die Strasse steht auch im Bild des ANDEREN lokalen Spielers. Das ist die Gegenprobe zur
    // blossen Vorschau: die liegt je Viewer (GameWorldViewer::visualNodes) und waere fuer
    // Spieler 0 unsichtbar. Was er sieht, kann nur die wirkliche Strasse sein.
    BOOST_TEST(viewerDrawsRoad(dsk->GetPlayerView(0).GetViewer(), spot.start, expected));
    BOOST_TEST(viewerDrawsRoad(viewer, spot.start, expected));
    BOOST_TEST((dsk->GetPlayerView(1).GetRoad().mode == RoadBuildMode::Disabled));
    // N10
    BOOST_TEST(ci().numErrors == 0u);
    BOOST_TEST(ci().numAsync == 0u);

    tearDownDesktop();

    // --- Z2: die Buchhaltung. Zwei Kommandos fuer Spieler 1 (Flagge + Strasse), sonst keins. -
    const auto replayPath = stopAndGetReplay();
    BOOST_TEST(numGCsForPlayer(replayPath, 1) == 2u);
    BOOST_TEST(numGCsForPlayer(replayPath, 0) == 0u); // N1: das stumme Pad
    BOOST_TEST(numGCsForPlayer(replayPath, 2) == 0u); // N2: die KI zaehlt mit
}

/// Zwei Padspieler bauen gleichzeitig, und JEDER wird fuer seine eigene Strasse belastet.
BOOST_FIXTURE_TEST_CASE(TwoPadPlayersBuildTwoRoadsAtTheSameTimeAndEachIsChargedForHisOwn, PadGameFixture)
{
    setUpTwoLocalPlayers();

    GameWorld& world_ = world();
    const GameWorldViewer& v0 = dsk->GetPlayerView(0).GetViewer();
    const GameWorldViewer& v1 = dsk->GetPlayerView(1).GetViewer();
    const RoadSpot s0 = findRoadSpotFromHQ(v0, 2, 5);
    const RoadSpot s1 = findRoadSpotFromHQ(v1, 2, 5);
    BOOST_TEST_REQUIRE(s0.isValid());
    BOOST_TEST_REQUIRE(s1.isValid());

    aimPadAt(10, 0, s0.start);
    aimPadAt(11, 1, s1.start);

    const unsigned startGF = GAMECLIENT.GetGFNumber();

    // Verschraenkt, nicht nacheinander.
    press(10, padRoad::Begin);
    press(11, padRoad::Begin);
    aimAt(0, s0.end);
    press(10, padRoad::Extend);
    aimAt(1, s1.end);
    press(11, padRoad::Extend);

    const std::vector<Direction> r0 = dsk->GetPlayerView(0).GetRoad().route;
    const std::vector<Direction> r1 = dsk->GetPlayerView(1).GetRoad().route;
    BOOST_TEST_REQUIRE(r0.size() >= 2u);
    BOOST_TEST_REQUIRE(r1.size() >= 2u);

    press(11, padRoad::Commit); // p1 schreibt zuerst fest
    press(10, padRoad::Commit); // dann p0
    pumpUntilGF(startGF + 40);

    BOOST_TEST(worldHasRoad(world_, s0.start, r0));
    BOOST_TEST(worldHasRoad(world_, s1.start, r1));
    BOOST_TEST(world_.GetSpecObj<noFlag>(s0.start)->GetPlayer() == 0u);
    BOOST_TEST(world_.GetSpecObj<noFlag>(s1.start)->GetPlayer() == 1u);
    // Beide Strassen sind in BEIDEN Bildern zu sehen. Das ist die Gegenprobe zur blossen
    // Vorschau: die liegt je Viewer (GameWorldViewer::visualNodes) und waere fuer den anderen
    // unsichtbar. Was hier gekreuzt sichtbar ist, kann nur die wirkliche Strasse sein.
    BOOST_TEST(viewerDrawsRoad(v0, s0.start, r0));
    BOOST_TEST(viewerDrawsRoad(v1, s1.start, r1));
    BOOST_TEST(viewerDrawsRoad(v0, s1.start, r1));
    BOOST_TEST(viewerDrawsRoad(v1, s0.start, r0));
    BOOST_TEST(ci().numErrors == 0u);
    BOOST_TEST(ci().numAsync == 0u);

    tearDownDesktop();

    const auto replayPath = stopAndGetReplay();
    BOOST_TEST(numGCsForPlayer(replayPath, 0) == 1u);
    BOOST_TEST(numGCsForPlayer(replayPath, 1) == 1u);
    BOOST_TEST(numGCsForPlayer(replayPath, 2) == 0u);
}

/// Die Negativkontrollen der Buchhaltung an einem Stueck: ein Bau, der abgebrochen wird, ein
/// Bau, der nur zurueckgenommen wird, ein Startversuch auf fremder Flagge und ein Padspieler,
/// der ueberhaupt nichts drueckt - fuer NIEMANDEN darf dabei ein Kommando entstehen.
/// (N3, N9 und die Abbruchhaelfte.)
BOOST_FIXTURE_TEST_CASE(NeitherCancellingNorSteppingBackNorAForeignFlagEverChargesAnyPlayer, PadGameFixture)
{
    setUpTwoLocalPlayers();

    GameWorld& world_ = world();
    const GameWorldViewer& viewer = dsk->GetPlayerView(1).GetViewer();
    const RoadSpot spot = findRoadSpotFromHQ(viewer, 2, 5);
    BOOST_TEST_REQUIRE(spot.isValid());

    const MapPoint foreignFlag = [&] {
        const MapPoint hqPos = world_.GetPlayer(0).GetHQPos();
        return world_.GetSpecObj<nobBaseWarehouse>(hqPos)->GetFlagPos();
    }();

    aimPadAt(10, 0, world_.GetPlayer(0).GetHQPos()); // stummes Pad
    aimPadAt(11, 1, foreignFlag);

    const unsigned startGF = GAMECLIENT.GetGFNumber();

    // N3: Startversuch auf der Flagge von Spieler 0
    press(11, padRoad::Begin);
    BOOST_TEST((dsk->GetPlayerView(1).GetRoad().mode == RoadBuildMode::Disabled));

    // Bauen, zuruecknehmen, abbrechen - und nichts davon festschreiben
    aimAt(1, spot.start);
    press(11, padRoad::Begin);
    aimAt(1, spot.end);
    press(11, padRoad::Extend);
    const auto len = dsk->GetPlayerView(1).GetRoad().route.size();
    BOOST_TEST_REQUIRE(len >= 2u);
    for(unsigned i = 0; i <= len; ++i)
        press(11, padRoad::StepBack);
    BOOST_TEST((dsk->GetPlayerView(1).GetRoad().mode == RoadBuildMode::Disabled));
    BOOST_TEST(!viewerDrawsAnyOf(viewer, spot.start, spot.route));

    pumpUntilGF(startGF + 40);
    BOOST_TEST(!worldHasRoad(world_, spot.start, spot.route));
    BOOST_TEST(ci().numErrors == 0u);
    BOOST_TEST(ci().numAsync == 0u);

    tearDownDesktop();

    const auto replayPath = stopAndGetReplay();
    BOOST_TEST(numGCsForPlayer(replayPath, 0) == 0u);
    BOOST_TEST(numGCsForPlayer(replayPath, 1) == 0u);
    BOOST_TEST(numGCsForPlayer(replayPath, 2) == 0u);
}

BOOST_AUTO_TEST_SUITE_END()
