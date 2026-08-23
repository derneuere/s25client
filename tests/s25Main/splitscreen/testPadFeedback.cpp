// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

// BEFUND 3: die Sackgasse am Pad - X ohne jede Rueckmeldung.
//
// Gemessen war: A einmal von der eigenen Flagge aus -> route.size() == 1. X -> CommitRoad ->
// route.size() < 2 -> false, Modus bleibt Normal. Noch einmal X: dasselbe. Der Spieler konnte
// beliebig oft druecken, nichts passierte, nichts sagte es ihm; einziger Ausweg war B.
//
// Ton und Chatzeile lassen sich im Test nicht messen (der eine braucht die Soundausgabe, die
// andere den Zeichenpfad). Gemessen wird deshalb der Zustand, aus dem BEIDE entstehen:
// PlayerView::GetRejection. Genau den liest dskGameInterface::PadReject, bevor es Ton und Text
// ausgibt - der Test prueft also die Entscheidung und nicht die Ausgabe.
//
// BEFUND 4 steht ebenfalls hier: der Wasserweg am Laengenanschlag.

#include "GamePlayer.h"
#include "PointOutput.h"
#include "WindowManager.h"
#include "buildings/nobBaseWarehouse.h"
#include "desktops/PlayerView.h"
#include "driver/PadEvent.h"
#include "drivers/VideoDriverWrapper.h"
#include "nodeObjs/noFlag.h"
#include "world/GameWorld.h"
#include "PadFixture.h"
#include "RttrForeachPt.h"
#include "lua/GameDataLoader.h"
#include "world/MapLoader.h"
#include "addons/AddonMaxWaterwayLength.h"
#include "worldFixtures/terrainHelpers.h"
#include "gameTypes/RoadBuildMode.h"
#include "gameData/const_gui_ids.h"
#include <boost/test/unit_test.hpp>

using namespace rttr::test;

namespace {
/// Eine WINZIGE Insel in einem See.
///
/// Warum nicht CreateWaterWorld: dort ist die Insel acht Knoten gross, das Gebiet des Spielers
/// reicht knapp bis an die Kueste, und PathConditionRoad::IsNodeOk verlangt fuer JEDEN
/// Zwischenknoten IsPlayerTerritory. Ein Bootsweg konnte dort deshalb hoechstens eine Kante
/// lang werden (gemessen) - der Laengenanschlag waere gar nicht erreichbar gewesen.
///
/// Mit einer Insel von drei Knoten Radius bleibt zwischen Kueste und Gebietsgrenze ein breiter
/// Ring aus EIGENEM Wasser, auf dem ein Bootsweg von mehr als drei Kanten Platz hat.
struct CreateLakeIslandWorld
{
    explicit CreateLakeIslandWorld(const MapExtent& size) : size_(size) {}

    bool operator()(GameWorld& world) const
    {
        loadGameData(world.GetDescriptionWriteable());
        world.Init(size_);
        const auto water = GetWaterTerrain(world.GetDescription());
        RTTR_FOREACH_PT(MapPoint, size_)
        {
            MapNode& node = world.GetNodeWriteable(pt);
            node.t1 = node.t2 = water;
        }
        const auto land = GetLandTerrain(world.GetDescription(), ETerrain::Buildable);
        const MapPoint hqPos(15, 15);
        for(const MapPoint& pt : world.GetPointsInRadiusWithCenter(hqPos, 3))
        {
            MapNode& node = world.GetNodeWriteable(pt);
            node.t1 = node.t2 = land;
        }
        BOOST_TEST_REQUIRE(MapLoader::PlaceHQs(world, std::vector<MapPoint>{hqPos}, false));
        return true;
    }

private:
    MapExtent size_;
};

using WaterPadFixture = PadViewFixture<1, 1, CreateLakeIslandWorld, 40, 40>;

MapPoint hqFlagOf(const GameWorldBase& world, const unsigned char player)
{
    const MapPoint hqPos = world.GetPlayer(player).GetHQPos();
    const auto* hq = world.GetSpecObj<nobBaseWarehouse>(hqPos);
    BOOST_TEST_REQUIRE(hq != nullptr);
    return hq->GetFlagPos();
}
} // namespace

BOOST_AUTO_TEST_SUITE(PadFeedbackTests)

// ============================================================================================
// BEFUND 3: X auf einer zu kurzen Strecke sagt es dem Spieler
// ============================================================================================

