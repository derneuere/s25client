// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "GamePlayer.h"
#include "Loader.h"
#include "WindowManager.h"
#include "controls/ctrlProgress.h"
#include "controls/ctrlTextButton.h"
#include "controls/ctrlTimer.h"
#include "factories/GameCommandFactory.h"
#include "ingameWindows/IngameWindow.h"
#include "ingameWindows/iwMilitary.h"
#include "input/FocusPath.h"
#include "PointOutput.h"
#include "RttrConfig.h"
#include "RttrForeachPt.h"
#include "desktops/PlayerView.h"
#include "driver/PadEvent.h"
#include "drivers/VideoDriverWrapper.h"
#include "files.h"
#include "network/GameClient.h"
#include "world/GameWorld.h"
#include "LocalGameFixture.h"
#include "PadFixture.h"
#include "PadGameFixture.h"
#include "Replay.h"
#include "variant.h"
#include "gameTypes/AIInfo.h"
#include "gameTypes/BuildingQuality.h"
#include "gameTypes/MapInfo.h"
#include "gameTypes/PlayerState.h"
#include "gameTypes/SettingsTypes.h"
#include "gameData/SettingTypeConv.h"
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using rttr::test::findExclusiveFlagSpot;
using rttr::test::formatMilitary;
using rttr::test::LocalGameFixture;
using rttr::test::numGCsForPlayer;
using rttr::test::PadFeeder;
using rttr::test::PadGameFixture;
using rttr::test::readMilitary;
using rttr::test::TestableGameInterface;

using rttr::test::FlagButtonWnd;

BOOST_AUTO_TEST_SUITE(PadCommandTests)

/// DER Nachweis dieser Phase, zweite Haelfte: ein Knopfdruck auf Pad 1 erzeugt einen
/// GameCommand fuer SEINEN Spieler - und der nimmt den vollstaendigen Weg
/// LocalPlayerGCFactory -> gameCommands_.Fetch(1) -> Socket -> GameServer -> Client ->
/// ExecuteNWF. Eine Abkuerzung gibt es nicht (siehe testSplitscreenGame.cpp:70-78).
///
/// M1 (Geraetezuordnung vertauscht) faellt hier doppelt um: die Flagge an p0 entstuende nicht,
/// weil Spieler 1 dort kein Gebiet hat, und die Replay-Zaehlung ergaebe 0/2 statt 1/1.
BOOST_FIXTURE_TEST_CASE(PadActionCreatesACommandForItsOwnPlayer, PadGameFixture)
{
    setUpTwoLocalPlayers();

    const MapPoint p0 = findExclusiveFlagSpot(world(), 0, 1);
    const MapPoint p1 = findExclusiveFlagSpot(world(), 1, 0);
    BOOST_TEST_REQUIRE(p0.isValid());
    BOOST_TEST_REQUIRE(p1.isValid());
    BOOST_TEST_REQUIRE((p0 != p1));
    BOOST_TEST_REQUIRE((world().GetNO(p0)->GetType() != NodalObjectType::Flag));
    BOOST_TEST_REQUIRE((world().GetNO(p1)->GetType() != NodalObjectType::Flag));

    aimPadAt(10, 0, p0);
    aimPadAt(11, 1, p1);
    // Gegenprobe zur Vorbedingung: die beiden Ansichten zeigen wirklich auf verschiedene Punkte
    BOOST_TEST_REQUIRE((dsk->GetPlayerView(0).GetView().GetSelectedPt()
                        != dsk->GetPlayerView(1).GetView().GetSelectedPt()));

    const unsigned startGF = GAMECLIENT.GetGFNumber();
    // X ist der Flaggenknopf (dskGameInterface::OnPadButton). A oeffnet seit der Trennung von
    // "oeffnen" und "setzen" nur noch Fenster und erzeugt selbst nie ein Kommando.
    pads.tap(10, PadButton::X);
    pads.tap(11, PadButton::X);
    dsk->UpdateInput(16, kMouseOffScreen);

    pumpUntilGF(startGF + 40);

    // --- Z1: Weltzustand. Jede Flagge steht, und zwar im Gebiet ihres Ausloesers. ---
    BOOST_TEST((world().GetNO(p0)->GetType() == NodalObjectType::Flag));
    BOOST_TEST((world().GetNO(p1)->GetType() == NodalObjectType::Flag));
    BOOST_TEST(ci().numErrors == 0u);
    BOOST_TEST(ci().numAsync == 0u);

    tearDownDesktop();

    // --- Z2: Buchhaltung. Genau ein Kommando je Spieler, keines fuer die KI. ---
    const auto replayPath = RTTRCONFIG.ExpandPath(s25::folders::replays) / GAMECLIENT.GetReplayFilename();
    GAMECLIENT.Stop(); // schliesst und komprimiert das Replay
    BOOST_TEST_REQUIRE(boost::filesystem::exists(replayPath));
    BOOST_TEST(numGCsForPlayer(replayPath, 0) == 1u);
    BOOST_TEST(numGCsForPlayer(replayPath, 1) == 1u);
    BOOST_TEST(numGCsForPlayer(replayPath, 2) == 0u);
}

