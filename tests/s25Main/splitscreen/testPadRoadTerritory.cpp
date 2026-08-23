// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

// BEFUND A: der Strassenbau am Pad ueber die eigene Gebietsgrenze hinaus - ein STILLER
// Fehlschlag, der trotzdem Netzverkehr erzeugt.
//
// Warum das ueberhaupt entstehen kann: PathConditionRoad::IsNodeOk verlangt IsPlayerTerritory,
// aber der Wegfinder ruft es fuer JEDEN Knoten AUSSER Start und Ziel
// (pathfinding/FreePathFinderImpl.h). Der ZIELknoten darf also ausserhalb des eigenen Gebiets
// liegen, und FindPathForRoad findet trotzdem einen Weg dorthin. GameWorld::BuildRoad prueft ihn
// erst am Ende, ueber "kann dort eine Flagge stehen" (world/GameWorld.cpp:222-241) - und
// ausserhalb des eigenen Gebiets ist GetBQ per AdjustBQ immer Nothing (world/World.cpp:222-225).
//
// Der Mauspfad kann in diesen Zustand gar nicht geraten: ContextClick verlangt fuer den
// Verlaengerungszweig ausdruecklich viewer.IsRoadAvailable(...) && viewer.IsPlayerTerritory(selPt),
// und die uebrigen Zweige haengen an GetBQ != Nothing bzw. an einer Flagge.
// dskGameInterface::PadExtendRoad prueft das NICHT.
//
// Gemessen wurde: X meldet Erfolg, der Modus geht auf Disabled, GetRejectionCount() bleibt bei 0
// (der Spieler bekommt KEINE Rueckmeldung), im Replay steht 1 GameCommand fuer Spieler 0 - und
// in der Welt steht keine einzige Kante.
//
// Beide Haelften stehen hier: die Entscheidung (ohne Netz, PadViewFixture) und die Buchhaltung
// (mit echtem Server und Replay, PadGameFixture).

#include "GamePlayer.h"
#include "Loader.h"
#include "PointOutput.h"
#include "RttrConfig.h"
#include "WindowManager.h"
#include "buildings/nobBaseWarehouse.h"
#include "desktops/PlayerView.h"
#include "driver/PadEvent.h"
#include "drivers/VideoDriverWrapper.h"
#include "files.h"
#include "network/GameClient.h"
#include "nodeObjs/noFlag.h"
#include "pathfinding/FindPathForRoad.h"
#include "world/GameWorld.h"
#include "PadFixture.h"
#include "PadGameFixture.h"
#include "gameTypes/BuildingQuality.h"
#include "helpers/EnumRange.h"
#include "gameTypes/RoadBuildMode.h"
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <vector>

using namespace rttr::test;

namespace {

/// Ein Zielknoten, den der Wegfinder vom eigenen HQ aus ERREICHT, auf dem aber niemals eine
/// Flagge stehen kann, weil er ausserhalb des eigenen Gebiets liegt.
///
/// Genau die Kombination ist der Befund: erreichbar (also baut die Vorschau) und trotzdem
/// unbaubar (also verwirft GameWorld::BuildRoad das Kommando still).
RoadSpot findRoadSpotOutsideTerritory(const GameWorldViewer& viewer, const unsigned minLen = 2,
                                      const unsigned maxLen = 10)
{
    const GameWorldBase& world = viewer.GetWorld();
    const auto player = static_cast<unsigned char>(viewer.GetPlayerId());
    const MapPoint hqPos = world.GetPlayer(player).GetHQPos();
    if(!hqPos.isValid())
        return {};
    const auto* hq = world.GetSpecObj<nobBaseWarehouse>(hqPos);
    if(!hq)
        return {};
    RoadSpot spot;
    spot.start = hq->GetFlagPos();
    for(const MapPoint& pt : world.GetPointsInRadiusWithCenter(spot.start, maxLen + 4))
    {
        if(pt == spot.start)
            continue;
        if(world.GetNode(pt).obj)
            continue; // dort steht schon etwas
        // DIE Bedingung des Befunds - und zwar so gelesen, wie der Produktivcode sie liest.
        if(viewer.IsPlayerTerritory(pt))
            continue;
        // Gegenprobe an der Simulation: dort kann keine Flagge entstehen, das Kommando ist also
        // sicher wirkungslos.
        if(world.GetBQ(pt, player) != BuildingQuality::Nothing)
            continue;
        std::vector<Direction> route = FindPathForRoad(viewer, spot.start, pt, false, 100);
        if(route.size() < minLen || route.size() > maxLen)
            continue;
        spot.end = pt;
        spot.route = std::move(route);
        return spot;
    }
    return {};
}

} // namespace

