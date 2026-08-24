// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

// BEFUND B: der Flaggenreiter war am Pad unerreichbar.
//
// Auf einer EIGENEN Flagge faengt A sofort den Strassenbau an (PadStartRoad) und kommt gar nicht
// bis zur dritten Stufe (PadOpenActionWindow). Damit war am Pad unerreichbar, was iwAction im
// Flaggenreiter anbietet: Flagge abreissen, Geologe rufen, Spaeher rufen - und an einer
// Wasserflagge zusaetzlich der Wasserweg-Knopf, weil A auch dort sofort in den LANDstrassenbau
// springt. Ohne "Flagge abreissen" kann ein Padspieler eine falsch gesetzte Flagge nie wieder
// loswerden; das ist keine Randlage, sondern eine Sackgasse.
//
// Die Entscheidung dieser Runde: A bleibt, was es ist (Strassenbau in EINEM Druck - die
// haeufigste Handlung an einer Flagge), und der Schulterknopf RECHTS oeffnet das
// Aktionsfenster. Begruendet in dskGameInterface::OnPadButton.
//
// BEFUND C: ein Padspieler konnte iwAction nicht schliessen, ohne zu handeln. B loeste nur den
// Fokus; die Titelleistenknoepfe sind keine Controls und damit nicht fokussierbar. B in der
// WELT schliesst jetzt sein oberstes Fenster - nach derselben Regel, nach der der Rechtsklick
// des Mausspielers es tut.

#include "GamePlayer.h"
#include "Loader.h"
#include "PointOutput.h"
#include "RttrForeachPt.h"
#include "WindowManager.h"
#include "buildings/nobBaseWarehouse.h"
#include "controls/ctrlButton.h"
#include "controls/ctrlGroup.h"
#include "controls/ctrlTab.h"
#include "desktops/PlayerView.h"
#include "desktops/dskGameInterface.h"
#include "driver/PadEvent.h"
#include "ingameWindows/iwAction.h"
#include "input/FocusPath.h"
#include "lua/GameDataLoader.h"
#include "nodeObjs/noFlag.h"
#include "world/GameWorld.h"
#include "world/MapLoader.h"
#include "PadFixture.h"
#include "PadGameFixture.h"
#include "worldFixtures/terrainHelpers.h"
#include "gameTypes/BuildingQuality.h"
#include "gameTypes/FlagType.h"
#include "gameTypes/RoadBuildMode.h"
#include "gameData/const_gui_ids.h"
#include <boost/test/unit_test.hpp>
#include <string>
#include <vector>

using namespace rttr::test;