BOOST_FIXTURE_TEST_CASE(CommittingATooShortRoadTellsThePlayerWhy, PadViewFixture<1>)
{
    const RoadSpot spot = findRoadSpotFromHQ(view(0).GetViewer(), 2, 4);
    BOOST_TEST_REQUIRE(spot.isValid());
    const std::vector<MapPoint> pts = roadPoints(worldFixture.world, spot.start, spot.route);

    aimPadAt(10, 0, spot.start);
    press(10, padRoad::Begin);
    BOOST_TEST_REQUIRE((view(0).GetRoad().mode == RoadBuildMode::Normal));
    BOOST_TEST_REQUIRE(!view(0).GetRejection().has_value());

    // Genau EIN Wegstueck - zu wenig fuer GameWorld::BuildRoad.
    aimAt(0, pts[1]);
    press(10, padRoad::Extend);
    BOOST_TEST_REQUIRE(view(0).GetRoad().route.size() == 1u);
    BOOST_TEST_REQUIRE(!view(0).GetRejection().has_value());

    // X. Vorher: nichts. Jetzt: eine Begruendung, die Ton und Chatzeile speist.
    press(10, padRoad::Commit);
    BOOST_TEST_REQUIRE(view(0).GetRejection().has_value());
    BOOST_TEST((*view(0).GetRejection() == PadRejection::RoadTooShort));
    BOOST_TEST(view(0).GetRejectionCount() == 1u);
    // Der Modus bleibt bewusst stehen: X bricht keinen laufenden Bau ab, das tut B.
    BOOST_TEST((view(0).GetRoad().mode == RoadBuildMode::Normal));

    // Noch einmal X: der Ton kommt wieder (jeder Druck zaehlt), die Ursache bleibt dieselbe -
    // daran haengt die Unterdrueckung der Chatzeile.
    press(10, padRoad::Commit);
    BOOST_TEST(view(0).GetRejectionCount() == 2u);
    BOOST_TEST((*view(0).GetRejection() == PadRejection::RoadTooShort));
}

/// Und die Meldung verschwindet wieder, sobald etwas gelingt - sonst stuende sie fuer immer.
BOOST_FIXTURE_TEST_CASE(AnythingThatWorksClearsTheStandingMessage, PadViewFixture<1>)
{
    const RoadSpot spot = findRoadSpotFromHQ(view(0).GetViewer(), 2, 4);
    BOOST_TEST_REQUIRE(spot.isValid());
    const std::vector<MapPoint> pts = roadPoints(worldFixture.world, spot.start, spot.route);

    aimPadAt(10, 0, spot.start);
    press(10, padRoad::Begin);
    aimAt(0, pts[1]);
    press(10, padRoad::Extend);
    press(10, padRoad::Commit);
    BOOST_TEST_REQUIRE(view(0).GetRejection().has_value());

    // Verlaengern gelingt - die stehende Meldung ist damit erledigt.
    aimAt(0, pts.back());
    press(10, padRoad::Extend);
    BOOST_TEST(view(0).GetRoad().route.size() == spot.route.size());
    BOOST_TEST(!view(0).GetRejection().has_value());
}

/// Der Sackgassencharakter ist damit weg: derselbe Knopf, dieselbe Lage, aber der Spieler
/// bekommt bei JEDEM Druck eine Rueckmeldung - und B ist nicht mehr der einzige Ausweg, den
/// er nur durch Probieren finden kann.
BOOST_FIXTURE_TEST_CASE(EveryPressIsAnswered, PadViewFixture<1>)
{
    const RoadSpot spot = findRoadSpotFromHQ(view(0).GetViewer(), 2, 4);
    BOOST_TEST_REQUIRE(spot.isValid());
    const std::vector<MapPoint> pts = roadPoints(worldFixture.world, spot.start, spot.route);
    aimPadAt(10, 0, spot.start);
    press(10, padRoad::Begin);
    aimAt(0, pts[1]);
    press(10, padRoad::Extend);

    for(unsigned i = 1; i <= 5u; ++i)
    {
        press(10, padRoad::Commit);
        BOOST_TEST(view(0).GetRejectionCount() == i);
    }
}

// ============================================================================================
// BEFUND 4: der Wasserweg am Laengenanschlag
// ============================================================================================

