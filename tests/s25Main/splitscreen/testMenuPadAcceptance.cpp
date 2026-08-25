// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "LocalGameFixture.h"
#include "MenuPadFixture.h"
#include "PadFixture.h"
#include "PointOutput.h"
#include "GameLobby.h"
#include "Settings.h"
#include "WindowManager.h"
#include "controls/ctrlTable.h"
#include "desktops/dskGameLobby.h"
#include "desktops/dskMainMenu.h"
#include "desktops/dskSelectMap.h"
#include "desktops/dskSinglePlayer.h"
#include "drivers/VideoDriverWrapper.h"
#include "input/MenuPadInput.h"
#include "input/PadRouter.h"
#include "network/GameClient.h"
#include "network/GameServer.h"
#include "rttr/test/random.hpp"
#include <boost/test/unit_test.hpp>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr PadButton Activate = PadButton::A;
constexpr PadButton NextCtrl = PadButton::RightShoulder;
constexpr PadButton Down = PadButton::DpadDown;
constexpr PadButton Up = PadButton::DpadUp;

/// dskSinglePlayer: "Freies Spiel" ist die 6, erster fokussierbarer Knopf ist die 3
/// (dskSinglePlayer.cpp:39-49). Von dort sind es drei Schritte in ID-Reihenfolge.
constexpr unsigned STEPS_TO_UNLIMITED_PLAY = 3;

/// Die Maus liegt bewusst ausserhalb jeder Ansicht - wie in PadGameFixture.
const Position kMouseOffScreen{-10000, -10000};

/// Ein leerer Desktop, der KEINE Padereignisse will - der Platzhalter fuer den Ladebildschirm.
struct EmptyDesktop : Desktop
{
    EmptyDesktop() : Desktop(nullptr) {}
};

/// Der Abnahmenachweis dieser Phase.
///
/// Er verbindet zwei Gerueste, weil er beides braucht: LocalGameFixture faehrt einen ECHTEN
/// GameServer und GameClient ueber einen Loopback-Socket, MenuPadFixture speist Padereignisse
/// in den MockupVideoDriver und laesst Frames laufen.
///
/// WAS DER TEST NICHT ANFASSEN DARF - und unten auch nirgends anfasst:
///  - GAMECLIENT.SetAdditionalLocalPlayers(...): sie zu ERREICHEN ist die Aufgabe dieser Phase.
///    Jeder bisherige Splitscreennachweis ruft sie selbst (PadGameFixture.h:122,
///    testSplitscreenGame.cpp:91); hier entsteht sie ausschliesslich aus Padeingaben.
///  - QuickStartGame(...) und jede Option aus s25client.cpp - der Testprozess hat keine
///    Kommandozeile.
///  - LocalGameFixture::hostAndEnterLobby(): gehostet wird ueber dskSelectMap, also produktiv.
///  - FocusPath, Window::Activate(), Desktop::Msg_ButtonClick(id), ctrlTable::SetSelection.
struct MenuPadAcceptanceFixture : rttr::test::LocalGameFixture, rttr::test::MenuPadFixture
{
    /// Ein Frame in derselben Reihenfolge wie GameManager::Run (GameManager.cpp:110-153):
    /// erst Client und Server, dann zeichnen.
    void frame() override
    {
        video.tickCount_ += frameMs;
        GAMECLIENT.Run();
        GAMESERVER.Run();
        WINDOWMANAGER.Draw();
    }

    /// Frames laufen lassen, bis die Bedingung wahr ist. Liefert false bei Zeitueberschreitung.
    template<class T_Pred>
    bool frameUntil(const T_Pred& isDone, const unsigned maxFrames = 2000)
    {
        for(unsigned i = 0; i < maxFrames && !isDone(); ++i)
            frame();
        return isDone();
    }
};

