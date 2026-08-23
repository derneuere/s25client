// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#define BOOST_TEST_MODULE RTTR_Splitscreen

#include <rttr/test/Fixture.hpp>
#include <s25util/Socket.h>
#include <boost/test/unit_test.hpp>

#if RTTR_HAS_VLD
#    include <vld.h>
#endif

/// Eigenes Testmodul statt Test_network, weil hier die ECHTEN Singletons GAMECLIENT und
/// GAMESERVER benutzt werden. testGameClient.cpp legt dagegen eine GameClient-Instanz auf den
/// Stack (testGameClient.cpp:89); deren Destruktor markiert den Singleton als zerstoert
/// (SingletonImp.hpp:29-32), sodass ein spaeteres GameClient::inst() im selben Prozess mit
/// "Access to dead singleton detected!" abbricht. Getrennte Prozesse, getrennte Sorgen.
struct SplitscreenFixture : rttr::test::Fixture
{
    SplitscreenFixture() { Socket::Initialize(); }
    ~SplitscreenFixture() { Socket::Shutdown(); }
};

BOOST_GLOBAL_FIXTURE(SplitscreenFixture);
