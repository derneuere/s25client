// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "WindowManager.h"
#include "controls/ctrlButton.h"
#include "desktops/PlayerView.h"
#include "desktops/dskGameInterface.h"
#include "driver/MouseCoords.h"
#include "drivers/VideoDriverWrapper.h"
#include "ingameWindows/IngameWindow.h"
#include "input/FocusPath.h"
#include "mockupDrivers/MockupVideoDriver.h"
#include "world/GameWorld.h"
#include "world/MapBase.h"
#include "PadFixture.h"
#include "WindowOwnerFixture.h"
#include "PointOutput.h"
#include "gameData/const_gui_ids.h"
#include <boost/test/unit_test.hpp>
#include <array>
#include <memory>
#include <string>

using rttr::test::OwnedWnd;
using rttr::test::PadViewFixture;

namespace {

/// Oeffnet ein Fenster auf dem Weg, den der Produktivcode nimmt: unter der Klammer der Ansicht,
/// die es oeffnet (dskGameInterface::ViewScope). Die Erzeugungsstelle nennt - wie im Spiel -
/// keinen Spieler.
OwnedWnd& openFor(unsigned viewIdx, unsigned guiId, bool& aliveFlag, const DrawPoint& pos = DrawPoint(0, 0))
{
    const dskGameInterface::ViewScope ownerScope(viewIdx);
    return WINDOWMANAGER.Show(std::make_unique<OwnedWnd>(guiId, aliveFlag, pos));
}

/// Umschalten auf demselben Weg: ToggleWindow, ohne Spielerangabe an der Aufrufstelle.
OwnedWnd* toggleFor(unsigned viewIdx, unsigned guiId, bool& aliveFlag, const DrawPoint& pos = DrawPoint(0, 0))
{
    const dskGameInterface::ViewScope ownerScope(viewIdx);
    return WINDOWMANAGER.ToggleWindow(std::make_unique<OwnedWnd>(guiId, aliveFlag, pos));
}

/// Ein vollstaendiger Mausklick auf einen Knopf, ueber den ECHTEN Einstieg des WindowManagers
/// (Msg_LeftDown -> findAndActivateWindow -> RelayMouseMessage). Ein direktes bt->Msg_LeftDown
/// waere am gepruefften Pfad vorbei.
void clickButton(MockupVideoDriver& video, ctrlButton& bt)
{
    const Rect r = bt.GetDrawRect();
    const Position center = r.getOrigin() + Position(r.getSize() / 2u);
    // Weit genug weg vom letzten Klick, damit daraus kein Doppelklick wird.
    video.tickCount_ += 5000;
    MouseCoords down(center);
    down.ldown = true;
    WINDOWMANAGER.Msg_LeftDown(down);
    WINDOWMANAGER.Msg_LeftUp(MouseCoords(center));
}

/// Der Kern, fuer JEDE Ansichtszahl gleich. Der Einzelspieler ist damit ein Datenpunkt und kein
/// Sonderfall - er kann nicht mehr stillschweigend aus der Abdeckung fallen.
template<unsigned T_numViews>
void checkEveryViewOwnsItsOwnWindowOfTheSameType()
{
    BOOST_TEST_CONTEXT("numViews = " << T_numViews)
    {
        PadViewFixture<T_numViews> f;
        std::array<bool, T_numViews> alive{};
        std::array<OwnedWnd*, T_numViews> wnds{};
        for(unsigned i = 0; i < T_numViews; ++i)
            wnds[i] = &openFor(i, CGI_MAINSELECTION, alive[i], DrawPoint(10 + 10 * i, 10 + 10 * i));
        WINDOWMANAGER.Draw();

        for(unsigned i = 0; i < T_numViews; ++i)
        {
            BOOST_TEST_CONTEXT("view " << i)
            {
                BOOST_TEST(alive[i]);
                BOOST_TEST(wnds[i]->GetOwner() == i);
                BOOST_TEST(!wnds[i]->ShouldBeClosed());
            }
        }

        // Ansicht 0 schaltet ihr Fenster wieder aus. Nur ihres darf verschwinden.
        bool ignored = false;
        BOOST_TEST(toggleFor(0, CGI_MAINSELECTION, ignored) == static_cast<OwnedWnd*>(nullptr));
        WINDOWMANAGER.Draw();
        BOOST_TEST(!alive[0]);
        for(unsigned i = 1; i < T_numViews; ++i)
        {
            BOOST_TEST_CONTEXT("view " << i)
            {
                BOOST_TEST(alive[i]);
                const unsigned before = wnds[i]->paints;
                WINDOWMANAGER.Draw();
                BOOST_TEST(wnds[i]->paints == before + 1);
                wnds[i]->Close();
            }
        }
        WINDOWMANAGER.Draw();
    }
}

} // namespace