/// Die Aussage von Befund 4, an der produktiven Naht: am Anschlag meldet BuildRoadPart
/// AtLengthLimit - und ausdruecklich NICHT Rejected. Nur mit dieser Unterscheidung kann der
/// Mauspfad darauf so reagieren wie frueher (naemlich gar nicht) und der Padpfad es dem
/// Spieler sagen.
BOOST_FIXTURE_TEST_CASE(AWaterwayAtItsLimitReportsTheLimitAndNotARejection, WaterPadFixture)
{
    // Kuerzester moeglicher Anschlag: 3 Kanten (AddonMaxWaterwayLength, Auswahl 0).
    worldFixture.ggs.setSelection(AddonId::MAX_WATERWAY_LENGTH, 0);
    BOOST_TEST_REQUIRE(waterwayLengths[0] == 3u);

    const GameWorldBase& world = worldFixture.world;
    const GameWorldViewer& viewer = view(0).GetViewer();

    // Ein Kuestenpunkt der Insel und ein Wasserpunkt weit draussen, zwischen denen ein
    // Bootsweg von mehr als drei Kanten moeglich ist. Gesucht wird mit DEMSELBEN
    // Produktivaufruf, den BuildRoadPart benutzt.
    MapPoint start = MapPoint::Invalid();
    MapPoint farPt = MapPoint::Invalid();
    const MapPoint hq = world.GetPlayer(0).GetHQPos();
    for(const MapPoint& s : world.GetPointsInRadiusWithCenter(hq, 12))
    {
        if(!world.IsWaterPoint(s))
            continue;
        for(const MapPoint& f : world.GetPointsInRadiusWithCenter(s, 10))
        {
            if(f == s || !world.IsWaterPoint(f))
                continue;
            const auto route = FindPathForRoad(viewer, s, f, true, 100);
            if(route.size() > 4u)
            {
                start = s;
                farPt = f;
                break;
            }
        }
        if(start.isValid())
            break;
    }
    BOOST_TEST_REQUIRE(start.isValid());
    BOOST_TEST_REQUIRE(farPt.isValid());

    dsk->GI_StartRoadBuilding(start, /*waterRoad*/ true);
    BOOST_TEST_REQUIRE((dsk->GetRoadMode() == RoadBuildMode::Boat));

    // Erster Zug: die Strecke wird auf drei Kanten zurechtgestutzt - das ist Erfolg, und cSel
    // wandert auf das neue, gekuerzte Wegende.
    MapPoint target = farPt;
    BOOST_TEST_REQUIRE((dsk->BuildRoadPart(view(0), target) == dskGameInterface::RoadPartResult::Built));
    BOOST_TEST_REQUIRE(view(0).GetRoad().route.size() == 3u);
    BOOST_TEST_REQUIRE((target != farPt)); // gekuerzt

    // Zweiter Zug auf denselben fernen Punkt: jetzt passt kein Stueck mehr hinein.
    MapPoint target2 = farPt;
    const auto res = dsk->BuildRoadPart(view(0), target2);
    BOOST_TEST((res == dskGameInterface::RoadPartResult::AtLengthLimit));
    BOOST_TEST((res != dskGameInterface::RoadPartResult::Rejected));
    // cSel bleibt unangetastet, und die Strecke waechst nicht.
    BOOST_TEST((target2 == farPt));
    BOOST_TEST(view(0).GetRoad().route.size() == 3u);
}

/// Und das Verhalten, um das es dem Mausspieler geht: der Klick am Anschlag tut nichts. Kein
/// Strassenfenster, und damit auch kein Mauswarp aus dessen Konstruktor.
BOOST_FIXTURE_TEST_CASE(AMouseClickAtTheWaterwayLimitOpensNoWindowAndDoesNotMoveTheMouse, WaterPadFixture)
{
    worldFixture.ggs.setSelection(AddonId::MAX_WATERWAY_LENGTH, 0);
    const GameWorldBase& world = worldFixture.world;
    const GameWorldViewer& viewer = view(0).GetViewer();

    // Diesmal muss der ferne Punkt AUCH die Bedingung des ersten ContextClick-Zweiges
    // erfuellen - sonst maesse dieser Fall einen anderen Zweig als den, den Befund 4 nennt.
    MapPoint start = MapPoint::Invalid();
    MapPoint farPt = MapPoint::Invalid();
    const MapPoint hq = world.GetPlayer(0).GetHQPos();
    for(const MapPoint& s : world.GetPointsInRadiusWithCenter(hq, 12))
    {
        if(!world.IsWaterPoint(s))
            continue;
        for(const MapPoint& f : world.GetPointsInRadiusWithCenter(s, 10))
        {
            if(f == s || !world.IsWaterPoint(f))
                continue;
            if(!viewer.IsRoadAvailable(true, f) || !viewer.IsPlayerTerritory(f))
                continue;
            if(FindPathForRoad(viewer, s, f, true, 100).size() > 4u)
            {
                start = s;
                farPt = f;
                break;
            }
        }
        if(start.isValid())
            break;
    }
    BOOST_TEST_REQUIRE(start.isValid());
    BOOST_TEST_REQUIRE(farPt.isValid());

    dsk->GI_StartRoadBuilding(start, /*waterRoad*/ true);
    MapPoint target = farPt;
    BOOST_TEST_REQUIRE((dsk->BuildRoadPart(view(0), target) == dskGameInterface::RoadPartResult::Built));
    BOOST_TEST_REQUIRE(view(0).GetRoad().route.size() == 3u);

    // Der Mausklick auf den fernen Punkt.
    const Position mousePos = nodeViewPos(0, farPt);
    BOOST_TEST_REQUIRE(view(0).ContainsViewPos(mousePos));
    VIDEODRIVER.SetMousePos(mousePos);
    step(16, mousePos);
    BOOST_TEST_REQUIRE((gwv(0).GetSelectedPt() == farPt));
    const auto routeBefore = view(0).GetRoad().route;

    dsk->ContextClick(MouseCoords(mousePos));

    BOOST_TEST(WINDOWMANAGER.GetTopMostWindow(0) == static_cast<IngameWindow*>(nullptr));
    BOOST_TEST(view(0).roadwindow == static_cast<IngameWindow*>(nullptr));
    BOOST_TEST((VIDEODRIVER.GetMousePos() == mousePos));
    BOOST_TEST((view(0).GetRoad().route == routeBefore), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_SUITE_END()
