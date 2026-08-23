// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Game.h"
#include "GamePlayer.h"
#include "ai/AIPlayer.h"
#include "network/ClientInterface.h"
#include "network/GameClient.h"
#include "network/GameMessages.h"

/**
 *  Chatbefehl, hängt eine Textnachricht in die Sende-Queue.
 *
 *  @param[in] text        Der Text
 *  @param[in] destination Ziel der Nachricht
 */
void GameClient::Command_Chat(const std::string& text, const ChatDestination cd)
{
    // Replaymodus oder kein Text --> nichts senden
    if(IsReplayModeOn() || text.empty())
        return;

    mainPlayer.sendMsgAsync(new GameMessage_Chat(0xff, cd, text));
}

void GameClient::Command_SetNation(Nation newNation)
{
    mainPlayer.sendMsgAsync(new GameMessage_Player_Nation(0xff, newNation));
}

void GameClient::Command_SetPortrait(unsigned portraitIndex)
{
    mainPlayer.sendMsgAsync(new GameMessage_Player_Portrait(0xff, portraitIndex));
}

void GameClient::Command_SetTeam(Team newTeam)
{
    mainPlayer.sendMsgAsync(new GameMessage_Player_Team(0xff, newTeam));
}

/**
 *  sendet den "Bereit"-Status.
 */
void GameClient::Command_SetReady(bool isReady)
{
    mainPlayer.sendMsgAsync(new GameMessage_Player_Ready(0xFF, isReady));
}

void GameClient::Command_SetColor(unsigned newColor)
{
    mainPlayer.sendMsgAsync(new GameMessage_Player_Color(0xFF, newColor));
}

/**
 *  wechselt einen Spieler.
 *
 *  @param[in] old_id Alte Spieler-ID
 *  @param[in] new_id Neue Spieler-ID
 */
void GameClient::ChangePlayerIngame(const unsigned char playerId1, const unsigned char playerId2)
{
    RTTR_Assert(state == ClientState::Game); // Must be ingame

    LOG.write("GameClient::ChangePlayer %i - %i \n") % static_cast<unsigned>(playerId1)
      % static_cast<unsigned>(playerId2);
    // Gleiche ID - wäre unsinnig zu wechseln
    if(playerId1 == playerId2)
        return;

    // ID auch innerhalb der Spielerzahl?
    if(playerId2 >= GetNumPlayers() || playerId1 >= GetNumPlayers())
        return;

    if(IsReplayModeOn())
    {
        RTTR_Assert(playerId1 == GetPlayerId());
        // There must be someone at this slot
        if(!GetPlayer(playerId2).isUsed())
            return;

        // In replay mode we don't touch the player
    } else
    {
        // old_id must be a player
        GamePlayer& player1 = GetPlayer(playerId1);
        if(player1.ps != PlayerState::Occupied)
            return;
        // new_id must be an AI
        GamePlayer& player2 = GetPlayer(playerId2);
        if(player2.ps != PlayerState::AI)
            return;

        // ACHTUNG: Ab hier wird die Welt veraendert. Dieser Block laeuft auf JEDEM Client, weil
        // der Server GameMessage_Player_Swap an alle broadcastet (GameServer.cpp:1701) und
        // OnGameMessage(GameMessage_Player_Swap) hier hereinspringt (GameClient.cpp:727).
        // Er darf deshalb NIEMALS von clientlokalem Zustand abhaengen - insbesondere nicht von
        // der Menge der lokal gesteuerten Spieler. Sonst haetten die Clients verschiedene
        // PlayerState und damit verschiedene isHuman()-Ergebnisse in der Simulation:
        // GameWorld.cpp:270 (AUTOFLAGS setzt Flaggen), GamePlayer.cpp:1803 (CancelPact mutiert
        // pacts[] sofort statt zu fragen), GamePlayer.cpp:1690 (SuggestPact loest ein Lua-Event
        // aus). -> Desync. Clientlokale Buchhaltung erst unterhalb, in
        // OnPlayerSlotsSwappedIngame.
        std::swap(player1.ps, player2.ps);
        std::swap(player1.aiInfo, player2.aiInfo);
        if(IsHost())
        {
            // Switch AIs
            game->aiPlayers_.erase_if([playerId2](const auto& player) { return player.GetPlayerId() == playerId2; });
            game->AddAIPlayer(CreateAIPlayer(playerId1, AI::Info(AI::Type::Dummy)));
        }
        GetPlayer(playerId1).ps = PlayerState::AI;
        GetPlayer(playerId2).ps = PlayerState::Occupied;
    }

    // Wenn wir betroffen waren, unsere ID neu setzen. Ebenfalls unbedingt: der Server hat die
    // Zuordnung Verbindung<->Slot bereits umgehaengt (GameServer.cpp:1696-1700). Wer hier nicht
    // mitzieht, adressiert dauerhaft den falschen Slot.
    if(mainPlayer.playerId == playerId1)
        mainPlayer.playerId = playerId2;

    // Die Einstellungen selbst wandern NICHT mit (oben werden nur ps und aiInfo getauscht) -
    // sie gehoeren zum Slot. Also die Anzeigewerte beider Slots aus dem Spielzustand neu
    // ableiten: wer den Slot jetzt bedient, muss dessen wirkliche Einstellungen sehen.
    // Rein clientlokal und damit desync-frei.
    GetPlayer(playerId1).FillVisualSettings(visualSettings_[playerId1]);
    GetPlayer(playerId2).FillVisualSettings(visualSettings_[playerId2]);

    // Rein clientlokale Buchhaltung - erst NACH der Weltaenderung und ohne eigenen return,
    // damit sie den Ablauf oben nicht beeinflussen kann.
    OnPlayerSlotsSwappedIngame(playerId1, playerId2);

    if(ci)
        ci->CI_PlayersSwapped(playerId1, playerId2);
}
