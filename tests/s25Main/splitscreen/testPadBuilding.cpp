// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

// DIE LETZTE LUECKE: der Padspieler konnte kein Gebaeude setzen.
//
// Erkunden, Fenster oeffnen und bedienen, Flaggen setzen, Strassen bauen - alles gemessen. Nur
// der Bau fehlte, und damit konnte ein Padspieler keine Partie von vorn spielen. Bauen laeuft
// im Original ueber iwAction, das Klick-Popup mit den Bautabs, und das hing ausschliesslich am
// Mauspfad: A auf einem leeren Knoten tat nichts.
//
// Gemessen wird hier ausschliesslich am PRODUKTIVEN Weg - nur Padereignisse, keine
// selbstgesetzten Besitzklammern, kein direkter Aufruf von ShowActionWindow oder
// SetBuildingSite. Genau das ist die Lehre dieses Projekts: mehrfach war ein Mechanismus
// korrekt gebaut, im Test bewiesen und im Spiel unerreichbar.

#include "GamePlayer.h"
#include "Loader.h"
#include "PointOutput.h"
#include "RttrConfig.h"
#include "RttrForeachPt.h"
#include "WindowManager.h"
#include "buildings/nobBaseWarehouse.h"
#include "controls/ctrlBuildingIcon.h"
#include "controls/ctrlButton.h"
#include "controls/ctrlGroup.h"
#include "controls/ctrlTab.h"
#include "desktops/PlayerView.h"
#include "desktops/dskGameInterface.h"
#include "drivers/VideoDriverWrapper.h"
#include "files.h"
#include "ingameWindows/iwAction.h"
#include "input/FocusPath.h"
#include "network/GameClient.h"
#include "buildings/noBuildingSite.h"
#include "nodeObjs/noFlag.h"
#include "world/GameWorld.h"
#include "world/GameWorldViewer.h"
#include "PadFixture.h"
#include "PadGameFixture.h"
#include "NodalObjectTypes.h"
#include "gameTypes/BuildingQuality.h"
#include "gameTypes/BuildingType.h"
#include "gameTypes/RoadBuildMode.h"
#include "gameData/const_gui_ids.h"
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <string>
#include <vector>

using namespace rttr::test;

namespace {

/// Ein Knoten, auf dem GENAU DIESER Spieler eine Huette setzen kann und der andere nicht.
///
/// Die zweite Haelfte ist der Punkt: GameWorld::SetBuildingSite prueft die BQ FUER DEN SPIELER,
/// der das Kommando geschickt hat. Landet das Kommando beim falschen Spieler, entsteht die
/// Baustelle also gar nicht - ein vertauschter Absender ist hier ein ECHTER Fehlschlag und
/// nicht bloss eine kosmetische Abweichung.
MapPoint findExclusiveHutSpot(const GameWorldBase& world, const GameWorldViewer& viewer,
                              const unsigned char otherPlayer)
{
    const auto player = static_cast<unsigned char>(viewer.GetPlayerId());
    RTTR_FOREACH_PT(MapPoint, world.GetSize())
    {
        if(!viewer.IsOwner(pt))
            continue;
        if(viewer.GetBQ(pt) < BuildingQuality::Hut)
            continue;
        if(world.GetBQ(pt, otherPlayer) != BuildingQuality::Nothing)
            continue; // waere kein Unterscheidungsmerkmal
        if(world.GetNO(pt)->GetType() != NodalObjectType::Nothing)
            continue;
        return pt;
    }
    return MapPoint::Invalid();
}

/// Ein eigener, freier Knoten mit MINDESTENS dieser Bauqualitaet. Gebraucht fuer den Nachweis
/// zur zweiten Reiterreihe: die Reiter "Huette/Haus/Burg" legt iwAction nur an, wenn die BQ sie
/// hergibt (iwAction.cpp: tabs.build_tabs).
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

/// Ein Knoten, auf dem dieser Spieler NICHTS tun kann: neutrales Gebiet, kein Objekt.
/// Gesucht wird in der Naehe seines HQ, damit der Zeiger ihn mit dem Stick auch erreicht.
MapPoint findDeadNode(const GameWorldBase& world, const GameWorldViewer& viewer)
{
    const MapPoint hqPos = world.GetPlayer(viewer.GetPlayerId()).GetHQPos();
    if(!hqPos.isValid())
        return MapPoint::Invalid();
    for(const MapPoint& pt : world.GetPointsInRadiusWithCenter(hqPos, 16))
    {
        if(viewer.IsOwner(pt))
            continue;
        if(world.GetNode(pt).owner != 0)
            continue; // fremdes Gebiet - dort gaebe es evtl. einen Angriffsreiter
        if(world.GetNO(pt)->GetType() != NodalObjectType::Nothing)
            continue;
        return pt;
    }
    return MapPoint::Invalid();
}

/// Das fokussierte Control DIESES Spielers - der Zustand, den der Padpfad wirklich fuehrt.
const Window* focusedOf(PlayerView& view)
{
    return view.GetFocus().GetFocused();
}

MapPoint hqFlagOf(const GameWorldBase& world, const unsigned char player)
{
    const MapPoint hqPos = world.GetPlayer(player).GetHQPos();
    const auto* hq = world.GetSpecObj<nobBaseWarehouse>(hqPos);
    BOOST_TEST_REQUIRE(hq != nullptr);
    return hq->GetFlagPos();
}

} // namespace

