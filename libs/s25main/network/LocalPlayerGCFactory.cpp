// Copyright (C) 2005 - 2025 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "network/LocalPlayerGCFactory.h"
#include "network/GameClient.h"
#include <utility>

bool LocalPlayerGCFactory::AddGC(gc::GameCommandPtr gc)
{
    return client_.AddPlayerGC(playerId_, std::move(gc));
}
