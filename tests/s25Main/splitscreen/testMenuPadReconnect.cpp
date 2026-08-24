// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

// BEFUND 1: Ein einziges Abziehen des Pads darf die Gamepadsteuerung nicht fuer den Rest der
// Programmlaufzeit unbrauchbar machen.
//
// Der Geraetebestand ist FLANKENBASIERT: der Treiber meldet ein Connected genau einmal
// (VideoSDL2.cpp:724, SDL_CONTROLLERDEVICEADDED) und danach nie wieder. Wer diese Flanke
// verpasst, kann das Geraet nicht nachtraeglich erfragen - es gibt keine Bestandsabfrage.
// Genau deshalb ist die Frage "WER leert die Warteschlange" hier keine Nebensache: waehrend
// einer Partie tut es dskGameInterface, ausserhalb der WindowManager.
//
// Alle Faelle unten nehmen den produktiven Zustellpfad. Die EINZIGE Naht liegt beim Treiber
// (MockupVideoDriver::padEvents_ -> IVideoDriver::FetchPadEvents); es wird nirgends ein Router,
// ein FocusPath oder ein Msg_ButtonClick von Hand angefasst.

#include "MenuPadFixture.h"
#include "PadFixture.h"
#include "Loader.h"
#include "WindowManager.h"
#include "desktops/Desktop.h"
#include "desktops/dskMainMenu.h"
#include "drivers/VideoDriverWrapper.h"
#include "helpers/containerUtils.h"
#include "input/MenuPadInput.h"
#include "input/PadRouter.h"
#include "network/GameClient.h"
#include "worldFixtures/CreateEmptyWorld.h"
#include "worldFixtures/WorldFixture.h"
#include <boost/test/unit_test.hpp>
#include <memory>
#include <vector>

namespace {

constexpr PadButton PickUpButton = PadButton::Start;

/// Die Maus liegt bewusst ausserhalb jeder Ansicht - wie in PadGameFixture.
const Position kMouseOffScreen{-10000, -10000};

/// Der Platzhalter fuer dskGameLoader: ein Bildschirm, der KEINE Padeingaben will und die
/// Warteschlange des Treibers deshalb nicht leert. Genau in diesem Zustand entsteht in
/// Produktion das dskGameInterface (dskGameLoader.cpp:118).
struct LoadingDesktop : Desktop
{
    LoadingDesktop() : Desktop(nullptr) {}
};

/// Der volle Weg Menue -> Partie -> Menue mit den ECHTEN Bildschirmen und dem echten
/// WindowManager.
///
/// WAS HIER NICHT ATTRAPPE IST: dskMainMenu, dskGameInterface, WindowManager::Draw samt
/// PumpPadInput und Desktopwechsel, MenuPadInput, beide PadRouter.
///
/// DIE EINE ERSETZUNG, und sie ist benannt: dskGameInterface::Msg_PaintBefore zeichnet Rahmen
/// und Statuen aus den ORIGINALEN S2-Daten und ist im Test nicht ausfuehrbar
/// (TestableGameInterface stellt es deshalb still). Der Aufruf, den es sonst weiterreicht -
/// Run() -> UpdateInput() -, macht der Test in demselben Frame selbst. UpdateInput ist die
/// Stelle, an der die Partie ihre Padereignisse abholt, und damit genau das Stueck
/// Produktivcode, um das es hier geht.
struct GameAndMenuPadFixture : rttr::test::MenuPadFixture
{
    WorldFixture<CreateEmptyWorld, 2, 60, 30> worldFixture;
    std::vector<uint8_t> oldAdditional_;
    rttr::test::TestableGameInterface* gi_ = nullptr;

    GameAndMenuPadFixture() : oldAdditional_(GAMECLIENT.GetAdditionalLocalPlayers())
    {
        // Ohne die Dummy-Kartengrafiken ist Loader::map_gfx ein Nullzeiger und der
        // Spielbildschirm nicht baubar.
        LOADER.LoadDummyMapFiles();
        GAMECLIENT.SetAdditionalLocalPlayers({});
    }

    ~GameAndMenuPadFixture() override
    {
        // Der Spielbildschirm haelt einen shared_ptr auf die Welt und traegt sich dort ein. Er
        // muss weg sein, BEVOR worldFixture stirbt - Member sterben vor der Basisklasse, deren
        // Destruktor sonst erst danach aufraeumen wuerde.
        dropGameDesktop();
        GAMECLIENT.SetAdditionalLocalPlayers(oldAdditional_);
    }

    void dropGameDesktop()
    {
        if(!gi_)
            return;
        WINDOWMANAGER.Switch(std::make_unique<LoadingDesktop>());
        gi_ = nullptr;
        WINDOWMANAGER.Draw();
    }

    void toMainMenu()
    {
        WINDOWMANAGER.Switch(std::make_unique<dskMainMenu>());
        gi_ = nullptr;
        frame();
        BOOST_TEST_REQUIRE(desktopAs<dskMainMenu>() != nullptr);
    }

    /// Der Moment, in dem eine Partie beginnt - in derselben Reihenfolge wie in Produktion:
    /// erst der Ladebildschirm, dann der Spielbildschirm.
    void enterGame()
    {
        WINDOWMANAGER.Switch(std::make_unique<LoadingDesktop>());
        gi_ = nullptr;
        frame();
        auto dsk = std::make_unique<rttr::test::TestableGameInterface>(
          worldFixture.game, std::shared_ptr<const NWFInfo>(), 0u, /*initOGL*/ false);
        gi_ = dsk.get();
        WINDOWMANAGER.Switch(std::move(dsk));
        frame();
        BOOST_TEST_REQUIRE(WINDOWMANAGER.GetCurrentDesktop() == static_cast<Desktop*>(gi_));
    }

