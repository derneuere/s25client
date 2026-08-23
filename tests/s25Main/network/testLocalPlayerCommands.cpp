// Copyright (C) 2005 - 2025 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "factories/GameCommandFactory.h"
#include "network/LocalPlayerCommands.h"
#include "gameTypes/MapCoordinates.h"
#include <boost/test/unit_test.hpp>
#include <vector>

namespace {
/// Minimale Fabrik, die das erzeugte GameCommand nur festhaelt.
/// Noetig, weil die GameCommand-Konstruktoren nur fuer GameCommandFactory zugaenglich sind.
struct CapturingFactory : GameCommandFactory
{
    gc::GameCommandPtr last;

protected:
    bool AddGC(gc::GameCommandPtr gc) override
    {
        last = gc;
        return true;
    }
};

gc::GameCommandPtr makeGC(unsigned x)
{
    CapturingFactory f;
    f.SetFlag(MapPoint(static_cast<MapCoord>(x), 0));
    return f.last;
}
} // namespace

BOOST_AUTO_TEST_SUITE(LocalPlayerCommandsTests)

BOOST_AUTO_TEST_CASE(EmptyByDefault)
{
    LocalPlayerCommands cmds;
    BOOST_TEST(cmds.GetNumPlayers() == 0u);
    BOOST_TEST(!cmds.IsLocalPlayer(0));
    BOOST_TEST(cmds.GetPlayerIds().empty());
}

BOOST_AUTO_TEST_CASE(AddRemovePlayers)
{
    LocalPlayerCommands cmds;
    cmds.AddPlayer(3);
    cmds.AddPlayer(1);
    BOOST_TEST(cmds.GetNumPlayers() == 2u);
    BOOST_TEST(cmds.IsLocalPlayer(1));
    BOOST_TEST(cmds.IsLocalPlayer(3));
    BOOST_TEST(!cmds.IsLocalPlayer(2));
    // Ids sind aufsteigend sortiert
    const std::vector<uint8_t> expected{1, 3};
    BOOST_TEST(cmds.GetPlayerIds() == expected, boost::test_tools::per_element());

    // Idempotent: erneutes Hinzufuegen aendert nichts und behaelt den Puffer
    cmds.Add(1, makeGC(1));
    cmds.AddPlayer(1);
    BOOST_TEST(cmds.GetNumPlayers() == 2u);
    BOOST_TEST(cmds.Fetch(1).size() == 1u);

    cmds.RemovePlayer(1);
    BOOST_TEST(cmds.GetNumPlayers() == 1u);
    BOOST_TEST(!cmds.IsLocalPlayer(1));

    cmds.Clear();
    BOOST_TEST(cmds.GetNumPlayers() == 0u);
}

BOOST_AUTO_TEST_CASE(FetchEmptiesBufferButKeepsPlayer)
{
    LocalPlayerCommands cmds;
    cmds.AddPlayer(0);
    cmds.Add(0, makeGC(1));
    cmds.Add(0, makeGC(2));

    const std::vector<gc::GameCommandPtr> fetched = cmds.Fetch(0);
    BOOST_TEST(fetched.size() == 2u);
    // Spieler bleibt registriert, Puffer ist leer
    BOOST_TEST(cmds.IsLocalPlayer(0));
    BOOST_TEST(cmds.Fetch(0).empty());
}

BOOST_AUTO_TEST_CASE(AppendKeepsOrderAfterOwnCommands)
{
    LocalPlayerCommands cmds;
    cmds.AddPlayer(2);
    const gc::GameCommandPtr own = makeGC(1);
    const gc::GameCommandPtr aiA = makeGC(2);
    const gc::GameCommandPtr aiB = makeGC(3);
    cmds.Add(2, own);
    cmds.Append(2, std::vector<gc::GameCommandPtr>{aiA, aiB});

    const std::vector<gc::GameCommandPtr> fetched = cmds.Fetch(2);
    BOOST_TEST_REQUIRE(fetched.size() == 3u);
    BOOST_TEST((fetched[0] == own));
    BOOST_TEST((fetched[1] == aiA));
    BOOST_TEST((fetched[2] == aiB));
}

BOOST_AUTO_TEST_CASE(BuffersAreIndependent)
{
    LocalPlayerCommands cmds;
    cmds.AddPlayer(0);
    cmds.AddPlayer(1);
    cmds.Add(0, makeGC(1));
    cmds.Add(1, makeGC(2));
    cmds.Add(1, makeGC(3));

    BOOST_TEST(cmds.Fetch(0).size() == 1u);
    BOOST_TEST(cmds.Fetch(1).size() == 2u);
}

BOOST_AUTO_TEST_SUITE_END()
