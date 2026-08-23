// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "GamePlayer.h"
#include "desktops/PlayerView.h"
#include "desktops/dskGameInterface.h"
#include "drivers/VideoDriverWrapper.h"
#include "network/GameMessages.h"
#include "world/ViewportLayout.h"
#include "LocalGameFixture.h"
#include "Replay.h"
#include "ai/AIPlayer.h"
#include "factories/GameCommandFactory.h"
#include "network/GameClient.h"
#include "variant.h"
#include "world/GameWorld.h"
#include "helpers/EnumRange.h"
#include "gameTypes/AIInfo.h"
#include "gameTypes/GameSettingTypes.h"
#include "gameTypes/GoodTypes.h"
#include "gameTypes/MapInfo.h"
#include "gameTypes/PlayerState.h"
#include "gameTypes/SettingsTypes.h"
#include "rttr/test/LogAccessor.hpp"
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <string>
#include <vector>

using rttr::test::LocalGameFixture;

namespace {
/// Militaereinstellungen von Spieler 0 und 1. Bewusst verschieden und bewusst innerhalb von
/// MILITARY_SETTINGS_SCALE ({10,5,5,5,8,8,8,8}), sonst schlaegt ein RTTR_Assert in
/// GamePlayer::ChangeMilitarySettings zu.
const MilitarySettings milA{{1, 2, 3, 4, 5, 6, 7, 8}};
const MilitarySettings milB{{9, 1, 0, 5, 2, 3, 4, 1}};

ToolSettings makeToolSettings(const uint8_t base)
{
    ToolSettings result;
    uint8_t v = base;
    for(const auto tool : helpers::enumRange<Tool>())
    {
        result[tool] = static_cast<uint8_t>(v % 11u);
        ++v;
    }
    return result;
}

MilitarySettings readMilitarySettings(const GamePlayer& player)
{
    MilitarySettings result{};
    for(unsigned i = 0; i < result.size(); i++)
        result[i] = player.GetMilitarySetting(i);
    return result;
}

ToolSettings readToolSettings(const GamePlayer& player)
{
    ToolSettings result;
    for(const auto tool : helpers::enumRange<Tool>())
        result[tool] = static_cast<uint8_t>(player.GetToolPriority(tool));
    return result;
}
} // namespace

BOOST_AUTO_TEST_SUITE(SplitscreenGameTests)