/// Die Gegenprobe, ohne die die Aussage oben nichts wert waere: drueckt NUR Pad 1, darf fuer
/// Spieler 0 kein einziges Kommando entstehen (M2 - "an alle Ansichten leiten" faellt hier um).
///
/// Im selben Durchlauf die Flankenpruefung (M9): zwei Down ohne Up dazwischen sind EIN
/// Kommando, erst ein Up macht das naechste Down wieder zu einer Flanke. Das ist der Nachweis,
/// dass das der Tastatur fehlende Loslassen (VideoDriverLoaderInterface.h kennt kein Msg_KeyUp)
/// fuer Pads wirklich vorhanden ist und ausgewertet wird.
BOOST_FIXTURE_TEST_CASE(OnlyThePressingPadsPlayerGetsACommandAndOnlyOnEdges, PadGameFixture)
{
    setUpTwoLocalPlayers();

    const MapPoint p1 = findExclusiveFlagSpot(world(), 1, 0);
    BOOST_TEST_REQUIRE(p1.isValid());
    aimPadAt(10, 0, world().GetPlayer(0).GetHQPos()); // Pad 0 ist angesteckt, drueckt aber nie
    aimPadAt(11, 1, p1);

    const unsigned startGF = GAMECLIENT.GetGFNumber();

    // Zweimal Down ohne Up dazwischen -> genau EIN Kommando
    pads.button(11, PadButton::X, true);
    dsk->UpdateInput(16, kMouseOffScreen);
    pads.button(11, PadButton::X, true);
    dsk->UpdateInput(16, kMouseOffScreen);
    // Erst das Up macht das naechste Down wieder zu einer Flanke -> zweites Kommando
    pads.button(11, PadButton::X, false);
    pads.button(11, PadButton::X, true);
    dsk->UpdateInput(16, kMouseOffScreen);

    pumpUntilGF(startGF + 40);
    BOOST_TEST((world().GetNO(p1)->GetType() == NodalObjectType::Flag));
    BOOST_TEST(ci().numErrors == 0u);
    BOOST_TEST(ci().numAsync == 0u);

    tearDownDesktop();

    const auto replayPath = RTTRCONFIG.ExpandPath(s25::folders::replays) / GAMECLIENT.GetReplayFilename();
    GAMECLIENT.Stop();
    BOOST_TEST_REQUIRE(boost::filesystem::exists(replayPath));
    BOOST_TEST(numGCsForPlayer(replayPath, 0) == 0u); // <- die Gegenprobe
    BOOST_TEST(numGCsForPlayer(replayPath, 1) == 2u); // <- die Flankenpruefung
    BOOST_TEST(numGCsForPlayer(replayPath, 2) == 0u);
}

