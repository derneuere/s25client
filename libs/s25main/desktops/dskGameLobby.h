// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Desktop.h"
#include "driver/PadEvent.h"
#include "network/ClientInterface.h"
#include "gameTypes/AIInfo.h"
#include "gameTypes/PlayerState.h"
#include "gameTypes/ServerType.h"
#include "liblobby/LobbyInterface.h"
#include <memory>
#include <vector>

class ctrlChat;
class GameLobby;
class LobbyPlayerInfo;
class LuaInterfaceSettings;
class GameLobbyController;
class ILobbyClient;
enum class Team : uint8_t;

/// Desktop für das Hosten-eines-Spiels-Fenster
class dskGameLobby final : public Desktop, public ClientInterface, public LobbyInterface
{
public:
    dskGameLobby(ServerType serverType, std::shared_ptr<GameLobby> gameLobby, unsigned playerId,
                 std::unique_ptr<ILobbyClient> lobbyClient);
    ~dskGameLobby();

    /// Größe ändern-Reaktionen die nicht vom Skaling-Mechanismus erfasst werden.
    void Resize(const Extent& newSize) override;
    void SetActive(bool activate = true) override;

    // --- Gamepad ------------------------------------------------------------------------------
    bool WantsPadInput() const override { return true; }
    /// Der EINZIGE Bildschirm mit mehr als einem Padfokus: hier sollen ja mehrere Leute
    /// gleichzeitig beitreten. Ausserhalb des Zuordnungsbildschirms bleibt es bei einem.
    unsigned GetNumPadSlots() const override;
    bool Msg_PadCommand(unsigned slot, PadButton button) override;
    Window* GetPadEntryCtrl(unsigned slot) override;

private:
    /// Woraus die Beschriftung einer Sitzkarte entsteht - und damit das Einzige, was sie
    /// aendern kann.
    enum class SeatCardKind
    {
        Host,
        Closed,
        Free,
        Slot,
        Pad
    };

    /// Ein Sitzplatz vor dem Fernseher.
    ///
    /// Der Sitz ist NICHT der Simulationsslot: `playerId` ist der Slot auf der Karte, der Index
    /// im Vektor ist die Nummer der Ansicht auf dem Bildschirm. Beide Reihenfolgen muessen
    /// zusammenpassen, weil dskGameInterface::CreateViews die Ansichten als "Hauptspieler vorn,
    /// dann GetAdditionalLocalPlayers() in Vektorreihenfolge" baut.
    struct LocalSeat
    {
        unsigned playerId = 0;
        /// Pad auf diesem Sitz. InvalidPadDevice heisst "kein Pad" - der Sitz kann trotzdem
        /// belegt sein, naemlich wenn er von --local-players kommt.
        PadDeviceId device = InvalidPadDevice;
        bool taken = false;
        /// Haben WIR diesen Slot in der Hand? Nur dann darf ihn das Aufstehen anfassen.
        ///
        /// BEFUND 2: ohne diese Unterscheidung schrieb jeder Beitritt ALLE nicht eingenommenen
        /// Sitze auf die Standard-KI zurueck - ein vom Host geschlossener Slot wurde wieder
        /// KI, eine auf Schwer gestellte KI wieder leicht. Der Zuordnungsbildschirm baut seine
        /// Sitze einmal beim Aufbau; was der Host danach einstellt, ist NICHT seine Sache.
        bool applied = false;
        /// Was auf dem Slot stand, bevor wir ihn genommen haben - das Ziel des Aufstehens.
        PlayerState savedPs = PlayerState::AI;
        AI::Info savedAi = AI::Info(AI::Type::Default, AI::Level::Easy);
        /// Der Zustand, aus dem die AKTUELL angezeigte Beschriftung gebaut wurde.
        ///
        /// Msg_PaintBefore laeuft je Frame und ruft UpdateSeatPanel; ohne dieses Gedaechtnis
        /// entstuenden dort je Frame bis zu vier formatierte Zeichenketten, von denen sich
        /// zwischen zwei Frames fast nie eine aendert.
        SeatCardKind cardKind = SeatCardKind::Host;
        unsigned cardValue = 0;
        bool cardValid = false;
    };

