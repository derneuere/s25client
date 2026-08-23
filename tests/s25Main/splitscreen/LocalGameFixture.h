// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Game.h"
#include "GameLobby.h"
#include "GameLobbyController.h"
#include "GameManager.h"
#include "JoinPlayerInfo.h"
#include "RttrConfig.h"
#include "Settings.h"
#include "files.h"
#include "network/ClientInterface.h"
#include "network/CreateServerInfo.h"
#include "network/GameClient.h"
#include "network/GameServer.h"
#include "gameTypes/MapDescription.h"
#include "gameTypes/ServerType.h"
#include "drivers/AudioDriverWrapper.h"
#include "drivers/VideoDriverWrapper.h"
#include "world/GameWorld.h"
#include "WindowManager.h"
#include "test/testConfig.h"
#include "rttr/test/ConfigOverride.hpp"
#include "rttr/test/TmpFolder.hpp"
#include "rttr/test/random.hpp"
#include "uiHelper/uiHelpers.hpp"
#include "s25util/Log.h"
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

/// Faehrt eine echte, vollstaendige Einzelspielerpartie im Testprozess hoch:
/// GameServer + GameClient ueber einen Loopback-Socket, exakt wie GameClient::HostGame es im
/// Spiel tut. Kein Test-Double fuer den Server - genau das ist der Punkt, weil der
/// Sendezweig fuer zusaetzliche lokale Spieler auf GameServer::GetTargetPlayer angewiesen ist.
///
/// Fallstricke, die den Aufbau bestimmen (alle im Produktivcode nachgelesen):
///  - GameClient::StartGame malt den "Mond" ueber VIDEODRIVER/LOADER -> uiHelper::initGUITests()
///  - GameClient::OnGameStart dereferenziert GAMEMANAGER -> eigene Instanz setzen
///  - OnGameStart muss zweimal laufen (1. Loaded->Game, 2. game->Start())
///  - GameClient::GetPlayer/GetNumPlayers sind privat -> Welt ueber den Game-Zeiger aus
///    CI_GameLoading
///  - Der Simulationscode greift auf das GAMECLIENT-Singleton zu; eine Stack-Instanz wie in
///    testGameClient.cpp ist hier nicht verwendbar.
namespace rttr::test {

class LocalGameFixture
{
public:
    /// Faengt die Callbacks des Clients ab. Bewusst kein turtle-Mock: wir brauchen echte
    /// Reaktionen (Game-Zeiger merken) und einen Zaehler fuer die Fehler-Callbacks.
    struct TestClientInterface : ClientInterface
    {
        std::shared_ptr<Game> game;
        bool gameStarted = false;
        unsigned numErrors = 0;
        unsigned numAsync = 0;
        unsigned numReplayAsync = 0;
        bool replayEnded = false;
        std::vector<std::pair<unsigned, unsigned>> swaps;

        void CI_GameLoading(std::shared_ptr<Game> g) override { game = std::move(g); }
        void CI_GameStarted() override { gameStarted = true; }
        void CI_Error(ClientError) override { ++numErrors; }
        void CI_Async(const std::string&) override { ++numAsync; }
        void CI_ReplayAsync(const std::string&) override { ++numReplayAsync; }
        void CI_ReplayEndReached(const std::string&) override { replayEnded = true; }
        void CI_PlayersSwapped(unsigned p1, unsigned p2) override { swaps.emplace_back(p1, p2); }
    };

    LocalGameFixture()
        : userData_(), userDataOverride_("USERDATA", userData_), gameManager_(LOG, SETTINGS, VIDEODRIVER, AUDIODRIVER,
                                                                              WINDOWMANAGER)
    {
        uiHelper::initGUITests();
        boost::filesystem::create_directories(RTTRCONFIG.ExpandPath(s25::folders::mapsPlayed));
        boost::filesystem::create_directories(RTTRCONFIG.ExpandPath(s25::folders::replays));
        // Autosave wuerde waehrend der Partie in HandleAutosave Dateien schreiben
        oldAutosave_ = SETTINGS.interface.autosaveInterval;
        SETTINGS.interface.autosaveInterval = 0;
        setGlobalGameManager(&gameManager_);
        GAMECLIENT.SetInterface(&ci_);

        // Karte OHNE das danebenliegende Lua-Skript kopieren: GameServer::Start zieht ein
        // gleichnamiges .lua neben der Karte automatisch mit hinein.
        mapPath_ = mapDir_ / "SplitscreenTest.SWD";
        boost::filesystem::copy_file(rttr::test::rttrBaseDir / "tests" / "testData" / "maps" / "LuaFunctions.SWD",
                                     mapPath_);
    }

    ~LocalGameFixture()
    {
        GAMECLIENT.RemoveInterface(&ci_);
        GAMECLIENT.Stop();
        GAMESERVER.Stop();
        setGlobalGameManager(nullptr);
        SETTINGS.interface.autosaveInterval = oldAutosave_;
    }

    TestClientInterface& ci() { return ci_; }
    const boost::filesystem::path& mapPath() const { return mapPath_; }