/// BEFUND B1: ein Knopf IN EINEM FENSTER muss sein Kommando fuer den Spieler erzeugen, der ihn
/// gedrueckt hat - nicht fuer den Hauptspieler.
///
/// Phase 3 hat das fuer den Weltpfad geloest (PadPlaceFlag holt GetGCFactory(view.GetPlayerId())).
/// Der Fensterpfad aus Phase 4 hat diese Sorgfalt nicht: ctrlButton::Activate ruft
/// Msg_ButtonClick ohne jeden Spielerbezug, das Fenster haelt GAMECLIENT als Fabrik, und
/// GameClient::AddGC leitet unbedingt auf GetPlayerId() - also auf den Hauptspieler.
///
/// Gemessen wird an der einzigen Stelle, die nicht luegen kann: an der Buchhaltung des Replays,
/// also an dem, was tatsaechlich vom Server zurueckkam.
BOOST_FIXTURE_TEST_CASE(WindowButtonCreatesACommandForTheActingPlayer, PadGameFixture)
{
    setUpTwoLocalPlayers();

    const MapPoint p1 = findExclusiveFlagSpot(world(), 1, 0);
    BOOST_TEST_REQUIRE(p1.isValid());
    BOOST_TEST_REQUIRE((world().GetNO(p1)->GetType() != NodalObjectType::Flag));

    auto& wnd = static_cast<FlagButtonWnd&>(WINDOWMANAGER.Show(std::make_unique<FlagButtonWnd>(GAMECLIENT, p1)));

    // Pad 10 nimmt Slot 0 (Spieler 0) und ruehrt sich danach nie wieder - es ist die Gegenprobe.
    pads.pickUp(10);
    dsk->UpdateInput(16, kMouseOffScreen);
    // Pad 11 nimmt Slot 1, also Spieler 1.
    pads.pickUp(11);
    dsk->UpdateInput(16, kMouseOffScreen);
    BOOST_TEST_REQUIRE(dsk->GetPlayerView(1).HasPadCursor());

    // NUR Spieler 1 betritt das Fenster und drueckt A auf dem Knopf.
    pads.tap(11, PadButton::Y);
    dsk->UpdateInput(16, kMouseOffScreen);
    BOOST_TEST_REQUIRE(dsk->GetPlayerView(1).GetFocus().IsActive());
    BOOST_TEST_REQUIRE(!dsk->GetPlayerView(0).GetFocus().IsActive());

    const unsigned startGF = GAMECLIENT.GetGFNumber();
    pads.tap(11, PadButton::A);
    dsk->UpdateInput(16, kMouseOffScreen);
    pumpUntilGF(startGF + 40);

    // --- Z1: Weltzustand. Die Flagge steht - und sie kann NUR stehen, wenn das Kommando fuer
    //     Spieler 1 erzeugt wurde: in seinem Gebiet ist die BQ von Spieler 0 Nothing.
    BOOST_TEST((world().GetNO(p1)->GetType() == NodalObjectType::Flag));
    BOOST_TEST(ci().numErrors == 0u);
    BOOST_TEST(ci().numAsync == 0u);

    wnd.Close();
    WINDOWMANAGER.Draw();
    tearDownDesktop();

    // --- Z2: Buchhaltung. Genau ein Kommando, und zwar fuer Spieler 1.
    const auto replayPath = RTTRCONFIG.ExpandPath(s25::folders::replays) / GAMECLIENT.GetReplayFilename();
    GAMECLIENT.Stop();
    BOOST_TEST_REQUIRE(boost::filesystem::exists(replayPath));
    const unsigned gcs0 = numGCsForPlayer(replayPath, 0);
    const unsigned gcs1 = numGCsForPlayer(replayPath, 1);
    BOOST_TEST_MESSAGE("GCs fuer Spieler 0: " << gcs0 << ", fuer Spieler 1: " << gcs1);
    BOOST_TEST(gcs1 == 1u);
    BOOST_TEST(gcs0 == 0u);
    BOOST_TEST(numGCsForPlayer(replayPath, 2) == 0u);
}