BOOST_AUTO_TEST_SUITE(PadBuildingTests)

// ============================================================================================
// 1. DER Nachweis: ein Padspieler setzt ein Gebaeude - und es wird IHM berechnet
// ============================================================================================

/// Nur Padereignisse: zielen, A (Aktionsfenster), Y (hinein), Stick bis auf das Bauicon, A.
/// Danach steht die Baustelle in der WELT und im Replay steht genau EIN GameCommand fuer IHN.
///
/// Gezaehlt wird im Replay, also nur, was tatsaechlich vom Server zurueckkam - ein
/// clientlokaler Kurzschluss taucht dort nicht auf.
BOOST_FIXTURE_TEST_CASE(APadPlayerBuildsAHutThroughTheActionWindow, PadGameFixture)
{
    setUpTwoLocalPlayers();

    PlayerView& padView = dsk->GetPlayerView(1);
    const MapPoint spot = findExclusiveHutSpot(world(), padView.GetViewer(), 0);
    BOOST_TEST_REQUIRE(spot.isValid());

    // Pad 10 nimmt Ansicht 0 (und ruehrt sich danach nicht mehr), Pad 11 Ansicht 1.
    aimPadAt(10, 0, hqFlagOf(world(), 0));
    aimPadAt(11, 1, spot);
    BOOST_TEST_REQUIRE((dsk->GetPlayerView(1).GetView().GetSelectedPt() == spot));
    BOOST_TEST_REQUIRE((dsk->GetPlayerView(0).GetView().GetSelectedPt() != spot));

    const unsigned startGF = GAMECLIENT.GetGFNumber();

    // --- A: das Aktionsfenster geht auf, im Besitz DIESES Sitzplatzes ---
    press(11, PadButton::A);
    iwAction* const wnd = padView.actionwindow;
    BOOST_TEST_REQUIRE(wnd != static_cast<iwAction*>(nullptr));
    BOOST_TEST(wnd->GetOwner() == 1u);
    BOOST_TEST(dsk->GetPlayerView(0).actionwindow == static_cast<iwAction*>(nullptr));
    BOOST_TEST((wnd->GetSelectedPt() == spot));

    // --- Y: hinein. Ab hier gehoert der linke Stick dem Fokus dieses Spielers. ---
    press(11, PadButton::Y);
    BOOST_TEST_REQUIRE(padView.GetFocus().IsActive());
    BOOST_TEST_REQUIRE(padView.GetFocus().GetRoot() == static_cast<Window*>(wnd));

    // --- Stick nach unten, bis der Fokus auf einem Bauicon steht ---
    unsigned inputs = 2; // A und Y
    const auto focusedIcon = [&] { return dynamic_cast<const ctrlBuildingIcon*>(padView.GetFocus().GetFocused()); };
    for(unsigned i = 0; i < 8u && !focusedIcon(); ++i)
    {
        press(11, PadButton::DpadDown);
        ++inputs;
    }
    const ctrlBuildingIcon* icon = focusedIcon();
    BOOST_TEST_REQUIRE(icon != static_cast<const ctrlBuildingIcon*>(nullptr));
    // ... und nach rechts, bis es das gewuenschte Gebaeude ist.
    for(unsigned i = 0; i < 12u && icon->GetType() != BuildingType::Woodcutter; ++i)
    {
        press(11, PadButton::DpadRight);
        ++inputs;
        icon = focusedIcon();
        BOOST_TEST_REQUIRE(icon != static_cast<const ctrlBuildingIcon*>(nullptr));
    }
    BOOST_TEST_REQUIRE((icon->GetType() == BuildingType::Woodcutter));

    // --- A: bauen ---
    press(11, PadButton::A);
    ++inputs;
    BOOST_TEST_MESSAGE("Eingaben fuer eine Huette (ohne Zielen): " << inputs);

    pumpUntilGF(startGF + 40);

    // --- Z1: Weltzustand. Die Baustelle steht, und sie gehoert Spieler 1. ---
    BOOST_TEST_REQUIRE((world().GetNO(spot)->GetType() == NodalObjectType::Buildingsite));
    const auto* site = world().GetSpecObj<noBuildingSite>(spot);
    BOOST_TEST_REQUIRE(site != static_cast<const noBuildingSite*>(nullptr));
    BOOST_TEST(unsigned(site->GetPlayer()) == 1u);
    BOOST_TEST((site->GetBuildingType() == BuildingType::Woodcutter));
    BOOST_TEST(ci().numErrors == 0u);
    BOOST_TEST(ci().numAsync == 0u);

    WINDOWMANAGER.Draw();
    tearDownDesktop();

    // --- Z2: Buchhaltung. Genau ein Kommando, und zwar fuer Spieler 1. ---
    const auto replayPath = stopAndGetReplay();
    BOOST_TEST(numGCsForPlayer(replayPath, 0) == 0u);
    BOOST_TEST(numGCsForPlayer(replayPath, 1) == 1u);
    BOOST_TEST(numGCsForPlayer(replayPath, 2) == 0u);
}