BOOST_AUTO_TEST_SUITE(PadRoadTerritoryTests)

// ============================================================================================
// 1. Die Entscheidung: A ueber die Gebietsgrenze verlaengert NICHT und sagt es
// ============================================================================================

/// Der gemessene Fall, an der produktiven Naht. Nur Padereignisse - kein GI_StartRoadBuilding,
/// kein BuildRoadPart, kein CommitRoad.
BOOST_FIXTURE_TEST_CASE(ExtendingAcrossTheOwnBorderIsRefusedAndTheReasonReachesThePlayer,
                        PadViewFixture<1>)
{
    const GameWorldViewer& viewer = view(0).GetViewer();
    const RoadSpot spot = findRoadSpotOutsideTerritory(viewer);
    BOOST_TEST_REQUIRE(spot.isValid());
    // Die Vorbedingungen des Befunds, ausgeschrieben: der Weg IST zu finden, der Endpunkt ist
    // trotzdem unbebaubar.
    BOOST_TEST_REQUIRE(!viewer.IsPlayerTerritory(spot.end));
    BOOST_TEST_REQUIRE((worldFixture.world.GetBQ(spot.end, 0) == BuildingQuality::Nothing));
    BOOST_TEST_INFO("Strecke ueber die Grenze: " << spot.route.size() << " Kanten nach " << spot.end);

    aimPadAt(10, 0, spot.start);
    press(10, padRoad::Begin);
    BOOST_TEST_REQUIRE((view(0).GetRoad().mode == RoadBuildMode::Normal));
    BOOST_TEST_REQUIRE(view(0).GetRejectionCount() == 0u);

    // A auf dem Knoten jenseits der Grenze.
    aimAt(0, spot.end);
    press(10, padRoad::Extend);

    // Es entsteht keine Vorschau ...
    BOOST_TEST(view(0).GetRoad().route.empty());
    BOOST_TEST((view(0).GetRoad().point == spot.start));
    BOOST_TEST(!viewerDrawsAnyOf(viewer, spot.start, spot.route));
    // ... und der Spieler erfaehrt, warum.
    BOOST_TEST(view(0).GetRejectionCount() == 1u);
    BOOST_TEST_REQUIRE(view(0).GetRejection().has_value());
    BOOST_TEST((*view(0).GetRejection() == PadRejection::RoadOutsideTerritory));
    // Der Baumodus bleibt stehen - abgebrochen wird mit B, nicht durch einen Fehlgriff.
    BOOST_TEST((view(0).GetRoad().mode == RoadBuildMode::Normal));
}

/// Und der Knopf, der wirklich etwas ins Netz schickt, kommt dort gar nicht mehr an: nach dem
/// abgelehnten A ist die Strecke leer, X meldet deshalb "zu kurz" und erzeugt kein Kommando.
BOOST_FIXTURE_TEST_CASE(AfterTheRefusalTheCommitButtonSendsNothing, PadViewFixture<1>)
{
    const RoadSpot spot = findRoadSpotOutsideTerritory(view(0).GetViewer());
    BOOST_TEST_REQUIRE(spot.isValid());

    aimPadAt(10, 0, spot.start);
    press(10, padRoad::Begin);
    aimAt(0, spot.end);
    press(10, padRoad::Extend);
    BOOST_TEST_REQUIRE(view(0).GetRoad().route.empty());

    press(10, padRoad::Commit);

    // Der Modus bleibt an (X bricht nichts ab) und die Ursache hat gewechselt - das ist die
    // Quittung, dass CommitRoad gar nicht erst durchgelaufen ist.
    BOOST_TEST((view(0).GetRoad().mode == RoadBuildMode::Normal));
    BOOST_TEST_REQUIRE(view(0).GetRejection().has_value());
    BOOST_TEST((*view(0).GetRejection() == PadRejection::RoadTooShort));
}