/// Der eigentliche Nachweis: zwei lokale Spieler in EINER laufenden Partie mit echtem
/// GameServer und echtem GameClient. Die NWF-Warteschlangen des Clients werden ausschliesslich
/// aus Servernachrichten befuellt (GameClient.cpp:1197-1207, einzige Fundstelle von
/// addPlayerCmds im Client), und ExecuteNWF liest ausschliesslich daraus
/// (GameClientGF_Game.cpp:20-35). Eine Weltaenderung fuer Spieler 1 kann also nur ueber
/// LocalPlayerGCFactory -> AddPlayerGC -> gameCommands_.Fetch(1) ->
/// GameMessage_GameCommand(1,...) -> Socket -> GameServer::GetTargetPlayer -> SendToAll ->
/// Client -> nwfInfo -> ExecuteNWF entstanden sein. Eine Abkuerzung gibt es nicht.
BOOST_FIXTURE_TEST_CASE(SecondLocalPlayerCommandsTakeTheFullNetworkRoundtrip, LocalGameFixture)
{
    rttr::test::LogAccessor logAcc;
    std::string collectedLog;
    const auto collectLog = [&] { collectedLog += logAcc.getLog(); };

    hostAndEnterLobby();
    collectLog();

    // Slot 1 ist der zweite lokale Mensch, Slot 2 eine echte (Dummy-)KI zur Kontrolle.
    BOOST_TEST_REQUIRE(GAMECLIENT.GetGameLobby()->getNumPlayers() >= 3u);
    BOOST_TEST_REQUIRE(GAMECLIENT.GetPlayerId() == 0u);
    GAMECLIENT.SetAdditionalLocalPlayers({1});
    // Das macht in Produktion dskGameLobby ueber GameClient::ApplyAdditionalLocalPlayers
    BOOST_TEST_REQUIRE(GameClient::ValidateAdditionalLocalPlayers(*GAMECLIENT.GetGameLobby(), GAMECLIENT.GetPlayerId(),
                                                                 GAMECLIENT.GetAdditionalLocalPlayers(), false)
                       == std::string());
    GameClient::ApplyAdditionalLocalPlayers(lobby(), GAMECLIENT.GetAdditionalLocalPlayers());
    lobby().SetPlayerState(2, PlayerState::AI, AI::Info(AI::Type::Dummy));
    {
        GlobalGameSettings ggs = lobby().GetGGS();
        ggs.speed = GameSpeed::VeryFast; // 30ms/GF -> kurze Livephase
        lobby().ChangeGlobalGameSettings(ggs);
    }
    pumpUntil(
      [] {
          const auto lobby = GAMECLIENT.GetGameLobby();
          return lobby->getPlayer(1).ps == PlayerState::AI && lobby->getPlayer(1).aiInfo.type == AI::Type::Dummy
                 && lobby->getPlayer(2).ps == PlayerState::AI && lobby->getSettings().speed == GameSpeed::VeryFast;
      },
      "lobby to apply the local player configuration");
    collectLog();

    startGame();
    collectLog();

    // --- Z1/Z2/Z3: Registrierung ---
    BOOST_TEST(GAMECLIENT.IsLocalHumanPlayer(0));
    BOOST_TEST(GAMECLIENT.IsLocalHumanPlayer(1));
    BOOST_TEST(!GAMECLIENT.IsLocalHumanPlayer(2));
    const std::vector<uint8_t> expectedAdditional{1};
    BOOST_TEST(GAMECLIENT.GetAdditionalLocalPlayers() == expectedAdditional, boost::test_tools::per_element());

    GameCommandFactory* const factory0 = GAMECLIENT.GetGCFactory(0);
    GameCommandFactory* const factory1 = GAMECLIENT.GetGCFactory(1);
    BOOST_TEST_REQUIRE(factory0 != static_cast<GameCommandFactory*>(nullptr));
    BOOST_TEST_REQUIRE(factory1 != static_cast<GameCommandFactory*>(nullptr));
    BOOST_TEST(GAMECLIENT.GetGCFactory(2) == static_cast<GameCommandFactory*>(nullptr));

    // Der lokal gesteuerte Slot bekommt KEINE KI, der Kontrollslot schon
    BOOST_TEST(GAMECLIENT.GetAIPlayer(1) == static_cast<const AIPlayer*>(nullptr));
    BOOST_TEST(GAMECLIENT.GetAIPlayer(2) != static_cast<const AIPlayer*>(nullptr));

    // Ausgangszustand festhalten, damit die Zusicherungen unten etwas bedeuten
    const MilitarySettings mil2Before = readMilitarySettings(world().GetPlayer(2));
    BOOST_TEST_REQUIRE((milA != mil2Before));
    BOOST_TEST_REQUIRE((milB != mil2Before));

    // --- Kommandos absetzen ---
    const unsigned startGF = GAMECLIENT.GetGFNumber();
    BOOST_TEST(factory0->ChangeMilitary(milA));
    BOOST_TEST(factory1->ChangeMilitary(milB));
    // Ein zweites Kommando von Spieler 1 einige GFs spaeter -> eigenes NWF-Paket
    pumpUntilGF(startGF + 10);
    ToolSettings toolsB = makeToolSettings(3);
    ToolSettings toolsBefore = readToolSettings(world().GetPlayer(1));
    BOOST_TEST_REQUIRE((toolsB != toolsBefore));
    BOOST_TEST(factory1->ChangeTools(toolsB));

    pumpUntilGF(startGF + 40);
    collectLog();

    // --- Z4: Weltzustand ---
    for(unsigned i = 0; i < milA.size(); i++)
    {
        BOOST_TEST_INFO("military setting " << i);
        BOOST_TEST(world().GetPlayer(0).GetMilitarySetting(i) == milA[i]);
        BOOST_TEST_INFO("military setting " << i);
        BOOST_TEST(world().GetPlayer(1).GetMilitarySetting(i) == milB[i]);
        BOOST_TEST_INFO("military setting " << i);
        BOOST_TEST(world().GetPlayer(2).GetMilitarySetting(i) == mil2Before[i]);
    }
    for(const auto tool : helpers::enumRange<Tool>())
    {
        BOOST_TEST_INFO("tool " << static_cast<unsigned>(tool));
        BOOST_TEST(world().GetPlayer(1).GetToolPriority(tool) == toolsB[tool]);
    }

    // --- Z5: NWF-Buchhaltung ---
    BOOST_TEST(GAMECLIENT.GetGFNumber() >= startGF + 40);
    BOOST_TEST(!GAMECLIENT.GetNWFInfo()->getPlayerInfo(0).isLagging);
    BOOST_TEST(!GAMECLIENT.GetNWFInfo()->getPlayerInfo(1).isLagging);
    BOOST_TEST(!GAMECLIENT.GetNWFInfo()->getPlayerInfo(2).isLagging);

    // --- Z6: keine verworfenen Kommandopakete ---
    BOOST_TEST(collectedLog.find("Could not add gamecommands") == std::string::npos);
    BOOST_TEST(ci().numErrors == 0u);
    BOOST_TEST(ci().numAsync == 0u);

    // --- Z7: Papierbeleg im Replay ---
    const auto replayPath = RTTRCONFIG.ExpandPath(s25::folders::replays) / GAMECLIENT.GetReplayFilename();
    GAMECLIENT.Stop(); // schliesst und komprimiert das Replay
    BOOST_TEST_REQUIRE(boost::filesystem::exists(replayPath));

    unsigned numGCsForPlayer1 = 0, numGCsForPlayer0 = 0;
    {
        Replay replay;
        BOOST_TEST_REQUIRE(replay.LoadHeader(replayPath));
        MapInfo mapInfo;
        BOOST_TEST_REQUIRE(replay.LoadGameData(mapInfo));
        for(auto gf = replay.ReadGF(); gf.has_value(); gf = replay.ReadGF())
        {
            visit(composeVisitor([](const Replay::ChatCommand&) {},
                                 [&](const Replay::GameCommand& cmd) {
                                     if(cmd.player == 1)
                                         numGCsForPlayer1 += cmd.cmds.gcs.size();
                                     else if(cmd.player == 0)
                                         numGCsForPlayer0 += cmd.cmds.gcs.size();
                                 }),
                 replay.ReadCommand());
        }
    }
    // Aufgezeichnet wird ausschliesslich aus nwfInfo (GameClientGF_Game.cpp:26-31),
    // also nur, was tatsaechlich vom Server zurueckkam.
    BOOST_TEST(numGCsForPlayer0 == 1u);
    BOOST_TEST(numGCsForPlayer1 == 2u);

    // --- Z8: Wiedergabe als Determinismusorakel ---
    ci().numReplayAsync = 0;
    ci().numErrors = 0;
    ci().replayEnded = false;
    GAMECLIENT.SetInterface(&ci());
    BOOST_TEST_REQUIRE(GAMECLIENT.StartReplay(replayPath));
    GAMECLIENT.GameLoaded();
    GAMECLIENT.SetPause(false);
    GAMECLIENT.skiptogf = GAMECLIENT.GetLastReplayGF();
    pumpUntil([this] { return ci().replayEnded || ci().numErrors > 0 || ci().numReplayAsync > 0; },
              "replay to reach its end");
    // GameClientGF_Replay.cpp:37-62 vergleicht die aufgezeichnete AsyncChecksum GF-genau
    // gegen die neu berechnete. Kein CI_ReplayAsync == kein Determinismusbruch.
    BOOST_TEST(ci().numReplayAsync == 0u);
    BOOST_TEST(ci().numErrors == 0u);
    BOOST_TEST(ci().replayEnded);
}