// ============================================================================================
// 2. Das Aktionsfenster am Pad darf die eine echte Maus nicht bewegen
// ============================================================================================

/// iwAction warpt im Konstruktor die Maus auf seinen ersten Reiter (VIDEODRIVER.SetMousePos) und
/// beim Schliessen wieder zurueck. Fuer den Mausspieler ist das seit jeher eine Bequemlichkeit -
/// es ist SEIN Zeiger. Oeffnet ein PADspieler dasselbe Fenster, risse es dem Mausspieler den
/// Zeiger aus der Hand.
BOOST_FIXTURE_TEST_CASE(TheActionWindowOpenedByAPadPlayerMovesNoMousePointer, PadViewFixture<2>)
{
    const MapPoint spot = findExclusiveHutSpot(worldFixture.world, view(1).GetViewer(), 0);
    BOOST_TEST_REQUIRE(spot.isValid());

    pads.connect(11);
    step(16);
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(11, 1));
    step(16);
    padSteerTo(11, 1, spot);

    const Position mouseBefore = VIDEODRIVER.GetMousePos();
    press(11, PadButton::A);
    BOOST_TEST_REQUIRE(view(1).actionwindow != static_cast<iwAction*>(nullptr));
    BOOST_TEST((VIDEODRIVER.GetMousePos() == mouseBefore));

    // Und auch das Schliessen zieht ihn nicht zurueck.
    view(1).actionwindow->Close();
    WINDOWMANAGER.Draw();
    BOOST_TEST((VIDEODRIVER.GetMousePos() == mouseBefore));
}