namespace {

/// Die Knopfbelegung dieses Befunds, an genau einer Stelle - wie padRoad in PadFixture.h.
namespace padFlag {
    /// Schulter rechts: das Aktionsfenster unter dem Zeiger oeffnen.
    constexpr PadButton OpenActions = PadButton::RightShoulder;
    /// B in der Welt: das oberste eigene Fenster schliessen.
    constexpr PadButton CloseWindow = PadButton::B;
    /// Y: in das oberste eigene Fenster hinein.
    constexpr PadButton Enter = PadButton::Y;
    /// A: bedienen (im Fenster) bzw. oeffnen/Strassenbau (in der Welt).
    constexpr PadButton Act = PadButton::A;
} // namespace padFlag

/// Die Knopfnummern des Flaggenreiters von iwAction (iwAction.cpp, Kopfkommentar).
constexpr unsigned kTabFlag = 4;
constexpr unsigned kFlagBtRoad = 1;
constexpr unsigned kFlagBtWaterway = 2;
constexpr unsigned kFlagBtPullDown = 3;
constexpr unsigned kFlagBtGeologist = 4;
constexpr unsigned kFlagBtScout = 5;

MapPoint hqFlagOf(const GameWorldBase& world, const unsigned char player)
{
    const MapPoint hqPos = world.GetPlayer(player).GetHQPos();
    const auto* hq = world.GetSpecObj<nobBaseWarehouse>(hqPos);
    BOOST_TEST_REQUIRE(hq != nullptr);
    return hq->GetFlagPos();
}

/// Ein Punkt im Gebiet DIESES Spielers, auf dem eine gewoehnliche Flagge stehen kann.
/// Bewusst NICHT die HQ-Flagge: die ist vom Typ HQ und bietet im Flaggenreiter nur einen
/// einzigen Knopf ("Strasse bauen") - an ihr laesst sich die Luecke gar nicht messen.
MapPoint findPlainFlagSpot(const GameWorld& world, const GameWorldViewer& viewer)
{
    const auto player = static_cast<unsigned char>(viewer.GetPlayerId());
    for(const MapPoint& pt : world.GetPointsInRadiusWithCenter(world.GetPlayer(player).GetHQPos(), 8))
    {
        if(!viewer.IsOwner(pt))
            continue;
        if(world.GetNO(pt)->GetType() != NodalObjectType::Nothing)
            continue;
        if(world.IsFlagAround(pt))
            continue;
        if(world.GetBQ(pt, player) < BuildingQuality::Flag)
            continue;
        // Der Nachbar im Nordwesten darf kein Gebaeude sein, sonst waere es eine Gebaeude- und
        // keine gewoehnliche Flagge (ShowActionWindow: FlagType::HQ / Storehouse).
        if(world.GetNO(world.GetNeighbour(pt, Direction::NorthWest))->GetType() != NodalObjectType::Nothing)
            continue;
        return pt;
    }
    return MapPoint::Invalid();
}

/// Ein eigener, freier Knoten mit MINDESTENS dieser Bauqualitaet - dort oeffnet A das
/// Aktionsfenster mit dem Baureiter. (Wortgleich zu der Hilfe in testPadBuilding.cpp; beide
/// liegen in einem anonymen Namensraum ihrer Uebersetzungseinheit.)
MapPoint findBuildSpot(const GameWorldBase& world, const GameWorldViewer& viewer, const BuildingQuality minBQ)
{
    RTTR_FOREACH_PT(MapPoint, world.GetSize())
    {
        if(!viewer.IsOwner(pt))
            continue;
        if(viewer.GetBQ(pt) < minBQ)
            continue;
        if(world.GetNO(pt)->GetType() != NodalObjectType::Nothing)
            continue;
        return pt;
    }
    return MapPoint::Invalid();
}

/// Der Knopf `btId` im Flaggenreiter des Aktionsfensters dieser Ansicht - oder nullptr.
ctrlButton* flagTabButton(PlayerView& view, const unsigned btId)
{
    iwAction* const wnd = view.actionwindow;
    if(!wnd)
        return nullptr;
    auto* mainTab = wnd->GetCtrl<ctrlTab>(0);
    if(!mainTab)
        return nullptr;
    ctrlGroup* group = mainTab->GetGroup(kTabFlag);
    if(!group)
        return nullptr;
    return group->GetCtrl<ctrlButton>(btId);
}

/// Faehrt den Fokus dieses Spielers mit dem ECHTEN Padweg (Schulter rechts = FocusPath::Dir::Next)
/// weiter, bis er auf `target` steht. Liefert die Zahl der dafuer noetigen Knopfdruecke, oder
/// 0, wenn das Ziel gar nicht erreicht wird.
template<class T_Fixture>
unsigned padFocusOnto(T_Fixture& f, const unsigned viewIdx, const PadDeviceId dev, const Window* target)
{
    PlayerView& view = f.playerViewOf(viewIdx);
    for(unsigned presses = 0; presses < 32u; ++presses)
    {
        if(view.GetFocus().GetFocused() == target)
            return presses;
        f.press(dev, PadButton::RightShoulder);
    }
    return 0;
}

/// Eine WINZIGE Insel in einem See - woertlich die Welt aus testPadFeedback.cpp. Gebraucht,
/// weil eine WASSERflagge ein Wasserfeld am Knoten voraussetzt (noFlag.cpp:41).
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

/// PadViewFixture mit dem Zugriff, den die Hilfen oben brauchen.
template<unsigned T_numViews, unsigned T_numPlayers = T_numViews, class T_WorldCreator = CreateEmptyWorld,
         unsigned T_width = 60, unsigned T_height = 30>
struct FlagPadFixture : PadViewFixture<T_numViews, T_numPlayers, T_WorldCreator, T_width, T_height>
{
    PlayerView& playerViewOf(unsigned idx) { return this->view(idx); }
};

using WaterFlagFixture = FlagPadFixture<1, 1, CreateLakeIslandWorld, 40, 40>;

} // namespace