    /// Gibt es hier ueberhaupt Sitzplaetze zu vergeben? Nur in einer selbst gehosteten,
    /// rein lokalen Partie ohne Savegame und ohne KI-Schlacht - genau die Bedingungen, unter
    /// denen GameClient::ValidateAdditionalLocalPlayers zustimmt und SetupLocalPlayers die
    /// Zusatzspieler beim Start annimmt (network/GameClient.cpp:1897-1939, 2007-2008).
    bool AreLocalSeatsAvailable() const;
    void CreateSeatPanel();
    void UpdateSeatPanel();
    /// Sitz dieses Geraets oder seats_.size(), wenn es keinen hat.
    unsigned SeatOfDevice(PadDeviceId device) const;
    /// Darf sich hier JETZT jemand hinsetzen?
    ///
    /// Gefragt wird der AKTUELLE Spielzustand und nicht der beim Aufbau des Bildschirms: der
    /// Host kann den Slot inzwischen geschlossen haben, und dann ist ein Beitritt kein
    /// Beitritt, sondern das stillschweigende Rueckgaengigmachen seiner Entscheidung.
    bool IsSeatJoinable(unsigned seat) const;
    void OnSeatButton(unsigned seat, unsigned slot);
    /// Sitzzustand und Spielzustand zusammenfuehren: abgezogene Pads und Sitze, die der Host
    /// inzwischen geschlossen hat. Laeuft je Frame aus Msg_PaintBefore.
    void SyncSeatsWithGameState();
    /// Die Sitzbelegung in den Spielzustand schreiben: GAMECLIENT.SetAdditionalLocalPlayers,
    /// Validierung, ApplyAdditionalLocalPlayers und die Padzuordnung.
    void ApplyLocalSeats();
    /// Ein Pad wurde abgezogen -> sein Sitz wird ausdruecklich frei. Ohne das zoege der
    /// PadRouter von sich aus ein danebenliegendes Pad in den frei gewordenen Slot nach, und
    /// ein Unbeteiligter saesse ungefragt auf Sitz 2 (XR-115).
    /// Liefert true, wenn sich etwas geaendert hat - der Aufrufer schreibt es fort.
    bool DropDisconnectedSeats();
    /// Der Host hat einen Sitz geschlossen oder vergeben, auf dem noch jemand SITZT.
    ///
    /// BEFUND A, die Gegenrichtung zu BEFUND 2: IsSeatJoinable schuetzte nur den BEITRITT und
    /// nie einen bereits gehaltenen Sitz. Uebrig blieb ein Slot, der gleichzeitig geschlossen
    /// und lokaler Zusatzspieler war - und genau daran scheitert GameClient::SetupLocalPlayers
    /// beim Spielstart, mit OnError(LocalPlayerSetup) und Stop(). Der Host entscheidet; der
    /// Sitz wird ausdruecklich geraeumt und die Sitzkarte sagt es.
    /// Liefert true, wenn sich etwas geaendert hat.
    bool DropSeatsClosedByTheHost();
    /// Letzte Klammer vor dem Countdown. Liefert false, wenn dieser Startversuch abgebrochen
    /// werden soll - der Zustand ist dann repariert und der Grund genannt.
    bool PrepareSeatsForStart();
    /// Die Rueckfrage vor dem Verlassen der Lobby - siehe Msg_PadCommand.
    void AskToLeave();


    void SetPlayerReady(unsigned char player, bool ready);
    // GGS von den Controls auslesen
    void UpdateGGS();
    /// Aktualisiert eine Spielerreihe (löscht Controls und legt neue an)
    void UpdatePlayerRow(unsigned row);

    /// Füllt die Felder einer Reihe aus
    void ChangeTeam(unsigned player, Team);
    void ChangeReady(unsigned player, bool ready);
    void ChangeNation(unsigned player, Nation);
    void ChangePortrait(unsigned player, unsigned portraitIndex);
    void ChangePing(unsigned playerId);
    void ChangeColor(unsigned player, unsigned color);

    void Msg_PaintBefore() override;
    void Msg_Group_ButtonClick(unsigned group_id, unsigned ctrl_id) override;
    void Msg_Group_CheckboxChange(unsigned group_id, unsigned ctrl_id, bool checked) override;
    void Msg_Group_ComboSelectItem(unsigned group_id, unsigned ctrl_id, unsigned selection) override;
    void Msg_ButtonClick(unsigned ctrl_id) override;
    void Msg_EditEnter(unsigned ctrl_id) override;
    void Msg_MsgBoxResult(unsigned msgbox_id, MsgboxResult mbr) override;
    void Msg_ComboSelectItem(unsigned ctrl_id, unsigned selection) override;
    void Msg_CheckboxChange(unsigned ctrl_id, bool checked) override;
    void Msg_OptionGroupChange(unsigned ctrl_id, unsigned selection) override;

    void CI_Error(ClientError ce) override;

    void CI_NewPlayer(unsigned playerId) override;
    void CI_PlayerLeft(unsigned playerId) override;

    void CI_GameLoading(std::shared_ptr<Game> game) override;

    void CI_PlayerDataChanged(unsigned playerId) override;
    void CI_PingChanged(unsigned playerId, unsigned short ping) override;
    void CI_ReadyChanged(unsigned playerId, bool ready) override;
    void CI_PlayersSwapped(unsigned player1, unsigned player2) override;
    void CI_GGSChanged(const GlobalGameSettings& ggs) override;

    void CI_Chat(unsigned playerId, ChatDestination cd, const std::string& msg) override;
    void CI_Countdown(unsigned remainingTimeInSec) override;
    void CI_CancelCountdown(bool error) override;

    void FlashGameChat();

    void LC_Status_Error(const std::string& error) override;
    void LC_Chat(const std::string& player, const std::string& text) override;

    /// Addon options check with regards to peaceful mode and economy mode
    bool forceOptions = false;
    bool checkOptions();

    void GoBack();
    bool IsSinglePlayer() const { return serverType == ServerType::Local; }
    /// Check whether the given setting can be changed, i.e. is not disabled by lua
    bool IsChangeAllowed(const std::string& setting) const;

    const ServerType serverType;
    std::shared_ptr<GameLobby> gameLobby_;
    unsigned localPlayerId_;
    std::unique_ptr<ILobbyClient> lobbyClient_;
    bool hasCountdown_;
    std::unique_ptr<LuaInterfaceSettings> lua;
    std::unique_ptr<GameLobbyController> lobbyController;
    bool wasActivated, allowAddonChange;
    ctrlChat *gameChat, *lobbyChat;
    unsigned lobbyChatTabAnimId, localChatTabAnimId;
    /// Sitz 0 ist immer der Host. Leer, wenn es hier keinen Splitscreen geben kann.
    std::vector<LocalSeat> seats_;
};