BOOST_AUTO_TEST_SUITE(WindowOwnershipTests)

/// DER Nachweis: zwei lokale Spieler halten GLEICHZEITIG ein Fenster derselben Art offen.
///
/// Vor dem Fensterbesitz schluesselten ToggleWindow/ReplaceWindow/Close ausschliesslich ueber
/// die global vergebene GUI_ID (gameData/const_gui_ids.h). Oeffnete Spieler 1 sein
/// Militaerfenster, fand FindNonModalWindow das von Spieler 0 und schloss es.
BOOST_FIXTURE_TEST_CASE(TwoViewsHoldTheSameWindowTypeOpenAtTheSameTime, PadViewFixture<2>)
{
    bool alive0 = false, alive1 = false;
    OwnedWnd& w0 = openFor(0, CGI_MAINSELECTION, alive0, DrawPoint(10, 10));
    OwnedWnd& w1 = openFor(1, CGI_MAINSELECTION, alive1, DrawPoint(300, 10));
    WINDOWMANAGER.Draw();

    BOOST_TEST(alive0);
    BOOST_TEST(alive1);
    BOOST_TEST(&w0 != &w1);
    BOOST_TEST(w0.GetOwner() == 0u);
    BOOST_TEST(w1.GetOwner() == 1u);
    BOOST_TEST(!w0.ShouldBeClosed());
    BOOST_TEST(!w1.ShouldBeClosed());

    // Nicht nur am Leben, sondern weiterhin gezeichnet - beide sind wirklich noch da.
    const unsigned p0 = w0.paints, p1 = w1.paints;
    WINDOWMANAGER.Draw();
    BOOST_TEST(w0.paints == p0 + 1);
    BOOST_TEST(w1.paints == p1 + 1);
}

/// Die schaerfere Haelfte: der Besitzer ist Teil des SCHLUESSELS und nicht bloss ein
/// zusaetzliches Feld. Schaltet Spieler 0 sein Fenster aus, bleibt das von Spieler 1 offen.
BOOST_FIXTURE_TEST_CASE(TogglingClosesOnlyTheTogglingViewsOwnWindow, PadViewFixture<2>)
{
    bool alive0 = false, alive1 = false, ignored = false;
    OwnedWnd& w1 = *toggleFor(1, CGI_MAINSELECTION, alive1, DrawPoint(300, 10));
    BOOST_TEST_REQUIRE(toggleFor(0, CGI_MAINSELECTION, alive0, DrawPoint(10, 10)) != static_cast<OwnedWnd*>(nullptr));
    WINDOWMANAGER.Draw();
    BOOST_TEST_REQUIRE(alive0);
    BOOST_TEST_REQUIRE(alive1);

    BOOST_TEST(toggleFor(0, CGI_MAINSELECTION, ignored) == static_cast<OwnedWnd*>(nullptr));
    WINDOWMANAGER.Draw();
    BOOST_TEST(!alive0);
    BOOST_TEST(alive1);
    const unsigned before = w1.paints;
    WINDOWMANAGER.Draw();
    BOOST_TEST(w1.paints == before + 1);
}