BOOST_AUTO_TEST_SUITE(PadFlagActionTests)

// ============================================================================================
// BEFUND B - 1. Die Luecke selbst: der Flaggenreiter ist am Pad erreichbar
// ============================================================================================

/// Auf einer eigenen Flagge oeffnet der Schulterknopf rechts das Aktionsfenster - und A faengt
/// unveraendert in EINEM Druck den Strassenbau an. Beide Knoepfe nebeneinander gemessen, damit
/// die Entscheidung nicht die eine Haelfte fuer die andere opfert.
BOOST_FIXTURE_TEST_CASE(OnHisOwnFlagThePadPlayerReachesBothTheRoadAndTheActionWindow, FlagPadFixture<2>)
{
    const MapPoint flagPt = hqFlagOf(worldFixture.world, 1);

    pads.connect(11);
    step(16);
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(11, 1));
    step(16);
    padSteerTo(11, 1, flagPt);

    // (a) Der Schulterknopf oeffnet das Aktionsfenster - und faengt KEINEN Strassenbau an.
    press(11, padFlag::OpenActions);
    BOOST_TEST_MESSAGE("AUDIT: Schulter rechts auf eigener Flagge -> actionwindow="
                       << (view(1).actionwindow ? "ja" : "nein")
                       << "  roadmode="
                       << (view(1).GetRoad().mode == RoadBuildMode::Disabled ? "Disabled" : "Normal"));
    BOOST_TEST_REQUIRE(view(1).actionwindow != static_cast<iwAction*>(nullptr));
    BOOST_TEST(view(1).actionwindow->GetOwner() == 1u);
    BOOST_TEST((view(1).GetRoad().mode == RoadBuildMode::Disabled));
    // Die andere Ansicht bekommt davon nichts.
    BOOST_TEST(view(0).actionwindow == static_cast<iwAction*>(nullptr));

    // Und der Flaggenreiter ist wirklich drin.
    BOOST_TEST(flagTabButton(view(1), kFlagBtRoad) != static_cast<ctrlButton*>(nullptr));

    // (b) Das Fenster wieder zu - mit B, ohne zu handeln (BEFUND C).
    iwAction* const wnd = view(1).actionwindow;
    press(11, padFlag::CloseWindow);
    BOOST_TEST_REQUIRE(wnd->ShouldBeClosed());
    if(!wnd->ShouldBeClosed())
        wnd->Close();
    dsk->Msg_WindowClosed(*wnd);
    WINDOWMANAGER.Draw();

    // (c) A auf demselben Knoten: der Strassenbau faengt in EINEM Druck an, wie bisher.
    press(11, padRoad::Begin);
    BOOST_TEST((view(1).GetRoad().mode == RoadBuildMode::Normal));
    BOOST_TEST((view(1).GetRoad().start == flagPt));
}

