// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "WindowManager.h"
#include "controls/ctrlButton.h"
#include "desktops/PlayerView.h"
#include "desktops/dskGameInterface.h"
#include "driver/KeyEvent.h"
#include "driver/MouseCoords.h"
#include "drivers/VideoDriverWrapper.h"
#include "ingameWindows/IngameWindow.h"
#include "ingameWindows/iwVictory.h"
#include "mockupDrivers/MockupVideoDriver.h"
#include "network/GameClient.h"
#include "PadFixture.h"
#include "gameData/const_gui_ids.h"
#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using rttr::test::PadViewFixture;

namespace {

/// Ein vollstaendiger Mausklick auf einen Knopf ueber den ECHTEN Einstieg des WindowManagers.
void clickButton(MockupVideoDriver& video, ctrlButton& bt)
{
    const Rect r = bt.GetDrawRect();
    const Position center = r.getOrigin() + Position(r.getSize() / 2u);
    video.tickCount_ += 5000; // kein Doppelklick
    MouseCoords down(center);
    down.ldown = true;
    WINDOWMANAGER.Msg_LeftDown(down);
    WINDOWMANAGER.Msg_LeftUp(MouseCoords(center));
}

/// Ein Fenster, das festhaelt, WER laut GameClient beim Schliessen gehandelt hat.
///
/// Genau das ist der Wert, aus dem GameClient::AddGC den Spieler eines Kommandos zieht - und
/// Close() ist die Stelle, an der die Wirtschaftsfenster ihre aufgelaufenen Aenderungen senden
/// (TransmitSettingsIgwAdapter::Close).
struct ActingRecordingWnd : IngameWindow
{
    ActingRecordingWnd(unsigned id, const DrawPoint& pos)
        : IngameWindow(id, pos, Extent(200, 120), "", nullptr, false, CloseBehavior::Regular)
    {}

    void Close() override
    {
        actingAtClose = GAMECLIENT.GetActingPlayer();
        ++closeCalls;
        IngameWindow::Close();
    }

    std::optional<uint8_t> actingAtClose;
    unsigned closeCalls = 0;
};

/// Oeffnet das Fenster auf dem produktiven Weg: unter der Klammer einer Ansicht.
ActingRecordingWnd& openFor(unsigned viewIdx, unsigned id, const DrawPoint& pos)
{
    const dskGameInterface::ViewScope ownerScope(viewIdx);
    return WINDOWMANAGER.Show(std::make_unique<ActingRecordingWnd>(id, pos));
}

} // namespace

BOOST_AUTO_TEST_SUITE(WindowOwnerInvariantTests)

/// BEFUND A: die Fensterkennung ist (GUI_ID, Besitzer). Damit dedupliziert eine GUI_ID nicht
/// mehr, wenn sie sowohl aus einem geklammerten als auch aus einem ungeklammerten Pfad entstehen
/// kann.
///
/// Gemessen im Einzelspieler: ALT+Q laeuft durch dskGameInterface::Msg_KeyDown und damit unter
/// der Klammer der Hauptansicht (Besitzer 0), der Knopf "End game" in iwVictory dagegen unter
/// dem Besitzer von iwVictory - und iwVictory entsteht in GI_Winner, ausserhalb jeder Klammer,
/// also mit SHARED_WINDOW_OWNER. Ergebnis: zwei Abbruchdialoge gleichzeitig. Vor dem
/// Fensterbesitz haette ReplaceWindow den vorhandenen ersetzt.
BOOST_FIXTURE_TEST_CASE(TheEndgameDialogExistsOnlyOnceNoMatterWhichPathOpensIt, PadViewFixture<1>)
{
    // 1. ALT+Q, ueber den echten Tastaturpfad des Spieldesktops.
    KeyEvent altQ(U'q');
    altQ.alt = true;
    BOOST_TEST_REQUIRE(dsk->Msg_KeyDown(altQ));
    WINDOWMANAGER.Draw();
    IngameWindow* first = WINDOWMANAGER.GetTopMostWindow();
    BOOST_TEST_REQUIRE(first != static_cast<IngameWindow*>(nullptr));
    BOOST_TEST_REQUIRE(first->GetGUIID() == CGI_ENDGAME);

    // 2. Der Siegdialog - ohne jede Klammer erzeugt, genau wie in GI_Winner.
    auto& victory = WINDOWMANAGER.Show(std::make_unique<iwVictory>(std::vector<std::string>{"Sieger"}));
    BOOST_TEST_REQUIRE(victory.GetOwner() == SHARED_WINDOW_OWNER);
    WINDOWMANAGER.Draw();

    // 3. Sein Knopf "End game" (ID_END_GAME == 3) ruft ReplaceWindow(iwEndgame).
    auto* endGameBt = victory.GetCtrl<ctrlButton>(3);
    BOOST_TEST_REQUIRE(endGameBt != static_cast<ctrlButton*>(nullptr));
    clickButton(pads.video, *endGameBt);

    // Es darf genau EIN Abbruchdialog stehen bleiben.
    BOOST_TEST(WINDOWMANAGER.FindNonModalWindow(CGI_ENDGAME, 0) == static_cast<IngameWindow*>(nullptr));
    BOOST_TEST(WINDOWMANAGER.FindNonModalWindow(CGI_ENDGAME, SHARED_WINDOW_OWNER)
               != static_cast<IngameWindow*>(nullptr));
    WINDOWMANAGER.Draw();
}