/// F1-Regression: ChangePlayerIngame wird per Server-Broadcast auf ALLEN Clients ausgefuehrt
/// (GameServer.cpp:1701 SendToAll -> GameClient.cpp:727 -> hier). Die Weltaenderung darf
/// deshalb niemals von clientlokalem Zustand abhaengen. Vor dem Fix kehrte der Guard fuer
/// lokal gesteuerte Slots VOR dem std::swap der PlayerState zurueck; dieser Test war damit rot.
BOOST_FIXTURE_TEST_CASE(IngameSwapOntoLocalSlotStillMutatesTheWorld, LocalGameFixture)
{
    hostAndEnterLobby();
    GAMECLIENT.SetAdditionalLocalPlayers({1});
    GameClient::ApplyAdditionalLocalPlayers(lobby(), GAMECLIENT.GetAdditionalLocalPlayers());
    lobby().SetPlayerState(2, PlayerState::AI, AI::Info(AI::Type::Dummy));
    pumpUntil(
      [] {
          const auto lobby = GAMECLIENT.GetGameLobby();
          return lobby->getPlayer(1).ps == PlayerState::AI && lobby->getPlayer(1).aiInfo.type == AI::Type::Dummy
                 && lobby->getPlayer(2).ps == PlayerState::AI;
      },
      "lobby to apply the local player configuration");
    startGame();

    BOOST_TEST_REQUIRE(GAMECLIENT.IsLocalHumanPlayer(1));
    BOOST_TEST_REQUIRE((world().GetPlayer(0).ps == PlayerState::Occupied));
    BOOST_TEST_REQUIRE((world().GetPlayer(1).ps == PlayerState::AI));

    // Das ist genau der Aufruf, den OnGameMessage(GameMessage_Player_Swap) ausloest.
    GAMECLIENT.ChangePlayerIngame(0, 1);

    // Weltzustand MUSS getauscht sein - sonst haetten verschiedene Clients verschiedene
    // isHuman()-Ergebnisse in der Simulation (GameWorld.cpp:270, GamePlayer.cpp:1690/1803).
    BOOST_TEST((world().GetPlayer(0).ps == PlayerState::AI));
    BOOST_TEST((world().GetPlayer(1).ps == PlayerState::Occupied));
    BOOST_TEST(GAMECLIENT.GetPlayerId() == 1u);
    BOOST_TEST_REQUIRE(ci().swaps.size() == 1u);
    BOOST_TEST(ci().swaps[0].first == 0u);
    BOOST_TEST(ci().swaps[0].second == 1u);
    // Clientlokale Buchhaltung: der Hauptspieler ist umgezogen, Slot 0 wird nicht mehr
    // lokal gesteuert, und der Getter ist nachgefuehrt (F2).
    BOOST_TEST(!GAMECLIENT.IsLocalHumanPlayer(0));
    BOOST_TEST(GAMECLIENT.IsLocalHumanPlayer(1));
    BOOST_TEST(GAMECLIENT.GetGCFactory(0) == static_cast<GameCommandFactory*>(nullptr));
    BOOST_TEST(GAMECLIENT.GetGCFactory(1) != static_cast<GameCommandFactory*>(nullptr));
    BOOST_TEST(GAMECLIENT.GetAdditionalLocalPlayers().empty());
}

