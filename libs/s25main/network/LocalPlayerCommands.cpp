// Copyright (C) 2005 - 2025 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "network/LocalPlayerCommands.h"
#include <RTTR_Assert.h>
#include <utility>

void LocalPlayerCommands::AddPlayer(const uint8_t playerId)
{
    // Idempotent: ein bereits registrierter Spieler behaelt seinen Puffer
    commandsByPlayer_.emplace(playerId, std::vector<gc::GameCommandPtr>());
}

void LocalPlayerCommands::RemovePlayer(const uint8_t playerId)
{
    commandsByPlayer_.erase(playerId);
}

void LocalPlayerCommands::Clear()
{
    commandsByPlayer_.clear();
}

bool LocalPlayerCommands::IsLocalPlayer(const uint8_t playerId) const
{
    return commandsByPlayer_.find(playerId) != commandsByPlayer_.end();
}

unsigned LocalPlayerCommands::GetNumPlayers() const
{
    return static_cast<unsigned>(commandsByPlayer_.size());
}

std::vector<uint8_t> LocalPlayerCommands::GetPlayerIds() const
{
    std::vector<uint8_t> result;
    result.reserve(commandsByPlayer_.size());
    for(const auto& entry : commandsByPlayer_)
        result.push_back(entry.first);
    return result;
}

void LocalPlayerCommands::Add(const uint8_t playerId, gc::GameCommandPtr gc)
{
    const auto it = commandsByPlayer_.find(playerId);
    RTTR_Assert(it != commandsByPlayer_.end());
    if(it == commandsByPlayer_.end())
        return;
    it->second.push_back(std::move(gc));
}

void LocalPlayerCommands::Append(const uint8_t playerId, const std::vector<gc::GameCommandPtr>& gcs)
{
    const auto it = commandsByPlayer_.find(playerId);
    RTTR_Assert(it != commandsByPlayer_.end());
    if(it == commandsByPlayer_.end())
        return;
    it->second.insert(it->second.end(), gcs.begin(), gcs.end());
}

std::vector<gc::GameCommandPtr> LocalPlayerCommands::Fetch(const uint8_t playerId)
{
    const auto it = commandsByPlayer_.find(playerId);
    RTTR_Assert(it != commandsByPlayer_.end());
    if(it == commandsByPlayer_.end())
        return {};
    std::vector<gc::GameCommandPtr> result;
    result.swap(it->second);
    return result;
}