/// Die harte Randbedingung dazu: fuer den MAUSspieler bleibt alles, wie es war - inklusive des
/// Warps auf den ersten Reiter. Es ist sein eigener Zeiger, und der Einzelspieler soll sich
/// exakt wie heute verhalten.
BOOST_FIXTURE_TEST_CASE(TheMousePlayersActionWindowStillWarpsHisOwnPointer, PadViewFixture<1>)
{
    const MapPoint spot = findExclusiveHutSpot(worldFixture.world, view(0).GetViewer(),
                                               static_cast<unsigned char>(1));
    BOOST_TEST_REQUIRE(spot.isValid());

    const Position mousePos = nodeViewPos(0, spot);
    BOOST_TEST_REQUIRE(view(0).ContainsViewPos(mousePos));
    step(16, mousePos);
    BOOST_TEST_REQUIRE((gwv(0).GetSelectedPt() == spot));

    BOOST_TEST(dsk->ContextClick(MouseCoords(mousePos)));
    BOOST_TEST_REQUIRE(view(0).actionwindow != static_cast<iwAction*>(nullptr));
    // Der Warp des Konstruktors: der Zeiger steht jetzt IM Fenster.
    BOOST_TEST((VIDEODRIVER.GetMousePos() == view(0).actionwindow->GetDrawPos() + DrawPoint(20, 75)));

    view(0).actionwindow->Close();
    WINDOWMANAGER.Draw();
    // ... und beim Schliessen wieder zurueck auf die Klickstelle.
    BOOST_TEST((VIDEODRIVER.GetMousePos() == mousePos));
}

// ============================================================================================
// 3. Ein Knoten, auf dem nichts geht, muss dem Spieler ANTWORTEN
// ============================================================================================

/// Ohne Rueckmeldung sieht ein Druck, der nichts tut, fuer den Spieler aus wie ein totes Pad -
/// genau der Befund, wegen dem PlayerView::NoteRejection eingefuehrt wurde.
BOOST_FIXTURE_TEST_CASE(AOnANodeWhereNothingIsPossibleAnswersThePlayer, PadViewFixture<2>)
{
    const MapPoint dead = findDeadNode(worldFixture.world, view(1).GetViewer());
    BOOST_TEST_REQUIRE(dead.isValid());

    pads.connect(11);
    step(16);
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(11, 1));
    step(16);
    padSteerTo(11, 1, dead);

    BOOST_TEST_REQUIRE(!view(1).GetRejection().has_value());
    press(11, PadButton::A);

    BOOST_TEST(view(1).actionwindow == static_cast<iwAction*>(nullptr));
    BOOST_TEST(view(1).GetRejection().has_value());
    BOOST_TEST(view(1).GetRejectionCount() == 1u);
    // Und der Nachbar bleibt davon voellig unberuehrt.
    BOOST_TEST(!view(0).GetRejection().has_value());
}

// ============================================================================================
// 4. Der Strassenknopf des Aktionsfensters wirkt auf DESSEN Ansicht
// ============================================================================================