/// BEFUND (Phase 4b, Nachpruefung): die ScopedActingPlayer-Klammer greift nur, solange der
/// GameCommand IM Knopfdruck entsteht. Die fuenf Wirtschaftsfenster tun genau das nicht.
///
/// TransmitSettingsIgwAdapter legt im Konstruktor einen 2-Sekunden-Timer an. Ein Knopfdruck
/// setzt dort nur ein Flag; der GameCommand entsteht erst in Msg_Timer - und Msg_Timer kommt
/// vom WindowManager (WindowManager.cpp:96 ruft Msg_PaintBefore je Fenster), also lange nachdem
/// dskGameInterface::OnPadButton samt Klammer zurueckgekehrt ist.
///
/// Gemessen wird mit dem ECHTEN iwMilitary, gebaut wie in dskGameInterface.cpp:379, und an der
/// einzigen Stelle, die nicht luegen kann: der Buchhaltung des Replays.
BOOST_FIXTURE_TEST_CASE(SettingsWindowTimerCommandGoesToTheActingPlayer, PadGameFixture)
{
    setUpTwoLocalPlayers();

    auto& wnd = static_cast<iwMilitary&>(
      WINDOWMANAGER.Show(std::make_unique<iwMilitary>(dsk->GetPlayerView(1).GetViewer(), GAMECLIENT)));

    // Pad 10 nimmt Slot 0 (Spieler 0) und ruehrt sich danach nie wieder - es ist die Gegenprobe.
    pads.pickUp(10);
    dsk->UpdateInput(16, kMouseOffScreen);
    pads.pickUp(11);
    dsk->UpdateInput(16, kMouseOffScreen);

    // NUR Spieler 1 betritt das Fenster.
    pads.tap(11, PadButton::Y);
    dsk->UpdateInput(16, kMouseOffScreen);
    BOOST_TEST_REQUIRE(dsk->GetPlayerView(1).GetFocus().IsActive());
    BOOST_TEST_REQUIRE(!dsk->GetPlayerView(0).GetFocus().IsActive());

    ctrlProgress* const prog = focusFirstProgressBar(dsk->GetPlayerView(1), 11);
    BOOST_TEST_REQUIRE(prog != nullptr);
    BOOST_TEST_REQUIRE(nudge(*prog, 11));

    const unsigned startGF = GAMECLIENT.GetGFNumber();
    // Der Timer feuert. Verkuerzt ist nur die Wartezeit - der Weg ist der echte:
    // ctrlTimer::Msg_PaintBefore -> iwMilitary::Msg_Timer -> TransmitSettings, aufgerufen aus
    // dem WindowManager und damit AUSSERHALB jeder ScopedActingPlayer-Klammer.
    fireTransmitTimer(wnd);
    pumpUntilGF(startGF + 40);

    BOOST_TEST(ci().numErrors == 0u);
    BOOST_TEST(ci().numAsync == 0u);

    WINDOWMANAGER.CloseNow(&wnd);
    tearDownDesktop();

    const auto replayPath = RTTRCONFIG.ExpandPath(s25::folders::replays) / GAMECLIENT.GetReplayFilename();
    GAMECLIENT.Stop();
    BOOST_TEST_REQUIRE(boost::filesystem::exists(replayPath));
    const unsigned gcs0 = numGCsForPlayer(replayPath, 0);
    const unsigned gcs1 = numGCsForPlayer(replayPath, 1);
    BOOST_TEST_MESSAGE("iwMilitary (Timer): GCs p0=" << gcs0 << " p1=" << gcs1);
    BOOST_TEST(gcs1 == 1u);
    BOOST_TEST(gcs0 == 0u);
    BOOST_TEST(numGCsForPlayer(replayPath, 2) == 0u);
}

