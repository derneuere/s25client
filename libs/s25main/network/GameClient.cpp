// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "GameClient.h"
#include "CreateServerInfo.h"
#include "EventManager.h"
#include "Game.h"
#include "GameEvent.h"
#include "GameLobby.h"
#include "GameManager.h"
#include "GameMessage_GameCommand.h"
#include "JoinPlayerInfo.h"
#include "LeatherLoader.h"
#include "Loader.h"
#include "NWFInfo.h"
#include "PlayerGameCommands.h"
#include "RTTR_Version.h"
#include "ReplayInfo.h"
#include "RttrConfig.h"
#include "Savegame.h"
#include "SerializedGameData.h"
#include "Settings.h"
#include "ai/AIPlayer.h"
#include "drivers/VideoDriverWrapper.h"
#include "factories/AIFactory.h"
#include "files.h"
#include "helpers/containerUtils.h"
#include "helpers/format.hpp"
#include "helpers/mathFuncs.h"
#include "lua/LuaInterfaceBase.h"
#include "network/ClientInterface.h"
#include "network/GameMessages.h"
#include "network/GameServer.h"
#include "network/IGameLobbyController.h"
#include "network/LocalPlayerGCFactory.h"
#include "ogl/FontStyle.h"
#include "ogl/glArchivItem_Bitmap.h"
#include "ogl/glFont.h"
#include "random/Random.h"
#include "random/randomIO.h"
#include "world/GameWorld.h"
#include "world/GameWorldView.h"
#include "world/ViewportLayout.h"
#include "world/MapLoader.h"
#include "gameTypes/RoadBuildState.h"
#include "gameData/GameConsts.h"
#include "gameData/PortraitConsts.h"
#include "libsiedler2/ArchivItem_Map.h"
#include "libsiedler2/ArchivItem_Map_Header.h"
#include "libsiedler2/prototypen.h"
#include "s25util/SocketSet.h"
#include "s25util/StringConversion.h"
#include "s25util/System.h"
#include "s25util/fileFuncs.h"
#include "s25util/strFuncs.h"
#include "s25util/utf8.h"
#include <boost/filesystem.hpp>
#include <helpers/chronoIO.h>
#include <memory>

namespace {
constexpr bool DEBUG_MODE =
#ifndef NDEBUG
  true;
#else
  false;
#endif
void copyFileIfPathDifferent(const boost::filesystem::path& src_path, const boost::filesystem::path& dst_path)
{
    if(src_path != dst_path)
    {
        boost::system::error_code ignoredEc;
        constexpr auto overwrite_existing =
#if BOOST_VERSION >= 107400
          boost::filesystem::copy_options::overwrite_existing;
#else
          boost::filesystem::copy_option::overwrite_if_exists;
#endif
        copy_file(src_path, dst_path, overwrite_existing, ignoredEc);
    }
}
} // namespace

void GameClient::ClientConfig::Clear()
{
    server.clear();
    gameName.clear();
    password.clear();
    port = 0;
    isHost = false;
}

GameClient::GameClient() : skiptogf(0), mainPlayer(0), state(ClientState::Stopped), ci(nullptr), replayMode(false) {}

GameClient::~GameClient()
{
    Stop();
}

/**
 *  Verbindet den Client mit einem Server
 *
 *  @param server    Hostname des Zielrechners
 *  @param password  Passwort des Spieles
 *  @param servertyp Servertyp des Spieles (Direct/LAN/usw)
 *  @param host      gibt an ob wir selbst der Host sind
 *
 *  @return true, wenn Client erfolgreich verbunden und gestartet
 */
bool GameClient::Connect(const std::string& server, const std::string& password, ServerType servertyp,
                         unsigned short port, bool host, bool use_ipv6)
{
    // Raeumt u.a. aiBattlePlayers_ und additionalLocalPlayers_ (siehe Ende von Stop()).
    Stop();

    RTTR_Assert(aiBattlePlayers_.empty());
    // F5: Ein RTTR_Assert(additionalLocalPlayers_.empty()) stuende hier per Konstruktion immer
    // wahr da, weil Stop() den Vektor gerade geleert hat - also wirkungslos. Deshalb keins.

    // Name und Password kopieren
    clientconfig.server = server;
    clientconfig.password = password;

    clientconfig.servertyp = servertyp;
    clientconfig.port = port;
    clientconfig.isHost = host;

    // Verbinden
    if(!mainPlayer.socket.Connect(server, port, use_ipv6, SETTINGS.proxy))
    {
        LOG.write("GameClient::Connect: ERROR: Connect failed!\n");
        if(host)
            GAMESERVER.Stop();
        return false;
    }

    state = ClientState::Connect;
    AdvanceState(ConnectState::Initiated);

    // Es wird kein Replay abgespielt, sondern dies ist ein richtiges Spiel
    replayMode = false;

    return true;
}

bool GameClient::HostGame(const CreateServerInfo& csi, const MapDescription& map)
{
    std::string hostPw = createRandString(20);
    // Copy the map and lua to the played map folders to avoid having to transmit it from the (local) server
    const auto playedMapPath = RTTRCONFIG.ExpandPath(s25::folders::mapsPlayed) / map.map_path.filename();
    copyFileIfPathDifferent(map.map_path, playedMapPath);
    if(map.lua_path)
    {
        const auto playedMapLuaPath = RTTRCONFIG.ExpandPath(s25::folders::mapsPlayed) / map.lua_path->filename();
        copyFileIfPathDifferent(*map.lua_path, playedMapLuaPath);
    }
    return GAMESERVER.Start(csi, map, hostPw) && Connect("localhost", hostPw, csi.type, csi.port, true, csi.ipv6);
}

/**
 *  Hauptschleife des Clients
 */
void GameClient::Run()
{
    if(state == ClientState::Stopped)
        return;

    SocketSet set;

    // erstmal auf Daten überprüfen
    set.Clear();

    // zum set hinzufügen
    set.Add(mainPlayer.socket);
    if(set.Select(0, 0) > 0)
    {
        // nachricht empfangen
        if(!mainPlayer.receiveMsgs())
        {
            LOG.write("Receiving Message from server failed\n");
            ServerLost();
        }
    }

    // nun auf Fehler prüfen
    set.Clear();

    // zum set hinzufügen
    set.Add(mainPlayer.socket);

    // auf fehler prüfen
    if(set.Select(0, 2) > 0)
    {
        if(set.InSet(mainPlayer.socket))
        {
            // Server ist weg
            LOG.write("Error on socket to server\n");
            ServerLost();
        }
    }

    if(state == ClientState::Loaded)
    {
        // All players ready?
        if(nwfInfo->isReady())
            OnGameStart();
    } else if(state == ClientState::Game)
        ExecuteGameFrame();

    // maximal 10 Pakete verschicken
    mainPlayer.sendMsgs(10);

    mainPlayer.executeMsgs(*this);
}

/**
 *  Stoppt das Spiel
 */
void GameClient::Stop()
{
    if(state == ClientState::Stopped)
        return;

    if(game)
        ExitGame();
    else if(state == ClientState::Connect || state == ClientState::Config)
        gameLobby.reset();

    if(IsHost())
        GAMESERVER.Stop();

    framesinfo.Clear();
    clientconfig.Clear();
    mapinfo.Clear();

    if(replayinfo)
    {
        if(replayinfo->replay.IsRecording())
            replayinfo->replay.StopRecording();
        replayinfo->replay.Close();
        replayinfo.reset();
    }

    mainPlayer.closeConnection();

    // clear jump target
    skiptogf = 0;

    // Consistency check: No game, no lobby remaining
    RTTR_Assert(!game);
    RTTR_Assert(!gameLobby);

    state = ClientState::Stopped;
    LOG.write("client state changed to stop\n");

    aiBattlePlayers_.clear();
    additionalLocalPlayers_.clear();
}

std::shared_ptr<GameLobby> GameClient::GetGameLobby()
{
    RTTR_Assert(state != ClientState::Config || gameLobby);
    return gameLobby;
}

const AIPlayer* GameClient::GetAIPlayer(unsigned id) const
{
    if(!game)
        return nullptr;
    return game->GetAIPlayer(id);
}

/**
 *  Startet ein Spiel oder Replay.
 *
 *  @param[in] random_init Initialwert des Zufallsgenerators.
 */