/// Die erste Zeile der Kartentabelle, die fuer eine Splitscreenpartie taugt: mindestens zwei
/// Spielerslots und kein Lua-Skript daneben (dskSelectMap haengt an ein Lua-Skript eine eigene
/// Einstellungslogik, die mit dieser Frage nichts zu tun hat).
///
/// Das ist LESEN, keine Eingabe: der Test schaut auf den Bildschirm, um zu wissen, wie oft er
/// nach unten druecken muss - genau das, was ein Mensch auch tut. Die Auswahl selbst entsteht
/// danach ausschliesslich aus Padereignissen.
///
/// DIE SPIELERZAHL STEHT NICHT IMMER VORN. Die Spalte ist ein UEBERSETZTER Satz: dskSelectMap
/// baut sie als boost::format(_("%d Player")) (dskSelectMap.cpp:442), und wo in diesem Satz die
/// Zahl steht, entscheidet der Katalog. Frueher las dieser Helfer players[0]; das gilt fuer
/// Englisch und Deutsch und nicht fuer Polnisch. Gemessen an derselben Binaerdatei:
///
///     LC_ALL=pl_PL.UTF-8 ./Test_splitscreen.exe --run_test="MenuPadAcceptanceTests/*"
///     testMenuPadAcceptance.cpp(160): fatal error: critical check !!targetRow has failed
///     *** 2 failures are detected
///
/// Der Helfer fand dort keine einzige taugliche Karte, und beide Abnahmefaelle dieser Datei -
/// das Abnahmekriterium der Phase - brachen ab. Gesucht wird deshalb die erste Ziffernfolge,
/// wo immer sie steht, und sie wird als ZAHL gelesen (nicht als erstes Zeichen: "12 Player"
/// waere sonst zu wenig Spieler).
std::optional<unsigned> findSplitscreenCapableRow(const ctrlTable& table)
{
    for(unsigned short row = 0; row < table.GetNumRows(); ++row)
    {
        const std::string& name = table.GetItemText(row, 0);
        if(name.find("(*)") != std::string::npos)
            continue; // Karte mit Lua-Skript
        const std::string& players = table.GetItemText(row, 2);
        const size_t begin = players.find_first_of("0123456789");
        if(begin == std::string::npos)
            continue;
        unsigned numPlayers = 0;
        for(size_t i = begin; i < players.size() && players[i] >= '0' && players[i] <= '9'; ++i)
            numPlayers = numPlayers * 10u + static_cast<unsigned>(players[i] - '0');
        if(numPlayers >= 2)
            return static_cast<unsigned>(row);
    }
    return std::nullopt;
}

} // namespace

BOOST_AUTO_TEST_SUITE(MenuPadAcceptanceTests)