/// iwAction ruft gi.GI_StartRoadBuilding() - ohne jeden Spielerbezug (GameInterface.h kennt
/// keine Ansichten). Die Funktion haengt bis hierher an primary(): oeffnet eine ANDERE Ansicht
/// das Aktionsfenster, startet der Bauknopf den Strassenbau bei der Hauptansicht.
///
/// Erreichbar ist dieser Zustand seit BEFUND 2 auf dem MAUSpfad: ContextClick oeffnet das
/// Fenster fuer die Ansicht unter dem Mauszeiger, und das kann jede Ansicht ohne Pad sein.
BOOST_FIXTURE_TEST_CASE(TheActionWindowRoadButtonActsOnTheViewThatOwnsTheWindow, PadViewFixture<2>)
{
    const GameWorldBase& world = worldFixture.world;
    const MapPoint flagPt = hqFlagOf(world, 1);

    // Ansicht 0 bekommt ein Pad, damit die Maus wirklich bei Ansicht 1 landet.
    pads.pickUp(10);
    step(16);
    BOOST_TEST_REQUIRE(view(0).HasPadCursor());
    BOOST_TEST_REQUIRE(!view(1).HasPadCursor());

    const Position mousePos = nodeViewPos(1, flagPt);
    BOOST_TEST_REQUIRE(view(1).ContainsViewPos(mousePos));
    step(16, mousePos);
    BOOST_TEST_REQUIRE((gwv(1).GetSelectedPt() == flagPt));

    BOOST_TEST_REQUIRE(dsk->ContextClick(MouseCoords(mousePos)));
    iwAction* const wnd = view(1).actionwindow;
    BOOST_TEST_REQUIRE(wnd != static_cast<iwAction*>(nullptr));
    BOOST_TEST_REQUIRE(view(0).actionwindow == static_cast<iwAction*>(nullptr));

    // Der Flaggenreiter (TAB_FLAG == 4) und darin Knopf 1: "Strasse bauen". Ausgeloest wird er
    // mit ctrlButton::Activate - GENAU der Aufruf, den auch Msg_LeftUp und der Padfokus machen.
    auto* mainTab = wnd->GetCtrl<ctrlTab>(0);
    BOOST_TEST_REQUIRE(mainTab != static_cast<ctrlTab*>(nullptr));
    ctrlGroup* flagGroup = mainTab->GetGroup(4);
    BOOST_TEST_REQUIRE(flagGroup != static_cast<ctrlGroup*>(nullptr));
    mainTab->SetSelection(0, true); // der Flaggenreiter ist der erste und einzige Haupttab hier
    auto* roadBt = flagGroup->GetCtrl<ctrlButton>(1);
    BOOST_TEST_REQUIRE(roadBt != static_cast<ctrlButton*>(nullptr));
    BOOST_TEST_REQUIRE(roadBt->Activate());

    BOOST_TEST((view(1).GetRoad().mode == RoadBuildMode::Normal));
    BOOST_TEST((view(1).GetRoad().start == flagPt));
    // ... und die Hauptansicht merkt nichts davon.
    BOOST_TEST((view(0).GetRoad().mode == RoadBuildMode::Disabled));

    WINDOWMANAGER.Draw();
}

