// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "GamePlayer.h"
#include "RttrConfig.h"
#include "WindowManager.h"
#include "controls/ctrlButton.h"
#include "controls/ctrlProgress.h"
#include "desktops/PlayerView.h"
#include "desktops/dskGameInterface.h"
#include "driver/MouseCoords.h"
#include "files.h"
#include "ingameWindows/iwMilitary.h"
#include "mockupDrivers/MockupVideoDriver.h"
#include "network/GameClient.h"
#include "world/GameWorld.h"
#include "nodeObjs/noBase.h"
#include "NodalObjectTypes.h"
#include "PadGameFixture.h"
#include "gameData/SettingTypeConv.h"
#include "gameTypes/SettingsTypes.h"
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <memory>

using rttr::test::formatMilitary;
using rttr::test::numGCsForPlayer;
using rttr::test::PadGameFixture;
using rttr::test::readMilitary;

namespace {

/// Oeffnet ein Wirtschaftsfenster GENAU SO, wie dskGameInterface es fuer eine Ansicht tut:
/// unter deren Klammer, mit ihrem Viewer und ihrer eigenen Kommandofabrik.
iwMilitary& openMilitaryFor(dskGameInterface& dsk, unsigned viewIdx)
{
    PlayerView& view = dsk.GetPlayerView(viewIdx);
    const dskGameInterface::ViewScope ownerScope(viewIdx);
    return WINDOWMANAGER.Show(
      std::make_unique<iwMilitary>(view.GetViewer(), dskGameInterface::gcFactoryFor(view)));
}

/// Klickt einen Regler an - ueber den echten Mauseinstieg des WindowManagers, nicht am Control
/// vorbei. Der Klickpunkt liegt so weit rechts im Balken, dass er ihn nach oben zieht.
void clickProgressRight(MockupVideoDriver& video, ctrlProgress& prog)
{
    const Rect r = prog.GetDrawRect();
    const Position pt(r.right - 10, (r.top + r.bottom) / 2);
    video.tickCount_ += 5000;
    MouseCoords down(pt);
    down.ldown = true;
    WINDOWMANAGER.Msg_LeftDown(down);
    WINDOWMANAGER.Msg_LeftUp(MouseCoords(pt));
}

} // namespace

BOOST_AUTO_TEST_SUITE(WindowDataOwnershipTests)

/// Der eigentliche Ertrag dieser Phase: ZWEI Wirtschaftsfenster derselben Art, gleichzeitig
/// offen, jedes mit den Daten SEINES Spielers.
///
/// Bis hierher war das nicht pruefbar - das Fenster von Spieler 1 musste geschlossen werden,
/// bevor das von Spieler 0 geoeffnet werden konnte, weil FindNonModalWindow allein ueber die
/// GUI_ID schluesselte (testPadCommands.cpp, Z3). Genau daran zeigt sich der Datenbesitz.
BOOST_FIXTURE_TEST_CASE(TwoOpenMilitaryWindowsShowTwoDifferentPlayersSettings, PadGameFixture)
{
    setUpTwoLocalPlayers();

    const MilitarySettings maxSettings = MILITARY_SETTINGS_SCALE;
    const MilitarySettings zeroSettings{};
    BOOST_TEST_REQUIRE(GAMECLIENT.GetGCFactory(1)->ChangeMilitary(maxSettings));
    BOOST_TEST_REQUIRE(GAMECLIENT.GetGCFactory(0)->ChangeMilitary(zeroSettings));
    pumpUntilGF(GAMECLIENT.GetGFNumber() + 40);

    // Vorbedingung, ohne die alles Weitere trivial waere.
    BOOST_TEST_REQUIRE((readMilitary(world().GetPlayer(1)) == maxSettings));
    BOOST_TEST_REQUIRE((readMilitary(world().GetPlayer(0)) == zeroSettings));
    GAMECLIENT.ResetVisualSettings();

    iwMilitary& w0 = openMilitaryFor(*dsk, 0);
    iwMilitary& w1 = openMilitaryFor(*dsk, 1);

    // Beide leben - vor dem Fensterbesitz haette das zweite das erste geschlossen.
    BOOST_TEST_REQUIRE(&w0 != &w1);
    BOOST_TEST(w0.GetOwner() == 0u);
    BOOST_TEST(w1.GetOwner() == 1u);
    BOOST_TEST(WINDOWMANAGER.GetTopMostWindow(0u) == static_cast<IngameWindow*>(&w0));
    BOOST_TEST(WINDOWMANAGER.GetTopMostWindow(1u) == static_cast<IngameWindow*>(&w1));

    const auto bars0 = w0.GetCtrls<ctrlProgress>();
    const auto bars1 = w1.GetCtrls<ctrlProgress>();
    BOOST_TEST_REQUIRE(bars0.size() == zeroSettings.size());
    BOOST_TEST_REQUIRE(bars1.size() == maxSettings.size());
    for(unsigned i = 0; i < bars0.size(); ++i)
    {
        BOOST_TEST_CONTEXT("Regler " << i)
        {
            BOOST_TEST(unsigned(bars0[i]->GetPosition()) == 0u);
            BOOST_TEST(unsigned(bars1[i]->GetPosition()) == unsigned(MILITARY_SETTINGS_SCALE[i]));
        }
    }

    WINDOWMANAGER.CloseNow(&w0);
    WINDOWMANAGER.CloseNow(&w1);
    tearDownDesktop();
}

