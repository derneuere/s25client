# Copyright (C) 2005 - 2021 Settlers Freaks <sf-team at siedler25.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

add_test(NAME s25client_showHelp COMMAND s25client --help
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(s25client_showHelp PROPERTIES
  PASS_REGULAR_EXPRESSION "--version"
)

add_test(NAME s25client_showVersion COMMAND s25client --version
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(s25client_showVersion PROPERTIES
  PASS_REGULAR_EXPRESSION "^Return To The Roots v[0-9.]+-[0-9a-f]+\n"
)

add_test(NAME s25client_invalidOption COMMAND s25client --nonExistantOption
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(s25client_invalidOption PROPERTIES
  WILL_FAIL TRUE
  FAIL_REGULAR_EXPRESSION "Error: .*nonExistantOption"
)

# Befund 3: --local-players wird geprueft, BEVOR Video und Audio hochfahren. Genau deshalb ist
# der Fehler hier ueberhaupt nachweisbar - ohne Fenster und ohne Grafikkarte.
add_test(NAME s25client_tooManyLocalPlayers COMMAND s25client --local-players 5
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(s25client_tooManyLocalPlayers PROPERTIES
  PASS_REGULAR_EXPRESSION "Error: --local-players must not exceed 4"
)

add_test(NAME s25client_zeroLocalPlayers COMMAND s25client --local-players 0
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(s25client_zeroLocalPlayers PROPERTIES
  PASS_REGULAR_EXPRESSION "Error: --local-players must be at least 1"
)
