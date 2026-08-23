// Copyright (C) 2005 - 2025 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "GameCommand.h"
#include <cstdint>
#include <map>
#include <vector>

/// Noch nicht verschickte GameCommands aller von DIESEM Client lokal gesteuerten Spieler.
/// Genau ein Puffer pro lokalem Spieler; Iteration immer aufsteigend nach playerId.
/// Rein clientseitig, wird NIE serialisiert (nicht in Savegame, nicht ins Replay, nicht ins Netz).
class LocalPlayerCommands
{
public:
    /// Registriert einen lokal gesteuerten Spieler. Idempotent: eine erneute Registrierung
    /// eines bereits registrierten Spielers laesst dessen Puffer unangetastet.
    void AddPlayer(uint8_t playerId);
    /// Entfernt einen Spieler samt seiner noch nicht verschickten Kommandos.
    void RemovePlayer(uint8_t playerId);
    /// Entfernt alle Spieler (Spielende).
    void Clear();

    bool IsLocalPlayer(uint8_t playerId) const;
    unsigned GetNumPlayers() const;
    /// Aufsteigend sortierte Ids aller lokal gesteuerten Spieler.
    std::vector<uint8_t> GetPlayerIds() const;

    /// Haengt ein Kommando an. Vorbedingung: IsLocalPlayer(playerId).
    /// Bei unbekannter Id: Assert und wirkungslos.
    void Add(uint8_t playerId, gc::GameCommandPtr gc);
    /// Haengt mehrere Kommandos an (KI, die auf einem lokal gesteuerten Slot laeuft).
    void Append(uint8_t playerId, const std::vector<gc::GameCommandPtr>& gcs);
    /// Liefert die gesammelten Kommandos und leert den Puffer.
    /// Der Spieler bleibt registriert. Bei unbekannter Id: Assert + leerer Vektor
    /// (Release-Verhalten bewusst tolerant, damit die "genau eine Nachricht pro
    ///  Slot pro NWF"-Invariante nicht durch einen Bug gebrochen wird).
    std::vector<gc::GameCommandPtr> Fetch(uint8_t playerId);

private:
    std::map<uint8_t, std::vector<gc::GameCommandPtr>> commandsByPlayer_;
};