/// DAS ABNAHMEKRITERIUM DIESER PHASE:
/// Eine Splitscreenpartie fuer zwei lokale Spieler entsteht AUSSCHLIESSLICH aus Padeingaben -
/// keine Kommandozeile, keine Tastatur, keine Maus.
///
/// Die Kette, Glied fuer Glied, jeweils produktiv:
///   dskMainMenu -> dskSinglePlayer -> dskSelectMap -> (GAMECLIENT.HostGame) -> iwConnecting
///   -> dskGameLobby -> Sitzplatz nehmen -> "Spiel starten"
///
/// DIE EINE LUECKE, benannt und nicht ueberbrueckt: dskGameLoader. Sein Texturschritt
/// (dskGameLoader.cpp:92-103 -> Loader::LoadFilesAtGame) braucht die ORIGINALEN S2-Daten, die
/// im Repository nicht liegen. Ab dort uebernimmt der Test dieselben zwei Aufrufe, die
/// LocalGameFixture::startGame seit jeher an derselben Stelle macht
/// (LocalGameFixture.h:177-194, mit Verweis auf dskGameLoader.cpp:118). Alles davor und alles
/// danach ist Produktivcode.
BOOST_FIXTURE_TEST_CASE(TwoLocalPlayersStartASplitscreenGameWithPadsOnly, MenuPadAcceptanceFixture)
{
    // AUSGANGSLAGE: nichts ist vorbereitet. Waere hier schon etwas gesetzt, bewiese der Test
    // nichts.
    BOOST_TEST_REQUIRE(GAMECLIENT.GetAdditionalLocalPlayers().empty());

    constexpr PadDeviceId padHost = 11;
    constexpr PadDeviceId padGuest = 12;

    // --- 1. Hauptmenue -> Einzelspieler ------------------------------------------------------
    WINDOWMANAGER.Switch(std::make_unique<dskMainMenu>());
    frame();
    BOOST_TEST_REQUIRE(desktopAs<dskMainMenu>() != nullptr);

    pickUp(padHost);
    BOOST_TEST_REQUIRE(router().GetSlot(padHost) == 0u);
    press(padHost, Activate);
    BOOST_TEST_REQUIRE(desktopAs<dskSinglePlayer>() != nullptr);

    // --- 2. Einzelspieler -> Freies Spiel -----------------------------------------------------
    frame();
    pressN(padHost, NextCtrl, STEPS_TO_UNLIMITED_PLAY);
    // Der Port wird zufaellig gewaehlt: dskSinglePlayer::createLocalGameInfo liest ihn beim
    // Knopfdruck aus SETTINGS.server.localPort. Ein fester Port faellt auf einem Rechner mit
    // TIME_WAIT-Resten sporadisch um - dieselbe Vorsichtsmassnahme wie in
    // LocalGameFixture::hostAndEnterLobby.
    SETTINGS.server.localPort = static_cast<uint16_t>(rttr::test::randomValue(1024, 49151));
    press(padHost, Activate);
    auto* selectMap = desktopAs<dskSelectMap>();
    BOOST_TEST_REQUIRE(selectMap != nullptr);

    // --- 3. Karte waehlen und hosten ----------------------------------------------------------
    frame();
    const auto* table = selectMap->GetCtrl<ctrlTable>(1);
    BOOST_TEST_REQUIRE(table != nullptr);
    BOOST_TEST_REQUIRE(table->GetNumRows() > 0);
    const auto targetRow = findSplitscreenCapableRow(*table);
    BOOST_TEST_REQUIRE(!!targetRow);

    // Der Fokus liegt auf der Tabelle - sie ist das erste fokussierbare Control
    // (dskSelectMap.cpp:69, ID 1). Steuerkreuz hoch/runter verbraucht das Control selbst als
    // Wertaenderung (ctrlTable::StepValue), es wandert also kein Fokus.
    BOOST_TEST_REQUIRE(focusedId(0) == 1u);
    for(unsigned i = 0; i < table->GetNumRows() + 2u && table->GetSelection() != targetRow; ++i)
    {
        const auto sel = table->GetSelection();
        press(padHost, (!sel || *sel < *targetRow) ? Down : Up);
    }
    BOOST_TEST_REQUIRE((table->GetSelection() == targetRow));

    // A auf der Tabelle ist "diese Karte" - derselbe Weg, den der Doppelklick nimmt
    // (ctrlTable::Activate -> dskSelectMap::Msg_TableChooseItem -> StartServer).
    press(padHost, Activate);

    // Ab hier laeuft eine echte Loopbackverbindung. iwConnecting traegt den Uebergang.
    BOOST_TEST_REQUIRE(frameUntil([this] { return desktopAs<dskGameLobby>() != nullptr; }),
                       "the lobby to appear after hosting");
    frame();
    BOOST_TEST_REQUIRE(!!GAMECLIENT.GetGameLobby());
    BOOST_TEST_REQUIRE(GAMECLIENT.GetGameLobby()->getNumPlayers() >= 2u);

    // --- 4. Der Zuordnungsbildschirm: Pad 2 nimmt Platz --------------------------------------
    // Immer noch nichts gesetzt - erst der Beitritt erzeugt die Zusatzspieler.
    BOOST_TEST_REQUIRE(GAMECLIENT.GetAdditionalLocalPlayers().empty());

    pickUp(padGuest);
    BOOST_TEST_REQUIRE(router().GetSlot(padGuest) == 1u); // die Lobby bietet mehrere Slots an
    // Sein Fokus liegt auf der ersten freien Sitzkarte (dskGameLobby::GetPadEntryCtrl).
    BOOST_TEST_REQUIRE(focused(1) != nullptr);
    press(padGuest, Activate);
    frame();

    const std::vector<uint8_t> expected{1};
    BOOST_TEST(GAMECLIENT.GetAdditionalLocalPlayers() == expected, boost::test_tools::per_element());
    // Und der Spielzustand dahinter: der Slot ist auf lokale Steuerung umgestellt.
    BOOST_TEST((GAMECLIENT.GetGameLobby()->getPlayer(1).ps == PlayerState::AI));

    // --- 5. "Spiel starten", mit dem Pad -----------------------------------------------------
    // ID_btStartGame ist die 0 und damit das erste fokussierbare Control der Lobby; der Fokus
    // des Hostpads liegt seit dem Desktopwechsel dort.
    BOOST_TEST_REQUIRE(focusedId(0) == 0u);
    // HIER beginnt die eine Luecke. GameClient haelt GENAU EIN ClientInterface
    // (network/GameClient.h:75), und in Produktion ist das ab jetzt dskGameLobby -> dskGameLoader.
    // Da der Ladebildschirm im Test unerreichbar ist, uebernimmt der Test diese Stelle; alles
    // andere an der Lobby bleibt unveraendert produktiv.
    GAMECLIENT.SetInterface(&ci());
    press(padHost, Activate);
    BOOST_TEST_REQUIRE(frameUntil([] { return GAMECLIENT.GetState() == ClientState::Loading; }, 20000),
                       "the countdown to finish and loading to start");

    // --- 6. DIE EINE LUECKE: dskGameLoader ----------------------------------------------------
    // Der Ladebildschirm braucht die originalen S2-Texturen und ist im Test unerreichbar. Er
    // wird durch einen leeren Desktop ersetzt und seine EINE produktive Wirkung von Hand
    // nachgebildet - GAMECLIENT.GameLoaded() (dskGameLoader.cpp:118).
    WINDOWMANAGER.Switch(std::make_unique<EmptyDesktop>());
    frame();
    GAMECLIENT.GameLoaded();
    pumpUntil([] { return GAMECLIENT.GetState() == ClientState::Game; }, "the client to enter the game state");
    BOOST_TEST_REQUIRE(!!ci().game);
    GAMECLIENT.OnGameStart();
    BOOST_TEST_REQUIRE(ci().game->IsStarted());

    // --- 7. Die Partie: zwei Ansichten, und die Sitze sind DIESELBEN --------------------------
    BOOST_TEST_REQUIRE(GAMECLIENT.IsLocalHumanPlayer(0));
    BOOST_TEST_REQUIRE(GAMECLIENT.IsLocalHumanPlayer(1));

    auto dsk = std::make_unique<rttr::test::TestableGameInterface>(ci().game, GAMECLIENT.GetNWFInfo(),
                                                                   GAMECLIENT.GetPlayerId(), /*initOGL*/ false);
    BOOST_TEST_REQUIRE(dsk->GetNumViews() == 2u);
    BOOST_TEST(dsk->GetPlayerView(0).GetPlayerId() == 0u);
    BOOST_TEST(dsk->GetPlayerView(1).GetPlayerId() == 1u);

    // DIE UNBEQUEME ZEILE: ohne sie waere auch eine Umsetzung gruen, die die Zuordnung beim
    // Spielstart wegwirft und im Spiel neu vergibt - vier Spieler saessen nach dem
    // Ladebildschirm auf vertauschten Voelkern. Gefragt wird der Router der Partie, nicht eine
    // Buchhaltung des Tests.
    dsk->UpdateInput(0, kMouseOffScreen);
    BOOST_TEST(rttr::test::padOfView(*dsk, 0) == padHost);
    BOOST_TEST(rttr::test::padOfView(*dsk, 1) == padGuest);
}