BOOST_FIXTURE_TEST_CASE(ClosingOneViewsWindowLeavesTheOtherViewsWindowUntouched, PadViewFixture<2>)
{
    bool alive0 = false, alive1 = false;
    OwnedWnd& w0 = openFor(0, CGI_MAINSELECTION, alive0, DrawPoint(10, 10));
    OwnedWnd& w1 = openFor(1, CGI_MAINSELECTION, alive1, DrawPoint(300, 10));
    WINDOWMANAGER.Draw();

    w0.Close();
    WINDOWMANAGER.Draw();
    BOOST_TEST(!alive0);
    BOOST_TEST(alive1);
    const unsigned before = w1.paints;
    WINDOWMANAGER.Draw();
    BOOST_TEST(w1.paints == before + 1);
}

/// Close(id, owner) trifft genau eine Ansicht, CloseAll(id) alle. Das ist die Aufspaltung, ohne
/// die ein Weltereignis (Gebaeude zerstoert) bei den uebrigen Spielern ein Fenster auf ein
/// nicht mehr vorhandenes Gebaeude stehen liesse.
BOOST_FIXTURE_TEST_CASE(CloseHitsOneViewWhileCloseAllHitsEveryView, PadViewFixture<2>)
{
    bool a0 = false, a1 = false;
    const unsigned id = CGI_BUILDING + MapBase::CreateGUIID(MapPoint(5, 7));
    openFor(0, id, a0, DrawPoint(10, 10));
    OwnedWnd& w1 = openFor(1, id, a1, DrawPoint(300, 10));
    WINDOWMANAGER.Draw();

    WINDOWMANAGER.Close(id, 0);
    WINDOWMANAGER.Draw();
    BOOST_TEST(!a0);
    BOOST_TEST(a1);
    BOOST_TEST(w1.GetOwner() == 1u);

    bool a0b = false;
    openFor(0, id, a0b, DrawPoint(10, 10));
    WINDOWMANAGER.Draw();
    BOOST_TEST_REQUIRE(a0b);
    WINDOWMANAGER.CloseAll(id);
    WINDOWMANAGER.Draw();
    BOOST_TEST(!a0b);
    BOOST_TEST(!a1);
}

/// Skalierung, und zugleich der Beweis, dass der Schluessel nicht bloss "Haupt- oder
/// Nichthauptspieler" unterscheidet.
BOOST_FIXTURE_TEST_CASE(FourViewsHoldFourWindowsOfTheSameTypeOpen, PadViewFixture<4>)
{
    std::array<bool, 4> alive{};
    std::array<OwnedWnd*, 4> wnds{};
    for(unsigned i = 0; i < 4; ++i)
        wnds[i] = &openFor(i, CGI_MAINSELECTION, alive[i], DrawPoint(10 + 150 * i, 10));
    WINDOWMANAGER.Draw();
    for(unsigned i = 0; i < 4; ++i)
    {
        BOOST_TEST_CONTEXT("view " << i)
        {
            BOOST_TEST(alive[i]);
            BOOST_TEST(wnds[i]->GetOwner() == i);
        }
    }
    for(unsigned i = 0; i < 4; ++i)
        for(unsigned j = i + 1; j < 4; ++j)
            BOOST_TEST(wnds[i] != wnds[j]);
}

/// Derselbe Kern fuer eine, zwei und vier Ansichten. Der Einzelspieler wird hier ausdruecklich
/// EINGESCHLOSSEN statt im Namen ausgeschlossen: eine zusaetzliche Ansichtszahl kostet eine
/// Zeile und keine Umbenennung.
BOOST_AUTO_TEST_CASE(EveryViewOwnsItsOwnWindowForOneTwoAndFourViews)
{
    checkEveryViewOwnsItsOwnWindowOfTheSameType<1>();
    checkEveryViewOwnsItsOwnWindowOfTheSameType<2>();
    checkEveryViewOwnsItsOwnWindowOfTheSameType<4>();
}