/// BEFUND B: von den vier Schliesswegen der Fenster lag nur EINER innerhalb der Besitzklammer.
/// ESC, ALT+W und der Rechtsklick liefen ungeklammert - und Close() ist bei den
/// Wirtschaftsfenstern genau die Stelle, an der aufgelaufene Einstellungen gesendet werden.
///
/// Gemessen wird der Wert, aus dem GameClient::AddGC seinen Spieler zieht.
BOOST_FIXTURE_TEST_CASE(EscapeClosesAWindowInTheNameOfItsOwner, PadViewFixture<2>)
{
    auto& wnd = openFor(1, CGI_MAINSELECTION, DrawPoint(50, 50));
    WINDOWMANAGER.Draw();
    BOOST_TEST_REQUIRE(!WINDOWMANAGER.IsDesktopActive());

    WINDOWMANAGER.Msg_KeyDown(KeyEvent(KeyType::Escape));
    BOOST_TEST_REQUIRE(wnd.closeCalls == 1u);
    BOOST_TEST(wnd.actingAtClose.value_or(99u) == 1u);
    WINDOWMANAGER.Draw();
}

BOOST_FIXTURE_TEST_CASE(AltWClosesAWindowInTheNameOfItsOwner, PadViewFixture<2>)
{
    auto& wnd = openFor(1, CGI_MAINSELECTION, DrawPoint(50, 50));
    WINDOWMANAGER.Draw();

    KeyEvent altW(U'w');
    altW.alt = true;
    WINDOWMANAGER.Msg_KeyDown(altW);
    BOOST_TEST_REQUIRE(wnd.closeCalls == 1u);
    BOOST_TEST(wnd.actingAtClose.value_or(99u) == 1u);
    WINDOWMANAGER.Draw();
}

BOOST_FIXTURE_TEST_CASE(RightClickClosesAWindowInTheNameOfItsOwner, PadViewFixture<2>)
{
    auto& wnd = openFor(1, CGI_MAINSELECTION, DrawPoint(50, 50));
    WINDOWMANAGER.Draw();

    const Rect r = wnd.GetDrawRect();
    WINDOWMANAGER.Msg_RightDown(MouseCoords(r.getOrigin() + Position(r.getSize() / 2u)));
    BOOST_TEST_REQUIRE(wnd.closeCalls == 1u);
    BOOST_TEST(wnd.actingAtClose.value_or(99u) == 1u);
    WINDOWMANAGER.Draw();
}

/// BEFUND C: actingPlayerId_ hatte zwei Eigentuemer. Der Destruktor von ScopedWindowOwner ruft
/// ueber OnWindowOwnerChanged das nicht-RAII SetActingPlayer(); wird eine Besitzklammer INNERHALB
/// einer ScopedActingPlayer geoeffnet und geschlossen, ueberschrieb die innere den Wert der
/// aeusseren fuer deren Restlaufzeit.
///
/// Der Vorrang ist ausdruecklich: eine ScopedActingPlayer nennt den Spieler EXPLIZIT (der
/// verzoegerte Sendepfad der Wirtschaftsfenster haengt daran) und schlaegt deshalb den ambienten
/// Fensterbesitz - auch dann, wenn sie nullopt sagt.
BOOST_FIXTURE_TEST_CASE(AnOwnerScopeInsideAnActingPlayerScopeCannotOverwriteIt, PadViewFixture<2>)
{
    BOOST_TEST_REQUIRE(!GAMECLIENT.GetActingPlayer().has_value());
    {
        const GameClient::ScopedActingPlayer acting(GAMECLIENT, static_cast<uint8_t>(1));
        BOOST_TEST_REQUIRE(GAMECLIENT.GetActingPlayer().value_or(99u) == 1u);
        {
            const dskGameInterface::ViewScope inner(0);
            BOOST_TEST(GAMECLIENT.GetActingPlayer().value_or(99u) == 1u);
        }
        BOOST_TEST(GAMECLIENT.GetActingPlayer().value_or(99u) == 1u);
    }
    BOOST_TEST(!GAMECLIENT.GetActingPlayer().has_value());

    // Dieselbe Aussage fuer die andere Richtung: "ausdruecklich der Hauptspieler" darf ein
    // ambienter Besitzer nicht ueberschreiben.
    {
        const dskGameInterface::ViewScope outer(1);
        BOOST_TEST_REQUIRE(GAMECLIENT.GetActingPlayer().value_or(99u) == 1u);
        {
            const GameClient::ScopedActingPlayer acting(GAMECLIENT, std::nullopt);
            BOOST_TEST(!GAMECLIENT.GetActingPlayer().has_value());
            {
                const dskGameInterface::ViewScope inner(0);
                BOOST_TEST(!GAMECLIENT.GetActingPlayer().has_value());
            }
            BOOST_TEST(!GAMECLIENT.GetActingPlayer().has_value());
        }
        // Nach dem Ende der expliziten Klammer gilt wieder der ambiente Besitzer.
        BOOST_TEST(GAMECLIENT.GetActingPlayer().value_or(99u) == 1u);
    }
    BOOST_TEST(!GAMECLIENT.GetActingPlayer().has_value());
}

BOOST_AUTO_TEST_SUITE_END()