void GameClient::StartGame(const unsigned random_init)
{
    RTTR_Assert(state == ClientState::Config || (state == ClientState::Stopped && replayMode));

    // Mond malen
    Position moonPos = VIDEODRIVER.GetMousePos();
    moonPos.y -= 40;
    LOADER.GetImageN("resource", 33)->DrawFull(moonPos);
    VIDEODRIVER.SwapBuffers();

    // Start in pause mode
    framesinfo.isPaused = true;

    // Je nach Geschwindigkeit GF-Länge einstellen
    framesinfo.gf_length = SPEED_GF_LENGTHS[gameLobby->getSettings().speed];
    framesinfo.gfLengthReq = framesinfo.gf_length;

    // Random-Generator initialisieren
    RANDOM.Init(random_init);

    if(!IsReplayModeOn() && mapinfo.savegame && !mapinfo.savegame->Load(mapinfo.filepath, SaveGameDataToLoad::All))
    {
        OnError(ClientError::InvalidMap);
        return;
    }

    // If we have a savegame, start at its first GF, else at 0
    unsigned startGF = (mapinfo.type == MapType::Savegame) ? mapinfo.savegame->start_gf : 0;
    // Create the game
    game =
      std::make_shared<Game>(std::move(gameLobby->getSettings()), startGF,
                             std::vector<PlayerInfo>(gameLobby->getPlayers().begin(), gameLobby->getPlayers().end()));
    if(!IsReplayModeOn())
    {
        for(unsigned id = 0; id < gameLobby->getNumPlayers(); id++)
        {
            if(gameLobby->getPlayer(id).isUsed())
                nwfInfo->addPlayer(id);
        }
    }
    // Release lobby
    gameLobby.reset();

    state = ClientState::Loading;

    // Lokal gesteuerte Spieler festlegen. Ab hier unveraenderlich bis ExitGame().
    // MUSS vor ci->CI_GameLoading stehen: ein ClientInterface darf daraus reentrant
    // GameLoaded() aufrufen, und GameLoaded braucht die Menge bereits (KI-Skip + Priming).
    if(!SetupLocalPlayers())
    {
        // Sichtbarer Abbruch statt stiller Degradierung zum Einzelspieler: OnError meldet ueber
        // ClientInterface::CI_Error und ruft Stop(). Der Spielstart bricht hier ab, bevor
        // irgendein Interface das halbfertige Spiel zu sehen bekommt.
        OnError(ClientError::LocalPlayerSetup);
        return;
    }

    if(ci)
        ci->CI_GameLoading(game);

    // Get standard settings before they get overwritten.
    // Je Slot aus SEINEM Spielzustand: der "Standard"-Knopf im Fenster eines zusaetzlichen
    // lokalen Spielers muss dessen Werkseinstellungen einsetzen, nicht die des Hauptspielers.
    for(unsigned id = 0; id < GetNumPlayers(); ++id)
        GetPlayer(id).FillVisualSettings(defaultSettings_[id]);

    GameWorld& gameWorld = game->world_;
    if(mapinfo.savegame)
        mapinfo.savegame->sgd.ReadSnapshot(*game, *this);
    else
    {
        RTTR_Assert(mapinfo.type != MapType::Savegame);
        /// Startbündnisse setzen
        for(auto& player : gameWorld.getPlayers())
            player.MakeStartPacts();

        MapLoader loader(gameWorld);
        if(!loader.Load(mapinfo.filepath)
           || (!mapinfo.luaFilepath.empty() && !loader.LoadLuaScript(*game, *this, mapinfo.luaFilepath)))
        {
            OnError(ClientError::InvalidMap);
            return;
        }
        // TODO (Replay): Always use true
        const bool fixFish = !GetReplay() || GetReplay()->GetMinorVersion() >= 3;
        MapLoader::SetupResources(gameWorld, fixFish);
    }
    gameWorld.InitAfterLoad();

    // Update visual settings
    ResetVisualSettings();

    if(!replayMode)
    {
        RTTR_Assert(!replayinfo);
        StartReplayRecording(random_init);
    }

    // Daten nach dem Schreiben des Replays ggf wieder löschen
    mapinfo.mapData.Clear();
}

void GameClient::GameLoaded()
{
    RTTR_Assert(state == ClientState::Loading);

    state = ClientState::Loaded;

    if(replayMode)
        OnGameStart();
    else
    {
        // Notify server that we are ready
        if(IsHost())
        {
            for(unsigned id = 0; id < GetNumPlayers(); id++)
            {
                if(GetPlayer(id).ps != PlayerState::AI)
                    continue;
                // Lokal von einem Menschen gesteuerte Slots bekommen keine KI
                if(gameCommands_.IsLocalPlayer(static_cast<uint8_t>(id)))
                    continue;
                game->AddAIPlayer(CreateAIPlayer(id, GetPlayer(id).aiInfo));
                SendNothingNC(id);
            }
            if(IsAIBattleModeOn())
                ToggleHumanAIPlayer(aiBattlePlayers_[GetPlayerId()]);

            // Startpaket fuer jeden zusaetzlichen lokalen Spieler. Ohne dieses Paket
            // wird NWFInfo::isReady() fuer den Slot nie wahr und die Partie steht lautlos.
            for(const uint8_t id : gameCommands_.GetPlayerIds())
            {
                if(id != GetPlayerId())
                    SendNothingNC(id);
            }
        }
        SendNothingNC();
    }
}

void GameClient::ExitGame()
{
    RTTR_Assert(state == ClientState::Game || state == ClientState::Loaded || state == ClientState::Loading);
    game.reset();
    nwfInfo.reset();
    // Clear remaining commands
    gameCommands_.Clear();
    localGCFactories_.clear();
    explicitActingPlayerId_.reset();
    hasExplicitActingPlayer_ = false;
    windowOwnerPlayerId_.reset();
    additionalLocalPlayers_.clear();
}

unsigned GameClient::GetGFNumber() const
{
    return game->em_->GetCurrentGF();
}

/**
 *  Ping-Nachricht.
 */
bool GameClient::OnGameMessage(const GameMessage_Ping& /*msg*/)
{
    mainPlayer.sendMsgAsync(new GameMessage_Pong());
    return true;
}

/**
 *  Player-ID-Nachricht.
 */
bool GameClient::OnGameMessage(const GameMessage_Player_Id& msg)
{
    if(!VerifyState(ConnectState::Initiated))
        return true;
    // haben wir eine ungültige ID erhalten? (aka Server-Voll)
    if(msg.player == GameMessageWithPlayer::NO_PLAYER_ID)
    {
        OnError(ClientError::ServerFull);
        return true;
    }

    mainPlayer.playerId = msg.player;

    // Server-Typ senden
    mainPlayer.sendMsgAsync(new GameMessage_Server_Type(clientconfig.servertyp, rttr::version::GetRevision()));
    AdvanceState(ConnectState::VerifyServer);
    return true;
}

/**
 *  Player-List-Nachricht.
 */
bool GameClient::OnGameMessage(const GameMessage_Player_List& msg)
{
    if(state != ClientState::Config && !VerifyState(ConnectState::QueryPlayerList))
        return true;
    RTTR_Assert(gameLobby);
    RTTR_Assert(gameLobby->getNumPlayers() == msg.playerInfos.size());
    if(gameLobby->getNumPlayers() != msg.playerInfos.size())
    {
        OnError(ClientError::InvalidMessage);
        return true;
    }

    for(unsigned i = 0; i < gameLobby->getNumPlayers(); ++i)
        gameLobby->getPlayer(i) = msg.playerInfos[i];

    if(state == ClientState::Connect)
        AdvanceState(ConnectState::QuerySettings);
    return true;
}

bool GameClient::OnGameMessage(const GameMessage_Player_Name& msg)
{
    if(state != ClientState::Config)
        return true;
    if(msg.player >= gameLobby->getNumPlayers())
        return true;
    gameLobby->getPlayer(msg.player).name = msg.playername;
    if(ci)
        ci->CI_PlayerDataChanged(msg.player);
    return true;
}

bool GameClient::OnGameMessage(const GameMessage_Player_Portrait& msg)
{
    if(state != ClientState::Config)
        return true;
    if(msg.player >= gameLobby->getNumPlayers())
        return true;
    if(msg.playerPortraitIndex >= Portraits.size())
        return true;
    gameLobby->getPlayer(msg.player).portraitIndex = msg.playerPortraitIndex;
    if(ci)
        ci->CI_PlayerDataChanged(msg.player);
    return true;
}

///////////////////////////////////////////////////////////////////////////////
/// player joined
/// @param message  Nachricht, welche ausgeführt wird
bool GameClient::OnGameMessage(const GameMessage_Player_New& msg)
{
    if(state != ClientState::Config)
        return true;

    if(msg.player >= gameLobby->getNumPlayers())
        return true;

    JoinPlayerInfo& playerInfo = gameLobby->getPlayer(msg.player);

    playerInfo.name = msg.name;
    playerInfo.ps = PlayerState::Occupied;
    playerInfo.ping = 0;

    if(ci)
        ci->CI_NewPlayer(msg.player);
    return true;
}

bool GameClient::OnGameMessage(const GameMessage_Player_Ping& msg)
{
    if(state == ClientState::Config)
    {
        if(msg.player >= gameLobby->getNumPlayers())
            return true;
        gameLobby->getPlayer(msg.player).ping = msg.ping;
    } else if(state == ClientState::Loading || state == ClientState::Loaded || state == ClientState::Game)
    {
        if(msg.player >= GetNumPlayers())
            return true;
        GetPlayer(msg.player).ping = msg.ping;
    } else
    {
        RTTR_Assert(false);
        return true;
    }

    if(ci)
        ci->CI_PingChanged(msg.player, msg.ping);
    return true;
}

/**
 *  Player-Toggle-State-Nachricht.
 */