/// Y betritt das oberste Fenster DIESES Spielers. Ohne Besitzerbezug landete Spieler 1 im
/// Fenster von Spieler 0 - und verstellte es anschliessend in seinem eigenen Namen, weil die
/// Klammer aus Phase 4b auf ihn selbst zeigt.
BOOST_FIXTURE_TEST_CASE(EachViewEntersItsOwnTopMostWindowWithY, PadViewFixture<2>)
{
    bool alive0 = false, alive1 = false;
    OwnedWnd& w0 = openFor(0, CGI_MAINSELECTION, alive0, DrawPoint(10, 10));
    OwnedWnd& w1 = openFor(1, CGI_MAINSELECTION, alive1, DrawPoint(300, 10));
    WINDOWMANAGER.Draw();
    // w1 liegt oben - vor der Korrektur betraten beide Spieler genau dieses eine Fenster.
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetTopMostWindow() == static_cast<IngameWindow*>(&w1));

    pads.pickUp(10);
    pads.pickUp(11);
    step(16);
    pads.tap(10, PadButton::Y);
    pads.tap(11, PadButton::Y);
    step(16);

    BOOST_TEST_REQUIRE(view(0).GetFocus().IsActive());
    BOOST_TEST_REQUIRE(view(1).GetFocus().IsActive());
    BOOST_TEST(view(0).GetFocus().GetRoot() == static_cast<Window*>(&w0));
    BOOST_TEST(view(1).GetFocus().GetRoot() == static_cast<Window*>(&w1));
}

/// Ein Fenster ohne Besitzer gehoert dem Bildschirm: Nachrichtenboxen, Chat, Systemfenster.
/// Es darf NICHT je Ansicht vervielfacht werden und jeder muss es bedienen koennen.
BOOST_FIXTURE_TEST_CASE(AWindowWithoutAnOwnerCanBeEnteredFromEveryView, PadViewFixture<2>)
{
    bool aliveShared = false;
    // Ohne jede Klammer erzeugt - genau so entstehen iwMsgbox aus CI_Error und iwVictory.
    auto& shared = WINDOWMANAGER.Show(std::make_unique<OwnedWnd>(CGI_MSGBOX, aliveShared, DrawPoint(100, 100)));
    WINDOWMANAGER.Draw();
    BOOST_TEST_REQUIRE(shared.GetOwner() == SHARED_WINDOW_OWNER);

    pads.pickUp(10);
    pads.pickUp(11);
    step(16);
    pads.tap(10, PadButton::Y);
    pads.tap(11, PadButton::Y);
    step(16);

    BOOST_TEST(view(0).GetFocus().GetRoot() == static_cast<Window*>(&shared));
    BOOST_TEST(view(1).GetFocus().GetRoot() == static_cast<Window*>(&shared));
}

/// Der Hebel, mit dem die ueber 100 Erzeugungsstellen unveraendert bleiben koennen: ein Fenster,
/// das AUS EINEM ANDEREN Fenster heraus geoeffnet wird - ohne Elternzeiger, ohne Spielerangabe,
/// genau wie iwMainMenu es fuer iwMilitary tut -, erbt dessen Besitzer.
///
/// Gemessen ueber den MAUSpfad, nicht ueber das Pad: der Mauspfad hatte bis hierher gar keine
/// Klammer, und genau dort ist die Vererbung neu.
BOOST_FIXTURE_TEST_CASE(AWindowOpenedFromInsideAnotherWindowInheritsItsOwner, PadViewFixture<2>)
{
    bool aliveParent = false, aliveChild = false;
    OwnedWnd& parent = openFor(1, CGI_MAINSELECTION, aliveParent, DrawPoint(300, 100));
    parent.OpenChildOnClick(CGI_HELP, aliveChild);
    WINDOWMANAGER.Draw();

    auto* bt = parent.GetCtrl<ctrlButton>(1);
    BOOST_TEST_REQUIRE(bt != static_cast<ctrlButton*>(nullptr));
    clickButton(pads.video, *bt);
    WINDOWMANAGER.Draw();

    BOOST_TEST_REQUIRE(parent.clicks == 1u);
    BOOST_TEST_REQUIRE(aliveChild);
    BOOST_TEST_REQUIRE(parent.child != static_cast<OwnedWnd*>(nullptr));
    BOOST_TEST(parent.child->GetOwner() == 1u);
}