/// Die Verschaerfung desselben Falls, und der Grund, warum die Antwort nicht aus einer Suche
/// ueber die Ansichten kommen darf: ZWEI Ansichten koennen gleichzeitig ein Aktionsfenster
/// offen haben. Mit der einen Maus ist das in zwei Klicks erreicht - ein Klick in den einen
/// Viewport, ein Klick in den anderen. Eine Suche "welche Ansicht hat ein Aktionsfenster"
/// liefert dann die ERSTE der Liste und damit unter Umstaenden die, die gar nicht gedrueckt
/// hat.
///
/// Gedrueckt wird deshalb hier nicht mit Activate(), sondern ueber den WindowManager: genau er
/// setzt beim Zustellen die Besitzklammer (RelayMouseMessage -> ScopedWindowOwner), aus der
/// ActionWindowOwner liest.
BOOST_FIXTURE_TEST_CASE(TheRoadButtonActsOnThePressedWindowEvenWithTwoOpen, PadViewFixture<2>)
{
    const GameWorldBase& world = worldFixture.world;

    // Erstes Fenster: Ansicht 0.
    const MapPoint flagPt0 = hqFlagOf(world, 0);
    const Position mousePos0 = nodeViewPos(0, flagPt0);
    BOOST_TEST_REQUIRE(view(0).ContainsViewPos(mousePos0));
    step(16, mousePos0);
    BOOST_TEST_REQUIRE((gwv(0).GetSelectedPt() == flagPt0));
    BOOST_TEST_REQUIRE(dsk->ContextClick(MouseCoords(mousePos0)));
    BOOST_TEST_REQUIRE(view(0).actionwindow != static_cast<iwAction*>(nullptr));

    // Zweites Fenster: Ansicht 1. Das erste bleibt dabei offen - iwAction wird je Ansicht
    // gefuehrt (PlayerView::actionwindow).
    const MapPoint flagPt1 = hqFlagOf(world, 1);
    const Position mousePos1 = nodeViewPos(1, flagPt1);
    BOOST_TEST_REQUIRE(view(1).ContainsViewPos(mousePos1));
    step(16, mousePos1);
    BOOST_TEST_REQUIRE((gwv(1).GetSelectedPt() == flagPt1));
    BOOST_TEST_REQUIRE(dsk->ContextClick(MouseCoords(mousePos1)));
    iwAction* const wnd = view(1).actionwindow;
    BOOST_TEST_REQUIRE(wnd != static_cast<iwAction*>(nullptr));
    BOOST_TEST_REQUIRE(view(0).actionwindow != static_cast<iwAction*>(nullptr));
    BOOST_TEST_REQUIRE(wnd->GetOwner() == 1u);

    auto* mainTab = wnd->GetCtrl<ctrlTab>(0);
    BOOST_TEST_REQUIRE(mainTab != static_cast<ctrlTab*>(nullptr));
    mainTab->SetSelection(0, true);
    ctrlGroup* flagGroup = mainTab->GetGroup(4); // TAB_FLAG
    BOOST_TEST_REQUIRE(flagGroup != static_cast<ctrlGroup*>(nullptr));
    auto* roadBt = flagGroup->GetCtrl<ctrlButton>(1);
    BOOST_TEST_REQUIRE(roadBt != static_cast<ctrlButton*>(nullptr));

    // Der volle Mausweg auf den Knopf. Der erste Losklick raeumt nur die Sperre, die
    // WindowManager::DoShow(..., mouse=true) gegen den Durchrutschklick setzt.
    const Position btPos = roadBt->GetDrawPos() + DrawPoint(roadBt->GetSize().x / 2, roadBt->GetSize().y / 2);
    WINDOWMANAGER.Msg_LeftUp(MouseCoords(btPos));
    WINDOWMANAGER.Msg_LeftDown(MouseCoords(btPos));
    WINDOWMANAGER.Msg_LeftUp(MouseCoords(btPos));

    BOOST_TEST((view(1).GetRoad().mode == RoadBuildMode::Normal));
    BOOST_TEST((view(1).GetRoad().start == flagPt1));
    BOOST_TEST((view(0).GetRoad().mode == RoadBuildMode::Disabled));

    WINDOWMANAGER.Draw();
}

// ============================================================================================
// 5. Beide Reiterreihen von iwAction sind per Fokus erreichbar
// ============================================================================================

