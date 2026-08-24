// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "MenuPadFixture.h"
#include "Loader.h"
#include "PointOutput.h"
#include "Settings.h"
#include "WindowManager.h"
#include "controls/ctrlButton.h"
#include "desktops/dskMainMenu.h"
#include "desktops/dskOptions.h"
#include "desktops/dskSinglePlayer.h"
#include "drivers/VideoDriverWrapper.h"
#include "ingameWindows/iwMsgbox.h"
#include "ingameWindows/iwTextfile.h"
#include "input/MenuPadInput.h"
#include "input/PadRouter.h"
#include "mockupDrivers/MockupVideoDriver.h"
#include "uiHelper/uiHelpers.hpp"
#include <boost/test/unit_test.hpp>
#include <memory>

using rttr::test::MenuPadFixture;

BOOST_AUTO_TEST_SUITE(MenuPadInputTests)

namespace {
/// Die Knopfbelegung des MENUES, an genau einer Stelle. Sie steht so in
/// input/MenuPadInput.cpp und input/FocusPath.cpp.
constexpr PadButton Activate = PadButton::A;
constexpr PadButton Back = PadButton::B;
constexpr PadButton NextCtrl = PadButton::RightShoulder;
constexpr PadButton PrevCtrl = PadButton::LeftShoulder;
constexpr PadButton Down = PadButton::DpadDown;
constexpr PadButton Up = PadButton::DpadUp;

/// dskMainMenu: ID_FIRST_FREE ist 3, also ist "Einzelspieler" die 3 und der erste
/// fokussierbare Knopf (dskMainMenu.cpp:24-38; 0..2 sind die Versionstexte von dskMenuBase).
constexpr unsigned ID_btSingleplayer = 3;
constexpr unsigned ID_btMultiplayer = 4;
constexpr unsigned ID_btOptions = 5;
/// dskSinglePlayer: "Freies Spiel" ist die 6 (dskSinglePlayer.cpp:45).
constexpr unsigned ID_btUnlimitedPlay = 6;

/// Mausklick auf einen Punkt, ueber die Treiber-Eintrittspunkte des WindowManagers - genau die
/// Methoden, die der echte SDL-Treiber ueber VideoDriverLoaderInterface ruft.
void mouseClick(const Position& pos)
{
    MouseCoords mc(pos);
    WINDOWMANAGER.Msg_MouseMove(mc);
    mc.ldown = true;
    WINDOWMANAGER.Msg_LeftDown(mc);
    mc.ldown = false;
    WINDOWMANAGER.Msg_LeftUp(mc);
}

/// Zaehlt jede Maus- und Tastaturnachricht, die der WindowManager an den Desktop zustellt.
/// Muster uebernommen aus testGamepadInput.cpp:659-711.
struct CountingDesktop : Desktop
{
    unsigned numMouseMove = 0, numLeftDown = 0, numLeftUp = 0, numRightDown = 0, numKeyDown = 0;

    CountingDesktop() : Desktop(nullptr)
    {
        AddTextButton(1, DrawPoint(10, 10), Extent(100, 20), TextureColor::Green1, "A", NormalFont);
        AddTextButton(2, DrawPoint(10, 40), Extent(100, 20), TextureColor::Green1, "B", NormalFont);
    }
    bool WantsPadInput() const override { return true; }
    bool Msg_MouseMove(const MouseCoords&) override
    {
        ++numMouseMove;
        return false;
    }
    bool Msg_LeftDown(const MouseCoords&) override
    {
        ++numLeftDown;
        return false;
    }
    bool Msg_LeftUp(const MouseCoords&) override
    {
        ++numLeftUp;
        return false;
    }
    bool Msg_RightDown(const MouseCoords&) override
    {
        ++numRightDown;
        return false;
    }
    bool Msg_KeyDown(const KeyEvent&) override
    {
        ++numKeyDown;
        return false;
    }
};
} // namespace