/// Dieselbe Luecke auf dem zweiten Weg: TransmitSettingsIgwAdapter::Close() sendet ebenfalls,
/// und Close() kommt beim Padspieler ueber B (FocusPath) bzw. beim Mausspieler ueber das
/// Schliesskreuz - in beiden Faellen ohne Klammer um die Kommandoerzeugung.
BOOST_FIXTURE_TEST_CASE(SettingsWindowCloseCommandGoesToTheActingPlayer, PadGameFixture)
{
    setUpTwoLocalPlayers();

    auto& wnd = static_cast<iwMilitary&>(
      WINDOWMANAGER.Show(std::make_unique<iwMilitary>(dsk->GetPlayerView(1).GetViewer(), GAMECLIENT)));

    pads.pickUp(10);
    dsk->UpdateInput(16, kMouseOffScreen);
    pads.pickUp(11);
    dsk->UpdateInput(16, kMouseOffScreen);

    pads.tap(11, PadButton::Y);
    dsk->UpdateInput(16, kMouseOffScreen);
    BOOST_TEST_REQUIRE(dsk->GetPlayerView(1).GetFocus().IsActive());

    ctrlProgress* const prog = focusFirstProgressBar(dsk->GetPlayerView(1), 11);
    BOOST_TEST_REQUIRE(prog != nullptr);
    BOOST_TEST_REQUIRE(nudge(*prog, 11));

    const unsigned startGF = GAMECLIENT.GetGFNumber();
    // Kein Timer: das Fenster geht direkt zu. Close() sendet die aufgelaufene Aenderung.
    wnd.Close();
    pumpUntilGF(startGF + 40);

    BOOST_TEST(ci().numErrors == 0u);
    BOOST_TEST(ci().numAsync == 0u);

    WINDOWMANAGER.CloseNow(&wnd);
    tearDownDesktop();

    const auto replayPath = RTTRCONFIG.ExpandPath(s25::folders::replays) / GAMECLIENT.GetReplayFilename();
    GAMECLIENT.Stop();
    BOOST_TEST_REQUIRE(boost::filesystem::exists(replayPath));
    const unsigned gcs0 = numGCsForPlayer(replayPath, 0);
    const unsigned gcs1 = numGCsForPlayer(replayPath, 1);
    BOOST_TEST_MESSAGE("iwMilitary (Close): GCs p0=" << gcs0 << " p1=" << gcs1);
    BOOST_TEST(gcs1 == 1u);
    BOOST_TEST(gcs0 == 0u);
    BOOST_TEST(numGCsForPlayer(replayPath, 2) == 0u);
}