/// PHASE 13: DIESER FALL MISST JETZT DEN RING, und der Grund ist ein Paradigmenwechsel und
/// keine Bequemlichkeit.
///
/// Was er VORHER mass: das zweistufige Reitersystem von iwAction, mit dem Steuerkreuz von den
/// Haupt- in die Baureiter und ins Icongitter. Seit Phase 13 oeffnet A das Fenster als
/// KREISMENUE - die Reiterkoepfe sind keine Fokusstationen mehr, sondern die BLAETTERACHSE
/// (LB/RB), und das Icongitter ist der Ring. Die alte Navigation gibt es nicht mehr; sie durch
/// eine Zusicherung am Leben zu halten hiesse, einen Weg zu pruefen, den kein Spieler mehr geht.
///
/// Was er JETZT misst - und das ist dieselbe Frage in der neuen Bedienform: kommt ein
/// Padspieler an ALLE Gebaeude eines Schlossplatzes heran, also ueber alle drei Groessenreiter
/// hinweg? Ausschliesslich ueber den produktiven Weg (Padereignis -> UpdateInput).
BOOST_FIXTURE_TEST_CASE(EveryBuildingTierIsReachableByTurningTheRingPages, PadViewFixture<2>)
{
    const MapPoint spot = findBuildSpot(worldFixture.world, view(1).GetViewer(), BuildingQuality::Castle);
    BOOST_TEST_REQUIRE(spot.isValid());

    pads.connect(11);
    step(16);
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(11, 1));
    step(16);
    padSteerTo(11, 1, spot);

    press(11, PadButton::A);
    iwAction* const wnd = view(1).actionwindow;
    BOOST_TEST_REQUIRE(wnd != static_cast<iwAction*>(nullptr));
    // DER RING IST OFFEN und der Fokus steht darin - ohne ein einziges Y. Das ist der
    // Unterschied zur alten Bedienform.
    BOOST_TEST_REQUIRE(view(1).GetRing().IsOpen());
    BOOST_TEST_REQUIRE(view(1).GetFocus().IsActive());
    // Das Fenster wird nicht mehr gezeichnet - der Ring zeichnet an seiner Stelle.
    BOOST_TEST(!wnd->IsVisible());

    auto* mainTab = wnd->GetCtrl<ctrlTab>(0);
    BOOST_TEST_REQUIRE(mainTab != static_cast<ctrlTab*>(nullptr));
    ctrlGroup* buildGroup = mainTab->GetGroup(1); // TAB_BUILD
    BOOST_TEST_REQUIRE(buildGroup != static_cast<ctrlGroup*>(nullptr));
    auto* buildTab = buildGroup->GetCtrl<ctrlTab>(1);
    BOOST_TEST_REQUIRE(buildTab != static_cast<ctrlTab*>(nullptr));

    // Der Ringeintrag unter dem Fokus ist ein GEBAEUDE, kein Reiterkopf.
    const auto* icon = dynamic_cast<const ctrlBuildingIcon*>(view(1).GetFocus().GetFocused());
    BOOST_TEST_REQUIRE(icon != static_cast<const ctrlBuildingIcon*>(nullptr));
    BOOST_TEST_REQUIRE(buildTab->GetCurrentTab() == unsigned(iwAction::BuildTab::Hut));

    // ALLE Gebaeude einsammeln, die der Ring ueber alle Seiten hinweg zeigt - immer nur mit RB.
    // 40 Druecke sind reichlich: drei Reiter mit hoechstens je zwei Seiten.
    std::vector<BuildingType> seen;
    std::vector<unsigned> tiersSeen;
    for(unsigned i = 0; i < 40u; ++i)
    {
        unsigned numPages = 1;
        for(const Window* const ctrl : dskGameInterface::RingPageCtrls(view(1), numPages))
        {
            if(const auto* const bi = dynamic_cast<const ctrlBuildingIcon*>(ctrl))
            {
                if(std::find(seen.begin(), seen.end(), bi->GetType()) == seen.end())
                    seen.push_back(bi->GetType());
            }
        }
        const unsigned tier = buildTab->GetCurrentTab();
        if(std::find(tiersSeen.begin(), tiersSeen.end(), tier) == tiersSeen.end())
            tiersSeen.push_back(tier);
        press(11, PadButton::RightShoulder);
    }
    BOOST_TEST_MESSAGE("AUDIT: ueber den Ring erreichte Gebaeude = " << seen.size()
                                                                     << ", Groessenreiter = " << tiersSeen.size());
    // Alle drei Groessenreiter eines Schlossplatzes sind wirklich vorbeigekommen.
    BOOST_TEST(tiersSeen.size() >= 3u);
    // Und je ein Vertreter aus jeder Reihe ist dabei - Huette, Haus, Burg.
    BOOST_TEST((std::find(seen.begin(), seen.end(), BuildingType::Woodcutter) != seen.end()));
    BOOST_TEST((std::find(seen.begin(), seen.end(), BuildingType::Sawmill) != seen.end()));
    BOOST_TEST((std::find(seen.begin(), seen.end(), BuildingType::Farm) != seen.end()));

    // B fuehrt aus dem Ring heraus - und nimmt das Fenster mit. EIN Druck, nicht zwei: der Ring
    // ist das Fenster, nicht etwas darin.
    press(11, PadButton::B);
    BOOST_TEST(!view(1).GetRing().IsOpen());
    BOOST_TEST(!view(1).GetFocus().IsActive());
    BOOST_TEST(wnd->ShouldBeClosed());

    if(view(1).actionwindow && !view(1).actionwindow->ShouldBeClosed())
        view(1).actionwindow->Close();
    WINDOWMANAGER.Draw();
}

BOOST_AUTO_TEST_SUITE_END()