// ---------------------------------------------------------------------------------------------
// 1. Padereignisse kommen ausserhalb einer Partie ueberhaupt an
// ---------------------------------------------------------------------------------------------

/// DER BEFUND DIESER PHASE, als Nachweis: ein Gamepad bedient das Hauptmenue.
///
/// Der Weg vom fokussierten Knopf zur Wirkung ist derselbe wie beim Mausklick:
/// ctrlButton::Activate() ruft GetParent()->Msg_ButtonClick(GetID()) - mit dem ausdruecklichen
/// Kommentar "identisch zum Mauspfad in Msg_LeftUp" (controls/ctrlButton.cpp:82-94). Gemessen
/// wird deshalb die WIRKUNG (welcher Desktop steht danach), nicht der Mechanismus.
BOOST_FIXTURE_TEST_CASE(APadCanReachSinglePlayerFromTheMainMenu, MenuPadFixture)
{
    WINDOWMANAGER.Switch(std::make_unique<dskMainMenu>());
    frame();
    BOOST_TEST_REQUIRE(desktopAs<dskMainMenu>() != nullptr);

    pickUp(1);
    // Der erste fokussierbare Knopf ist "Einzelspieler".
    BOOST_TEST_REQUIRE(focusedId(0) == ID_btSingleplayer);

    press(1, Activate);
    BOOST_TEST(desktopAs<dskSinglePlayer>() != nullptr);
}

/// NEGATIVKONTROLLE ZUR ZUSTELLUNG: ohne Frame passiert nichts. Ohne diesen Fall bewiese der
/// Fall oben nicht, dass die Pumpe im WindowManager der Wirkweg ist.
BOOST_FIXTURE_TEST_CASE(WithoutAFrameNothingIsDelivered, MenuPadFixture)
{
    WINDOWMANAGER.Switch(std::make_unique<dskMainMenu>());
    frame();

    connect(1);
    tap(1, PadButton::Start);
    // KEIN frame()
    BOOST_TEST(router().GetSlot(1) == PadRouter::NoSlot);
    BOOST_TEST(focused(0) == nullptr);
    BOOST_TEST(desktopAs<dskMainMenu>() != nullptr);

    // Gegenprobe: derselbe Vorrat, ein Frame - und jetzt wirkt er.
    frame();
    BOOST_TEST(router().GetSlot(1) == 0u);
    BOOST_TEST(focusedId(0) == ID_btSingleplayer);

    tap(1, Activate);
    BOOST_TEST(desktopAs<dskMainMenu>() != nullptr); // immer noch kein Frame
    frame();
    BOOST_TEST(desktopAs<dskSinglePlayer>() != nullptr);
}

/// NEGATIVKONTROLLE: blosses Anstecken vergibt keinen Slot und zeigt keinen Rahmen
/// (input/PadRouter.h: Uebernahme durch Benutzung). Das ist zugleich die harte Randbedingung
/// fuer den Mausspieler, an dessen Rechner ein Controller haengt.
BOOST_FIXTURE_TEST_CASE(APluggedButUnusedPadGetsNoSlotAndNoFocus, MenuPadFixture)
{
    WINDOWMANAGER.Switch(std::make_unique<dskMainMenu>());
    frame();

    connect(7);
    frame();
    BOOST_TEST(router().GetSlot(7) == PadRouter::NoSlot);
    BOOST_TEST(padInput().HasDevice(0) == false);
    BOOST_TEST(focused(0) == nullptr);

    // Gegenprobe, damit "ist leer" nicht aus einer kaputten Messung stammt.
    press(7, PadButton::Start);
    BOOST_TEST(router().GetSlot(7) == 0u);
    BOOST_TEST(focused(0) != nullptr);
}

/// NEGATIVKONTROLLE: ein nie angestecktes Geraet wirkt nicht.
BOOST_FIXTURE_TEST_CASE(AnUnknownDeviceHasNoEffect, MenuPadFixture)
{
    WINDOWMANAGER.Switch(std::make_unique<dskMainMenu>());
    frame();

    press(42, Activate);
    BOOST_TEST(desktopAs<dskMainMenu>() != nullptr);
    BOOST_TEST(focused(0) == nullptr);
}