/// Die eigentliche Sackgasse: "Flagge abreissen". Gemessen wird der ganze Weg vom Zeiger bis auf
/// den Knopf - ausschliesslich mit Padereignissen - und dabei gezaehlt, was er kostet.
BOOST_FIXTURE_TEST_CASE(ThePullDownFlagButtonIsReachableByPad, FlagPadFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint flagPt = findPlainFlagSpot(world, view(1).GetViewer());
    BOOST_TEST_REQUIRE(flagPt.isValid());
    // Testaufbau, nicht der Pfad unter Pruefung: eine gewoehnliche Flagge dieses Spielers.
    world.SetFlag(flagPt, 1);
    BOOST_TEST_REQUIRE((world.GetNO(flagPt)->GetType() == NodalObjectType::Flag));
    BOOST_TEST_REQUIRE((world.GetSpecObj<noFlag>(flagPt)->GetFlagType() == FlagType::Normal));

    pads.connect(11);
    step(16);
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(11, 1));
    step(16);
    padSteerTo(11, 1, flagPt);

    unsigned inputs = 0;
    press(11, padFlag::OpenActions);
    ++inputs;
    BOOST_TEST_REQUIRE(view(1).actionwindow != static_cast<iwAction*>(nullptr));
    // Alle vier Knoepfe des gewoehnlichen Flaggenreiters sind da.
    BOOST_TEST_REQUIRE(flagTabButton(view(1), kFlagBtRoad) != static_cast<ctrlButton*>(nullptr));
    ctrlButton* const pullDown = flagTabButton(view(1), kFlagBtPullDown);
    BOOST_TEST_REQUIRE(pullDown != static_cast<ctrlButton*>(nullptr));
    BOOST_TEST(flagTabButton(view(1), kFlagBtGeologist) != static_cast<ctrlButton*>(nullptr));
    BOOST_TEST(flagTabButton(view(1), kFlagBtScout) != static_cast<ctrlButton*>(nullptr));

    press(11, padFlag::Enter);
    ++inputs;
    BOOST_TEST_REQUIRE(view(1).GetFocus().IsActive());

    const unsigned steps = padFocusOnto(*this, 1, 11, pullDown);
    BOOST_TEST_REQUIRE(view(1).GetFocus().GetFocused() == static_cast<Window*>(pullDown));
    inputs += steps;
    ++inputs; // das A, das den Knopf ausloest

    BOOST_TEST_MESSAGE("AUDIT: Eingaben bis auf den Knopf 'Flagge abreissen' (Schulterschritte) = " << inputs);
    BOOST_TEST(inputs <= 8u);

    // Derselbe Weg mit dem STICKKREUZ - die Knoepfe des Reiters liegen in einer Reihe unter den
    // Reiterkoepfen, also ist das der kuerzere Weg. Gemessen und nicht behauptet: erst den Fokus
    // loesen (B im Fenster), dann neu hinein und rein raeumlich navigieren.
    press(11, padFlag::CloseWindow); // B im Fenster: nur der Fokus geht
    BOOST_TEST_REQUIRE(!view(1).GetFocus().IsActive());
    BOOST_TEST_REQUIRE(view(1).actionwindow != static_cast<iwAction*>(nullptr));

    unsigned dpadInputs = 1; // die Schulter, die das Fenster geoeffnet hat
    press(11, padFlag::Enter);
    ++dpadInputs;
    press(11, PadButton::DpadDown);
    ++dpadInputs;
    press(11, PadButton::DpadRight);
    ++dpadInputs;
    BOOST_TEST_REQUIRE(view(1).GetFocus().GetFocused() == static_cast<Window*>(pullDown));
    ++dpadInputs; // das A
    BOOST_TEST_MESSAGE("AUDIT: Eingaben bis auf den Knopf 'Flagge abreissen' (Stickkreuz) = " << dpadInputs);
    BOOST_TEST(dpadInputs == 5u);

    iwAction* const wnd = view(1).actionwindow;
    if(!wnd->ShouldBeClosed())
        wnd->Close();
    dsk->Msg_WindowClosed(*wnd);
    WINDOWMANAGER.Draw();
}