bool GameClient::OnGameMessage(const GameMessage_Player_State& msg)
{
    if(state != ClientState::Config)
        return true;

    if(msg.player >= gameLobby->getNumPlayers())
        return true;

    JoinPlayerInfo& playerInfo = gameLobby->getPlayer(msg.player);
    bool wasUsed = playerInfo.isUsed();
    playerInfo.ps = msg.ps;
    playerInfo.aiInfo = msg.aiInfo;

    if(ci)
    {
        if(playerInfo.isUsed())
            ci->CI_NewPlayer(msg.player);
        else if(wasUsed)
            ci->CI_PlayerLeft(msg.player);
        else
            ci->CI_PlayerDataChanged(msg.player);
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
/// nation button gedrückt
/// @param message  Nachricht, welche ausgeführt wird
bool GameClient::OnGameMessage(const GameMessage_Player_Nation& msg)
{
    if(state != ClientState::Config)
        return true;

    if(msg.player >= gameLobby->getNumPlayers())
        return true;

    gameLobby->getPlayer(msg.player).nation = msg.nation;

    if(ci)
        ci->CI_PlayerDataChanged(msg.player);
    return true;
}

///////////////////////////////////////////////////////////////////////////////
/// team button gedrückt
/// @param message  Nachricht, welche ausgeführt wird
bool GameClient::OnGameMessage(const GameMessage_Player_Team& msg)
{
    if(state != ClientState::Config)
        return true;

    if(msg.player >= gameLobby->getNumPlayers())
        return true;

    gameLobby->getPlayer(msg.player).team = msg.team;

    if(ci)
        ci->CI_PlayerDataChanged(msg.player);
    return true;
}

///////////////////////////////////////////////////////////////////////////////
/// color button gedrückt
/// @param message  Nachricht, welche ausgeführt wird
bool GameClient::OnGameMessage(const GameMessage_Player_Color& msg)
{
    if(state != ClientState::Config)
        return true;

    if(msg.player >= gameLobby->getNumPlayers())
        return true;

    gameLobby->getPlayer(msg.player).color = msg.color;

    if(ci)
        ci->CI_PlayerDataChanged(msg.player);
    return true;
}

/**
 *  Ready-state eines Spielers hat sich geändert.
 *
 *  @param[in] message Nachricht, welche ausgeführt wird
 */
bool GameClient::OnGameMessage(const GameMessage_Player_Ready& msg)
{
    if(state != ClientState::Config)
        return true;

    if(msg.player >= gameLobby->getNumPlayers())
        return true;

    gameLobby->getPlayer(msg.player).isReady = msg.ready;

    if(ci)
        ci->CI_ReadyChanged(msg.player, msg.ready);
    return true;
}

///////////////////////////////////////////////////////////////////////////////
/// player gekickt
/// @param message  Nachricht, welche ausgeführt wird
bool GameClient::OnGameMessage(const GameMessage_Player_Kicked& msg)
{
    if(state == ClientState::Config)
    {
        if(msg.player >= gameLobby->getNumPlayers())
            return true;
        gameLobby->getPlayer(msg.player).ps = PlayerState::Free;
    } else if(state == ClientState::Loading || state == ClientState::Loaded || state == ClientState::Game)
    {
        // Im Spiel anzeigen, dass der Spieler das Spiel verlassen hat
        GamePlayer& player = GetPlayer(msg.player);
        if(player.ps != PlayerState::AI)
        {
            player.ps = PlayerState::AI;
            player.aiInfo = AI::Info(AI::Type::Dummy);
            // Host has to handle it
            if(IsHost())
            {
                game->AddAIPlayer(CreateAIPlayer(msg.player, player.aiInfo));
                SendNothingNC(msg.player);
            }
        }
    } else
        return true;

    if(ci)
        ci->CI_PlayerLeft(msg.player);
    return true;
}

bool GameClient::OnGameMessage(const GameMessage_Player_Swap& msg)
{
    LOG.writeToFile("<<< NMS_PLAYER_SWAP(%u, %u)\n") % unsigned(msg.player) % unsigned(msg.player2);

    if(state == ClientState::Config)
    {
        if(msg.player >= gameLobby->getNumPlayers() || msg.player2 >= gameLobby->getNumPlayers())
            return true;

        // During preparation just swap the players
        using std::swap;
        swap(gameLobby->getPlayer(msg.player), gameLobby->getPlayer(msg.player2));
        // Some things cannot be changed in savegames
        if(mapinfo.type == MapType::Savegame)
            gameLobby->getPlayer(msg.player).FixSwappedSaveSlot(gameLobby->getPlayer(msg.player2));

        // Evtl. sind wir betroffen?
        if(mainPlayer.playerId == msg.player)
            mainPlayer.playerId = msg.player2;
        else if(mainPlayer.playerId == msg.player2)
            mainPlayer.playerId = msg.player;

        // Die vorkonfigurierten zusaetzlichen lokalen Slots wandern mit, sonst zeigt der
        // beim Start validierte Index nach einem Tausch auf den falschen Slot.
        for(uint8_t& id : additionalLocalPlayers_)
        {
            if(id == msg.player)
                id = msg.player2;
            else if(id == msg.player2)
                id = msg.player;
        }

        if(ci)
            ci->CI_PlayersSwapped(msg.player, msg.player2);
    } else if(state == ClientState::Loading || state == ClientState::Loaded || state == ClientState::Game)
        ChangePlayerIngame(msg.player, msg.player2);
    else
        return true;
    mainPlayer.sendMsgAsync(new GameMessage_Player_SwapConfirm(msg.player, msg.player2));
    return true;
}

/**
 *  Server-Typ-Nachricht.
 */
bool GameClient::OnGameMessage(const GameMessage_Server_TypeOK& msg)
{
    if(!VerifyState(ConnectState::VerifyServer))
        return true;

    using StatusCode = GameMessage_Server_TypeOK::StatusCode;
    switch(msg.err_code)
    {
        case StatusCode::Ok: break;

        default:
        case StatusCode::InvalidServerType:
        {
            OnError(ClientError::InvalidServerType);
            return true;
        }
        break;

        case StatusCode::WrongVersion:
        {
            LOG.write(_("Version mismatch. Server version: %1%, your version %2%")) % msg.version
              % rttr::version::GetRevision();
            OnError(ClientError::WrongVersion);
            return true;
        }
        break;
    }

    mainPlayer.sendMsgAsync(new GameMessage_Server_Password(clientconfig.password));

    AdvanceState(ConnectState::QueryPw);
    return true;
}

/**
 *  Server-Passwort-Nachricht.
 */
bool GameClient::OnGameMessage(const GameMessage_Server_Password& msg)
{
    if(!VerifyState(ConnectState::QueryPw))
        return true;

    if(msg.password != "true")
    {
        OnError(ClientError::WrongPassword);
        return true;
    }

    mainPlayer.sendMsgAsync(new GameMessage_Player_Name(0xFF, SETTINGS.lobby.name));
    mainPlayer.sendMsgAsync(new GameMessage_Player_Portrait(0xFF, SETTINGS.lobby.portraitIndex));
    mainPlayer.sendMsgAsync(new GameMessage_MapRequest(true));

    AdvanceState(ConnectState::QueryMapInfo);
    return true;
}

/**
 *  Server-Name-Nachricht.
 */
bool GameClient::OnGameMessage(const GameMessage_Server_Name& msg)
{
    if(!VerifyState(ConnectState::QueryServerName))
        return true;
    clientconfig.gameName = msg.name;

    AdvanceState(ConnectState::QueryPlayerList);
    return true;
}

/**
 *  Server-Start-Nachricht
 */
bool GameClient::OnGameMessage(const GameMessage_Server_Start& msg)
{
    if(state != ClientState::Config)
        return true;

    nwfInfo = std::make_shared<NWFInfo>();
    nwfInfo->init(msg.firstNwf, msg.cmdDelay);
    try
    {
        StartGame(msg.random_init);
    } catch(const SerializedGameData::Error& error)
    {
        LOG.write("Error when loading game: %s\n") % error.what();
        Stop();
        GAMEMANAGER.ShowMenu();
    }
    return true;
}

/**
 *  Server-Chat-Nachricht.
 */
bool GameClient::OnGameMessage(const GameMessage_Chat& msg)
{
    if(msg.destination == ChatDestination::System)
    {
        SystemChat(msg.text, (msg.player < game->world_.GetNumPlayers()) ? msg.player : GetPlayerId());
        return true;
    }
    if(state == ClientState::Game)
    {
        // Ingame message: Do some checking and logging
        if(msg.player >= game->world_.GetNumPlayers())
            return true;

        /// Mit im Replay aufzeichnen
        if(replayinfo && replayinfo->replay.IsRecording())
            replayinfo->replay.AddChatCommand(GetGFNumber(), msg.player, msg.destination, msg.text);

        const GamePlayer& player = game->world_.GetPlayer(msg.player);

        // Besiegte dürfen nicht mehr heimlich mit Verbüdeten oder Feinden reden
        if(player.IsDefeated() && msg.destination != ChatDestination::All)
            return true;

        const auto isValidRecipient = [&msg, &player](const unsigned playerId) {
            // Always send to self
            if(msg.player == playerId)
                return true;
            switch(msg.destination)
            {
                case ChatDestination::System:
                case ChatDestination::All: return true;
                case ChatDestination::Allies: return player.IsAlly(playerId);
                case ChatDestination::Enemies: return !player.IsAlly(playerId);
            }
            return true; // LCOV_EXCL_LINE
        };
        for(AIPlayer& ai : game->aiPlayers_)
        {
            if(isValidRecipient(ai.GetPlayerId()))
                ai.OnChatMessage(msg.player, msg.destination, msg.text);
        }

        if(!isValidRecipient(GetPlayerId()))
            return true;
    } else if(state == ClientState::Config)
    {
        // GameLobby message: Just check for valid player
        if(msg.player >= gameLobby->getNumPlayers())
            return true;
    } else
        return true;

    if(ci)
        ci->CI_Chat(msg.player, msg.destination, msg.text);
    return true;
}

/**
 *  Server-Async-Nachricht.
 */
bool GameClient::OnGameMessage(const GameMessage_Server_Async& msg)
{
    if(state != ClientState::Game)
        return true;

    // Liste mit Namen und Checksummen erzeugen
    std::stringstream checksum_list;
    for(unsigned i = 0; i < msg.checksums.size(); ++i)
    {
        checksum_list << GetPlayer(i).name << ": " << msg.checksums[i];
        if(i + 1 < msg.checksums.size())
            checksum_list << ", ";
    }

    // Fehler ausgeben (Konsole)!
    LOG.write(_("The Game is not in sync. Checksums of some players don't match."));
    LOG.write("\n%1%\n") % checksum_list.str();

    // Messenger im Game
    if(ci)
        ci->CI_Async(checksum_list.str());

    std::string fileName = s25util::Time::FormatTime("async_%Y-%m-%d_%H-%i-%s");
    fileName += "_" + s25util::toStringClassic(GetPlayerId()) + "_";
    fileName += GetPlayer(GetPlayerId()).name;

    const bfs::path filePathSave = RTTRCONFIG.ExpandPath(s25::folders::save) / makePortableFileName(fileName + ".sav");
    const bfs::path filePathLog =
      RTTRCONFIG.ExpandPath(s25::folders::logs) / makePortableFileName(fileName + "Player.log");
    saveRandomLog(filePathLog, RANDOM.GetAsyncLog());
    SaveToFile(filePathSave);
    LOG.write(_("Async log saved at %1%,\ngame saved at %2%\n")) % filePathLog % filePathSave;
    return true;
}

/**
 *  Server-Countdown-Nachricht.
 */
bool GameClient::OnGameMessage(const GameMessage_Countdown& msg)
{
    if(state != ClientState::Config)
        return true;
    if(ci)
        ci->CI_Countdown(msg.countdown);
    return true;
}

/**
 *  Server-Cancel-Countdown-Nachricht.
 */
bool GameClient::OnGameMessage(const GameMessage_CancelCountdown& msg)
{
    if(state != ClientState::Config)
        return true;
    if(ci)
        ci->CI_CancelCountdown(msg.error);
    return true;
}

/**
 *  verarbeitet die MapInfo-Nachricht, in der die gepackte Größe,
 *  die normale Größe und Teilanzahl der Karte übertragen wird.
 *
 *  @param message Nachricht, welche ausgeführt wird
 */
bool GameClient::OnGameMessage(const GameMessage_Map_Info& msg)
{
    if(!VerifyState(ConnectState::QueryMapInfo))
        return true;

    // full path
    const std::string portFilename = makePortableFileName(msg.filename);
    if(portFilename.empty())
    {
        LOG.write("Invalid filename received!\n");
        OnError(ClientError::InvalidMap);
        return true;
    }
    if(!MapInfo::verifySize(msg.mapLen, msg.luaLen, msg.mapCompressedLen, msg.luaCompressedLen))
    {
        OnError(ClientError::InvalidMap);
        return true;
    }
    const auto targetPath =
      RTTRCONFIG.ExpandPath((msg.mt == MapType::Savegame) ? s25::folders::save : s25::folders::mapsPlayed);
    bfs::create_directories(targetPath);
    mapinfo.filepath = targetPath / portFilename;
    mapinfo.type = msg.mt;

    // lua script file path
    if(msg.luaLen > 0)
        mapinfo.luaFilepath = bfs::path(mapinfo.filepath).replace_extension("lua");
    else
        mapinfo.luaFilepath.clear();

    // We have the map locally already, so prepare and ask if this is the same as the one on the server
    if(bfs::exists(mapinfo.filepath) && (mapinfo.luaFilepath.empty() || bfs::exists(mapinfo.luaFilepath))
       && CreateLobby())
    {
        mapinfo.mapData.CompressFromFile(mapinfo.filepath, &mapinfo.mapChecksum);
        if(mapinfo.mapData.data.size() == msg.mapCompressedLen && mapinfo.mapData.uncompressedLength == msg.mapLen)
        {
            bool ok = true;
            if(!mapinfo.luaFilepath.empty())
            {
                mapinfo.luaData.CompressFromFile(mapinfo.luaFilepath, &mapinfo.luaChecksum);
                ok = (mapinfo.luaData.data.size() == msg.luaCompressedLen
                      && mapinfo.luaData.uncompressedLength == msg.luaLen);
            }

            if(ok)
            {
                mainPlayer.sendMsgAsync(new GameMessage_Map_Checksum(mapinfo.mapChecksum, mapinfo.luaChecksum));
                AdvanceState(ConnectState::VerifyMap);
                return true;
            }
        }
        gameLobby.reset();
    }
    mapinfo.mapData.uncompressedLength = msg.mapLen;
    mapinfo.luaData.uncompressedLength = msg.luaLen;
    mapinfo.mapData.data.resize(msg.mapCompressedLen);
    mapinfo.luaData.data.resize(msg.luaCompressedLen);
    mainPlayer.sendMsgAsync(new GameMessage_MapRequest(false));
    AdvanceState(ConnectState::ReceiveMap);
    return true;
}

///////////////////////////////////////////////////////////////////////////////
/// Kartendaten
/// @param message  Nachricht, welche ausgeführt wird
bool GameClient::OnGameMessage(const GameMessage_Map_Data& msg)
{
    if(!VerifyState(ConnectState::ReceiveMap))
        return true;

    LOG.writeToFile("<<< NMS_MAP_DATA(%u)\n") % msg.data.size();
    std::vector<char>& targetData = (msg.isMapData) ? mapinfo.mapData.data : mapinfo.luaData.data;
    if(msg.data.size() > targetData.size() || msg.offset > targetData.size() - msg.data.size())
    {
        OnError(ClientError::MapTransmission);
        return true;
    }
    std::copy(msg.data.begin(), msg.data.end(), targetData.begin() + msg.offset);

    uint32_t totalSize = mapinfo.mapData.data.size();
    uint32_t receivedSize = msg.offset + msg.data.size();
    if(!mapinfo.luaFilepath.empty())
    {
        totalSize += mapinfo.luaData.data.size();
        // Assumes lua data comes after the map data
        if(!msg.isMapData)
            receivedSize += mapinfo.mapData.data.size();
    }
    if(ci)
        ci->CI_MapPartReceived(receivedSize, totalSize);

    if(receivedSize == totalSize)
    {
        if(!mapinfo.mapData.DecompressToFile(mapinfo.filepath, &mapinfo.mapChecksum))
        {
            OnError(ClientError::MapTransmission);
            return true;
        }
        if(!mapinfo.luaFilepath.empty() && !mapinfo.luaData.DecompressToFile(mapinfo.luaFilepath, &mapinfo.luaChecksum))
        {
            OnError(ClientError::MapTransmission);
            return true;
        }
        RTTR_Assert(!mapinfo.luaFilepath.empty() || mapinfo.luaChecksum == 0);

        if(!CreateLobby())
        {
            OnError(ClientError::MapTransmission);
            return true;
        }

        mainPlayer.sendMsgAsync(new GameMessage_Map_Checksum(mapinfo.mapChecksum, mapinfo.luaChecksum));
        AdvanceState(ConnectState::VerifyMap);
    }
    return true;
}

bool GameClient::OnGameMessage(const GameMessage_SkipToGF& msg)
{
    skiptogf = msg.targetGF;
    LOG.write("Jumping from GF %1% to GF %2%\n") % GetGFNumber() % skiptogf;
    return true;
}

void GameClient::OnError(ClientError error)
{
    if(ci)
        ci->CI_Error(error);
    Stop();
}

void GameClient::AdvanceState(ConnectState newState)
{
    connectState = newState;
    if(ci)
        ci->CI_NextConnectState(connectState);
}

bool GameClient::VerifyState(ConnectState expectedState)
{
    if(state != ClientState::Connect || connectState != expectedState)
    {
        OnError(ClientError::InvalidMessage);
        return false;
    }
    return true;
}

bool GameClient::CreateLobby()
{
    RTTR_Assert(!gameLobby);

    unsigned numPlayers;

    switch(mapinfo.type)
    {
        case MapType::OldMap:
        {
            libsiedler2::Archiv map;

            // Karteninformationen laden
            if(libsiedler2::loader::LoadMAP(mapinfo.filepath, map, true) != 0)
            {
                LOG.write("GameClient::OnMapData: ERROR: Map %1%, couldn't load header!\n") % mapinfo.filepath;
                return false;
            }

            const libsiedler2::ArchivItem_Map_Header& header =
              checkedCast<const libsiedler2::ArchivItem_Map*>(map.get(0))->getHeader();
            numPlayers = header.getNumPlayers();
            mapinfo.title = s25util::ansiToUTF8(header.getName());
        }
        break;
        case MapType::Savegame:
            mapinfo.savegame = std::make_unique<Savegame>();
            if(!mapinfo.savegame->Load(mapinfo.filepath, SaveGameDataToLoad::HeaderAndSettings))
                return false;

            numPlayers = mapinfo.savegame->GetNumPlayers();
            mapinfo.title = mapinfo.savegame->GetMapName();
            break;
        default: return false;
    }

    if(GetPlayerId() >= numPlayers)
        return false;

    gameLobby = std::make_shared<GameLobby>(mapinfo.type == MapType::Savegame, IsHost(), numPlayers);
    return true;
}

///////////////////////////////////////////////////////////////////////////////
/// map-checksum
/// @param message  Nachricht, welche ausgeführt wird
bool GameClient::OnGameMessage(const GameMessage_Map_ChecksumOK& msg)
{
    if(!VerifyState(ConnectState::VerifyMap))
        return true;
    LOG.writeToFile("<<< NMS_MAP_CHECKSUM(%d)\n") % (msg.correct ? 1 : 0);

    if(msg.correct)
        AdvanceState(ConnectState::QueryServerName);
    else
    {
        gameLobby.reset();
        if(msg.retryAllowed)
        {
            mainPlayer.sendMsgAsync(new GameMessage_MapRequest(false));
            AdvanceState(ConnectState::ReceiveMap);
        } else
            OnError(ClientError::MapTransmission);
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
/// server typ
/// @param message  Nachricht, welche ausgeführt wird
bool GameClient::OnGameMessage(const GameMessage_GGSChange& msg)
{
    if(state != ClientState::Config && !VerifyState(ConnectState::QuerySettings))
        return true;
    LOG.writeToFile("<<< NMS_GGS_CHANGE\n");

    gameLobby->getSettings() = msg.ggs;

    if(state == ClientState::Connect)
    {
        state = ClientState::Config;
        AdvanceState(ConnectState::Finished);
    } else if(ci)
        ci->CI_GGSChanged(msg.ggs);
    return true;
}

bool GameClient::OnGameMessage(const GameMessage_RemoveLua&)
{
    if(state != ClientState::Connect && state != ClientState::Config)
        return true;
    mapinfo.luaFilepath.clear();
    mapinfo.luaData.Clear();
    mapinfo.luaChecksum = 0;
    return true;
}

///////////////////////////////////////////////////////////////////////////////
/// NFC Antwort vom Server
/// @param message  Nachricht, welche ausgeführt wird
bool GameClient::OnGameMessage(const GameMessage_GameCommand& msg)
{
    if(nwfInfo)
    {
        if(!nwfInfo->addPlayerCmds(msg.player, msg.cmds))
        {
            LOG.write("Could not add gamecommands for player %1%. He might be cheating!\n") % unsigned(msg.player);
            RTTR_Assert(false);
        }
    }
    return true;
}

void GameClient::IncreaseSpeed(const bool wraparound)
{
    const auto curSpeed = framesinfo.gfLengthReq;
    // Note: Higher speed = lower gf_length value
    // Go from debug speed directly back to min speed, else in fixed steps
    static_assert(MIN_SPEED_DEBUG > MIN_SPEED);
    if(framesinfo.gfLengthReq > MIN_SPEED)
        SetNewSpeed(MIN_SPEED); // NOLINT(bugprone-branch-clone)
    else
        SetNewSpeed((framesinfo.gfLengthReq - 1ms) / SPEED_STEP * SPEED_STEP);
    // If unchanged we capped at max speed, so wrap around if requested
    if(wraparound && framesinfo.gfLengthReq == curSpeed)
        SetNewSpeed(MIN_SPEED);
}

void GameClient::DecreaseSpeed()
{
    if((DEBUG_MODE || replayMode) && framesinfo.gfLengthReq >= MIN_SPEED)
        SetNewSpeed(MIN_SPEED_DEBUG);
    else
        SetNewSpeed(framesinfo.gfLengthReq / SPEED_STEP * SPEED_STEP + SPEED_STEP);
}

void GameClient::SetNewSpeed(FramesInfo::milliseconds32_t gfLength)
{
    static_assert(MIN_SPEED >= SPEED_GF_LENGTHS[GameSpeed::VeryFast], "Not all speeds reachable");
    static_assert(MAX_SPEED <= SPEED_GF_LENGTHS[GameSpeed::VerySlow], "Not all speeds reachable");
    const auto minSpeed = (replayMode || DEBUG_MODE) ? MIN_SPEED_DEBUG : MIN_SPEED;
    const auto maxSpeed = (replayMode || DEBUG_MODE) ? MAX_SPEED_DEBUG : MAX_SPEED;
    const auto oldSpeed = framesinfo.gfLengthReq;
    framesinfo.gfLengthReq = helpers::clamp<decltype(gfLength)>(gfLength, maxSpeed, minSpeed);
    if(replayMode)
        framesinfo.gf_length = framesinfo.gfLengthReq;
    else if(framesinfo.gfLengthReq != oldSpeed)
        mainPlayer.sendMsgAsync(new GameMessage_Speed(framesinfo.gfLengthReq));
}

///////////////////////////////////////////////////////////////////////////////
/// NFC Done vom Server
/// @param message  Nachricht, welche ausgeführt wird
bool GameClient::OnGameMessage(const GameMessage_Server_NWFDone& msg)
{
    if(!nwfInfo)
        return true;

    if(!nwfInfo->addServerInfo(NWFServerInfo(msg.gf, msg.gf_length, msg.nextNWF)))
    {
        RTTR_Assert(false);
        LOG.write("Failed to add server info. Invalid server?\n");
    }

    return true;
}

bool GameClient::OnGameMessage(const GameMessage_Pause& msg)
{
    // Ignore recorded pause messages in replay mode
    if(state != ClientState::Game || replayMode)
        return true;
    if(framesinfo.isPaused == msg.paused)
        return true;
    framesinfo.isPaused = msg.paused;

    LOG.writeToFile("<<< NMS_NFC_PAUSE(%1%)\n") % msg.paused;

    if(msg.paused)
        ci->CI_GamePaused();
    else
        ci->CI_GameResumed();
    return true;
}

/**
 *  NFC GetAsyncLog von Server
 *
 *  @param[in] message Nachricht, welche ausgeführt wird
 */
bool GameClient::OnGameMessage(const GameMessage_GetAsyncLog& /*msg*/)
{
    if(state != ClientState::Game)
        return true;
    std::string systemInfo = System::getCompilerName() + " @ " + System::getOSName();
    mainPlayer.sendMsgAsync(new GameMessage_AsyncLog(systemInfo));

    // AsyncLog an den Server senden

    std::vector<RandomEntry> async_log = RANDOM.GetAsyncLog();

    // stückeln...
    std::vector<RandomEntry> part;
    for(auto& it : async_log)
    {
        part.push_back(it);

        if(part.size() == 10)
        {
            mainPlayer.sendMsgAsync(new GameMessage_AsyncLog(part, false));
            part.clear();
        }
    }

    mainPlayer.sendMsgAsync(new GameMessage_AsyncLog(part, true));
    return true;
}

///////////////////////////////////////////////////////////////////////////////
/// testet ob ein Netwerkframe abgelaufen ist und führt dann ggf die Befehle aus
void GameClient::ExecuteGameFrame()
{
    if(framesinfo.isPaused)
        return; // Pause

    FramesInfo::UsedClock::time_point currentTime = FramesInfo::UsedClock::now();

    if(framesinfo.forcePauseLen.count())
    {
        if(currentTime - framesinfo.forcePauseStart > framesinfo.forcePauseLen)
            framesinfo.forcePauseLen = FramesInfo::milliseconds32_t::zero();
        else
            return; // Pause
    }

    const unsigned curGF = GetGFNumber();
    const bool isSkipping = skiptogf > curGF;
    // Is it time for the next GF? If we are skipping, it is always time for the next GF
    if(isSkipping || (currentTime - framesinfo.lastTime) >= framesinfo.gf_length)
    {
        try
        {
            if(isSkipping)
            {
                // We are always in realtime
                framesinfo.lastTime = currentTime;
            } else
            {
                // Advance simulation time (lastTime) by 1 GF
                framesinfo.lastTime += framesinfo.gf_length;
            }
            if(replayMode)
            {
                // In replay mode we have all commands in the file -> Execute them
                ExecuteGameFrame_Replay();
            } else
            {
                RTTR_Assert(curGF <= nwfInfo->getNextNWF());
                bool isNWF = (curGF == nwfInfo->getNextNWF());
                // Is it time for a NWF, handle that first
                if(isNWF)
                {
                    // If a player is lagging (we did not got his commands) "pause" the game by skipping the rest of
                    // this function
                    // -> Don't execute GF, don't autosave etc.
                    if(!nwfInfo->isReady())
                    {
                        // If a player is a few GFs behind, he will never catch up and always lag
                        // Hence, pause up to 4 GFs randomly before trying again to execute this NWF
                        // Do not reset frameTime or lastTime as this will mess up interpolation for drawing
                        framesinfo.forcePauseStart = currentTime;
                        framesinfo.forcePauseLen = (rand() * 4 * framesinfo.gf_length) / RAND_MAX;
                        return;
                    }

                    RTTR_Assert(nwfInfo->getServerInfo().gf == curGF);

                    ExecuteNWF();

                    FramesInfo::milliseconds32_t oldGFLen = framesinfo.gf_length;
                    nwfInfo->execute(framesinfo);
                    if(oldGFLen != framesinfo.gf_length)
                    {
                        LOG.write("Client: Speed changed at %1% from %2% to %3% (NWF: %4%)\n") % curGF
                          % helpers::withUnit(oldGFLen) % helpers::withUnit(framesinfo.gf_length)
                          % framesinfo.nwf_length;
                    }
                }

                NextGF(isNWF);
                RTTR_Assert(curGF <= nwfInfo->getNextNWF());
                HandleAutosave();

                // GF-Ende im Replay aktualisieren
                if(replayinfo && replayinfo->replay.IsRecording())
                    replayinfo->replay.UpdateLastGF(curGF);
            }

        } catch(const LuaExecutionError& e)
        {
            SystemChat((boost::format(_("Error during execution of lua script: %1\nGame stopped!")) % e.what()).str());
            OnError(ClientError::InvalidMap);
        }
        if(skiptogf == GetGFNumber())
            skiptogf = 0;
    }
    framesinfo.frameTime = std::chrono::duration_cast<FramesInfo::milliseconds32_t>(currentTime - framesinfo.lastTime);
    // Check remaining time until next GF
    if(framesinfo.frameTime >= framesinfo.gf_length)
    {
        // This can happen, if we don't call this method in intervals less than gf_length or gf_length has changed
        // TODO: Run multiple GFs per call.
        // For now just make sure it is less than gf_length by skipping some simulation time,
        // until we are only a bit less than 1 GF behind
        // However we allow the simulation to lack behind for a few frames, so if there was a single spike we can still
        // catch up in the next visual frames
        using DurationType = decltype(framesinfo.gf_length);
        constexpr auto maxLackFrames = 5;

        RTTR_Assert(framesinfo.gf_length > DurationType::zero());
        const auto maxFrameTime = framesinfo.gf_length - DurationType(1);

        if(framesinfo.frameTime > maxLackFrames * framesinfo.gf_length)
            framesinfo.lastTime += framesinfo.frameTime - maxFrameTime; // Skip simulation time until caught up
        framesinfo.frameTime = maxFrameTime;
    }
    // This is assumed by drawing code for interpolation
    RTTR_Assert(framesinfo.frameTime < framesinfo.gf_length);
}

void GameClient::HandleAutosave()
{
    // If inactive or during replay -> no autosave
    if(!SETTINGS.interface.autosaveInterval || replayMode)
        return;

    // Alle .... GF
    if(GetGFNumber() % SETTINGS.interface.autosaveInterval == 0)
    {
        std::string filename;
        if(mapinfo.title.empty())
            filename = std::string(_("Auto-Save")) + ".sav";
        else
            filename = mapinfo.title + " (" + _("Auto-Save") + ").sav";

        SaveToFile(RTTRCONFIG.ExpandPath(s25::folders::save) / filename);
    }
}

/// Führt notwendige Dinge für nächsten GF aus
void GameClient::NextGF(bool wasNWF)
{
    for(AIPlayer& ai : game->aiPlayers_)
        ai.RunGF(GetGFNumber(), wasNWF);
    game->RunGF();
}

void GameClient::ExecuteAllGCs(uint8_t playerId, const PlayerGameCommands& gcs)
{
    for(const gc::GameCommandPtr& gc : gcs.gcs)
        gc->Execute(game->world_, playerId);
}

void GameClient::SendNothingNC(uint8_t player)
{
    mainPlayer.sendMsgAsync(
      new GameMessage_GameCommand(player, AsyncChecksum::create(*game), std::vector<gc::GameCommandPtr>()));
}

void GameClient::WritePlayerInfo(SavedFile& file)
{
    RTTR_Assert(state == ClientState::Loading || state == ClientState::Loaded || state == ClientState::Game);
    // Spielerdaten
    for(unsigned i = 0; i < GetNumPlayers(); ++i)
        file.AddPlayer(GetPlayer(i));
}

void GameClient::OnGameStart()
{
    if(state == ClientState::Loaded)
    {
        GAMEMANAGER.ResetAverageGFPS();
        framesinfo.lastTime = FramesInfo::UsedClock::now();
        state = ClientState::Game;
        if(ci)
            ci->CI_GameStarted();
    } else if(state == ClientState::Game && !game->IsStarted())
    {
        framesinfo.isPaused = replayMode;
        game->Start(!!mapinfo.savegame);
    }
}

void GameClient::StartReplayRecording(const unsigned random_init)
{
    replayinfo = std::make_unique<ReplayInfo>();
    replayinfo->filename = s25util::Time::FormatTime("%Y-%m-%d_%H-%i-%s") + ".rpl";

    WritePlayerInfo(replayinfo->replay);
    replayinfo->replay.ggs = game->ggs_;

    if(!replayinfo->replay.StartRecording(RTTRCONFIG.ExpandPath(s25::folders::replays) / replayinfo->filename, mapinfo,
                                          random_init))
    {
        LOG.write(_("Replayfile couldn't be opened. No replay will be recorded\n"));
        replayinfo.reset();
    }
}

bool GameClient::StartReplay(const boost::filesystem::path& path)
{
    RTTR_Assert(state == ClientState::Stopped);
    mapinfo.Clear();
    replayinfo = std::make_unique<ReplayInfo>();

    if(!replayinfo->replay.LoadHeader(path) || !replayinfo->replay.LoadGameData(mapinfo)) //-V807
    {
        LOG.write(_("Invalid Replay %1%! Reason: %2%\n")) % path
          % (replayinfo->replay.GetLastErrorMsg().empty() ? _("Unknown") : replayinfo->replay.GetLastErrorMsg());
        OnError(ClientError::InvalidMap);
        replayinfo.reset();
        return false;
    }
    replayinfo->filename = path.filename();

    gameLobby = std::make_shared<GameLobby>(true, true, replayinfo->replay.GetNumPlayers());

    for(unsigned i = 0; i < replayinfo->replay.GetNumPlayers(); ++i)
        gameLobby->getPlayer(i) = JoinPlayerInfo(replayinfo->replay.GetPlayer(i));

    bool playerFound = false;
    // Find a player to spectate from
    // First find a human player
    for(unsigned char i = 0; i < gameLobby->getNumPlayers(); ++i)
    {
        if(gameLobby->getPlayer(i).ps == PlayerState::Occupied)
        {
            mainPlayer.playerId = i;
            playerFound = true;
            break;
        }
    }
    if(!playerFound)
    {
        // If no human found, take the first AI
        for(unsigned char i = 0; i < gameLobby->getNumPlayers(); ++i)
        {
            if(gameLobby->getPlayer(i).ps == PlayerState::AI)
            {
                mainPlayer.playerId = i;
                break;
            }
        }
    }

    // GGS-Daten
    gameLobby->getSettings() = replayinfo->replay.ggs;

    switch(mapinfo.type)
    {
        default: break;
        case MapType::OldMap:
        {
            // Richtigen Pfad zur Map erstellen
            bfs::path mapFilePath = RTTRCONFIG.ExpandPath(s25::folders::mapsPlayed) / mapinfo.filepath.filename();
            mapinfo.filepath = mapFilePath;
            if(!mapinfo.mapData.DecompressToFile(mapinfo.filepath))
            {
                LOG.write(_("Error decompressing map file"));
                OnError(ClientError::MapTransmission);
                return false;
            }
            if(mapinfo.luaData.uncompressedLength)
            {
                mapinfo.luaFilepath = mapFilePath.replace_extension("lua");
                if(!mapinfo.luaData.DecompressToFile(mapinfo.luaFilepath))
                {
                    LOG.write(_("Error decompressing lua file"));
                    OnError(ClientError::MapTransmission);
                    return false;
                }
            }
        }
        break;
        case MapType::Savegame: break;
    }

    replayMode = true;

    try
    {
        StartGame(replayinfo->replay.getSeed());
    } catch(const SerializedGameData::Error& error)
    {
        LOG.write(_("Error when loading game from replay: %s\n")) % error.what();
        OnError(ClientError::InvalidMap);
        return false;
    }

    /*
      We have to to this if we have a old replay starting from scratch not containing a savegame. If a savegame is
      contained in the replay the compatibility code in GamePlayer deserialization function takes care of handling this.
      If we have a replay starting from scratch in the constructor of the gameplayer the standard distributions are
      loaded. These contain also the new leather addon buildings. When the distribution is recomputed these buildings
      are added to the possible goals for wares. This leads to the problem that we have more buildings then before in
      the list. So it happens for example for wood that the ware is deliverd to a different goal and then the replay
      gets out of sync.
    */
    if(!mapinfo.savegame && replayinfo->replay.GetMinorVersion() < 2)
    {
        auto newDistributions = defaultSettings_[GetPlayerId()].distribution;
        unsigned idx = 0;
        for(const DistributionMapping& mapping : distributionMap)
        {
            if(leatheraddon::isLeatherAddonBuildingType(std::get<1>(mapping)))
                newDistributions[idx] = 0;
            idx++;
        }

        for(auto& player : game->world_.getPlayers())
            player.ChangeDistribution(newDistributions);
    }

    replayinfo->next_gf = replayinfo->replay.ReadGF();

    return true;
}

void GameClient::SetAIBattlePlayers(std::vector<AI::Info> aiInfos)
{
    aiBattlePlayers_ = std::move(aiInfos);
}

unsigned GameClient::GetGlobalAnimation(const unsigned short max, const unsigned char factor_numerator,
                                        const unsigned char factor_denumerator, const unsigned offset)
{
    // Unit for animations is 630ms (dividable by 2, 3, 5, 6, 7, 10, 15, ...)
    // But this also means: If framerate drops below approx. 15Hz, you won't see
    // every frame of an 8-part animation anymore.
    // An animation runs fully in (factor_numerator / factor_denumerator) multiples of 630ms
    const unsigned unit = 630 /*ms*/ * factor_numerator / factor_denumerator;
    const unsigned currenttime = std::chrono::duration_cast<FramesInfo::milliseconds32_t>(
                                   (framesinfo.lastTime + framesinfo.frameTime).time_since_epoch())
                                   .count();
    return ((currenttime % unit) * max / unit + offset) % max;
}

unsigned GameClient::Interpolate(unsigned max_val, const GameEvent* ev)
{
    RTTR_Assert(ev);
    // TODO: Move to some animation system that is part of game
    FramesInfo::milliseconds32_t elapsedTime;
    if(state == ClientState::Game)
        elapsedTime = (GetGFNumber() - ev->startGF) * framesinfo.gf_length + framesinfo.frameTime;
    else
        elapsedTime = FramesInfo::milliseconds32_t::zero();
    FramesInfo::milliseconds32_t duration = ev->length * framesinfo.gf_length;
    return helpers::interpolate(0u, max_val, elapsedTime, duration);
}

int GameClient::Interpolate(int x1, int x2, const GameEvent* ev)
{
    RTTR_Assert(ev);
    FramesInfo::milliseconds32_t elapsedTime;
    if(state == ClientState::Game)
        elapsedTime = (GetGFNumber() - ev->startGF) * framesinfo.gf_length + framesinfo.frameTime;
    else
        elapsedTime = FramesInfo::milliseconds32_t::zero();
    FramesInfo::milliseconds32_t duration = ev->length * framesinfo.gf_length;
    return helpers::interpolate(x1, x2, elapsedTime, duration);
}

void GameClient::ServerLost()
{
    OnError(ClientError::ConnectionLost);
    // Stop game
    framesinfo.isPaused = true;
}

/**
 *  überspringt eine bestimmte Anzahl von Gameframes.
 *
 *  @param[in] dest_gf Zielgameframe
 */
void GameClient::SkipGF(unsigned gf, GameWorldView& gwv)
{
    if(gf <= GetGFNumber())
        return;

    unsigned start_ticks = VIDEODRIVER.GetTickCount();

    if(!replayMode)
    {
        // unpause before skipping
        SetPause(false);
        mainPlayer.sendMsgAsync(new GameMessage_SkipToGF(gf));
        return;
    }

    SetPause(false);
    if(GetGFNumber() < GetLastReplayGF())
        gf = std::min(gf, GetLastReplayGF() + 1u);
    skiptogf = gf;

    // GFs überspringen
    for(unsigned i = GetGFNumber(); i < skiptogf; ++i)
    {
        if(i % 1000 == 0)
        {
            RoadBuildState road;
            road.mode = RoadBuildMode::Disabled;

            // spiel aktualisieren
            gwv.Draw(road, MapPoint::Invalid(), false);

            // text oben noch hinschreiben
            boost::format nwfString(_("current GF: %u - still fast forwarding: %d GFs left (%d %%)"));
            nwfString % GetGFNumber() % (gf - i) % (i * 100 / gf);
            LargeFont->Draw(DrawPoint(VIDEODRIVER.GetRenderSize() / 2u), nwfString.str(), FontStyle::CENTER,
                            COLOR_YELLOW);

            VIDEODRIVER.SwapBuffers();
        }
        ExecuteGameFrame();
    }
    // Either we force-stopped skipping (skiptogf set to 0) or we reached the target gf
    RTTR_Assert(skiptogf == 0u || GetGFNumber() == gf);

    // Spiel pausieren & text ausgabe wie lang das jetzt gedauert hat
    unsigned ticks = VIDEODRIVER.GetTickCount() - start_ticks;
    boost::format text(_("Jump finished (%1$.3g seconds)."));
    text % (ticks / 1000.0);
    SystemChat(text.str());
    SetPause(true);
}

void GameClient::SystemChat(const std::string& text)
{
    SystemChat(text, GetPlayerId());
}

void GameClient::SystemChat(const std::string& text, unsigned char fromPlayerIdx)
{
    if(ci)
        ci->CI_Chat(fromPlayerIdx, ChatDestination::System, text);
}

bool GameClient::SaveToFile(const boost::filesystem::path& filepath)
{
    mainPlayer.sendMsg(GameMessage_Chat(GetPlayerId(), ChatDestination::System, _("Saving game...")));

    // Mond malen
    Position moonPos = VIDEODRIVER.GetMousePos();
    moonPos.y -= 40;
    LOADER.GetImageN("resource", 33)->DrawFull(moonPos);
    VIDEODRIVER.SwapBuffers();

    Savegame save;

    WritePlayerInfo(save);

    // GGS-Daten
    save.ggs = game->ggs_;

    save.start_gf = GetGFNumber();

    // Enable/Disable debugging of savegames
    save.sgd.debugMode = SETTINGS.global.debugMode;

    try
    {
        // Spiel serialisieren
        save.sgd.MakeSnapshot(*game);
        // Und alles speichern
        return save.Save(filepath, mapinfo.title);
    } catch(const std::exception& e)
    {
        SystemChat(std::string(_("Error during saving: ")) + e.what());
        return false;
    }
}

void GameClient::ResetVisualSettings()
{
    // Jeder Slot bekommt SEINE eigenen Anzeigewerte. Frueher stand hier nur der Hauptspieler;
    // die Fenster der zusaetzlichen lokalen Spieler lasen dann dessen Werte und schrieben sie
    // beim naechsten Regler beim Senden in den eigenen Spielzustand zurueck.
    for(unsigned id = 0; id < GetNumPlayers(); ++id)
        GetPlayer(id).FillVisualSettings(visualSettings_[id]);
}

VisualSettings& GameClient::GetVisualSettings(const unsigned playerId)
{
    RTTR_Assert(playerId < MAX_PLAYERS);
    return visualSettings_[playerId];
}

const VisualSettings& GameClient::GetVisualSettings(const unsigned playerId) const
{
    RTTR_Assert(playerId < MAX_PLAYERS);
    return visualSettings_[playerId];
}

const VisualSettings& GameClient::GetDefaultSettings(const unsigned playerId) const
{
    RTTR_Assert(playerId < MAX_PLAYERS);
    return defaultSettings_[playerId];
}

void GameClient::SetPause(bool pause)
{
    if(state == ClientState::Stopped)
    {
        // We can never continue from pause if stopped as the reason for stopping might be that the game was finished
        // However we allow to pause even when stopped so we can pause after we received the stop notification
        if(!pause)
            return;
        framesinfo.isPaused = true;
        framesinfo.frameTime = FramesInfo::milliseconds32_t::zero();
    } else if(replayMode)
    {
        // Pause instantly
        framesinfo.isPaused = pause;
        framesinfo.frameTime = FramesInfo::milliseconds32_t::zero();
    } else if(IsHost())
    {
        auto* msg = new GameMessage_Pause(pause);
        if(pause)
            OnGameMessage(*msg);
        mainPlayer.sendMsgAsync(msg);
    }
}

void GameClient::SetReplayFOW(const bool hideFOW)
{
    if(replayinfo)
        replayinfo->all_visible = hideFOW;
}

bool GameClient::IsReplayFOWDisabled() const
{
    return replayMode && replayinfo->all_visible;
}

unsigned GameClient::GetLastReplayGF() const
{
    return replayinfo ? replayinfo->replay.GetLastGF() : 0u;
}

bool GameClient::AddGC(gc::GameCommandPtr gc)
{
    // GameClient IST die GameCommandFactory, die alle Ingame-Fenster hereingereicht bekommen.
    // Ein Fensterknopf nennt beim Ausloesen keinen Spieler, also muss der Eingabepfad sagen,
    // wer gerade handelt (ScopedActingPlayer). Sagt niemand etwas - Mauspfad, Tastatur,
    // Einzelspieler, Netzwerkpartie -, ist es wie bisher der Hauptspieler.
    return AddPlayerGC(static_cast<uint8_t>(GetActingPlayer().value_or(static_cast<uint8_t>(GetPlayerId()))),
                       std::move(gc));
}

bool GameClient::AddPlayerGC(const uint8_t playerId, gc::GameCommandPtr gc)
{
    // Nicht in der Pause und nicht im Replay
    if(framesinfo.isPaused || IsReplayModeOn())
        return false;
    // Nur fuer Slots, die dieser Client steuert
    if(!gameCommands_.IsLocalPlayer(playerId))
        return false;
    // Nicht, wenn DIESER Spieler besiegt wurde
    if(GetPlayer(playerId).IsDefeated())
        return false;

    gameCommands_.Add(playerId, std::move(gc));
    return true;
}

GameCommandFactory* GameClient::GetGCFactory(const uint8_t playerId)
{
    const auto it = localGCFactories_.find(playerId);
    return (it == localGCFactories_.end()) ? nullptr : it->second.get();
}

void GameClient::SetAdditionalLocalPlayers(std::vector<uint8_t> playerIds)
{
    additionalLocalPlayers_ = std::move(playerIds);
}

std::string GameClient::ValidateAdditionalLocalPlayers(const GameLobby& lobby, const unsigned mainPlayerId,
                                                       const std::vector<uint8_t>& playerIds, const bool isAIBattle)
{
    if(playerIds.empty())
        return "";
    // F3: --local-players und --ai schliessen sich aus. Bisher gewann --ai stillschweigend.
    if(isAIBattle)
        return _("Additional local players cannot be combined with an AI battle");
    if(!lobby.isHost())
        return _("Additional local players require hosting the game");
    // Befund 3: je lokalem Spieler eine Ansicht - mehr als MAX_VIEWPORTS kann der Bildschirm
    // nicht aufteilen. Ohne diese Schranke haette SetupLocalPlayers alle Slots registriert,
    // waehrend CreateViews die ueberzaehligen verworfen haette: lokal gesteuert, aber ohne
    // Ansicht, ohne Eingabegeraet und ohne KI. Der Hauptspieler zaehlt mit, deshalb "+ 1".
    if(playerIds.size() + 1 > MAX_VIEWPORTS)
        return helpers::format(_("At most %1% local players are supported, %2% were requested"),
                               unsigned(MAX_VIEWPORTS), unsigned(playerIds.size() + 1));

    const unsigned numPlayers = lobby.getNumPlayers();
    std::vector<uint8_t> seen;
    for(const uint8_t id : playerIds)
    {
        if(id >= numPlayers)
            return helpers::format(_("Slot %1% does not exist, the map has %2% slots"), unsigned(id), numPlayers);
        if(id == mainPlayerId)
            return helpers::format(_("Slot %1% is already the main local player"), unsigned(id));
        if(helpers::contains(seen, id))
            return helpers::format(_("Slot %1% was requested twice"), unsigned(id));
        // Occupied heisst: eine echte Netzwerkverbindung sitzt dort (GameServer.cpp:1234)
        if(lobby.getPlayer(id).ps == PlayerState::Occupied)
            return helpers::format(_("Slot %1% is occupied by a remote player"), unsigned(id));
        // PlayerState::Locked heisst: der Slot ist geschlossen. Bei einem Savegame kann ihn auch
        // der Host nicht mehr oeffnen, weil der Spieler auf der Karte gar nicht existiert:
        // GameServer::OnGameMessage(GameMessage_Player_State) laesst Locked-Slots eines Savegames
        // unveraendert (GameServer.cpp:979-987). ApplyAdditionalLocalPlayers bliebe dort also
        // wirkungslos und SetupLocalPlayers wuerde den Slot beim Spielstart verwerfen. Deshalb
        // hier ein sichtbarer Fehler statt einer stillen Degradierung.
        if(lobby.getPlayer(id).ps == PlayerState::Locked && lobby.isSavegame())
            return helpers::format(_("Slot %1% is closed and cannot be opened in a savegame"), unsigned(id));
        seen.push_back(id);
    }
    return "";
}

void GameClient::ApplyAdditionalLocalPlayers(IGameLobbyController& lobbyController,
                                             const std::vector<uint8_t>& playerIds)
{
    for(const uint8_t id : playerIds)
    {
        // Dummy statt Default: sollte die Registrierung beim Spielstart doch scheitern, idlet der
        // Slot, statt dass eine echte KI den Platz des Menschen uebernimmt.
        // Reihenfolge zaehlt: dieser Aufruf muss NACH der Standardbelegung in
        // dskGameLobby.cpp laufen, damit er die dortige Default-KI ueberschreibt.
        lobbyController.SetPlayerState(id, PlayerState::AI, AI::Info(AI::Type::Dummy));
        // Name NACH dem State setzen - der Server ueberschreibt ihn sonst per SetAIName
        // (GameServer.cpp:1002-1006).
        lobbyController.SetName(id, helpers::format(_("Local player %1%"), unsigned(id) + 1));
    }
}

void GameClient::RefreshAdditionalLocalPlayers()
{
    // Einzige Wahrheit ist gameCommands_; der Getter ist nur eine Sicht darauf (F2).
    additionalLocalPlayers_ = gameCommands_.GetPlayerIds();
    helpers::erase_if(additionalLocalPlayers_,
                      [mainId = static_cast<uint8_t>(GetPlayerId())](const uint8_t id) { return id == mainId; });
}

bool GameClient::SetupLocalPlayers()
{
    gameCommands_.Clear();
    localGCFactories_.clear();
    explicitActingPlayerId_.reset();
    hasExplicitActingPlayer_ = false;
    windowOwnerPlayerId_.reset();

    // Im Replay steuert niemand einen Spieler: es koennen keine Kommandos entstehen
    // (AddPlayerGC lehnt im Replaymodus ab) und ExecuteNWF laeuft nicht. Ohne diesen
    // Sonderfall wuerde IsLocalHumanPlayer(GetPlayerId()) beim Zuschauen wahr - der
    // beobachtete Slot ist im Replay aber oft eine KI.
    if(IsReplayModeOn())
    {
        if(!additionalLocalPlayers_.empty())
            LOG.write("Ignoring additional local players: replay mode\n");
        additionalLocalPlayers_.clear();
        return true;
    }

    gameCommands_.AddPlayer(static_cast<uint8_t>(GetPlayerId()));

    // Zusatzspieler nur in einer rein lokalen Partie: dort gibt es per Definition genau eine
    // Verbindung, also keine fremde Kommandoquelle und keinen fremdinitiierten Spielerwechsel.
    // (IsHost() allein reicht nicht - eine selbst gehostete Netzwerkpartie ist auch Host.)
    if(!additionalLocalPlayers_.empty()
       && (!IsHost() || clientconfig.servertyp != ServerType::Local || IsAIBattleModeOn()))
    {
        LOG.write("Rejecting additional local players: not a purely local hosted game\n");
        additionalLocalPlayers_.clear();
        return false;
    }

    // F6: Filtern und Registrieren als getrennte Schritte - keine Seiteneffekte im Praedikat.
    // Restposten Phase 1: ein angeforderter, aber nicht registrierbarer Slot war bisher nur eine
    // Logzeile - die Partie startete dann still als Einzelspieler. Haeufigste Ursache war ein
    // Locked-Slot in einem Savegame, den der Server nicht oeffnet (GameServer.cpp:979-987);
    // ValidateAdditionalLocalPlayers faengt das inzwischen schon in der Lobby ab. Bleibt hier
    // trotzdem etwas uebrig, ist das ein Fehler und kein Grund, klammheimlich weiterzuspielen.
    bool allRegistered = true;
    for(const uint8_t id : additionalLocalPlayers_)
    {
        const bool ok = id < GetNumPlayers() && id != GetPlayerId() && !gameCommands_.IsLocalPlayer(id)
                        && GetPlayer(id).ps == PlayerState::AI;
        if(ok)
            gameCommands_.AddPlayer(id);
        else
        {
            LOG.write("Dropping invalid additional local player %1%\n") % unsigned(id);
            allRegistered = false;
        }
    }

    for(const uint8_t id : gameCommands_.GetPlayerIds())
        localGCFactories_[id] = std::make_unique<LocalPlayerGCFactory>(*this, id);

    // F4: Ergebnis der Registrierung melden und den Getter darauf nachfuehren (F2).
    RefreshAdditionalLocalPlayers();
    LOG.write("Local players: main slot %1%, %2% additional local slot(s)\n") % unsigned(GetPlayerId())
      % additionalLocalPlayers_.size();
    return allRegistered;
}

void GameClient::OnPlayerSlotsSwappedIngame(const uint8_t playerId1, const uint8_t playerId2)
{
    if(IsReplayModeOn())
        return;

    const bool wasLocal1 = gameCommands_.IsLocalPlayer(playerId1);
    const bool wasLocal2 = gameCommands_.IsLocalPlayer(playerId2);

    // Beide Slots werden von diesem Client gesteuert. Bisher stand hier nur
    //   RTTR_Assert_Msg(!(wasLocal1 && wasLocal2), ...)
    // und darunter lief der wasLocal1-Zweig. Mit NDEBUG ist das Assert wirkungslos
    // (RTTR_Assert.h:11-15), und der wasLocal1-Zweig ist fuer diesen Fall falsch: sein
    // gameCommands_.AddPlayer(playerId2) ist idempotent (LocalPlayerCommands.cpp:9-13) und
    // LAESST DEN ALTEN PUFFER VON playerId2 STEHEN. Die dort gesammelten Kommandos stammen aber
    // von einem anderen Menschen und wuerden nach dem Tausch dem umgezogenen Hauptspieler
    // zugerechnet. Das ist im Releasebuild eine stille Fehlzuordnung von Kommandos.
    //
    // Wirksame Absicherung, in jedem Build: playerId1 wird bewusst und protokolliert aufgegeben,
    // playerId2 bekommt einen LEEREN Puffer. Aufgeben ist hier die einzige richtige Wahl - die
    // Weltaenderung darueber hat playerId1 auf PlayerState::AI gesetzt und beim Host bereits
    // einen Dummy-KI-Spieler dafuer angelegt (GameClientCommands.cpp:107-111). Wuerden wir
    // playerId1 zusaetzlich lokal steuern, haette dieser Slot zwei Kommandoquellen.
    if(wasLocal1 && wasLocal2)
    {
        LOG.write("Swap between two locally controlled slots %1% and %2%: giving up local control "
                  "of %1% and dropping the pending commands of both\n")
          % unsigned(playerId1) % unsigned(playerId2);
        gameCommands_.RemovePlayer(playerId1);
        localGCFactories_.erase(playerId1);
        gameCommands_.RemovePlayer(playerId2); // verwirft den Puffer des anderen Menschen
        gameCommands_.AddPlayer(playerId2);    // und legt ihn leer neu an
        localGCFactories_[playerId2] = std::make_unique<LocalPlayerGCFactory>(*this, playerId2);
        RefreshAdditionalLocalPlayers();
        return;
    }

    if(wasLocal1)
    {
        // Der lokale Slot zieht um. Die bis hier gesammelten Kommandos des alten Slots sind
        // ungueltig - sie wuerden den alten, jetzt KI-gesteuerten Spieler veraendern.
        // Die Puffer der uebrigen lokalen Spieler bleiben unangetastet.
        gameCommands_.RemovePlayer(playerId1);
        localGCFactories_.erase(playerId1);
        gameCommands_.AddPlayer(playerId2);
        localGCFactories_[playerId2] = std::make_unique<LocalPlayerGCFactory>(*this, playerId2);
    } else if(wasLocal2)
    {
        // P3: Ein Mensch, den wir NICHT steuern, ist auf einen von uns gesteuerten Slot gezogen.
        //
        // Das ist ueber das Netz ausloesbar und keineswegs unmoeglich: ChangePlayerIngame
        // verlangt playerId1 == Occupied und playerId2 == AI (GameClientCommands.cpp:87-92), und
        // unsere zusaetzlichen lokalen Slots SIND in der Welt AI (nur der Kommandopfad ist
        // lokal). Ein fremder Client, der per GameMessage_Player_Swap auf so einen Slot zieht,
        // landet exakt hier. Hier stand bis Phase 3 ein RTTR_Assert_Msg(false, ...) - im
        // Debugbuild also ein ueber das Netz ausloesbarer Programmabbruch an einer Stelle, an der
        // der alte Code gar nichts tat. Das ist ersatzlos entfernt.
        //
        // Die wirksame Behandlung ist das Aufgeben des Slots und NICHT etwa die Spiegelung des
        // wasLocal1-Zweigs: playerId1 gehoert einem fremden Menschen und ist nach dem Tausch
        // PlayerState::AI, wofuer der Host bereits einen Dummy angelegt hat
        // (GameClientCommands.cpp:106-111). Wuerden wir playerId1 uebernehmen, haette dieser Slot
        // zwei Kommandoquellen. Der Puffer von playerId2 wird verworfen - die dort gesammelten
        // Kommandos stammen von unserem Menschen und wuerden sonst dem Fremden zugerechnet.
        LOG.write("Giving up local control of player %1%: slot was taken over by a swap to player %2%\n")
          % unsigned(playerId2) % unsigned(playerId1);
        gameCommands_.RemovePlayer(playerId2);
        localGCFactories_.erase(playerId2);
    }

    // F2, Ingame-Zweig: Getter nachfuehren.
    RefreshAdditionalLocalPlayers();
}

unsigned GameClient::GetNumPlayers() const
{
    RTTR_Assert(state == ClientState::Loading || state == ClientState::Loaded || state == ClientState::Game);
    return game->world_.GetNumPlayers();
}

GamePlayer& GameClient::GetPlayer(const unsigned id)
{
    RTTR_Assert(state == ClientState::Loading || state == ClientState::Loaded || state == ClientState::Game);
    RTTR_Assert(id < GetNumPlayers());
    return game->world_.GetPlayer(id);
}

std::unique_ptr<AIPlayer> GameClient::CreateAIPlayer(unsigned playerId, const AI::Info& aiInfo)
{
    return AIFactory::Create(aiInfo, playerId, game->world_);
}

/// Wandelt eine GF-Angabe in eine Zeitangabe um (HH:MM:SS oder MM:SS wenn Stunden = 0)
std::string GameClient::FormatGFTime(const unsigned gf) const
{
    using seconds = std::chrono::duration<uint32_t, std::chrono::seconds::period>;
    using hours = std::chrono::duration<uint32_t, std::chrono::hours::period>;
    using minutes = std::chrono::duration<uint32_t, std::chrono::minutes::period>;
    using std::chrono::duration_cast;

    seconds numSeconds = duration_cast<seconds>(gfs_to_duration(gf));

    hours numHours = duration_cast<hours>(numSeconds);
    numSeconds -= numHours;
    minutes numMinutes = duration_cast<minutes>(numSeconds);
    numSeconds -= numMinutes;

    // Use hour format only if we have hours
    if(numHours.count())
        return helpers::format("%u:%02u:%02u", numHours.count(), numMinutes.count(), numSeconds.count());
    else
        return helpers::format("%02u:%02u", numMinutes.count(), numSeconds.count());
}

const boost::filesystem::path& GameClient::GetReplayFilename() const
{
    static boost::filesystem::path emptyString;
    return replayinfo ? replayinfo->filename : emptyString;
}

Replay* GameClient::GetReplay()
{
    return replayinfo ? &replayinfo->replay : nullptr;
}

std::shared_ptr<const NWFInfo> GameClient::GetNWFInfo() const
{
    return nwfInfo;
}

/// Is tournament mode activated (0 if not)? Returns the durations of the tournament mode in gf otherwise
unsigned GameClient::GetTournamentModeDuration() const
{
    using namespace std::chrono;
    if(game && rttr::enum_cast(game->ggs_.objective) >= rttr::enum_cast(GameObjective::Tournament1)
       && static_cast<unsigned>(rttr::enum_cast(game->ggs_.objective))
            < rttr::enum_cast(GameObjective::Tournament1) + NUM_TOURNAMENT_MODES)
    {
        const auto turnamentMode = rttr::enum_cast(game->ggs_.objective) - rttr::enum_cast(GameObjective::Tournament1);
        return duration_to_gfs(TOURNAMENT_MODES_DURATION[turnamentMode]);
    } else
        return 0;
}

void GameClient::ToggleHumanAIPlayer(const AI::Info& aiInfo)
{
    RTTR_Assert(!IsReplayModeOn());
    auto it = helpers::find_if(game->aiPlayers_,
                               [id = this->GetPlayerId()](const auto& player) { return player.GetPlayerId() == id; });
    if(it != game->aiPlayers_.end())
    {
        game->aiPlayers_.erase(it);
        SystemChat(_("Disabled AI for current player"));
    } else
    {
        game->AddAIPlayer(CreateAIPlayer(GetPlayerId(), aiInfo));
        SystemChat(_("Enabled AI for current player"));
    }
}

void GameClient::RequestSwapToPlayer(const unsigned char newId)
{
    if(state != ClientState::Game)
        return;
    // Auf einen lokal gesteuerten Slot darf nicht gewechselt werden - das wuerde
    // zwei Kommandoquellen fuer denselben Slot erzeugen.
    if(gameCommands_.IsLocalPlayer(newId))
        return;
    GamePlayer& player = GetPlayer(newId);
    if(player.ps == PlayerState::AI && player.aiInfo.type == AI::Type::Dummy)
        mainPlayer.sendMsgAsync(new GameMessage_Player_Swap(0xFF, newId));
}