/// NEGATIVKONTROLLE: ein abgezogenes Pad wirkt nicht mehr, und sein Slot ist frei.
BOOST_FIXTURE_TEST_CASE(ADisconnectedPadStopsWorking, MenuPadFixture)
{
    WINDOWMANAGER.Switch(std::make_unique<dskMainMenu>());
    frame();
    pickUp(1);
    BOOST_TEST_REQUIRE(router().GetSlot(1) == 0u);

    disconnect(1);
    frame();
    BOOST_TEST(router().GetSlot(1) == PadRouter::NoSlot);
    BOOST_TEST(padInput().HasDevice(0) == false);
    BOOST_TEST(focused(0) == nullptr);

    press(1, Activate);
    BOOST_TEST(desktopAs<dskMainMenu>() != nullptr);
}

/// Der AUFNAHMEDRUCK wirkt nicht: wer sein Pad mit A in die Hand nimmt, loest damit nicht schon
/// den ersten Knopf aus. Ohne diese Bremse waere jeder Beitritt ein Fehlklick.
BOOST_FIXTURE_TEST_CASE(TheTakeUpPressDoesNotActivateAnything, MenuPadFixture)
{
    WINDOWMANAGER.Switch(std::make_unique<dskMainMenu>());
    frame();

    connect(1);
    tap(1, Activate); // A ist hier die Uebernahme
    frame();
    BOOST_TEST_REQUIRE(router().GetSlot(1) == 0u);
    BOOST_TEST(desktopAs<dskMainMenu>() != nullptr); // NICHT gewechselt
    BOOST_TEST(focusedId(0) == ID_btSingleplayer);

    // Der ZWEITE A-Druck wirkt dann.
    press(1, Activate);
    BOOST_TEST(desktopAs<dskSinglePlayer>() != nullptr);
}

// ---------------------------------------------------------------------------------------------
// 2. Fokusnavigation auf Desktops
// ---------------------------------------------------------------------------------------------

/// Die Schulterknoepfe laufen die Controls in ID-Reihenfolge ab, das Steuerkreuz geometrisch.
/// Beide Wege muessen denselben Knopf erreichen koennen.
BOOST_FIXTURE_TEST_CASE(BothShoulderAndDpadMoveTheFocus, MenuPadFixture)
{
    WINDOWMANAGER.Switch(std::make_unique<dskMainMenu>());
    frame();
    pickUp(1);
    BOOST_TEST_REQUIRE(focusedId(0) == ID_btSingleplayer);

    press(1, NextCtrl);
    BOOST_TEST(focusedId(0) == ID_btMultiplayer);
    press(1, NextCtrl);
    BOOST_TEST(focusedId(0) == ID_btOptions);
    press(1, PrevCtrl);
    BOOST_TEST(focusedId(0) == ID_btMultiplayer);

    press(1, Down);
    BOOST_TEST(focusedId(0) == ID_btOptions);
    press(1, Up);
    BOOST_TEST(focusedId(0) == ID_btMultiplayer);
}

/// Kein Umlauf ueber die Fenstergrenze: der Fokus bleibt am Rand stehen, statt zu springen.
BOOST_FIXTURE_TEST_CASE(TheFocusDoesNotWrapAround, MenuPadFixture)
{
    WINDOWMANAGER.Switch(std::make_unique<dskMainMenu>());
    frame();
    pickUp(1);
    BOOST_TEST_REQUIRE(focusedId(0) == ID_btSingleplayer);

    press(1, PrevCtrl);
    BOOST_TEST(focusedId(0) == ID_btSingleplayer);
}