/// Die Gegenprobe, ohne die die Aussage oben nichts wert waere: INNERHALB des eigenen Gebiets
/// verlaengert derselbe Knopf weiterhin genau wie bisher.
BOOST_FIXTURE_TEST_CASE(InsideTheOwnTerritoryExtendingStillWorksExactlyAsBefore, PadViewFixture<1>)
{
    const RoadSpot spot = findRoadSpotFromHQ(view(0).GetViewer(), 3, 6);
    BOOST_TEST_REQUIRE(spot.isValid());

    aimPadAt(10, 0, spot.start);
    press(10, padRoad::Begin);
    aimAt(0, spot.end);
    press(10, padRoad::Extend);

    BOOST_TEST((view(0).GetRoad().route == spot.route), boost::test_tools::per_element());
    BOOST_TEST(view(0).GetRejectionCount() == 0u);
    BOOST_TEST(viewerDrawsRoad(view(0).GetViewer(), spot.start, spot.route));
}

// ============================================================================================
// 1b. Dieselbe Klasse Fehler am ENDE der Strecke: dort kann keine Flagge stehen
// ============================================================================================

/// Die Gebietspruefung allein reicht nicht. GameWorld::BuildRoad verlangt am Wegende, dass dort
/// eine Flagge STEHEN kann - und das ist auch INNERHALB des eigenen Gebiets nicht immer so:
/// neben einer bestehenden Flagge geht keine zweite (BQCalculator, "If any neighbour is a flag ->
/// Flag is impossible").
///
/// Der Mausspieler ist an dieser Stelle gedeckt, ohne dass es jemand aufgeschrieben haette:
/// iwRoadWindow bekommt sein enable_flag aus GetBQ(road.point) != Nothing, der Bauknopf ist dort
/// also gar nicht erst anwaehlbar. Der Padspieler hat kein Fenster - er braucht die Pruefung im
/// Knopf.
BOOST_FIXTURE_TEST_CASE(CommittingARoadThatCannotEndWhereItStopsIsRefused, PadViewFixture<1>)
{
    GameWorld& world = worldFixture.world;
    const GameWorldViewer& viewer = view(0).GetViewer();
    const RoadSpot spot = findRoadSpotFromHQ(viewer, 2, 4);
    BOOST_TEST_REQUIRE(spot.isValid());
    const std::vector<MapPoint> pts = roadPoints(world, spot.start, spot.route);

    // Eine zweite eigene Flagge NEBEN dem Zielknoten - nicht auf der Strecke selbst. Danach
    // kann am Zielknoten keine Flagge mehr entstehen, der Weg dorthin bleibt aber baubar.
    MapPoint blocker = MapPoint::Invalid();
    for(const Direction dir : helpers::EnumRange<Direction>{})
    {
        const MapPoint nb = world.GetNeighbour(spot.end, dir);
        if(std::find(pts.begin(), pts.end(), nb) != pts.end())
            continue;
        if(world.GetNode(nb).obj || world.IsFlagAround(nb))
            continue;
        if(world.GetBQ(nb, 0) == BuildingQuality::Nothing)
            continue;
        world.SetFlag(nb, 0);
        if(world.GetNO(nb)->GetType() == NodalObjectType::Flag)
        {
            blocker = nb;
            break;
        }
    }
    BOOST_TEST_REQUIRE(blocker.isValid());
    view(0).RecalcAllColors();
    BOOST_TEST_REQUIRE(viewer.IsPlayerTerritory(spot.end)); // im eigenen Gebiet ...
    // ... und trotzdem der Punkt, an dem GameWorld::BuildRoad die fertige Strasse ablehnt: es
    // steht eine Flagge daneben (world/GameWorld.cpp:234, "|| IsFlagAround(curPt)").
    BOOST_TEST_REQUIRE(world.IsFlagAround(spot.end));

    aimPadAt(10, 0, spot.start);
    press(10, padRoad::Begin);
    aimAt(0, spot.end);
    press(10, padRoad::Extend);
    // Verlaengern ist ausdruecklich ERLAUBT - genau wie beim Mausspieler, der von hier aus
    // weiterbauen darf.
    BOOST_TEST_REQUIRE((view(0).GetRoad().route == spot.route), boost::test_tools::per_element());
    BOOST_TEST_REQUIRE(view(0).GetRejectionCount() == 0u);
    // Erst JETZT liegt der Zielknoten auf einem Weg, und erst jetzt sagt seine Bauqualitaet, dass
    // dort keine Flagge mehr hinpasst - genau der Wert, an dem auch iwRoadWindow seinen Bauknopf
    // sperrt.
    BOOST_TEST_REQUIRE((viewer.GetBQ(spot.end) == BuildingQuality::Nothing));

    press(10, padRoad::Commit);

    BOOST_TEST_REQUIRE(view(0).GetRejection().has_value());
    BOOST_TEST((*view(0).GetRejection() == PadRejection::RoadEndBlocked));
    // Nichts abgeschickt, nichts verloren: die Strecke steht noch, der Modus auch.
    BOOST_TEST((view(0).GetRoad().mode == RoadBuildMode::Normal));
    BOOST_TEST((view(0).GetRoad().route == spot.route), boost::test_tools::per_element());
    BOOST_TEST(!worldHasRoad(world, spot.start, spot.route));

    // Gegenprobe: einen Knoten zurueck, dort kann eine Flagge stehen - und X schreibt fest.
    press(10, padRoad::StepBack);
    BOOST_TEST_REQUIRE(view(0).GetRoad().route.size() == spot.route.size() - 1u);
    if(view(0).GetRoad().route.size() >= 2u)
    {
        press(10, padRoad::Commit);
        BOOST_TEST((view(0).GetRoad().mode == RoadBuildMode::Disabled));
    }
}

