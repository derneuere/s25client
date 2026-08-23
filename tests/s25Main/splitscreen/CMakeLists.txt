# Copyright (C) 2005 - 2026 Settlers Freaks <sf-team at siedler25.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

# Full in-process game (GameServer + GameClient over a loopback socket) with more than one
# locally controlled player. Needs testUIHelper because GameClient::StartGame draws through
# VIDEODRIVER/LOADER, and testConfig for the path to the test map.
# testWorldFixtures liefert WorldFixture/CreateEmptyWorld fuer die Viewport-Nachweise, die ohne
# laufende Partie und ohne OpenGL auskommen (testViewports.cpp).
add_testcase(NAME splitscreen
    LIBS s25Main testHelpers testConfig testUIHelper testWorldFixtures rttr::vld
    COST 30
)
