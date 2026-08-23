// Copyright (C) 2005 - 2025 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "factories/GameCommandFactory.h"
#include <cstdint>

class GameClient;

/// GameCommandFactory fuer genau einen lokal gesteuerten Spieler.
/// Vorbild: AIInterface::AddGC - duenne Senke, die in den spielereigenen Puffer schreibt.
class LocalPlayerGCFactory final : public GameCommandFactory
{
public:
    LocalPlayerGCFactory(GameClient& client, uint8_t playerId) : client_(client), playerId_(playerId) {}
    uint8_t GetPlayerId() const { return playerId_; }

protected:
    bool AddGC(gc::GameCommandPtr gc) override;

private:
    GameClient& client_;
    uint8_t playerId_;
};
