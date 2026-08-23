// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Game.h"
#include "GameMessage_Chat.h"
#include "GameMessage_GameCommand.h"
#include "NWFInfo.h"
#include "ReplayInfo.h"
#include "ai/AIPlayer.h"
#include "network/GameClient.h"

void GameClient::ExecuteNWF()
{
    // Geschickte Network Commands der Spieler ausführen und ggf. im Replay aufzeichnen

    AsyncChecksum checksum = AsyncChecksum::create(*game);
    const unsigned curGF = GetGFNumber();

    for(const NWFPlayerInfo& player : nwfInfo->getPlayerInfos())
    {
        const PlayerGameCommands& currentGCs = player.commands.front();

        // Command im Replay aufzeichnen (wenn nicht gerade eins schon läuft xD)
        // Nur Commands reinschreiben, KEINE PLATZHALTER (nc_count = 0)
        if(!currentGCs.gcs.empty() && replayinfo && replayinfo->replay.IsRecording())
        {
            // Set the current checksum as the GF checksum. The checksum from the command is from the last NWF!
            PlayerGameCommands replayCmds(checksum, currentGCs.gcs);
            replayinfo->replay.AddGameCommand(curGF, player.id, replayCmds);
        }

        // Das ganze Zeug soll die andere Funktion ausführen
        ExecuteAllGCs(player.id, currentGCs);
    }

    // Send GC message for this NWF
    // First for all potential AIs as we need to combine the AI cmds of a locally controlled
    // player with his own ones
    for(AIPlayer& ai : game->aiPlayers_)
    {
        const std::vector<gc::GameCommandPtr> aiGCs = ai.FetchGameCommands();
        /// Cmds from an AI running on a locally controlled slot get added to that player's gcs
        if(gameCommands_.IsLocalPlayer(static_cast<uint8_t>(ai.GetPlayerId())))
            gameCommands_.Append(static_cast<uint8_t>(ai.GetPlayerId()), aiGCs);
        else
            mainPlayer.sendMsgAsync(new GameMessage_GameCommand(ai.GetPlayerId(), checksum, aiGCs));
        for(auto& msg : ai.getAIInterface().FetchChatMessages())
            mainPlayer.sendMsgAsync(msg.release());
    }
    // Own player: keep the NO_PLAYER_ID placeholder so single player and network multiplayer
    // produce the exact same message as before
    mainPlayer.sendMsgAsync(
      new GameMessage_GameCommand(0xFF, checksum, gameCommands_.Fetch(static_cast<uint8_t>(GetPlayerId()))));
    // Additional local players: explicit id, exactly like the AI players above.
    // Exactly one message per registered slot per NWF - empty ones are the required placeholders.
    for(const uint8_t id : gameCommands_.GetPlayerIds())
    {
        if(id == GetPlayerId())
            continue;
        mainPlayer.sendMsgAsync(new GameMessage_GameCommand(id, checksum, gameCommands_.Fetch(id)));
    }
}