/// Restposten Phase 1 (b): der Fall "beide getauschten Slots sind lokal gesteuert" war nur
/// per RTTR_Assert_Msg gesichert - im Releasebuild wirkungslos. Der Zweig darunter liess den
/// ALTEN Puffer von playerId2 stehen (LocalPlayerCommands::AddPlayer ist idempotent), sodass
/// die Kommandos des zweiten lokalen Menschen nach dem Tausch dem umgezogenen Hauptspieler
/// zugerechnet worden waeren.
///
/// Der Tausch laeuft hier ueber den echten Nachrichtenweg (GameMessage_Player_Swap an den
/// Server, GameServer.cpp:1635-1648 -> SwapPlayer -> SendToAll), damit auch die Zuordnung
/// Verbindung<->Slot auf der Serverseite mitzieht und die Partie danach weiterlaufen kann.
/// RequestSwapToPlayer wuerde diesen Tausch ablehnen (GameClient.cpp:2091-2092) - genau deshalb
/// muss die Nachricht hier von Hand kommen: sie simuliert die einzige Quelle, aus der der Fall
/// ueberhaupt entstehen kann.
///
/// Die Pause zwischen Kommando und Tausch ist notwendig und exakt: SetPause(true) wirkt beim
/// Host sofort (GameClient.cpp:1806-1811, OnGameMessage wird lokal aufgerufen), es laeuft also
/// garantiert kein NWF - und damit kein ExecuteNWF/Fetch - zwischen dem Absetzen des Kommandos
/// und dem Tausch.
BOOST_FIXTURE_TEST_CASE(SwapBetweenTwoLocalSlotsDropsThePendingCommandsOfBothSlots, LocalGameFixture)
{
    hostAndEnterLobby();
    GAMECLIENT.SetAdditionalLocalPlayers({1});
    GameClient::ApplyAdditionalLocalPlayers(lobby(), GAMECLIENT.GetAdditionalLocalPlayers());
    lobby().SetPlayerState(2, PlayerState::AI, AI::Info(AI::Type::Dummy));
    {
        GlobalGameSettings ggs = lobby().GetGGS();
        ggs.speed = GameSpeed::VeryFast;
        lobby().ChangeGlobalGameSettings(ggs);
    }
    pumpUntil(
      [] {
          const auto lobby = GAMECLIENT.GetGameLobby();
          return lobby->getPlayer(1).ps == PlayerState::AI && lobby->getPlayer(1).aiInfo.type == AI::Type::Dummy
                 && lobby->getPlayer(2).ps == PlayerState::AI && lobby->getSettings().speed == GameSpeed::VeryFast;
      },
      "lobby to apply the local player configuration");
    startGame();

    BOOST_TEST_REQUIRE(GAMECLIENT.IsLocalHumanPlayer(0));
    BOOST_TEST_REQUIRE(GAMECLIENT.IsLocalHumanPlayer(1));
    const MilitarySettings mil1Before = readMilitarySettings(world().GetPlayer(1));
    BOOST_TEST_REQUIRE((milB != mil1Before));

    GameCommandFactory* const factory1 = GAMECLIENT.GetGCFactory(1);
    BOOST_TEST_REQUIRE(factory1 != static_cast<GameCommandFactory*>(nullptr));
    // Liegt jetzt unverschickt im Puffer von Slot 1 - verschickt wird erst im naechsten NWF.
    BOOST_TEST_REQUIRE(factory1->ChangeMilitary(milB));
    // Ab hier laeuft kein GF mehr, der Puffer bleibt also nachweislich gefuellt.
    GAMECLIENT.SetPause(true);
    BOOST_TEST_REQUIRE(GAMECLIENT.IsPaused());
    // Gegenprobe, dass die Pause wirklich greift: ein weiteres Kommando wird jetzt abgelehnt.
    BOOST_TEST_REQUIRE(!factory1->ChangeMilitary(milA));

    GAMECLIENT.GetMainPlayer().sendMsgAsync(new GameMessage_Player_Swap(0xFF, 1));
    pumpUntil([this] { return !ci().swaps.empty(); }, "the server to broadcast the swap");
    BOOST_TEST_REQUIRE(ci().swaps.size() == 1u);
    BOOST_TEST(ci().swaps[0].first == 0u);
    BOOST_TEST(ci().swaps[0].second == 1u);

    // Buchhaltung wie im Fall "nur playerId1 lokal": Slot 0 ist aufgegeben, Slot 1 ist der
    // Hauptspieler. Zusaetzlich - und das ist der Fix - ist der Puffer von Slot 1 geleert.
    BOOST_TEST(!GAMECLIENT.IsLocalHumanPlayer(0));
    BOOST_TEST(GAMECLIENT.IsLocalHumanPlayer(1));
    BOOST_TEST(GAMECLIENT.GetPlayerId() == 1u);
    BOOST_TEST(GAMECLIENT.GetGCFactory(0) == static_cast<GameCommandFactory*>(nullptr));
    BOOST_TEST(GAMECLIENT.GetAdditionalLocalPlayers().empty());

    GAMECLIENT.SetPause(false);
    pumpUntil([] { return !GAMECLIENT.IsPaused(); }, "the game to continue");
    const unsigned startGF = GAMECLIENT.GetGFNumber();
    pumpUntilGF(startGF + 20);

    // Der eigentliche Nachweis: das gepufferte Kommando hat die Welt nie erreicht.
    for(unsigned i = 0; i < milB.size(); i++)
    {
        BOOST_TEST_INFO("military setting " << i);
        BOOST_TEST(world().GetPlayer(1).GetMilitarySetting(i) == mil1Before[i]);
    }
    BOOST_TEST(ci().numErrors == 0u);
    BOOST_TEST(ci().numAsync == 0u);
}