/// Die Wasserflagge: dort sprang A ebenfalls sofort in den LANDstrassenbau, und der zweite
/// Knopf des Reiters - der Wasserweg - war zusammen mit dem Rest unerreichbar. Der
/// Schulterknopf sieht die Flaggenart gar nicht an; hier wird gemessen, dass das Fenster dort
/// wirklich mit allen fuenf Knoepfen aufgeht.
BOOST_FIXTURE_TEST_CASE(OnAWaterFlagThePadPlayerReachesTheWholeFlagTab, WaterFlagFixture)
{
    GameWorld& world = worldFixture.world;
    const GameWorldViewer& viewer = view(0).GetViewer();
    const auto isWater = [](const auto& desc) { return desc.kind == TerrainKind::Water; };

    MapPoint flagPt = MapPoint::Invalid();
    for(const MapPoint& pt : world.GetPointsInRadiusWithCenter(world.GetPlayer(0).GetHQPos(), 6))
    {
        if(!viewer.IsOwner(pt) || !world.HasTerrain(pt, isWater))
            continue;
        if(world.GetNO(pt)->GetType() != NodalObjectType::Nothing || world.IsFlagAround(pt))
            continue;
        if(world.GetBQ(pt, 0) < BuildingQuality::Flag)
            continue;
        if(world.GetNO(world.GetNeighbour(pt, Direction::NorthWest))->GetType() != NodalObjectType::Nothing)
            continue;
        flagPt = pt;
        break;
    }
    BOOST_TEST_REQUIRE(flagPt.isValid());
    world.SetFlag(flagPt, 0);
    BOOST_TEST_REQUIRE((world.GetSpecObj<noFlag>(flagPt)->GetFlagType() == FlagType::Water));

    aimPadAt(10, 0, flagPt);
    press(10, padFlag::OpenActions);
    BOOST_TEST_REQUIRE(view(0).actionwindow != static_cast<iwAction*>(nullptr));
    BOOST_TEST((view(0).GetRoad().mode == RoadBuildMode::Disabled));
    for(const unsigned btId : {kFlagBtRoad, kFlagBtWaterway, kFlagBtPullDown, kFlagBtGeologist, kFlagBtScout})
        BOOST_TEST_CONTEXT("Knopf " << btId)
    BOOST_TEST(flagTabButton(view(0), btId) != static_cast<ctrlButton*>(nullptr));

    iwAction* const wnd = view(0).actionwindow;
    if(!wnd->ShouldBeClosed())
        wnd->Close();
    dsk->Msg_WindowClosed(*wnd);
    WINDOWMANAGER.Draw();
}

/// Was ComputeActionOptions auf einer EIGENEN Flagge WIRKLICH anbietet - gemessen statt
/// uebernommen. Der Pruefbericht nannte zusaetzlich cutroad und upgradeRoad; beide sind auf
/// einem Flaggenknoten ausdruecklich ausgeschlossen (dskGameInterface.cpp: der Wegblock laeuft
/// nur, wenn der Knoten WEDER Flagge NOCH Gebaeude ist). Dieser Nachweis haelt das fest, damit
/// die Behauptung nicht ein zweites Mal weitergereicht wird.
BOOST_FIXTURE_TEST_CASE(AFlagNodeNeverOffersCutRoadOrUpgradeRoad, FlagPadFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint flagPt = findPlainFlagSpot(world, view(1).GetViewer());
    BOOST_TEST_REQUIRE(flagPt.isValid());
    world.SetFlag(flagPt, 1);

    pads.connect(11);
    step(16);
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(11, 1));
    step(16);
    padSteerTo(11, 1, flagPt);
    press(11, padFlag::OpenActions);
    BOOST_TEST_REQUIRE(view(1).actionwindow != static_cast<iwAction*>(nullptr));

    auto* mainTab = view(1).actionwindow->GetCtrl<ctrlTab>(0);
    BOOST_TEST_REQUIRE(mainTab != static_cast<ctrlTab*>(nullptr));
    BOOST_TEST(mainTab->GetGroup(kTabFlag) != static_cast<ctrlGroup*>(nullptr));
    BOOST_TEST(mainTab->GetGroup(5) == static_cast<ctrlGroup*>(nullptr)); // TAB_CUTROAD

    iwAction* const wnd = view(1).actionwindow;
    if(!wnd->ShouldBeClosed())
        wnd->Close();
    dsk->Msg_WindowClosed(*wnd);
    WINDOWMANAGER.Draw();
}

// ============================================================================================
// BEFUND B - 2. Bis zum Replay: die Flagge verschwindet, und zwar auf dem RICHTIGEN Konto
// ============================================================================================

