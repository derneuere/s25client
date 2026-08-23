// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "ClientError.h"
#include "FramesInfo.h"
#include "GameCommand.h"
#include "GameMessageInterface.h"
#include "ILocalGameState.h"
#include "NetworkPlayer.h"
#include "network/LocalPlayerCommands.h"
#include "factories/GameCommandFactory.h"
#include "gameTypes/AIInfo.h"
#include "gameTypes/ChatDestination.h"
#include "gameTypes/MapDescription.h"
#include "gameTypes/MapInfo.h"
#include "gameTypes/Nation.h"
#include "gameTypes/ServerType.h"
#include "gameTypes/TeamTypes.h"
#include "gameTypes/VisualSettings.h"
#include "gameData/MaxPlayers.h"
#include "s25util/Singleton.h"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace AI {
struct Info;
}

class AIPlayer;
class ClientInterface;
class Game;
class GameEvent;
class GameLobby;
class GamePlayer;
class GameWorldView;
class IGameLobbyController;
class LocalPlayerGCFactory;
class NWFInfo;
class Replay;
class SavedFile;
enum class ConnectState;
struct CreateServerInfo;
struct PlayerGameCommands;
struct ReplayInfo;

enum class ClientState
{
    Stopped,
    Connect,
    Config,
    Loading,
    Loaded,
    Game
};

