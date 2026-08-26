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
# testPadBrief liest die ausgelieferten .mo aus dem Ausgabeverzeichnis (shippedCatalogs()).
# Gefuellt wird das erst vom POST_BUILD-Schritt des Ziels "translations". Ohne diese Abhaengigkeit
# haengt das nur daran, dass "translations" ein ALL-Ziel ist - Glueck statt Zusicherung.
add_dependencies(Test_splitscreen translations)