/// P2 aus Phase 2 - der dritte Fall der Tauschbuchhaltung: ein Mensch, den DIESER Client nicht
/// steuert, zieht auf einen von uns lokal gesteuerten Zusatzslot (wasLocal2 && !wasLocal1,
/// network/GameClient.cpp:2029).
///
/// Hier stand bis Phase 3 ein RTTR_Assert_Msg(false, "Remote swap onto a locally controlled
/// slot") - im Debugbuild also ein ueber das Netz ausloesbarer Programmabbruch an einer Stelle,
/// an der der alte Code gar nichts tat; und "gar nichts tun" war ebenfalls falsch, weil unser
/// Mensch den Slot dann weiter befehligt haette, obwohl dort jetzt ein Fremder sitzt. Die
/// wirksame, nicht abbrechende Behandlung ist das protokollierte Aufgeben des Slots.
///
/// EHRLICHE GRENZE dieses Tests: LocalGameFixture macht bewusst nur EINE Verbindung auf, und
/// GameClient::ChangePlayerIngame verlangt playerId1 == Occupied (GameClientCommands.cpp:87-88).
/// Ein fremder Occupied-Slot kann in dieser Aufstellung also nicht auf dem normalen Weg
/// entstehen. Der Weltzustand, den ein zweiter verbundener Client erzeugt haette, wird deshalb
/// direkt gesetzt. Was danach geprueft wird - der Zweig selbst - laeuft vollstaendig im
/// Produktivcode ab; clientlokal ist der Slot echt fremd, weil gameCommands_ von dieser
/// Zuweisung unberuehrt bleibt.
BOOST_FIXTURE_TEST_CASE(SwapOfAForeignHumanOntoALocalSlotGivesUpThatSlot, LocalGameFixture)
{
    hostAndEnterLobby();
    GAMECLIENT.SetAdditionalLocalPlayers({1});
    GameClient::ApplyAdditionalLocalPlayers(lobby(), GAMECLIENT.GetAdditionalLocalPlayers());
    lobby().SetPlayerState(2, PlayerState::AI, AI::Info(AI::Type::Dummy));
    pumpUntil(
      [] {
          const auto lobby = GAMECLIENT.GetGameLobby();
          return lobby->getPlayer(1).ps == PlayerState::AI && lobby->getPlayer(2).ps == PlayerState::AI;
      },
      "lobby to apply the local player configuration");
    startGame();

    BOOST_TEST_REQUIRE(GAMECLIENT.IsLocalHumanPlayer(0));
    BOOST_TEST_REQUIRE(GAMECLIENT.IsLocalHumanPlayer(1));

    // Der Zustand, den der Beitritt eines zweiten Clients hinterlassen haette
    world().GetPlayer(2).ps = PlayerState::Occupied;
    BOOST_TEST_REQUIRE(!GAMECLIENT.IsLocalHumanPlayer(2));

    // Genau der Aufruf, den OnGameMessage(GameMessage_Player_Swap) ingame ausloest
    // (GameClient.cpp:726). Er darf nicht abbrechen.
    GAMECLIENT.ChangePlayerIngame(2, 1);

    // Die Welt hat getauscht - das ist der Teil, der auf JEDEM Client gleich laufen muss
    BOOST_TEST((world().GetPlayer(2).ps == PlayerState::AI));
    BOOST_TEST((world().GetPlayer(1).ps == PlayerState::Occupied));

    // Die wirksame Behandlung: der Slot ist aufgegeben, unsere Kommandoquelle dafuer ist weg.
    // Mit dem alten Code (nur ein Assert, sonst nichts) waeren beide Zusicherungen rot: der
    // Slot bliebe lokal gesteuert und unsere Kommandos landeten beim fremden Menschen.
    BOOST_TEST(!GAMECLIENT.IsLocalHumanPlayer(1));
    BOOST_TEST(GAMECLIENT.GetGCFactory(1) == static_cast<GameCommandFactory*>(nullptr));
    BOOST_TEST(GAMECLIENT.GetAdditionalLocalPlayers().empty());

    // ... und zwar NUR dieser Slot. Der Hauptspieler bleibt unangetastet und spielbar.
    BOOST_TEST(GAMECLIENT.IsLocalHumanPlayer(0));
    BOOST_TEST(GAMECLIENT.GetPlayerId() == 0u);
    BOOST_TEST(GAMECLIENT.GetGCFactory(0) != static_cast<GameCommandFactory*>(nullptr));
    // Kein Abbruch und kein Fehler-Callback
    BOOST_TEST(ci().numErrors == 0u);

    // Ab hier wird bewusst nicht weitergespielt: der Weltzustand wurde von Hand gesetzt und
    // passt nicht mehr zu dem, was der Server fuehrt. Geprueft war der Zweig, nicht die Partie.
}