/// Ein MAUSklick in das Fenster von Spieler 1 erzeugt ein Kommando fuer Spieler 1 - nicht fuer
/// den Hauptspieler.
///
/// Bis hierher hatte der Mauspfad ueberhaupt keine Klammer: GameClient::AddGC buchte
/// bedingungslos auf den Hauptspieler. Die Padklammer aus Phase 4b half hier nicht, weil kein
/// Pad beteiligt ist. Gemessen wird am Spielzustand UND im Replay, also an dem, was der Server
/// zurueckgegeben hat.
BOOST_FIXTURE_TEST_CASE(AMouseClickInAWindowCommandsForItsOwnerNotForTheMainPlayer, PadGameFixture)
{
    setUpTwoLocalPlayers();

    const MilitarySettings zeroSettings{};
    BOOST_TEST_REQUIRE(GAMECLIENT.GetGCFactory(1)->ChangeMilitary(zeroSettings));
    BOOST_TEST_REQUIRE(GAMECLIENT.GetGCFactory(0)->ChangeMilitary(zeroSettings));
    pumpUntilGF(GAMECLIENT.GetGFNumber() + 40);
    BOOST_TEST_REQUIRE((readMilitary(world().GetPlayer(0)) == zeroSettings));
    BOOST_TEST_REQUIRE((readMilitary(world().GetPlayer(1)) == zeroSettings));
    GAMECLIENT.ResetVisualSettings();

    // NUR das Fenster von Spieler 1 ist offen; kein Pad ist im Spiel.
    iwMilitary& w1 = openMilitaryFor(*dsk, 1);
    WINDOWMANAGER.Draw();
    const auto bars = w1.GetCtrls<ctrlProgress>();
    BOOST_TEST_REQUIRE(bars.size() == zeroSettings.size());

    clickProgressRight(*uiHelper::GetVideoDriver(), *bars.front());
    BOOST_TEST_REQUIRE(unsigned(bars.front()->GetPosition()) > 0u);

    const unsigned startGF = GAMECLIENT.GetGFNumber();
    fireTransmitTimer(w1);
    pumpUntilGF(startGF + 40);

    const MilitarySettings after1 = readMilitary(world().GetPlayer(1));
    const MilitarySettings after0 = readMilitary(world().GetPlayer(0));
    BOOST_TEST_MESSAGE("AUDIT P5 Spieler 1 nachher:" << formatMilitary(after1));
    BOOST_TEST_MESSAGE("AUDIT P5 Spieler 0 nachher:" << formatMilitary(after0));

    // --- Z1: Spieler 1 hat die Aenderung, Spieler 0 nicht.
    BOOST_TEST((after1 != zeroSettings));
    BOOST_TEST((after0 == zeroSettings));
    BOOST_TEST(ci().numErrors == 0u);
    BOOST_TEST(ci().numAsync == 0u);

    WINDOWMANAGER.CloseNow(&w1);
    tearDownDesktop();

    // --- Z2: Buchhaltung. Genau ein Kommando, und zwar fuer Spieler 1.
    const auto replayPath = RTTRCONFIG.ExpandPath(s25::folders::replays) / GAMECLIENT.GetReplayFilename();
    GAMECLIENT.Stop();
    BOOST_TEST_REQUIRE(boost::filesystem::exists(replayPath));
    // Je ein ChangeMilitary aus der Ausgangslage, dazu die eine Aenderung von Spieler 1.
    BOOST_TEST(numGCsForPlayer(replayPath, 1) == 2u);
    BOOST_TEST(numGCsForPlayer(replayPath, 0) == 1u);
    BOOST_TEST(numGCsForPlayer(replayPath, 2) == 0u);
}