/// GEGENPROBE ZUM ABNAHMEKRITERIUM: dieselbe Kette mit nur EINEM Pad ergibt eine ganz normale
/// Einzelspielerpartie mit EINER Ansicht. Ohne diesen Fall koennte der neue Weg den alten
/// kaputtgemacht haben, ohne dass es auffaellt.
BOOST_FIXTURE_TEST_CASE(ASinglePadStillProducesAnOrdinarySinglePlayerGame, MenuPadAcceptanceFixture)
{
    BOOST_TEST_REQUIRE(GAMECLIENT.GetAdditionalLocalPlayers().empty());
    constexpr PadDeviceId padHost = 21;

    WINDOWMANAGER.Switch(std::make_unique<dskMainMenu>());
    frame();
    pickUp(padHost);
    press(padHost, Activate);
    BOOST_TEST_REQUIRE(desktopAs<dskSinglePlayer>() != nullptr);
    frame();
    pressN(padHost, NextCtrl, STEPS_TO_UNLIMITED_PLAY);
    SETTINGS.server.localPort = static_cast<uint16_t>(rttr::test::randomValue(1024, 49151));
    press(padHost, Activate);
    auto* selectMap = desktopAs<dskSelectMap>();
    BOOST_TEST_REQUIRE(selectMap != nullptr);
    frame();

    const auto* table = selectMap->GetCtrl<ctrlTable>(1);
    const auto targetRow = findSplitscreenCapableRow(*table);
    BOOST_TEST_REQUIRE(!!targetRow);
    for(unsigned i = 0; i < table->GetNumRows() + 2u && table->GetSelection() != targetRow; ++i)
    {
        const auto sel = table->GetSelection();
        press(padHost, (!sel || *sel < *targetRow) ? Down : Up);
    }
    press(padHost, Activate);
    BOOST_TEST_REQUIRE(frameUntil([this] { return desktopAs<dskGameLobby>() != nullptr; }), "the lobby to appear");
    frame();

    // NIEMAND nimmt einen zweiten Sitz. Der Zuordnungsbildschirm steht da, tut aber nichts.
    BOOST_TEST(GAMECLIENT.GetAdditionalLocalPlayers().empty());

    // Dieselbe Luecke wie oben: ab dem Startknopf ersetzt der Test den Ladebildschirm.
    GAMECLIENT.SetInterface(&ci());
    press(padHost, Activate); // "Spiel starten"
    BOOST_TEST_REQUIRE(frameUntil([] { return GAMECLIENT.GetState() == ClientState::Loading; }, 20000),
                       "loading to start");
    WINDOWMANAGER.Switch(std::make_unique<EmptyDesktop>());
    frame();
    GAMECLIENT.GameLoaded();
    pumpUntil([] { return GAMECLIENT.GetState() == ClientState::Game; }, "the client to enter the game state");
    GAMECLIENT.OnGameStart();

    BOOST_TEST_REQUIRE(GAMECLIENT.IsLocalHumanPlayer(0));
    BOOST_TEST(!GAMECLIENT.IsLocalHumanPlayer(1));
    auto dsk = std::make_unique<rttr::test::TestableGameInterface>(ci().game, GAMECLIENT.GetNWFInfo(),
                                                                   GAMECLIENT.GetPlayerId(), /*initOGL*/ false);
    BOOST_TEST(dsk->GetNumViews() == 1u);
}

BOOST_AUTO_TEST_SUITE_END()