/// Der Fokus wandert mit dem Desktop mit und faengt dort wieder vorn an. Das ist die Stelle,
/// an der ein Fokus mit einer toten Wurzel auffallen wuerde.
BOOST_FIXTURE_TEST_CASE(TheFocusFollowsADesktopSwitch, MenuPadFixture)
{
    WINDOWMANAGER.Switch(std::make_unique<dskMainMenu>());
    frame();
    pickUp(1);
    press(1, Activate);
    BOOST_TEST_REQUIRE(desktopAs<dskSinglePlayer>() != nullptr);

    // Der Geraetebestand hat den Wechsel ueberlebt - das Pad muss nicht neu aufgenommen werden.
    BOOST_TEST(router().GetSlot(1) == 0u);
    frame();
    BOOST_TEST(padInput().GetFocus(0).GetRoot() == static_cast<Window*>(desktop()));
    BOOST_TEST(focused(0) != nullptr);
}

/// Die ganze Kette bis zur Kartenauswahl, nur mit dem Pad: Hauptmenue -> Einzelspieler ->
/// Freies Spiel. Der dritte Schritt hostet einen Server und wird deshalb erst im
/// Abnahmenachweis gegangen.
BOOST_FIXTURE_TEST_CASE(APadReachesUnlimitedPlay, MenuPadFixture)
{
    WINDOWMANAGER.Switch(std::make_unique<dskMainMenu>());
    frame();
    pickUp(1);
    press(1, Activate);
    BOOST_TEST_REQUIRE(desktopAs<dskSinglePlayer>() != nullptr);
    frame();

    // "Letztes Spiel fortsetzen" (3) ist der erste Knopf; "Freies Spiel" ist die 6.
    BOOST_TEST_REQUIRE(focusedId(0) == 3u);
    press(1, NextCtrl); // 4 Replay
    press(1, NextCtrl); // 5 Kampagne
    press(1, NextCtrl); // 6 Freies Spiel
    BOOST_TEST(focusedId(0) == ID_btUnlimitedPlay);
}

/// Ein Fenster ueber dem Desktop faengt die Navigation ab - sonst bliebe der Padpfad an jeder
/// Nachrichtenbox und an iwConnecting haengen, also mitten auf dem Weg in die Partie.
BOOST_FIXTURE_TEST_CASE(AnOpenWindowTakesOverTheNavigation, MenuPadFixture)
{
    WINDOWMANAGER.Switch(std::make_unique<dskMainMenu>());
    frame();
    pickUp(1);
    BOOST_TEST_REQUIRE(padInput().GetFocus(0).GetRoot() == static_cast<Window*>(desktop()));

    IngameWindow& wnd = WINDOWMANAGER.Show(
      std::make_unique<iwMsgbox>("Titel", "Text", nullptr, MsgboxButton::Ok, MsgboxIcon::ExclamationRed, 1));
    frame();
    BOOST_TEST(padInput().GetFocus(0).GetRoot() == static_cast<Window*>(&wnd));
    BOOST_TEST(focused(0) != nullptr); // im Fenster gibt es etwas zu bedienen

    // A auf dem OK-Knopf schliesst die Box, und die Navigation faellt auf den Desktop zurueck.
    // B taete es hier NICHT: iwMsgbox ist CloseBehavior::Custom (iwMsgbox.cpp:37), und davor
    // haelt der Padpfad genauso an wie der ESC-Pfad der Tastatur
    // (WindowManager::RelayKeyboardMessage).
    press(1, Back);
    frame();
    BOOST_TEST(WINDOWMANAGER.GetTopMostWindow() == &wnd);

    press(1, Activate);
    frame();
    BOOST_TEST(WINDOWMANAGER.GetTopMostWindow() == nullptr);
    BOOST_TEST(padInput().GetFocus(0).GetRoot() == static_cast<Window*>(desktop()));
}