/// Dieselbe Aussage fuer ein Fenster, das GAMECLIENT als Fabrik bekommen hat - also fuer die
/// grosse Mehrheit der bestehenden Erzeugungsstellen, die noch keine eigene Fabrik nennen.
///
/// Hier traegt die Aussage allein die Besitzklammer auf dem Mauspfad
/// (WindowManager::RelayMouseMessage -> Desktop::OnWindowOwnerChanged -> handelnder Spieler).
/// Der Nachweis ist ein echter Fehlschlag und keine kosmetische Abweichung: der Punkt liegt
/// ausschliesslich im Gebiet von Spieler 1, gebucht auf Spieler 0 entstuende gar keine Flagge.
BOOST_FIXTURE_TEST_CASE(AMouseClickInAWindowWithTheSharedFactoryStillCommandsForItsOwner, PadGameFixture)
{
    setUpTwoLocalPlayers();

    const MapPoint p1 = rttr::test::findExclusiveFlagSpot(world(), 1, 0);
    BOOST_TEST_REQUIRE(p1.isValid());
    BOOST_TEST_REQUIRE((world().GetNO(p1)->GetType() != NodalObjectType::Flag));

    // Bewusst GAMECLIENT als Fabrik - genau wie dskGameInterface es bis Phase 4 ueberall tat.
    rttr::test::FlagButtonWnd* wnd = nullptr;
    {
        const dskGameInterface::ViewScope ownerScope(1);
        wnd = &WINDOWMANAGER.Show(std::make_unique<rttr::test::FlagButtonWnd>(GAMECLIENT, p1));
    }
    WINDOWMANAGER.Draw();
    BOOST_TEST_REQUIRE(wnd->GetOwner() == 1u);

    auto* bt = wnd->GetCtrl<ctrlButton>(1);
    BOOST_TEST_REQUIRE(bt != static_cast<ctrlButton*>(nullptr));
    const Rect r = bt->GetDrawRect();
    const Position center = r.getOrigin() + Position(r.getSize() / 2u);
    uiHelper::GetVideoDriver()->tickCount_ += 5000;
    MouseCoords down(center);
    down.ldown = true;
    const unsigned startGF = GAMECLIENT.GetGFNumber();
    WINDOWMANAGER.Msg_LeftDown(down);
    WINDOWMANAGER.Msg_LeftUp(MouseCoords(center));
    pumpUntilGF(startGF + 40);

    BOOST_TEST((world().GetNO(p1)->GetType() == NodalObjectType::Flag));
    BOOST_TEST(ci().numErrors == 0u);
    BOOST_TEST(ci().numAsync == 0u);

    WINDOWMANAGER.CloseNow(wnd);
    tearDownDesktop();

    const auto replayPath = RTTRCONFIG.ExpandPath(s25::folders::replays) / GAMECLIENT.GetReplayFilename();
    GAMECLIENT.Stop();
    BOOST_TEST_REQUIRE(boost::filesystem::exists(replayPath));
    BOOST_TEST(numGCsForPlayer(replayPath, 1) == 1u);
    BOOST_TEST(numGCsForPlayer(replayPath, 0) == 0u);
    BOOST_TEST(numGCsForPlayer(replayPath, 2) == 0u);
}

BOOST_AUTO_TEST_SUITE_END()
