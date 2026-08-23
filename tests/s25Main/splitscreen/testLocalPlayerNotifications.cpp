// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "WindowManager.h"
#include "desktops/PlayerView.h"
#include "ingameWindows/IngameWindow.h"
#include "notifications/BuildingNote.h"
#include "world/GameWorld.h"
#include "PadFixture.h"
#include "gameTypes/BuildingType.h"
#include "gameData/const_gui_ids.h"
#include <boost/test/unit_test.hpp>
#include <memory>

using rttr::test::PadViewFixture;

namespace {
/// Minimales Fenster mit frei waehlbarer ID - genau das, was OnBuildingNote schliessen soll.
/// Vorbild: tests/s25Main/UI/testWindowManager.cpp:234.
struct IdWindow : IngameWindow
{
    explicit IdWindow(unsigned id) : IngameWindow(id, DrawPoint(0, 0), Extent(100, 100), "", nullptr) {}
};
} // namespace

using TwoViewsThreePlayers = PadViewFixture<2, 3>;
using OneViewTwoPlayers = PadViewFixture<1, 2>;

BOOST_AUTO_TEST_SUITE(LocalPlayerNotificationTests)

/// P4, der Nachweis der getroffenen Entscheidung.
///
/// Angekuendigt war ein eigenes BuildingNote-Abo je Ansicht. Das ist gestrichen: der Rueckruf
/// schliesst ein Fenster mit GLOBAL vergebener ID (dskGameInterface::OnBuildingNote ->
/// WINDOWMANAGER.Close(CGI_BUILDING + MapBase::CreateGUIID(pos))). Vier Abos waeren vier
/// identische Rueckrufe auf denselben globalen WindowManager - Aufwand ohne Wirkung. Erst mit
/// Fensterbesitz (Phase 4) haette eine Aufteilung eine Bedeutung.
///
/// Was stattdessen noetig war und hier geprueft wird: das EINE Abo war auf den Hauptspieler
/// gefiltert (worldViewer.GetPlayerId()). Fuer einen zusaetzlichen lokalen Spieler waere das
/// Fenster nach dem Abriss seines Gebaeudes offen stehen geblieben - mit einem Zeiger auf ein
/// Gebaeude, das es nicht mehr gibt. Der Filter deckt jetzt jeden lokal dargestellten Spieler ab.
BOOST_FIXTURE_TEST_CASE(BuildingNoteOfASecondLocalPlayerAlsoClosesItsWindow, TwoViewsThreePlayers)
{
    const MapPoint pos(5, 5);
    const unsigned id = CGI_BUILDING + MapBase::CreateGUIID(pos);

    // Vorbedingung: die zweite Ansicht gehoert wirklich Spieler 1, Spieler 2 ist nicht dargestellt
    BOOST_TEST_REQUIRE(dsk->GetNumViews() == 2u);
    BOOST_TEST_REQUIRE(dsk->GetPlayerView(1).GetPlayerId() == 1u);
    BOOST_TEST_REQUIRE(worldFixture.world.GetNumPlayers() == 3u);

    WINDOWMANAGER.Show(std::make_unique<IdWindow>(id));
    WINDOWMANAGER.Draw();
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetTopMostWindow() != static_cast<IngameWindow*>(nullptr));

    // Gegenprobe zuerst: ein Gebaeude eines NICHT dargestellten Spielers geht uns nichts an
    worldFixture.world.GetNotifications().publish(
      BuildingNote(BuildingNote::Destroyed, 2, pos, BuildingType::Woodcutter));
    WINDOWMANAGER.Draw();
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetTopMostWindow() != static_cast<IngameWindow*>(nullptr));

    // ... das Gebaeude des ZWEITEN lokalen Spielers dagegen schon. Mit dem Filter auf den
    // Hauptspieler allein bliebe das Fenster hier offen.
    worldFixture.world.GetNotifications().publish(
      BuildingNote(BuildingNote::Destroyed, 1, pos, BuildingType::Woodcutter));
    WINDOWMANAGER.Draw();
    BOOST_TEST(WINDOWMANAGER.GetTopMostWindow() == static_cast<IngameWindow*>(nullptr));
}

/// Die Regressionsklammer dazu: fuer den Hauptspieler verhaelt es sich wie immer. Der
/// Einzelspieler sieht von der Erweiterung nichts.
BOOST_FIXTURE_TEST_CASE(BuildingNoteOfTheMainPlayerStillClosesItsWindow, OneViewTwoPlayers)
{
    const MapPoint pos(7, 9);
    const unsigned id = CGI_BUILDING + MapBase::CreateGUIID(pos);

    WINDOWMANAGER.Show(std::make_unique<IdWindow>(id));
    WINDOWMANAGER.Draw();
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetTopMostWindow() != static_cast<IngameWindow*>(nullptr));

    worldFixture.world.GetNotifications().publish(
      BuildingNote(BuildingNote::Constructed, 0, pos, BuildingType::Woodcutter));
    WINDOWMANAGER.Draw();
    BOOST_TEST(WINDOWMANAGER.GetTopMostWindow() == static_cast<IngameWindow*>(nullptr));
}

BOOST_AUTO_TEST_SUITE_END()