/// Der ganze Weg in einer LAUFENDEN Partie: der Padspieler von Ansicht 1 setzt mit X eine
/// Flagge, oeffnet mit der Schulter das Aktionsfenster, geht mit Y hinein, faehrt auf
/// "Flagge abreissen" und drueckt A. Gezaehlt wird im Replay - also nur, was tatsaechlich vom
/// Server zurueckkam - und in der Welt.
BOOST_FIXTURE_TEST_CASE(APadPlayerCanPullDownHisOwnFlagAndItBooksOnHisOwnAccount, PadGameFixture)
{
    setUpTwoLocalPlayers();

    const MapPoint flagPt = findExclusiveFlagSpot(world(), 1, 0);
    BOOST_TEST_REQUIRE(flagPt.isValid());

    pads.pickUp(10); // Ansicht 0
    step(16);
    aimPadAt(11, 1, flagPt);

    // 1. Flagge setzen - X, der einzige Knopf, der in der Welt etwas festschreibt.
    const unsigned startGF = GAMECLIENT.GetGFNumber();
    press(11, PadButton::X);
    pumpUntilGF(startGF + 20);
    BOOST_TEST_REQUIRE((world().GetNO(flagPt)->GetType() == NodalObjectType::Flag));
    BOOST_TEST_REQUIRE(world().GetSpecObj<noFlag>(flagPt)->GetPlayer() == 1u);

    // 2. Das Aktionsfenster - der Knopf, den es vor dieser Runde nicht gab.
    press(11, PadButton::RightShoulder);
    PlayerView& view1 = dsk->GetPlayerView(1);
    BOOST_TEST_REQUIRE(view1.actionwindow != static_cast<iwAction*>(nullptr));
    BOOST_TEST_REQUIRE(view1.actionwindow->GetOwner() == 1u);
    auto* mainTab = view1.actionwindow->GetCtrl<ctrlTab>(0);
    BOOST_TEST_REQUIRE(mainTab != static_cast<ctrlTab*>(nullptr));
    ctrlGroup* flagGroup = mainTab->GetGroup(kTabFlag);
    BOOST_TEST_REQUIRE(flagGroup != static_cast<ctrlGroup*>(nullptr));
    auto* pullDown = flagGroup->GetCtrl<ctrlButton>(kFlagBtPullDown);
    BOOST_TEST_REQUIRE(pullDown != static_cast<ctrlButton*>(nullptr));

    // 3. Hinein und auf den Knopf - nur mit Padereignissen.
    press(11, PadButton::Y);
    BOOST_TEST_REQUIRE(view1.GetFocus().IsActive());
    for(unsigned i = 0; i < 32u && view1.GetFocus().GetFocused() != static_cast<Window*>(pullDown); ++i)
        press(11, PadButton::RightShoulder);
    BOOST_TEST_REQUIRE(view1.GetFocus().GetFocused() == static_cast<Window*>(pullDown));

    const unsigned demolishGF = GAMECLIENT.GetGFNumber();
    press(11, PadButton::A);
    pumpUntilGF(demolishGF + 30);

    BOOST_TEST_MESSAGE("AUDIT: Flagge nach dem Abreissen noch da - "
                       << (world().GetNO(flagPt)->GetType() == NodalObjectType::Flag ? "ja" : "nein"));
    BOOST_TEST((world().GetNO(flagPt)->GetType() != NodalObjectType::Flag));
    BOOST_TEST(ci().numErrors == 0u);
    BOOST_TEST(ci().numAsync == 0u);

    if(IngameWindow* wnd = dsk->GetPlayerView(1).actionwindow)
    {
        if(!wnd->ShouldBeClosed())
            wnd->Close();
        dsk->Msg_WindowClosed(*wnd);
    }
    WINDOWMANAGER.Draw();

    tearDownDesktop();
    const auto replayPath = stopAndGetReplay();
    const unsigned gcs0 = numGCsForPlayer(replayPath, 0);
    const unsigned gcs1 = numGCsForPlayer(replayPath, 1);
    BOOST_TEST_MESSAGE("AUDIT: GameCommands im Replay -> Spieler0=" << gcs0 << "  Spieler1=" << gcs1);
    BOOST_TEST(gcs1 == 2u); // Flagge setzen + Flagge abreissen
    BOOST_TEST(gcs0 == 0u);
}

// ============================================================================================
// BEFUND C - B schliesst das Fenster, ohne dass gehandelt werden muss
// ============================================================================================