// ============================================================================================
// 2. Die Buchhaltung: kein wirkungsloses Kommando im Netz
// ============================================================================================

/// Dieselbe Bedienfolge in einer LAUFENDEN Partie mit echtem Server. Gezaehlt wird im Replay -
/// also nur, was tatsaechlich vom Server zurueckkam.
///
/// Vorher: 1 GameCommand fuer Spieler 0 und 0 von N Kanten in der Welt. Jetzt: 0 Kommandos.
BOOST_FIXTURE_TEST_CASE(NoIneffectiveRoadCommandEverLeavesTheClient, PadGameFixture)
{
    setUpTwoLocalPlayers();

    const RoadSpot spot = findRoadSpotOutsideTerritory(dsk->GetPlayerView(0).GetViewer());
    BOOST_TEST_REQUIRE(spot.isValid());
    BOOST_TEST_INFO("Strecke ueber die Grenze: " << spot.route.size() << " Kanten nach " << spot.end);

    aimPadAt(10, 0, spot.start);
    press(10, PadButton::A); // Baumodus auf der eigenen HQ-Flagge
    BOOST_TEST_REQUIRE((dsk->GetPlayerView(0).GetRoad().mode == RoadBuildMode::Normal));

    aimAt(0, spot.end);
    const unsigned startGF = GAMECLIENT.GetGFNumber();
    press(10, PadButton::A); // verlaengern ueber die Grenze
    press(10, PadButton::X); // festschreiben

    pumpUntilGF(startGF + 40);

    // Z1: in der Welt steht keine einzige Kante - das war vorher genauso, es ist der Teil,
    // den der Spieler sieht.
    BOOST_TEST(!worldHasRoad(world(), spot.start, spot.route));
    BOOST_TEST(ci().numErrors == 0u);
    BOOST_TEST(ci().numAsync == 0u);

    tearDownDesktop();

    // Z2: und es ist auch nichts unterwegs gewesen.
    const auto replayPath = stopAndGetReplay();
    BOOST_TEST(numGCsForPlayer(replayPath, 0) == 0u);
}

BOOST_AUTO_TEST_SUITE_END()