/// Restposten Phase 1 (a), zweite Haelfte: bleibt beim Spielstart doch ein angeforderter
/// Zusatzslot uebrig, der sich nicht registrieren laesst, bricht der Start sichtbar ab
/// (ClientError::LocalPlayerSetup), statt still als Einzelspieler weiterzulaufen. Der bogus-Slot
/// wird hier absichtlich erst NACH der Lobbykonfiguration gesetzt und laeuft an
/// ValidateAdditionalLocalPlayers vorbei - SetupLocalPlayers ist die letzte Verteidigungslinie.
BOOST_FIXTURE_TEST_CASE(StartGameFailsVisiblyWhenALocalSlotCannotBeRegistered, LocalGameFixture)
{
    hostAndEnterLobby();
    const auto numPlayers = static_cast<uint8_t>(GAMECLIENT.GetGameLobby()->getNumPlayers());
    GAMECLIENT.SetAdditionalLocalPlayers({1});
    GameClient::ApplyAdditionalLocalPlayers(lobby(), GAMECLIENT.GetAdditionalLocalPlayers());
    lobby().SetPlayerState(2, PlayerState::AI, AI::Info(AI::Type::Dummy));
    pumpUntil(
      [] {
          const auto lobby = GAMECLIENT.GetGameLobby();
          return lobby->getPlayer(1).ps == PlayerState::AI && lobby->getPlayer(2).ps == PlayerState::AI;
      },
      "lobby to apply the local player configuration");

    // Ein Slot, den es auf der Karte nicht gibt
    GAMECLIENT.SetAdditionalLocalPlayers({1, numPlayers});

    GAMECLIENT.Command_SetReady(true);
    lobby().StartCountdown(0);
    pumpUntil([this] { return ci().numErrors > 0 || !!ci().game; },
              "the client to report the failed local player setup");
    BOOST_TEST(ci().numErrors == 1u);
    BOOST_TEST(!ci().game); // CI_GameLoading darf gar nicht erst gelaufen sein
    BOOST_TEST(!ci().gameStarted);
    BOOST_TEST((GAMECLIENT.GetState() == ClientState::Stopped));
}

/// PHASE 2, der eigentliche Nachweis: aus zwei lokal gesteuerten Spielern entstehen zwei
/// GameWorldViews nebeneinander auf dem Bildschirm - jede mit eigenem Viewer (eigener Fog of
/// War, eigener TerrainRenderer) und eigenem, ueberschneidungsfreiem Ausschnitt der
/// Renderflaeche.
///
/// Gebaut wird ein ECHTES dskGameInterface auf der echten, laufenden Partie. initOGL=false, weil
/// TerrainRenderer::GenerateOpenGL ohne S2-Daten wirft; alles Gepruefte ist GL-frei.
BOOST_FIXTURE_TEST_CASE(TwoLocalPlayersGetTwoViewportsSideBySide, LocalGameFixture)
{
    hostAndEnterLobby();
    GAMECLIENT.SetAdditionalLocalPlayers({1});
    GameClient::ApplyAdditionalLocalPlayers(lobby(), GAMECLIENT.GetAdditionalLocalPlayers());
    lobby().SetPlayerState(2, PlayerState::AI, AI::Info(AI::Type::Dummy));
    pumpUntil(
      [] {
          const auto lobby = GAMECLIENT.GetGameLobby();
          return lobby->getPlayer(1).ps == PlayerState::AI && lobby->getPlayer(2).ps == PlayerState::AI;
      },
      "lobby to apply the local player configuration");
    startGame();

    BOOST_TEST_REQUIRE(GAMECLIENT.IsLocalHumanPlayer(0));
    BOOST_TEST_REQUIRE(GAMECLIENT.IsLocalHumanPlayer(1));

    const Extent renderSize = VIDEODRIVER.GetRenderSize();
    {
        auto desktop = std::make_unique<dskGameInterface>(ci().game, GAMECLIENT.GetNWFInfo(),
                                                          GAMECLIENT.GetPlayerId(), /*initOGL*/ false);

        // Zwei Ansichten, je eine je lokalem Spieler, in dieser Reihenfolge
        BOOST_TEST_REQUIRE(desktop->GetNumViews() == 2u);
        BOOST_TEST(desktop->GetPlayerView(0).GetPlayerId() == 0u);
        BOOST_TEST(desktop->GetPlayerView(1).GetPlayerId() == 1u);

        // Eigener Viewer je Ansicht - sonst waere der Fog of War wieder global
        const GameWorldViewer& viewer0 = desktop->GetPlayerView(0).GetViewer();
        const GameWorldViewer& viewer1 = desktop->GetPlayerView(1).GetViewer();
        BOOST_TEST((&viewer0 != &viewer1));
        BOOST_TEST(viewer0.GetPlayerId() == 0u);
        BOOST_TEST(viewer1.GetPlayerId() == 1u);
        BOOST_TEST_REQUIRE(!viewer0.IsAllVisible());
        // Jeder Spieler sieht sein eigenes HQ und nicht das des anderen
        const MapPoint hq0 = world().GetPlayer(0).GetHQPos();
        const MapPoint hq1 = world().GetPlayer(1).GetHQPos();
        BOOST_TEST_REQUIRE((hq0 != hq1));
        BOOST_TEST((viewer0.GetVisibility(hq0) == Visibility::Visible));
        BOOST_TEST((viewer0.GetVisibility(hq1) == Visibility::Invisible));
        BOOST_TEST((viewer1.GetVisibility(hq1) == Visibility::Visible));
        BOOST_TEST((viewer1.GetVisibility(hq0) == Visibility::Invisible));

        // ... und beide zeigen auf ihr eigenes HQ, also verschiedene Kartenausschnitte
        const GameWorldView& left = desktop->GetPlayerView(0).GetView();
        const GameWorldView& right = desktop->GetPlayerView(1).GetView();
        BOOST_TEST((left.GetOffset() != right.GetOffset()));
        BOOST_TEST((left.GetFirstPt() != right.GetFirstPt()));

        // Die Ansichten liegen nebeneinander und ueberlappen sich nicht
        const std::vector<Viewport> expected = CalcViewports(renderSize, 2);
        BOOST_TEST_REQUIRE(expected.size() == 2u);
        BOOST_TEST((left.GetPos() == expected[0].origin));
        BOOST_TEST((left.GetSize() == expected[0].size));
        BOOST_TEST((right.GetPos() == expected[1].origin));
        BOOST_TEST((right.GetSize() == expected[1].size));
        BOOST_TEST(left.GetPos().x + static_cast<int>(left.GetSize().x) <= right.GetPos().x);
        // ... und die glScissor-Rechtecke ebenfalls nicht
        const Rect scLeft = left.GetScissorRect();
        const Rect scRight = right.GetScissorRect();
        BOOST_TEST(scLeft.right <= scRight.left);

        // Ein Resize verteilt neu, ohne dass eine Ansicht die Renderflaeche verlaesst
        desktop->Resize(renderSize);
        BOOST_TEST((desktop->GetPlayerView(0).GetView().GetSize() == expected[0].size));
        BOOST_TEST((desktop->GetPlayerView(1).GetView().GetSize() == expected[1].size));

        // Der Zeiger gehoert der Ansicht: derselbe Bildschirmpunkt liegt nur in einer von beiden
        const Position ptInLeft(10, 10);
        const Position ptInRight(expected[1].origin + Position(10, 10));
        BOOST_TEST(desktop->GetPlayerView(0).ContainsViewPos(ptInLeft));
        BOOST_TEST(!desktop->GetPlayerView(1).ContainsViewPos(ptInLeft));
        BOOST_TEST(!desktop->GetPlayerView(0).ContainsViewPos(ptInRight));
        BOOST_TEST(desktop->GetPlayerView(1).ContainsViewPos(ptInRight));
    }
    // Der Desktop hat sich in der Welt eingetragen (dskGameInterface.cpp: SetGameInterface(this)),
    // der Destruktor traegt sich nicht aus. Im Spiel lebt er bis zum Partieende, hier nicht.
    world().SetGameInterface(nullptr);
}