/// Und die Gegenprobe zu B: ein gewoehnliches Fenster geht damit zu - dieselbe Wirkung, die die
/// Tastatur mit ESC hat.
BOOST_FIXTURE_TEST_CASE(BClosesAnOrdinaryWindow, MenuPadFixture)
{
    WINDOWMANAGER.Switch(std::make_unique<dskMainMenu>());
    frame();
    pickUp(1);

    IngameWindow& wnd = WINDOWMANAGER.Show(std::make_unique<iwTextfile>("readme.txt", "Readme"));
    frame();
    BOOST_TEST_REQUIRE(padInput().GetFocus(0).GetRoot() == static_cast<Window*>(&wnd));

    press(1, Back);
    frame();
    BOOST_TEST(WINDOWMANAGER.GetTopMostWindow() == nullptr);
    BOOST_TEST(padInput().GetFocus(0).GetRoot() == static_cast<Window*>(desktop()));
}

// ---------------------------------------------------------------------------------------------
// 3. Maus und Tastatur bleiben unveraendert
// ---------------------------------------------------------------------------------------------

/// (b) KEIN PAD GESTECKT: der Mausweg ist unveraendert. Das ist der Fall, der 99 % der Spieler
/// betrifft.
BOOST_FIXTURE_TEST_CASE(TheMousePathIsUnchangedWithoutAnyPad, MenuPadFixture)
{
    WINDOWMANAGER.Switch(std::make_unique<dskMainMenu>());
    frame();

    auto* bt = desktop()->GetCtrl<ctrlButton>(ID_btSingleplayer);
    BOOST_TEST_REQUIRE(bt != nullptr);
    mouseClick(bt->GetDrawPos() + Position(bt->GetSize() / 2u));
    frame();
    BOOST_TEST(desktopAs<dskSinglePlayer>() != nullptr);
}

/// (c) PAD GESTECKT, ABER NICHT BENUTZT: ebenfalls unveraendert, und kein Fokusrahmen. Das ist
/// der schaerfere Fall und der Grund fuer die Uebernahme durch Benutzung.
BOOST_FIXTURE_TEST_CASE(APluggedPadDoesNotDisturbTheMouse, MenuPadFixture)
{
    WINDOWMANAGER.Switch(std::make_unique<dskMainMenu>());
    frame();
    connect(9);
    frame();
    BOOST_TEST_REQUIRE(focused(0) == nullptr); // kein Rahmen

    auto* bt = desktop()->GetCtrl<ctrlButton>(ID_btSingleplayer);
    mouseClick(bt->GetDrawPos() + Position(bt->GetSize() / 2u));
    frame();
    BOOST_TEST(desktopAs<dskSinglePlayer>() != nullptr);
}

/// STRUKTURELL: ein reiner Padframe erzeugt KEINE einzige Maus- oder Tastaturnachricht, und die
/// Mausposition bleibt stehen.
BOOST_FIXTURE_TEST_CASE(APadFrameProducesNoMouseOrKeyboardMessages, MenuPadFixture)
{
    auto* dsk = static_cast<CountingDesktop*>(WINDOWMANAGER.Switch(std::make_unique<CountingDesktop>()));
    frame();
    const Position mouseBefore = VIDEODRIVER.GetMousePos();
    const unsigned moveBefore = dsk->numMouseMove;

    pickUp(3);
    press(3, NextCtrl);
    press(3, Activate);

    BOOST_TEST(dsk->numMouseMove == moveBefore); // der Desktopwechsel-Dummy zaehlt hier nicht mit
    BOOST_TEST(dsk->numLeftDown == 0u);
    BOOST_TEST(dsk->numLeftUp == 0u);
    BOOST_TEST(dsk->numRightDown == 0u);
    BOOST_TEST(dsk->numKeyDown == 0u);
    BOOST_TEST(VIDEODRIVER.GetMousePos() == mouseBefore);

    // GEGENPROBE: die Zaehler zaehlen ueberhaupt.
    mouseClick(Position(400, 300));
    BOOST_TEST(dsk->numLeftDown == 1u);
    BOOST_TEST(dsk->numLeftUp == 1u);
}

BOOST_AUTO_TEST_SUITE_END()