/// Der gemeldete Fall: A auf einem bebaubaren Knoten oeffnet iwAction, und der Spieler will es
/// nur wieder loswerden. Vorher blieb es stehen, bis er handelte.
BOOST_FIXTURE_TEST_CASE(APadPlayerCanCloseHisActionWindowWithoutActing, FlagPadFixture<2>)
{
    const MapPoint spot = findBuildSpot(worldFixture.world, view(1).GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(spot.isValid());

    pads.connect(11);
    step(16);
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(11, 1));
    step(16);
    padSteerTo(11, 1, spot);

    press(11, padFlag::Act);
    iwAction* const wnd = view(1).actionwindow;
    BOOST_TEST_REQUIRE(wnd != static_cast<iwAction*>(nullptr));

    press(11, padFlag::CloseWindow);
    BOOST_TEST_MESSAGE("AUDIT: iwAction nach B geschlossen - " << (wnd->ShouldBeClosed() ? "ja" : "nein"));
    BOOST_TEST(wnd->ShouldBeClosed());
    // Und es ist dabei nichts passiert: kein Baumodus, keine Ablehnungsmeldung als Ersatz.
    BOOST_TEST((view(1).GetRoad().mode == RoadBuildMode::Disabled));

    if(!wnd->ShouldBeClosed())
        wnd->Close();
    dsk->Msg_WindowClosed(*wnd);
    WINDOWMANAGER.Draw();
    BOOST_TEST(WINDOWMANAGER.GetTopMostWindow(1) == static_cast<IngameWindow*>(nullptr));
}

/// B bleibt INNERHALB des Fensters, was es war: der Knopf, der den Fokus loest. Erst der
/// zweite Druck - jetzt in der Welt - schliesst. Damit verliert der Padspieler nicht die
/// Moeglichkeit, ein Fenster stehen zu lassen und weiterzuschauen.
BOOST_FIXTURE_TEST_CASE(BFirstReleasesTheFocusAndOnlyThenClosesTheWindow, FlagPadFixture<2>)
{
    const MapPoint spot = findBuildSpot(worldFixture.world, view(1).GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(spot.isValid());

    pads.connect(11);
    step(16);
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(11, 1));
    step(16);
    padSteerTo(11, 1, spot);

    press(11, padFlag::Act);
    iwAction* const wnd = view(1).actionwindow;
    BOOST_TEST_REQUIRE(wnd != static_cast<iwAction*>(nullptr));
    press(11, padFlag::Enter);
    BOOST_TEST_REQUIRE(view(1).GetFocus().IsActive());

    // Erstes B: nur der Fokus geht - das Fenster bleibt.
    press(11, padFlag::CloseWindow);
    BOOST_TEST(!view(1).GetFocus().IsActive());
    BOOST_TEST(!wnd->ShouldBeClosed());

    // Zweites B: jetzt schliesst es.
    press(11, padFlag::CloseWindow);
    BOOST_TEST(wnd->ShouldBeClosed());

    if(!wnd->ShouldBeClosed())
        wnd->Close();
    dsk->Msg_WindowClosed(*wnd);
    WINDOWMANAGER.Draw();
}

/// Der Besitz gilt auch fuer das Schliessen: B von Spieler 1 darf das Fenster von Spieler 0
/// niemals anfassen - genauso wenig, wie Y es betreten darf.
BOOST_FIXTURE_TEST_CASE(TheCloseButtonNeverTouchesAnotherPlayersWindow, FlagPadFixture<2>)
{
    const MapPoint spot0 = findBuildSpot(worldFixture.world, view(0).GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(spot0.isValid());

    pads.connect(10);
    pads.connect(11);
    step(16);
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(10, 0));
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(11, 1));
    step(16);

    padSteerTo(10, 0, spot0);
    press(10, padFlag::Act);
    iwAction* const wnd0 = view(0).actionwindow;
    BOOST_TEST_REQUIRE(wnd0 != static_cast<iwAction*>(nullptr));
    BOOST_TEST_REQUIRE(wnd0->GetOwner() == 0u);

    // Spieler 1 drueckt B - sein eigenes oberstes Fenster gibt es nicht, das von Spieler 0
    // bleibt unangetastet.
    press(11, padFlag::CloseWindow);
    BOOST_TEST(!wnd0->ShouldBeClosed());

    // Spieler 0 drueckt B - jetzt geht seines zu.
    press(10, padFlag::CloseWindow);
    BOOST_TEST(wnd0->ShouldBeClosed());

    if(!wnd0->ShouldBeClosed())
        wnd0->Close();
    dsk->Msg_WindowClosed(*wnd0);
    WINDOWMANAGER.Draw();
}