    /// Ein Durchlauf von Server und Client. Genau das macht GameManager::Run im Spiel.
    static void pump()
    {
        GAMESERVER.Run();
        GAMECLIENT.Run();
    }

    /// Pumpt, bis die Bedingung wahr ist oder die Zeit abgelaufen ist. Liefert das Ergebnis der
    /// Bedingung, ohne den Test scheitern zu lassen.
    template<class T_Predicate>
    static bool pumpWhile(const T_Predicate& isDone, std::chrono::seconds timeout)
    {
        const auto start = std::chrono::steady_clock::now();
        while(!isDone())
        {
            if(std::chrono::steady_clock::now() - start >= timeout)
                return false;
            pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

    /// Pumpt, bis die Bedingung wahr ist. Ein Timeout ist ein Testfehlschlag, kein Haenger.
    template<class T_Predicate>
    static void pumpUntil(const T_Predicate& isDone, const std::string& what,
                          std::chrono::seconds timeout = std::chrono::seconds(60))
    {
        BOOST_TEST_REQUIRE(pumpWhile(isDone, timeout), "Timeout while waiting for: " << what);
    }

    /// Hostet die Partie und wartet, bis der Client in der Lobby (ClientState::Config) ist.
    ///
    /// Der Port wird zufaellig gewaehlt. Ein erfolgreicher HostGame() heisst nur, dass der Server
    /// binden konnte - die Loopback-Verbindung des Clients kann trotzdem scheitern (Port bereits
    /// als ausgehender Port belegt, TIME_WAIT). Deshalb wird der GESAMTE Vorgang bis
    /// ClientState::Config wiederholt und nicht nur das Binden; sonst wartet der Test 60s auf
    /// eine Verbindung, die nie zustande kommt, und faellt sporadisch um.
    void hostAndEnterLobby()
    {
        bool inLobby = false;
        for(unsigned i = 0; i < 10 && !inLobby; i++)
        {
            const auto port = static_cast<uint16_t>(rttr::test::randomValue(1024, 49151));
            const CreateServerInfo csi(ServerType::Local, port, "SplitscreenTest");
            if(GAMECLIENT.HostGame(csi, MapDescription(mapPath_, MapType::OldMap)))
            {
                inLobby = pumpWhile([] { return GAMECLIENT.GetState() == ClientState::Config; },
                                    std::chrono::seconds(10));
            }
            if(!inLobby)
            {
                GAMECLIENT.Stop();
                GAMESERVER.Stop();
            }
        }
        BOOST_TEST_REQUIRE(inLobby);
        BOOST_TEST_REQUIRE(GAMECLIENT.GetGameLobby());
        lobbyController_ = std::make_unique<GameLobbyController>(GAMECLIENT.GetGameLobby(), GAMECLIENT.GetMainPlayer());
    }

    GameLobbyController& lobby()
    {
        BOOST_TEST_REQUIRE(!!lobbyController_);
        return *lobbyController_;
    }

    /// Startet die Partie aus der Lobby heraus und laeuft bis zum ersten laufenden GF.
    void startGame()
    {
        GAMECLIENT.Command_SetReady(true);
        // Der Server startet bei genau einer Verbindung sofort (GameServer.cpp:1381)
        lobby().StartCountdown(0);
        pumpUntil([] { return GAMECLIENT.GetState() == ClientState::Loading; }, "client to start loading");
        // In Produktion macht das dskGameLoader (dskGameLoader.cpp:118)
        GAMECLIENT.GameLoaded();
        // 1. OnGameStart (Loaded -> Game) passiert in GameClient::Run.
        pumpUntil([] { return GAMECLIENT.GetState() == ClientState::Game; }, "client to enter game state");
        // 2. OnGameStart entpausiert und ruft game->Start(). In Produktion: dskGameInterface.cpp:226
        BOOST_TEST_REQUIRE(!!ci_.game);
        GAMECLIENT.OnGameStart();
        BOOST_TEST_REQUIRE(ci_.game->IsStarted());
        BOOST_TEST_REQUIRE(!GAMECLIENT.IsPaused());
        lobbyController_.reset(); // Die Lobby ist ab jetzt weg (GameClient.cpp: gameLobby.reset())
    }

    /// Laeuft, bis der angegebene GF erreicht ist.
    static void pumpUntilGF(const unsigned targetGF)
    {
        pumpUntil([targetGF] { return GAMECLIENT.GetGFNumber() >= targetGF; },
                  "GF " + std::to_string(targetGF) + " to be reached");
    }

    GameWorld& world()
    {
        BOOST_TEST_REQUIRE(!!ci_.game);
        return ci_.game->world_;
    }

private:
    rttr::test::TmpFolder userData_;
    rttr::test::ConfigOverride userDataOverride_;
    rttr::test::TmpFolder mapDir_;
    boost::filesystem::path mapPath_;
    GameManager gameManager_;
    TestClientInterface ci_;
    std::unique_ptr<GameLobbyController> lobbyController_;
    unsigned oldAutosave_ = 0;
};

} // namespace rttr::test