/// Gegenprobe: EIN lokaler Spieler ergibt genau EINE Ansicht ueber die volle Renderflaeche -
/// die harte Randbedingung "Einzelspieler darf nicht regressieren", am echten Desktop gemessen.
BOOST_FIXTURE_TEST_CASE(SingleLocalPlayerStillGetsOneFullscreenViewport, LocalGameFixture)
{
    hostAndEnterLobby();
    lobby().SetPlayerState(1, PlayerState::AI, AI::Info(AI::Type::Dummy));
    lobby().SetPlayerState(2, PlayerState::AI, AI::Info(AI::Type::Dummy));
    pumpUntil(
      [] {
          const auto lobby = GAMECLIENT.GetGameLobby();
          return lobby->getPlayer(1).ps == PlayerState::AI && lobby->getPlayer(2).ps == PlayerState::AI;
      },
      "lobby to apply the AI configuration");
    startGame();
    BOOST_TEST_REQUIRE(GAMECLIENT.GetAdditionalLocalPlayers().empty());

    {
        auto desktop = std::make_unique<dskGameInterface>(ci().game, GAMECLIENT.GetNWFInfo(),
                                                          GAMECLIENT.GetPlayerId(), /*initOGL*/ false);
        BOOST_TEST_REQUIRE(desktop->GetNumViews() == 1u);
        const GameWorldView& view = desktop->GetPlayerView(0).GetView();
        BOOST_TEST((view.GetPos() == Position(0, 0)));
        BOOST_TEST((view.GetSize() == VIDEODRIVER.GetRenderSize()));
        BOOST_TEST((&desktop->GetView() == &view)); // GetView() liefert weiter den Hauptspieler
    }
    world().SetGameInterface(nullptr);
}

/// F3-Regression: Netzwerkmehrspieler darf keine zusaetzlichen lokalen Spieler bekommen und
/// --local-players darf nicht still gegen --ai verlieren. Beides ist ohne Netz pruefbar,
/// weil ValidateAdditionalLocalPlayers rein auf der Lobby arbeitet.
BOOST_AUTO_TEST_CASE(ValidateAdditionalLocalPlayersRejectsInvalidRequests)
{
    GameLobby lobby(/*isSavegame*/ false, /*isHost*/ true, /*numPlayers*/ 3);
    lobby.getPlayer(0).ps = PlayerState::Occupied;
    lobby.getPlayer(0).isHost = true;
    lobby.getPlayer(1).ps = PlayerState::AI;
    lobby.getPlayer(2).ps = PlayerState::AI;

    // Nichts angefordert -> immer in Ordnung
    BOOST_TEST(GameClient::ValidateAdditionalLocalPlayers(lobby, 0, {}, false) == std::string());
    BOOST_TEST(GameClient::ValidateAdditionalLocalPlayers(lobby, 0, {}, true) == std::string());
    // Gueltig
    BOOST_TEST(GameClient::ValidateAdditionalLocalPlayers(lobby, 0, {1, 2}, false) == std::string());
    // F3: KI-Battle schliesst zusaetzliche lokale Spieler aus
    BOOST_TEST(!GameClient::ValidateAdditionalLocalPlayers(lobby, 0, {1}, true).empty());
    // Slot existiert nicht
    BOOST_TEST(!GameClient::ValidateAdditionalLocalPlayers(lobby, 0, {3}, false).empty());
    // Slot ist der Hauptspieler
    BOOST_TEST(!GameClient::ValidateAdditionalLocalPlayers(lobby, 0, {0}, false).empty());
    // Doppelt angefordert
    BOOST_TEST(!GameClient::ValidateAdditionalLocalPlayers(lobby, 0, {1, 1}, false).empty());
    // Von einem echten Netzwerkspieler belegt
    lobby.getPlayer(2).ps = PlayerState::Occupied;
    BOOST_TEST(!GameClient::ValidateAdditionalLocalPlayers(lobby, 0, {2}, false).empty());
    // Nicht Host
    GameLobby guestLobby(false, /*isHost*/ false, 3);
    guestLobby.getPlayer(1).ps = PlayerState::AI;
    BOOST_TEST(!GameClient::ValidateAdditionalLocalPlayers(guestLobby, 0, {1}, false).empty());
}