    /// Ein Frame WAEHREND der Partie. WindowManager::Draw holt hier bewusst nichts ab
    /// (dskGameInterface::WantsPadInput() == false); abgeholt wird in UpdateInput.
    void gameFrame()
    {
        BOOST_TEST_REQUIRE(gi_ != nullptr);
        video.tickCount_ += frameMs;
        WINDOWMANAGER.Draw();
        gi_->UpdateInput(frameMs, kMouseOffScreen);
    }

    const PadRouter& gameRouter() const { return gi_->GetPadRouter(); }

    static bool menuKnows(PadDeviceId dev) { return helpers::contains(router().GetDevices(), dev); }
};

} // namespace

BOOST_AUTO_TEST_SUITE(MenuPadReconnectTests)

/// GEGENPROBE ZUERST: im Menue allein funktioniert Abziehen und Wiederanstecken. Ohne diesen
/// Fall koennte man den Befund fuer ein allgemeines Problem des Wiederansteckens halten - er
/// ist es nicht, er haengt am Wechsel in die Partie und zurueck.
BOOST_FIXTURE_TEST_CASE(UnpluggingAndReplugginInTheMenuKeepsTheMenuUsable, GameAndMenuPadFixture)
{
    constexpr PadDeviceId before = 61;
    constexpr PadDeviceId after = 62; // SDL vergibt beim Wiederanstecken eine NEUE Instanz-Id

    toMainMenu();
    pickUp(before);
    BOOST_TEST_REQUIRE(router().GetSlot(before) == 0u);

    disconnect(before);
    frame();
    pickUp(after);
    BOOST_TEST(router().GetSlot(after) == 0u);
    BOOST_TEST(focused(0) != nullptr);
    BOOST_TEST(!menuKnows(before));
}

/// DER BEFUND: Kabel raus und wieder rein WAEHREND der Partie. Danach muss das Pad das Menue
/// bedienen koennen - sonst kommt der Spieler ohne Neustart des Programms nirgends mehr hin.
BOOST_FIXTURE_TEST_CASE(APadReconnectedDuringAGameStillDrivesTheMenu, GameAndMenuPadFixture)
{
    constexpr PadDeviceId before = 63;
    constexpr PadDeviceId after = 64;

    toMainMenu();
    pickUp(before);
    BOOST_TEST_REQUIRE(router().GetSlot(before) == 0u);

    enterGame();
    BOOST_TEST_REQUIRE(gameRouter().GetSlot(before) == 0u);

    // Kabel raus, Kabel rein - MITTEN in der Partie.
    disconnect(before);
    gameFrame();
    connect(after);
    tap(after, PickUpButton);
    gameFrame();
    // In der Partie selbst geht es sofort wieder: dort holt dskGameInterface ja ab.
    BOOST_TEST_REQUIRE(gameRouter().GetSlot(after) == 0u);

    // Die Partie ist zu Ende.
    toMainMenu();

    // Und jetzt das, was jeder erwartet, der ein Kabel wieder eingesteckt hat.
    tap(after, PickUpButton);
    frame();
    BOOST_TEST(router().GetSlot(after) == 0u);
    BOOST_TEST(focused(0) != nullptr);
    // Das abgezogene Geraet steht nicht mehr im Bestand des Menues.
    BOOST_TEST(!menuKnows(before));
}

/// Dieselbe Trennung, eine Runde weiter: auch die NAECHSTE Partie muss das wieder angesteckte
/// Pad annehmen. Ohne diesen Fall bliebe offen, ob nur das Menue oder auch die Uebergabe an die
/// Partie betroffen ist.
BOOST_FIXTURE_TEST_CASE(APadReconnectedDuringAGameDrivesTheNextGame, GameAndMenuPadFixture)
{
    constexpr PadDeviceId before = 65;
    constexpr PadDeviceId after = 66;

    toMainMenu();
    pickUp(before);
    enterGame();
    BOOST_TEST_REQUIRE(gameRouter().GetSlot(before) == 0u);

    disconnect(before);
    gameFrame();
    connect(after);
    tap(after, PickUpButton);
    gameFrame();
    BOOST_TEST_REQUIRE(gameRouter().GetSlot(after) == 0u);

    toMainMenu();
    tap(after, PickUpButton);
    frame();

    // Zweite Partie.
    enterGame();
    BOOST_TEST(gameRouter().GetSlot(after) == 0u);
    BOOST_TEST(gameRouter().GetSlot(before) == PadRouter::NoSlot);
}

/// Die andere Haelfte desselben Befunds: das abgezogene Pad kommt NICHT wieder. Sein Geist darf
/// den Platz nicht besetzt halten, sonst ist auch ein zweites, voellig gesundes Pad im Menue
/// wirkungslos.
BOOST_FIXTURE_TEST_CASE(APadUnpluggedDuringAGameDoesNotBlockAnotherPadInTheMenu, GameAndMenuPadFixture)
{
    constexpr PadDeviceId gone = 67;
    constexpr PadDeviceId other = 68;

    toMainMenu();
    pickUp(gone);
    BOOST_TEST_REQUIRE(router().GetSlot(gone) == 0u);

    enterGame();
    BOOST_TEST_REQUIRE(gameRouter().GetSlot(gone) == 0u);
    disconnect(gone);
    gameFrame();
    BOOST_TEST_REQUIRE(gameRouter().GetSlot(gone) == PadRouter::NoSlot);

    toMainMenu();

    // Ein ANDERES Pad wird angesteckt und benutzt. Der einzige Platz ist frei.
    pickUp(other);
    BOOST_TEST(router().GetSlot(other) == 0u);
    BOOST_TEST(!menuKnows(gone));
}

BOOST_AUTO_TEST_SUITE_END()