/// BEFUND (Phase 4c, Nachpruefung): die Kommandobuchung stimmt jetzt - der INHALT nicht.
///
/// Gemessen wurde:
///     AUDIT F  Spieler 1 vorher : 10 5 5 5 8 8 8 8
///     AUDIT F  Spieler 1 nachher:  1 0 0 0 0 0 0 0
///
/// Ursache: GameClient haelt EIN globales Feld visual_settings. iwMilitary laedt seine Regler
/// beim Oeffnen daraus und baut beim Senden daraus die zu uebertragenden Einstellungen. Beides
/// sind die Werte des HAUPTSPIELERS - egal, welchem Spieler das Fenster gehoert. Bewegt
/// Spieler 1 einen einzigen Regler, gehen deshalb ALLE acht Einstellungen von Spieler 1
/// verloren und werden durch die des Hauptspielers ersetzt. Anschliessend ueberschreibt das
/// Fenster visual_settings, was auch den Anzeigezustand des Hauptspielers verfaelscht.
///
/// Gemessen wird am Spielzustand selbst (GamePlayer::GetMilitarySetting) - nicht an der
/// Anzeige. Das ist Datenverlust, keine Anzeigefrage.
BOOST_FIXTURE_TEST_CASE(SettingsWindowKeepsTheActingPlayersOwnSettings, PadGameFixture)
{
    setUpTwoLocalPlayers();

    // Ausgangslage: Spieler 1 hat EIGENE Einstellungen (alles auf Maximum), der Hauptspieler
    // lauter Nullen. Gesetzt wird auf dem echten Weg - je ein Kommando aus der eigenen Fabrik
    // des jeweiligen Spielers.
    const MilitarySettings maxSettings = MILITARY_SETTINGS_SCALE;
    const MilitarySettings zeroSettings{};
    BOOST_TEST_REQUIRE(GAMECLIENT.GetGCFactory(0) != nullptr);
    BOOST_TEST_REQUIRE(GAMECLIENT.GetGCFactory(1) != nullptr);
    BOOST_TEST_REQUIRE(GAMECLIENT.GetGCFactory(1)->ChangeMilitary(maxSettings));
    BOOST_TEST_REQUIRE(GAMECLIENT.GetGCFactory(0)->ChangeMilitary(zeroSettings));
    pumpUntilGF(GAMECLIENT.GetGFNumber() + 40);

    const MilitarySettings before = readMilitary(world().GetPlayer(1));
    BOOST_TEST_REQUIRE((before == maxSettings));
    BOOST_TEST_REQUIRE((readMilitary(world().GetPlayer(0)) == zeroSettings));

    // Wie nach Spielstart und nach einem Spielerwechsel: die Anzeigewerte werden aus dem
    // Spielzustand neu abgeleitet.
    GAMECLIENT.ResetVisualSettings();

    // Das Fenster von Spieler 1, gebaut wie in dskGameInterface.cpp:379 - mit SEINER Ansicht.
    auto& wnd = static_cast<iwMilitary&>(
      WINDOWMANAGER.Show(std::make_unique<iwMilitary>(dsk->GetPlayerView(1).GetViewer(), GAMECLIENT)));

    pads.pickUp(10); // Gegenprobe: Pad 0 ist angesteckt, ruehrt sich aber nie
    dsk->UpdateInput(16, kMouseOffScreen);
    pads.pickUp(11);
    dsk->UpdateInput(16, kMouseOffScreen);

    // NUR Spieler 1 betritt das Fenster und bewegt GENAU EINEN Regler um eine Stufe.
    pads.tap(11, PadButton::Y);
    dsk->UpdateInput(16, kMouseOffScreen);
    BOOST_TEST_REQUIRE(dsk->GetPlayerView(1).GetFocus().IsActive());
    BOOST_TEST_REQUIRE(!dsk->GetPlayerView(0).GetFocus().IsActive());

    ctrlProgress* const prog = focusFirstProgressBar(dsk->GetPlayerView(1), 11);
    BOOST_TEST_REQUIRE(prog != nullptr);
    BOOST_TEST_REQUIRE(nudge(*prog, 11));

    const unsigned startGF = GAMECLIENT.GetGFNumber();
    fireTransmitTimer(wnd);
    pumpUntilGF(startGF + 40);

    BOOST_TEST(ci().numErrors == 0u);
    BOOST_TEST(ci().numAsync == 0u);

    const MilitarySettings after = readMilitary(world().GetPlayer(1));
    BOOST_TEST_MESSAGE("AUDIT F  Spieler 1 vorher :" << formatMilitary(before));
    BOOST_TEST_MESSAGE("AUDIT F  Spieler 1 nachher:" << formatMilitary(after));

    // --- Z1: Genau EINE Einstellung von Spieler 1 hat sich geaendert, und zwar um eine Stufe.
    unsigned numChanged = 0;
    for(unsigned i = 0; i < after.size(); ++i)
    {
        if(after[i] != before[i])
        {
            ++numChanged;
            BOOST_TEST(std::abs(int(after[i]) - int(before[i])) == 1);
        }
    }
    BOOST_TEST(numChanged == 1u);

    // --- Z2: Die uebrigen Einstellungen von Spieler 1 sind erhalten (die eigentliche Messung
    //     des Pruefberichts: 10 5 5 5 8 8 8 8 darf nicht zu 1 0 0 0 0 0 0 0 werden).
    for(unsigned i = 0; i < after.size(); ++i)
    {
        if(after[i] == before[i])
            continue;
        // der eine geaenderte Regler; alle anderen muessen gleich geblieben sein
        for(unsigned j = 0; j < after.size(); ++j)
            if(j != i)
                BOOST_TEST(unsigned(after[j]) == unsigned(before[j]));
        break;
    }

    // --- Z3: Der Anzeigezustand des Hauptspielers ist unangetastet. Gemessen an dem, was SEIN
    //     Fenster beim Oeffnen zeigt - vor der Korrektur steht dort die Aenderung von Spieler 1.
    WINDOWMANAGER.CloseNow(&wnd);
    auto& mainWnd = static_cast<iwMilitary&>(
      WINDOWMANAGER.Show(std::make_unique<iwMilitary>(dsk->GetPlayerView(0).GetViewer(), GAMECLIENT)));
    const auto bars = mainWnd.GetCtrls<ctrlProgress>();
    BOOST_TEST_REQUIRE(bars.size() == zeroSettings.size());
    for(unsigned i = 0; i < bars.size(); ++i)
        BOOST_TEST(unsigned(bars[i]->GetPosition()) == 0u);
    WINDOWMANAGER.CloseNow(&mainWnd);

    // --- Z4: Und der Spielzustand des Hauptspielers erst recht.
    BOOST_TEST((readMilitary(world().GetPlayer(0)) == zeroSettings));

    tearDownDesktop();
}

BOOST_AUTO_TEST_SUITE_END()