/// Befund 3: mehr lokale Spieler als Ansichten. Frueher registrierte SetupLocalPlayers alle
/// angeforderten Slots und erst dskGameInterface::CreateViews brach bei MAX_VIEWPORTS wortlos
/// ab - die ueberzaehligen Spieler waren lokal gesteuert, bekamen aber wegen GameLoaded keine
/// KI, keine Ansicht und kein Eingabegeraet: stumme Geisterslots. Jetzt gibt es dafuer einen
/// Fehler, bevor irgendetwas eingerichtet wird.
BOOST_AUTO_TEST_CASE(ValidateAdditionalLocalPlayersRejectsMoreLocalPlayersThanViewports)
{
    GameLobby lobby(/*isSavegame*/ false, /*isHost*/ true, /*numPlayers*/ 8);
    lobby.getPlayer(0).ps = PlayerState::Occupied;
    lobby.getPlayer(0).isHost = true;
    for(unsigned i = 1; i < 8; ++i)
        lobby.getPlayer(i).ps = PlayerState::AI;

    // Der Hauptspieler zaehlt mit: MAX_VIEWPORTS-1 zusaetzliche Slots sind gerade noch erlaubt
    std::vector<uint8_t> ids;
    for(uint8_t i = 1; i < MAX_VIEWPORTS; ++i)
        ids.push_back(i);
    BOOST_TEST(GameClient::ValidateAdditionalLocalPlayers(lobby, 0, ids, false) == std::string());

    // Einer mehr nicht - und der Fehler nennt beide Zahlen
    ids.push_back(static_cast<uint8_t>(MAX_VIEWPORTS));
    const std::string err = GameClient::ValidateAdditionalLocalPlayers(lobby, 0, ids, false);
    BOOST_TEST_REQUIRE(!err.empty());
    BOOST_TEST(err.find(std::to_string(MAX_VIEWPORTS)) != std::string::npos);
    BOOST_TEST(err.find(std::to_string(MAX_VIEWPORTS + 1)) != std::string::npos);
}

/// Restposten Phase 1 (a): PlayerState::Locked war der Pruefung unbekannt. Bei einem Savegame
/// laesst GameServer.cpp:979-987 einen Locked-Slot unveraendert, ApplyAdditionalLocalPlayers
/// blieb also wirkungslos und SetupLocalPlayers verwarf den Spieler beim Start still.
BOOST_AUTO_TEST_CASE(ValidateAdditionalLocalPlayersRejectsLockedSavegameSlots)
{
    GameLobby saveLobby(/*isSavegame*/ true, /*isHost*/ true, /*numPlayers*/ 3);
    saveLobby.getPlayer(0).ps = PlayerState::Occupied;
    saveLobby.getPlayer(0).isHost = true;
    saveLobby.getPlayer(1).ps = PlayerState::AI;
    saveLobby.getPlayer(2).ps = PlayerState::Locked;

    // Der offene KI-Slot bleibt gueltig, auch im Savegame
    BOOST_TEST(GameClient::ValidateAdditionalLocalPlayers(saveLobby, 0, {1}, false) == std::string());
    // Der geschlossene Slot nicht - und der Fehler nennt den Slot
    const std::string err = GameClient::ValidateAdditionalLocalPlayers(saveLobby, 0, {2}, false);
    BOOST_TEST(!err.empty());
    BOOST_TEST(err.find('2') != std::string::npos);
    // Auch dann nicht, wenn er zusammen mit einem gueltigen Slot angefordert wird
    BOOST_TEST(!GameClient::ValidateAdditionalLocalPlayers(saveLobby, 0, {1, 2}, false).empty());

    // Ohne Savegame kann der Server einen Locked-Slot sehr wohl oeffnen (GameServer.cpp:988-991),
    // dort bleibt die Anforderung also gueltig.
    GameLobby mapLobby(/*isSavegame*/ false, /*isHost*/ true, /*numPlayers*/ 3);
    mapLobby.getPlayer(0).ps = PlayerState::Occupied;
    mapLobby.getPlayer(0).isHost = true;
    mapLobby.getPlayer(1).ps = PlayerState::AI;
    mapLobby.getPlayer(2).ps = PlayerState::Locked;
    BOOST_TEST(GameClient::ValidateAdditionalLocalPlayers(mapLobby, 0, {2}, false) == std::string());
}

BOOST_AUTO_TEST_SUITE_END()