/// Dieselbe Regel wie beim Rechtsklick des Mausspielers (WindowManager::Msg_RightDown): ein
/// angeheftetes Fenster bleibt stehen. Ohne das koennte der Padspieler wegwerfen, was der
/// Mausspieler ausdruecklich festgesteckt hat.
BOOST_FIXTURE_TEST_CASE(APinnedWindowSurvivesTheCloseButton, FlagPadFixture<2>)
{
    const MapPoint spot = findBuildSpot(worldFixture.world, view(1).GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(spot.isValid());

    pads.connect(11);
    step(16);
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(11, 1));
    step(16);
    padSteerTo(11, 1, spot);

    press(11, padFlag::Act);
    iwAction* const wnd = view(1).actionwindow;
    BOOST_TEST_REQUIRE(wnd != static_cast<iwAction*>(nullptr));
    wnd->SetPinned(true);

    press(11, padFlag::CloseWindow);
    BOOST_TEST(!wnd->ShouldBeClosed());

    wnd->SetPinned(false);
    press(11, padFlag::CloseWindow);
    BOOST_TEST(wnd->ShouldBeClosed());

    if(!wnd->ShouldBeClosed())
        wnd->Close();
    dsk->Msg_WindowClosed(*wnd);
    WINDOWMANAGER.Draw();
}

/// Im Baumodus behaelt B seine gewachsene Bedeutung: ein Wegstueck zurueck. Das Schliessen darf
/// ihm dort nicht dazwischenfunken - sonst verloere der Padspieler seinen Rueckwaertsgang.
BOOST_FIXTURE_TEST_CASE(InRoadModeBStillStepsBackAndClosesNothing, FlagPadFixture<2>)
{
    const GameWorldBase& world = worldFixture.world;
    const RoadSpot spot = findRoadSpotFromHQ(view(1).GetViewer(), 2, 4);
    BOOST_TEST_REQUIRE(spot.isValid());
    const std::vector<MapPoint> pts = roadPoints(world, spot.start, spot.route);

    pads.connect(11);
    step(16);
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(11, 1));
    step(16);
    padSteerTo(11, 1, spot.start);

    // Ein Fenster, das offen bleiben MUSS, waehrend der Baumodus laeuft.
    press(11, padFlag::OpenActions);
    iwAction* const wnd = view(1).actionwindow;
    BOOST_TEST_REQUIRE(wnd != static_cast<iwAction*>(nullptr));

    press(11, padRoad::Begin);
    BOOST_TEST_REQUIRE((view(1).GetRoad().mode == RoadBuildMode::Normal));
    aimAt(1, pts[1]);
    press(11, padRoad::Extend);
    BOOST_TEST_REQUIRE(view(1).GetRoad().route.size() == 1u);

    press(11, padRoad::StepBack);
    BOOST_TEST(view(1).GetRoad().route.empty());
    BOOST_TEST((view(1).GetRoad().mode == RoadBuildMode::Normal));
    BOOST_TEST(!wnd->ShouldBeClosed());

    // Noch ein B: leere Strecke -> Abbruch, und auch das schliesst nichts.
    press(11, padRoad::StepBack);
    BOOST_TEST((view(1).GetRoad().mode == RoadBuildMode::Disabled));
    BOOST_TEST(!wnd->ShouldBeClosed());

    if(!wnd->ShouldBeClosed())
        wnd->Close();
    dsk->Msg_WindowClosed(*wnd);
    WINDOWMANAGER.Draw();
}

BOOST_AUTO_TEST_SUITE_END()