/// Der Einzelspieler mit Maus, in ALLEN Padzustaenden - der Zustand ist eine Kontextschleife im
/// Test und keine Einschraenkung im Namen. Geprueft wird der volle Weg auf und zu, ueber den
/// echten Mauseinstieg.
BOOST_FIXTURE_TEST_CASE(OneLocalViewOpensTogglesAndClosesItsWindowWhateverThePadState, PadViewFixture<1>)
{
    for(const char* padState : {"kein Pad", "gesteckt", "benutzt"})
    {
        BOOST_TEST_CONTEXT("Padzustand: " << padState)
        {
            if(std::string(padState) == "gesteckt")
                pads.connect(20);
            else if(std::string(padState) == "benutzt")
                pads.pickUp(21);
            step(16);

            bool alive = false, ignored = false;
            OwnedWnd* wnd = toggleFor(0, CGI_MAINSELECTION, alive, DrawPoint(50, 50));
            WINDOWMANAGER.Draw();
            BOOST_TEST_REQUIRE(wnd != static_cast<OwnedWnd*>(nullptr));
            BOOST_TEST(alive);
            BOOST_TEST(wnd->GetOwner() == 0u);
            BOOST_TEST(WINDOWMANAGER.GetTopMostWindow() == static_cast<IngameWindow*>(wnd));

            auto* bt = wnd->GetCtrl<ctrlButton>(1);
            BOOST_TEST_REQUIRE(bt != static_cast<ctrlButton*>(nullptr));
            clickButton(pads.video, *bt);
            BOOST_TEST(wnd->clicks == 1u);

            BOOST_TEST(toggleFor(0, CGI_MAINSELECTION, ignored) == static_cast<OwnedWnd*>(nullptr));
            WINDOWMANAGER.Draw();
            BOOST_TEST(!alive);
            BOOST_TEST(WINDOWMANAGER.GetTopMostWindow() == static_cast<IngameWindow*>(nullptr));
        }
    }
}

/// Lebensdauer: ein Fenster ueberlebt den Desktop, dem die Ansichten gehoerten.
///
/// Die Fokusrahmen zeigen VOM Fenster AUF den FocusPath der Ansicht. Sterben die Ansichten
/// zuerst - Partieende, Desktopwechsel, Spielabbruch -, bleibt das Fenster in der Liste des
/// WindowManagers stehen und dereferenziert seine Rahmen spaeter in ~IngameWindow. Ohne
/// Abmeldung ist das ein Zugriff auf freigegebenen Speicher; er faellt nur manchmal auf, was
/// ihn schlimmer macht und nicht harmloser.
BOOST_AUTO_TEST_CASE(AWindowDropsTheFocusRingsOfViewsThatDieBeforeIt)
{
    bool alive = false;
    {
        PadViewFixture<2> f;
        OwnedWnd& w = openFor(1, CGI_MAINSELECTION, alive, DrawPoint(300, 10));
        WINDOWMANAGER.Draw();
        // Reihenfolge zaehlt: der PadRouter vergibt die Ansichten in der Reihenfolge der
        // Benutzung. Pad 10 nimmt Ansicht 0, Pad 11 danach Ansicht 1.
        f.pads.pickUp(10);
        f.step(16);
        f.pads.pickUp(11);
        f.step(16);
        f.pads.tap(11, PadButton::Y);
        f.step(16);
        BOOST_TEST_REQUIRE(f.view(1).GetFocus().GetRoot() == static_cast<Window*>(&w));
        BOOST_TEST_REQUIRE(w.GetNumFocusRings() == 1u);

        // Der Desktop und mit ihm alle Ansichten sterben; das Fenster bleibt stehen.
        f.dsk.reset();
        f.worldFixture.world.SetGameInterface(nullptr);
        BOOST_TEST_REQUIRE(alive);
        BOOST_TEST(w.GetNumFocusRings() == 0u);
    }
    // Der Fixture-Destruktor wechselt den Desktop und raeumt das Fenster ab - erst hier
    // dereferenziert ~IngameWindow seine Rahmen.
    BOOST_TEST(!alive);
}

BOOST_AUTO_TEST_SUITE_END()