class GameClient final :
    public Singleton<GameClient, SingletonPolicies::WithLongevity>,
    public GameMessageInterface,
    public GameCommandFactory,
    public ILocalGameState
{
public:
    static constexpr unsigned Longevity = 5;

    GameClient();
    ~GameClient();

    void SetInterface(ClientInterface* ci) { this->ci = ci; }
    /// Removes the given interface (if it is not yet overwritten by another one)
    void RemoveInterface(ClientInterface* ci)
    {
        if(this->ci == ci)
            this->ci = nullptr;
    }
    bool IsHost() const override { return clientconfig.isHost; }
    /// Manually set the host status. Normally done in connect call
    void SetIsHost(bool isHost) { clientconfig.isHost = isHost; }
    std::string GetGameName() const { return clientconfig.gameName; }

    unsigned GetPlayerId() const override { return mainPlayer.playerId; }

    bool Connect(const std::string& server, const std::string& password, ServerType servertyp, unsigned short port,
                 bool host, bool use_ipv6);

    /// Start the server and connect to it
    bool HostGame(const CreateServerInfo& csi, const MapDescription& map);
    void Run();
    void Stop();

    /// Gibt Map-Titel zurück
    const std::string& GetMapTitle() const { return mapinfo.title; }
    /// Gibt Pfad zu der Map zurück
    const boost::filesystem::path& GetMapPath() const { return mapinfo.filepath; }
    /// Gibt Map-Typ zurück
    MapType GetMapType() const { return mapinfo.type; }
    const boost::filesystem::path& GetLuaFilePath() const { return mapinfo.luaFilepath; }

    // Initialisiert und startet das Spiel
    void StartGame(unsigned random_init);
    /// Called when the game is loaded
    void GameLoaded();

    /// Beendet das Spiel, zerstört die Spielstrukturen
    void ExitGame();

    ClientState GetState() const { return state; }
    Replay* GetReplay();
    std::shared_ptr<const NWFInfo> GetNWFInfo() const;
    std::shared_ptr<GameLobby> GetGameLobby();
    const AIPlayer* GetAIPlayer(unsigned id) const;

    unsigned GetGFNumber() const;
    FramesInfo::milliseconds32_t GetGFLength() const { return framesinfo.gf_length; }
    unsigned GetNWFLength() const { return framesinfo.nwf_length; }
    FramesInfo::milliseconds32_t GetFrameTime() const { return framesinfo.frameTime; }
    unsigned GetGlobalAnimation(unsigned short max, unsigned char factor_numerator, unsigned char factor_denumerator,
                                unsigned offset);
    unsigned Interpolate(unsigned max_val, const GameEvent* ev);
    int Interpolate(int x1, int x2, const GameEvent* ev);

    void Command_Chat(const std::string& text, ChatDestination cd);
    void Command_SetNation(Nation newNation);
    void Command_SetPortrait(unsigned portraitIndex);
    void Command_SetTeam(Team newTeam);
    void Command_SetColor(unsigned newColor);
    void Command_SetReady(bool isReady);

    /// Called internally when the game is ready to start (loaded and all players ready)
    /// And a 2nd time when the GUI is ready which actually starty the game
    void OnGameStart();

    void IncreaseSpeed(bool wraparound = false);
    void DecreaseSpeed();
    void SetNewSpeed(FramesInfo::milliseconds32_t gfLength);
    // Used by tests (stinks, but what to do?)
    FramesInfo::milliseconds32_t GetGFLengthReq() { return framesinfo.gfLengthReq; }

    /// Lädt ein Replay und startet dementsprechend das Spiel
    bool StartReplay(const boost::filesystem::path& path);

    /// When a non-empty vector is given then an AI battle with the given AIs is started
    void SetAIBattlePlayers(std::vector<AI::Info> aiInfos);
    const std::vector<AI::Info>& GetAIBattlePlayers() const { return aiBattlePlayers_; }
    bool IsAIBattleModeOn() const { return !aiBattlePlayers_.empty(); }

    /// Fuegt ein GameCommand fuer einen lokal gesteuerten Spieler hinzu.
    /// false, wenn abgelehnt: Pause, Replaymodus, Spieler nicht lokal gesteuert, Spieler besiegt.
    bool AddPlayerGC(uint8_t playerId, gc::GameCommandPtr gc);
    /// Ist dieser Slot von diesem Client aus lokal gesteuert (Mensch)?
    bool IsLocalHumanPlayer(uint8_t playerId) const { return gameCommands_.IsLocalPlayer(playerId); }
    /// GameCommandFactory fuer einen lokal gesteuerten Spieler.
    /// nullptr, wenn der Slot nicht lokal gesteuert wird. Gueltig bis ExitGame().
    GameCommandFactory* GetGCFactory(uint8_t playerId);

    /// "Wer handelt gerade?" - waehrend der Verarbeitung EINER Eingabe.
    ///
    /// GameClient ist selbst eine GameCommandFactory, und die Ingame-Fenster bekommen sie in
    /// genau dieser Gestalt herein (dskGameInterface uebergibt GAMECLIENT). Ein Fensterknopf
    /// nennt beim Ausloesen keinen Spieler - ctrlButton::Activate ruft Msg_ButtonClick, und die
    /// rund 30 Fensterklassen, die GameCommands erzeugen, kennen nur ihre Fabrik. Solange
    /// AddGC() unbedingt auf GetPlayerId() (den Hauptspieler) leitet, handelt JEDER lokale
    /// Spieler an einem Fenster als Spieler 0.
    ///
    /// Diese Klammer setzt den Spieler, fuer den AddGC() waehrend ihrer Lebensdauer bucht.
    /// Sie wird vom Eingabepfad gesetzt, der als einziger weiss, WER gerade drueckt
    /// (dskGameInterface::OnPadButton/OnPadMove). Ohne sie ist das Verhalten unveraendert:
    /// AddGC() bucht dann wie bisher auf den Hauptspieler.
    ///
    /// Bewusst nicht kopierbar und nur auf dem Stack verwendbar - der Ueberschreibung darf
    /// niemals ein GameCommand entkommen, der spaeter erzeugt wird.
    ///
    /// Der Spieler ist bewusst optional: ein Aufrufer, der eine ZEITVERSETZT entstehende
    /// Kommandoerzeugung klammert (TransmitSettingsIgwAdapter), hat sich den Spieler beim
    /// ausloesenden Ereignis gemerkt - und dort kann "niemand hat etwas gesagt" (Maus,
    /// Tastatur, Einzelspieler) das richtige Ergebnis sein. nullopt bedeutet dann ausdruecklich
    /// "Hauptspieler", statt eine zufaellig offene fremde Klammer durchschlagen zu lassen.
    class ScopedActingPlayer
    {
    public:
        ScopedActingPlayer(GameClient& client, std::optional<uint8_t> playerId)
            : client_(client), previous_(client.explicitActingPlayerId_),
              hadPrevious_(client.hasExplicitActingPlayer_)
        {
            client_.explicitActingPlayerId_ = playerId;
            client_.hasExplicitActingPlayer_ = true;
        }
        ~ScopedActingPlayer()
        {
            client_.explicitActingPlayerId_ = previous_;
            client_.hasExplicitActingPlayer_ = hadPrevious_;
        }
        ScopedActingPlayer(const ScopedActingPlayer&) = delete;
        ScopedActingPlayer& operator=(const ScopedActingPlayer&) = delete;

    private:
        GameClient& client_;
        std::optional<uint8_t> previous_;
        bool hadPrevious_;
    };
    /// Fuer wen bucht AddGC() gerade? nullopt = fuer den Hauptspieler (Normalfall).
    ///
    /// Zwei Quellen, mit festem Vorrang: eine offene ScopedActingPlayer nennt den Spieler
    /// AUSDRUECKLICH und schlaegt den ambienten Fensterbesitz - auch dann, wenn sie nullopt
    /// sagt ("ausdruecklich der Hauptspieler"). Ohne sie gilt der Besitzer des Fensters, in
    /// dessen Namen gerade verarbeitet wird.
    std::optional<uint8_t> GetActingPlayer() const
    {
        return hasExplicitActingPlayer_ ? explicitActingPlayerId_ : windowOwnerPlayerId_;
    }
    /// Der Spieler, der aus dem AMBIENTEN Fensterbesitz faellt. Ohne RAII, weil die Klammer,
    /// die ihn setzt, ihren Vorzustand selbst verwaltet: WindowManager::ScopedWindowOwner meldet
    /// jeden Wechsel ueber dskGameInterface::OnWindowOwnerChanged hierher und stellt den vorigen
    /// Besitzer beim Verlassen wieder her.
    ///
    /// BEFUND C: bewusst ein EIGENES Feld und nicht dasselbe wie das der ScopedActingPlayer.
    /// Solange beide Klammern in denselben Wert schrieben, ueberschrieb eine Besitzklammer, die
    /// innerhalb einer ScopedActingPlayer geoeffnet und geschlossen wurde, deren Wert fuer die
    /// Restlaufzeit der aeusseren Klammer - und keine der beiden konnte das bemerken. Getrennte
    /// Felder mit festem Vorrang machen die Kollision unmoeglich statt bloss unwahrscheinlich.
    void SetWindowOwnerPlayer(std::optional<uint8_t> playerId) { windowOwnerPlayerId_ = playerId; }
    /// Zusaetzlich zum Hauptspieler lokal gesteuerte Slots (Splitscreen).
    /// Muss VOR StartGame() gesetzt werden. Nur in einer rein lokalen Partie (ServerType::Local,
    /// Host) und nur fuer Slots gueltig, die zum Spielstart PlayerState::AI sind.
    /// Die Lobby prueft das vorab ueber ValidateAdditionalLocalPlayers und bricht bei einem
    /// Fehler ab; StartGame verwirft ansonsten nur noch, was ein Bug hinterlassen hat.
    void SetAdditionalLocalPlayers(std::vector<uint8_t> playerIds);
    /// Vor dem Spielstart: die ANGEFORDERTEN Zusatzslots.
    /// Ab StartGame(): die tatsaechlich registrierten - abgeleitet aus gameCommands_, der
    /// einzigen Wahrheit, und bei jedem Ingame-Spielertausch nachgefuehrt.
    const std::vector<uint8_t>& GetAdditionalLocalPlayers() const { return additionalLocalPlayers_; }

    /// Prueft eine Anforderung zusaetzlicher lokaler Spieler gegen eine konkrete Lobby.
    /// Rueckgabe: leerer String = gueltig, sonst die Fehlerursache (der Aufrufer bricht ab).
    /// Bewusst statisch und ohne Clientzustand: ohne Singleton und ohne Netzwerk testbar.
    static std::string ValidateAdditionalLocalPlayers(const GameLobby& lobby, unsigned mainPlayerId,
                                                      const std::vector<uint8_t>& playerIds, bool isAIBattle);
    /// Konfiguriert die angeforderten Slots als Dummy-KI-Platzhalter fuer lokale Menschen.
    /// Vorbedingung: ValidateAdditionalLocalPlayers hat "" geliefert.
    static void ApplyAdditionalLocalPlayers(IGameLobbyController& lobbyController,
                                            const std::vector<uint8_t>& playerIds);

    void SetPause(bool pause);
    void TogglePause() { SetPause(!framesinfo.isPaused); }
    /// Hide or show the fog-of-war. Only in replay mode
    void SetReplayFOW(bool hideFOW);
    /// Return whether we are in replay mode and fog-of-war is disabled
    bool IsReplayFOWDisabled() const;
    /// Gibt Replay-Ende (GF) zurück
    unsigned GetLastReplayGF() const;
    /// Wandelt eine GF-Angabe in eine Zeitangabe um (HH:MM:SS oder MM:SS wenn Stunden = 0)
    std::string FormatGFTime(unsigned gf) const override;

    /// Gibt Replay-Dateiname zurück
    const boost::filesystem::path& GetReplayFilename() const;
    /// Wird ein Replay abgespielt?
    bool IsReplayModeOn() const { return replayMode; }

    /// Is tournament mode activated (0 if not)? Returns the durations of the tournament mode in gf otherwise
    unsigned GetTournamentModeDuration() const;

    void SkipGF(unsigned gf, GameWorldView& gwv);

    /// Changes the player ingame (for replay or debugging)
    void ChangePlayerIngame(unsigned char playerId1, unsigned char playerId2);
    /// Sends a request to swap places with the requested player. Only for debugging!
    void RequestSwapToPlayer(unsigned char newId);

    /// Spiel pausiert?
    bool IsPaused() const { return framesinfo.isPaused; }
    /// Schreibt Header der Save-Datei
    bool SaveToFile(const boost::filesystem::path& filepath);
    /// Visuelle Einstellungen aus den richtigen ableiten
    void ResetVisualSettings();
    void SystemChat(const std::string& text) override;
    void SystemChat(const std::string& text, unsigned char fromPlayerIdx);

    /// Toggle current player to be an AI player of the given type
    void ToggleHumanAIPlayer(const AI::Info& aiInfo);

    NetworkPlayer& GetMainPlayer() { return mainPlayer; }

private:
    /// Create an AI player for the current world
    std::unique_ptr<AIPlayer> CreateAIPlayer(unsigned playerId, const AI::Info& aiInfo);

    /// Add the gamecommand. Return true in success, false otherwise (paused, or defeated)
    bool AddGC(gc::GameCommandPtr gc) override;

    unsigned GetNumPlayers() const;
    /// Liefert einen Player zurück
    GamePlayer& GetPlayer(unsigned id);

    /// Versucht einen neuen GameFrame auszuführen, falls die Zeit dafür gekommen ist
    void ExecuteGameFrame();
    void ExecuteGameFrame_Replay();
    void ExecuteNWF();
    /// Filtert aus einem Network-Command-Paket alle Commands aus und führt sie aus, falls ein Spielerwechsel-Command
    /// dabei ist, füllt er die übergebenen IDs entsprechend aus
    void ExecuteAllGCs(uint8_t playerId, const PlayerGameCommands& gcs);
    /// Sendet ein NC-Paket ohne Befehle
    void SendNothingNC(uint8_t player = 0xFF);

    /// Führt notwendige Dinge für nächsten GF aus
    void NextGF(bool wasNWF);
    /// Checks if its time for autosaving (if enabled) and does it
    void HandleAutosave();

    //  Netzwerknachrichten
    RTTR_IGNORE_OVERLOADED_VIRTUAL
    bool OnGameMessage(const GameMessage_Ping& msg) override;

    bool OnGameMessage(const GameMessage_Server_TypeOK& msg) override;
    bool OnGameMessage(const GameMessage_Server_Password& msg) override;
    bool OnGameMessage(const GameMessage_Server_Name& msg) override;
    bool OnGameMessage(const GameMessage_Server_Start& msg) override;
    bool OnGameMessage(const GameMessage_Chat& msg) override;
    bool OnGameMessage(const GameMessage_Server_Async& msg) override;
    bool OnGameMessage(const GameMessage_Countdown& msg) override;
    bool OnGameMessage(const GameMessage_CancelCountdown& msg) override;

    bool OnGameMessage(const GameMessage_Player_Id& msg) override;
    bool OnGameMessage(const GameMessage_Player_List& msg) override;
    bool OnGameMessage(const GameMessage_Player_Name& msg) override;
    bool OnGameMessage(const GameMessage_Player_Portrait& msg) override;
    bool OnGameMessage(const GameMessage_Player_State& msg) override;
    bool OnGameMessage(const GameMessage_Player_Nation& msg) override;
    bool OnGameMessage(const GameMessage_Player_Team& msg) override;
    bool OnGameMessage(const GameMessage_Player_Color& msg) override;
    bool OnGameMessage(const GameMessage_Player_Kicked& msg) override;
    bool OnGameMessage(const GameMessage_Player_Ping& msg) override;
    bool OnGameMessage(const GameMessage_Player_New& msg) override;
    bool OnGameMessage(const GameMessage_Player_Ready& msg) override;
    bool OnGameMessage(const GameMessage_Player_Swap& msg) override;

    bool OnGameMessage(const GameMessage_Map_Info& msg) override;
    bool OnGameMessage(const GameMessage_Map_Data& msg) override;
    bool OnGameMessage(const GameMessage_Map_ChecksumOK& msg) override;

    bool OnGameMessage(const GameMessage_Pause& msg) override;
    bool OnGameMessage(const GameMessage_SkipToGF& msg) override;
    bool OnGameMessage(const GameMessage_Server_NWFDone& msg) override;
    bool OnGameMessage(const GameMessage_GameCommand& msg) override;

    bool OnGameMessage(const GameMessage_GGSChange& msg) override;
    bool OnGameMessage(const GameMessage_RemoveLua& msg) override;

    bool OnGameMessage(const GameMessage_GetAsyncLog& msg) override;
    RTTR_POP_DIAGNOSTIC

    /// Report the error and stop
    void OnError(ClientError error);
    /// Advance to new connect state
    void AdvanceState(ConnectState newState);
    /// Verifies that the current connect state matches the expected one
    /// On error the error is reported and the connection terminated as likely the server is faulty
    bool VerifyState(ConnectState expectedState);

    bool CreateLobby();

    /// Wird aufgerufen, wenn der Server gegangen ist (Verbindung verloren, ungültige Nachricht etc.)
    void ServerLost();

    // Replaymethoden

    /// Schreibt den Header der Replaydatei
    void StartReplayRecording(unsigned random_init);
    void WritePlayerInfo(SavedFile& file);

public:
    /// Virtuelle Werte der Einstellungsfenster, die aber noch nicht wirksam sind, nur um die
    /// Verzögerungen zu verstecken - JE SPIELER-SLOT.
    ///
    /// Bis Phase 4c war das EIN Feld fuer den ganzen Client. Bei mehreren lokalen Spielern war
    /// das kein Anzeigefehler, sondern Datenverlust im Spielzustand: die fuenf
    /// Wirtschaftsfenster laden ihre Regler beim Oeffnen HIERAUS und bauen beim Senden ihr
    /// Kommando ebenfalls HIERAUS. Bewegte Spieler 1 einen einzigen Regler, uebertrug er damit
    /// die vollstaendige Einstellungsgruppe des HAUPTSPIELERS auf sich selbst - gemessen:
    ///     Spieler 1 vorher : 10 5 5 5 8 8 8 8
    ///     Spieler 1 nachher:  1 0 0 0 0 0 0 0
    /// - und ueberschrieb hinterher auch noch den Anzeigezustand des Hauptspielers.
    ///
    /// Array ueber MAX_PLAYERS statt Map ueber die lokalen Spieler, obwohl es hoechstens
    /// MAX_VIEWPORTS lokale Spieler gibt:
    ///   * Es muss fuer JEDE gueltige Spieler-Id ein Eintrag da sein, nicht nur fuer die lokal
    ///     gesteuerten. Im Replaymodus setzt ChangePlayerIngame den Betrachter auf einen
    ///     beliebigen Slot - auch auf einen KI-Slot - und die Fenster lesen dann dessen
    ///     Anzeigewerte. Eine Map ueber die lokalen Spieler haette dort kein Ergebnis und
    ///     brauchte einen Rueckfallwert, also genau die stille Falschanzeige, die hier
    ///     abgestellt wird.
    ///   * Es gibt keine Reihenfolgeabhaengigkeit zur Registrierung der lokalen Spieler:
    ///     StartGame() befuellt die Werkseinstellungen, bevor irgendein Fenster existiert.
    ///   * MAX_PLAYERS ist 8 und VisualSettings ist ein paar Dutzend Byte gross - die
    ///     ungenutzten Slots kosten nichts, und der Rest des Codes haelt Spielerzustand
    ///     genauso (PostManager, EconomyModeHandler, ctrlPreviewMinimap).
    // TODO: Move to viewer
    VisualSettings& GetVisualSettings(unsigned playerId);
    const VisualSettings& GetVisualSettings(unsigned playerId) const;
    /// Werkseinstellungen desselben Slots ("Standard"-Knopf der Wirtschaftsfenster).
    const VisualSettings& GetDefaultSettings(unsigned playerId) const;
    /// skip ahead how many gf?
    unsigned skiptogf;

private:
    /// Siehe GetVisualSettings(). Index = Spieler-Slot.
    std::array<VisualSettings, MAX_PLAYERS> visualSettings_{}, defaultSettings_{};

    NetworkPlayer mainPlayer;

    ClientState state;
    ConnectState connectState;

    /// Game state itself (valid during LOADING and GAME state)
    std::shared_ptr<Game> game;
    /// NWF info
    std::shared_ptr<NWFInfo> nwfInfo;
    /// Game lobby (valid during CONFIG state)
    std::shared_ptr<GameLobby> gameLobby;

    class ClientConfig
    {
    public:
        ClientConfig() { Clear(); }
        void Clear();

        std::string server;
        std::string gameName;
        std::string password;
        ServerType servertyp;
        unsigned short port;
        bool isHost;
    } clientconfig;

    MapInfo mapinfo;

    FramesInfoClient framesinfo;

    ClientInterface* ci;

    /// GameCommands, die vom Client noch an den Server gesendet werden müssen - je lokalem Spieler
    LocalPlayerCommands gameCommands_;
    /// Zusaetzlich (neben mainPlayer) lokal gesteuerte Slots. Vor dem Spielstart gesetzt,
    /// in StartGame() validiert, danach unveraenderlich.
    std::vector<uint8_t> additionalLocalPlayers_;
    /// Je lokalem Spieler eine GameCommandFactory (Injektionspunkt fuer die spaetere UI-Phase)
    std::map<uint8_t, std::unique_ptr<LocalPlayerGCFactory>> localGCFactories_;
    /// Siehe ScopedActingPlayer. Gilt nur zusammen mit hasExplicitActingPlayer_.
    std::optional<uint8_t> explicitActingPlayerId_;
    /// Steht gerade eine ScopedActingPlayer offen? Noetig, weil nullopt dort eine BEDEUTUNG hat
    /// ("ausdruecklich der Hauptspieler") und deshalb nicht "nichts gesagt" heissen kann.
    bool hasExplicitActingPlayer_ = false;
    /// Siehe SetWindowOwnerPlayer. Ausserhalb jeder Besitzklammer nullopt.
    std::optional<uint8_t> windowOwnerPlayerId_;
    /// Legt die Menge der lokal gesteuerten Spieler fest (in StartGame, vor CI_GameLoading).
    /// false, wenn ein angeforderter Zusatzslot NICHT registriert werden konnte - der Aufrufer
    /// bricht den Spielstart dann mit ClientError::LocalPlayerSetup ab, statt still als
    /// Einzelspieler weiterzumachen.
    [[nodiscard]] bool SetupLocalPlayers();
    /// Fuehrt die clientlokale Buchhaltung (Kommandopuffer, GC-Factories, additionalLocalPlayers_)
    /// nach einem bereits vollzogenen Ingame-Spielertausch nach. Veraendert KEINEN Weltzustand.
    void OnPlayerSlotsSwappedIngame(uint8_t playerId1, uint8_t playerId2);
    /// Setzt additionalLocalPlayers_ auf die tatsaechlich registrierten Slots ausser dem Hauptspieler.
    void RefreshAdditionalLocalPlayers();

    std::unique_ptr<ReplayInfo> replayinfo;
    bool replayMode;

    /// Configured players for an AI battle.
    std::vector<AI::Info> aiBattlePlayers_;
};

///////////////////////////////////////////////////////////////////////////////
// Makros / Defines
#define GAMECLIENT GameClient::inst()
