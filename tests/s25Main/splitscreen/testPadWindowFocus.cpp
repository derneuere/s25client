// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

// Phase 4, Schritt 2 am durchgaengigen Pfad: ein Padspieler betritt ein Fenster, navigiert
// darin und loest einen Knopf aus - ueber GENAU den Weg, den auch der SDL2-Treiber nimmt
// (MockupVideoDriver::padEvents_ -> dskGameInterface::UpdateInput -> IVideoDriver::FetchPadEvents
// -> PadRouter -> PlayerView::GetFocus()).
//
// Was hier NOCH NICHT geprueft werden kann: dass jeder Spieler SEIN eigenes Fenster hat. Die
// Fensterliste des WindowManagers ist bis auf Weiteres gemeinsam (Schritt 3 dieser Phase).
// Geprueft wird deshalb das, was heute wahr ist: zwei Spieler in DEMSELBEN Fenster haben
// trotzdem zwei unabhaengige Fokusse.

#include "PadFixture.h"
#include "WindowManager.h"
#include "controls/ctrlEdit.h"
#include "controls/ctrlTextButton.h"
#include "desktops/PlayerView.h"
#include "ingameWindows/IngameWindow.h"
#include "driver/KeyEvent.h"
#include "input/FocusPath.h"
#include "Loader.h"
#include <boost/test/unit_test.hpp>
#include <vector>

using namespace rttr::test;

namespace {
/// Fenster mit zwei Knoepfen, das sich merkt, welcher gedrueckt wurde.
struct ButtonWnd : IngameWindow
{
    ButtonWnd()
        : IngameWindow(CGI_HELP, DrawPoint(0, 0), Extent(200, 120), "", nullptr, false, CloseBehavior::Regular)
    {
        AddTextButton(1, DrawPoint(10, 10), Extent(80, 20), TextureColor::Green1, "A", NormalFont);
        AddTextButton(2, DrawPoint(10, 50), Extent(80, 20), TextureColor::Green1, "B", NormalFont);
    }
    std::vector<unsigned> clicks;
    void Msg_ButtonClick(unsigned id) override { clicks.push_back(id); }
};

/// Fenster mit einem Textfeld zwischen zwei Knoepfen - der Bauform von iwTrade oder iwSave.
struct EditWnd : IngameWindow
{
    EditWnd() : IngameWindow(CGI_HELP, DrawPoint(0, 0), Extent(200, 160), "", nullptr, false, CloseBehavior::Regular)
    {
        // Alle drei exakt uebereinander (gleiche Mitte in x): der senkrechte Schritt der
        // Fokusnavigation MUESSTE hier ueber das Textfeld fuehren, wenn es eine Station waere.
        AddTextButton(1, DrawPoint(10, 10), Extent(120, 20), TextureColor::Green1, "A", NormalFont);
        AddEdit(2, DrawPoint(10, 50), Extent(120, 22), TextureColor::Green1, NormalFont);
        AddTextButton(3, DrawPoint(10, 90), Extent(120, 20), TextureColor::Green1, "B", NormalFont);
    }
    std::vector<unsigned> clicks;
    void Msg_ButtonClick(unsigned id) override { clicks.push_back(id); }
};

/// Vollausschlag des linken Sticks nach unten, lange genug fuer genau einen Rasterschritt.
void stickDown(PadFeeder& pads, PadDeviceId dev)
{
    pads.axis(dev, PadAxis::LeftY, 1.f);
}
void stickCenter(PadFeeder& pads, PadDeviceId dev)
{
    pads.axis(dev, PadAxis::LeftY, 0.f);
}
} // namespace

BOOST_AUTO_TEST_SUITE(PadWindowFocus)

// --------------------------------------------------------------------------------------------
// HARTE RANDBEDINGUNG, und zwar in der Fassung, die den kritischen Fall EINSCHLIESST:
// eine Ansicht, ein Mausspieler - und zwar in allen drei Padzustaenden (keins, gesteckt,
// benutzt). Ein offenes Fenster darf daran nichts aendern, solange niemand Y drueckt.
// --------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(SingleViewIsUnaffectedWhateverThePadState, PadViewFixture<1>)
{
    auto& wnd = static_cast<ButtonWnd&>(WINDOWMANAGER.Show(std::make_unique<ButtonWnd>()));

    struct Case
    {
        const char* name;
        bool connect;
        bool use;
    };
    for(const Case& c : {Case{"keinPad", false, false}, Case{"padGesteckt", true, false},
                         Case{"padBenutzt", true, true}})
    {
        BOOST_TEST_CONTEXT(c.name)
        {
            if(c.connect)
                pads.connect(10);
            if(c.use)
                pads.tap(10, PadButton::Start);
            step(16);

            // Kein Fokus. Weder das blosse Anstecken noch das In-die-Hand-Nehmen (Start)
            // nimmt dem Mausspieler sein Fenster ab. Nur ein bewusster Druck auf Y taete das.
            BOOST_TEST(!view(0).GetFocus().IsActive());

            // Der Mausklick auf den Knopf loest weiterhin aus - genau wie ohne Phase 4.
            const auto numBefore = wnd.clicks.size();
            auto* bt = wnd.GetCtrl<ctrlButton>(1);
            BOOST_TEST_REQUIRE(bt != nullptr);
            const MouseCoords onBt(bt->GetDrawPos() + DrawPoint(5, 5));
            bt->Msg_LeftDown(onBt);
            bt->Msg_LeftUp(onBt);
            BOOST_TEST(wnd.clicks.size() == numBefore + 1u);
            BOOST_TEST(wnd.clicks.back() == 1u);
        }
    }
    wnd.Close();
    WINDOWMANAGER.Draw();
}

// --------------------------------------------------------------------------------------------
// Der durchgaengige Pfad: Y betritt das Fenster, der Stick navigiert, A loest aus.
// --------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(PadEntersWindowNavigatesAndClicks, PadViewFixture<1>)
{
    auto& wnd = static_cast<ButtonWnd&>(WINDOWMANAGER.Show(std::make_unique<ButtonWnd>()));

    // Pad in die Hand nehmen (Start, unveraendert wirkungslos), dann bewusst mit Y das
    // Fenster betreten.
    pads.pickUp(10);
    step(16);
    BOOST_TEST_REQUIRE(!view(0).GetFocus().IsActive());
    pads.tap(10, PadButton::Y);
    step(16);
    BOOST_TEST_REQUIRE(view(0).GetFocus().IsActive());
    BOOST_TEST_REQUIRE(view(0).GetFocus().GetRoot() == static_cast<Window*>(&wnd));
    BOOST_TEST(view(0).GetFocus().GetFocused()->GetID() == 1u);

    // Ein Rasterschritt nach unten. Der Stick liefert PadRouter::PixelsPerSecond Pixel/s, also
    // braucht die Rasterweite FocusPath::StepDistance eine entsprechende Zeit.
    stickDown(pads, 10);
    step(200);
    BOOST_TEST(view(0).GetFocus().GetFocused()->GetID() == 2u);
    stickCenter(pads, 10);
    step(16);

    // A loest den fokussierten Knopf aus - und legt KEINE Fahne, obwohl A das in der Welt taete.
    pads.tap(10, PadButton::A);
    step(16);
    BOOST_TEST_REQUIRE(wnd.clicks.size() == 1u);
    BOOST_TEST(wnd.clicks.back() == 2u);

    // B verlaesst das Fenster. Ab da wirkt A wieder auf die Welt.
    pads.tap(10, PadButton::B);
    step(16);
    BOOST_TEST(!view(0).GetFocus().IsActive());
    pads.tap(10, PadButton::A);
    step(16);
    BOOST_TEST(wnd.clicks.size() == 1u); // nichts dazugekommen

    wnd.Close();
    WINDOWMANAGER.Draw();
}

// --------------------------------------------------------------------------------------------
// Zwei Ansichten, ein Fenster, zwei Fokusse. Negativkontrolle M6 (ein globaler Fokus) faellt
// hier durch: Spieler 0 loese dann den Knopf von Spieler 1 aus.
// --------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(EachViewKeepsItsOwnFocus, PadViewFixture<2>)
{
    auto& wnd = static_cast<ButtonWnd&>(WINDOWMANAGER.Show(std::make_unique<ButtonWnd>()));

    pads.pickUp(10); // -> Slot 0
    step(16);
    pads.pickUp(11); // -> Slot 1
    step(16);
    pads.tap(10, PadButton::Y);
    step(16);
    pads.tap(11, PadButton::Y);
    step(16);
    BOOST_TEST_REQUIRE(view(0).GetFocus().IsActive());
    BOOST_TEST_REQUIRE(view(1).GetFocus().IsActive());

    // Spieler 1 wandert auf den zweiten Knopf, Spieler 0 bleibt auf dem ersten.
    stickDown(pads, 11);
    step(200);
    stickCenter(pads, 11);
    step(16);
    BOOST_TEST(view(1).GetFocus().GetFocused()->GetID() == 2u);
    BOOST_TEST(view(0).GetFocus().GetFocused()->GetID() == 1u);

    // Jeder loest SEINEN Knopf aus.
    pads.tap(10, PadButton::A);
    step(16);
    pads.tap(11, PadButton::A);
    step(16);
    BOOST_TEST_REQUIRE(wnd.clicks.size() == 2u);
    BOOST_TEST(wnd.clicks[0] == 1u);
    BOOST_TEST(wnd.clicks[1] == 2u);

    wnd.Close();
    WINDOWMANAGER.Draw();
}

// --------------------------------------------------------------------------------------------
// Lebensdauer: schliesst das Fenster, waehrend ein Spieler darin steht, darf kein toter Zeiger
// zurueckbleiben. Das ist die einzige Stelle, an der FocusPath::root_ haengen koennte.
// --------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(ClosingTheWindowReleasesTheFocus, PadViewFixture<1>)
{
    auto& wnd = static_cast<ButtonWnd&>(WINDOWMANAGER.Show(std::make_unique<ButtonWnd>()));
    pads.pickUp(10);
    step(16);
    pads.tap(10, PadButton::Y);
    step(16);
    BOOST_TEST_REQUIRE(view(0).GetFocus().IsActive());

    wnd.Close();
    WINDOWMANAGER.Draw(); // erst hier wird das Fenster wirklich freigegeben
    BOOST_TEST(!view(0).GetFocus().IsActive());
    BOOST_TEST(view(0).GetFocus().GetRoot() == static_cast<Window*>(nullptr));

    // Und das Pad wirkt wieder auf die Welt, ohne dass irgendetwas dereferenziert wird.
    pads.tap(10, PadButton::A);
    step(16);
}

// Ohne offenes Fenster tut Y nichts - insbesondere bleibt die Uebernahme durch Benutzung aus
// Phase 3 unveraendert.
BOOST_FIXTURE_TEST_CASE(EnterWithoutAWindowDoesNothing, PadViewFixture<1>)
{
    pads.pickUp(10);
    step(16);
    pads.tap(10, PadButton::Y);
    step(16);
    BOOST_TEST(!view(0).GetFocus().IsActive());
    BOOST_TEST(view(0).HasPadCursor()); // die Ansicht gehoert dem Pad, wie in Phase 3
}

// --------------------------------------------------------------------------------------------
// BEFUND B3: ein MINIMIERTES Fenster darf per Pad nicht bedienbar bleiben.
//
// EnterTopMostWindow prueft IsMinimized() nur beim Betreten. Minimiert der Mausspieler das
// Fenster danach, bedient der Padspieler ein Fenster, das er nicht sieht, und trifft Knoepfe,
// die gar nicht gezeichnet werden (IngameWindow::Draw_ zeichnet den Inhalt nur, wenn nicht
// minimiert; IsMessageRelayAllowed sperrt genau dafuer auch den Mauspfad).
// --------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(MinimizingReleasesThePadFocus, PadViewFixture<1>)
{
    auto& wnd = static_cast<ButtonWnd&>(WINDOWMANAGER.Show(std::make_unique<ButtonWnd>()));
    // Der Weltzeiger darf hier nicht auf dem eigenen HQ stehen. Die Ansicht startet dort
    // (PlayerView::MoveToOwnHQ), und A oeffnet inzwischen das Fenster des Gebaeudes unter dem
    // Zeiger - das A weiter unten oeffnete sonst ein zusaetzliches Fenster, und der Nachweis
    // maesse dieses statt des minimierten.
    aimPadAt(10, 0, MapPoint(5, 5));
    pads.tap(10, PadButton::Y);
    step(16);
    BOOST_TEST_REQUIRE(view(0).GetFocus().IsActive());

    // Der Mausspieler minimiert - jederzeit moeglich und vom Padspieler nicht zu verhindern.
    wnd.SetMinimized(true);
    step(16);
    BOOST_TEST(!view(0).GetFocus().IsActive());

    // Und ab jetzt trifft sein A keinen Knopf mehr.
    pads.tap(10, PadButton::A);
    step(16);
    BOOST_TEST(wnd.clicks.size() == 0u);

    // Auch neu betreten laesst sich ein minimiertes Fenster nicht (unveraendert seit Phase 4).
    pads.tap(10, PadButton::Y);
    step(16);
    BOOST_TEST(!view(0).GetFocus().IsActive());

    // Wiederhergestellt ist es wieder ganz normal bedienbar.
    wnd.SetMinimized(false);
    step(16);
    pads.tap(10, PadButton::Y);
    step(16);
    BOOST_TEST(view(0).GetFocus().IsActive());
    pads.tap(10, PadButton::A);
    step(16);
    BOOST_TEST(wnd.clicks.size() == 1u);

    wnd.Close();
    WINDOWMANAGER.Draw();
}

// --------------------------------------------------------------------------------------------
// BEFUND B4: ein abgezogenes Pad muss seinen Fokus mitnehmen.
//
// Sonst steht der Fokus weiter im Fenster, der Rahmen des verschwundenen Spielers bleibt am
// Fenster registriert - und das naechste Geraet, das den frei gewordenen Slot bekommt, erbt
// beides: sein erster A-Druck klickt einen Knopf, statt eine Fahne zu setzen.
// --------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(UnpluggingThePadReleasesTheFocus, PadViewFixture<1>)
{
    auto& wnd = static_cast<ButtonWnd&>(WINDOWMANAGER.Show(std::make_unique<ButtonWnd>()));
    pads.pickUp(10);
    step(16);
    pads.tap(10, PadButton::Y);
    step(16);
    BOOST_TEST_REQUIRE(view(0).GetFocus().IsActive());

    pads.disconnect(10);
    step(16);
    BOOST_TEST(!view(0).HasPadCursor()); // Phase 3, unveraendert
    BOOST_TEST(!view(0).GetFocus().IsActive());

    // Ein neues Geraet uebernimmt den frei gewordenen Slot - mit NEUER Kennung, so wie es ein
    // echter Treiber liefert.
    pads.pickUp(20);
    step(16);
    BOOST_TEST_REQUIRE(view(0).HasPadCursor());
    BOOST_TEST(!view(0).GetFocus().IsActive());

    // Sein erster A-Druck gehoert der Welt und nicht dem Fenster des Vorgaengers.
    pads.tap(20, PadButton::A);
    step(16);
    BOOST_TEST(wnd.clicks.size() == 0u);

    wnd.Close();
    WINDOWMANAGER.Draw();
}

// --------------------------------------------------------------------------------------------
// LUECKE M-F: die Fokusaufloesung in dskGameInterface::Msg_WindowClosed.
//
// Sie ist NICHT durch den Backstop im Destruktor von IngameWindow gedeckt, und deshalb faellt
// ihr Entfernen in ClosingTheWindowReleasesTheFocus nicht auf. Der Unterschied ist der
// ZEITPUNKT und die ABMELDUNG: Msg_WindowClosed laeuft, waehrend das Fenster noch vollstaendig
// lebt (WindowManager::DoClose haelt es bis nach dem Rueckruf am Leben), und meldet den Rahmen
// am Fenster ab. Der Destruktor kann nur noch den Fokus leeren - der Rahmen bleibt bis zum
// letzten Moment eingetragen, und der abgeleitete Teil des Fensters ist da schon zerstoert.
//
// Geprueft wird deshalb genau der Aufruf, den WindowManager::DoClose macht.
// --------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(WindowClosedNotificationReleasesFocusAndRing, PadViewFixture<1>)
{
    auto& wnd = static_cast<ButtonWnd&>(WINDOWMANAGER.Show(std::make_unique<ButtonWnd>()));
    pads.pickUp(10);
    step(16);
    pads.tap(10, PadButton::Y);
    step(16);
    BOOST_TEST_REQUIRE(view(0).GetFocus().IsActive());
    BOOST_TEST_REQUIRE(wnd.HasFocusRing(view(0).GetFocus()));

    // Das ist woertlich der Aufruf aus WindowManager::DoClose - das Fenster lebt dabei noch.
    dsk->Msg_WindowClosed(wnd);

    BOOST_TEST(!view(0).GetFocus().IsActive());
    BOOST_TEST(view(0).GetFocus().GetRoot() == static_cast<Window*>(nullptr));
    BOOST_TEST(!wnd.HasFocusRing(view(0).GetFocus())); // der Rahmen ist abgemeldet, nicht bloss tot

    wnd.Close();
    WINDOWMANAGER.Draw();
}

// --------------------------------------------------------------------------------------------
// Der Rahmen gehoert dem SPIELER, nicht dem Fenster. Zwei Spieler in demselben Fenster muessen
// zwei Rahmen bekommen, und verlaesst einer das Fenster, darf nur SEINER verschwinden.
//
// Mit einem einzigen Rahmenplatz je Fenster (der Stand vor dieser Runde) ueberschreibt der
// zweite Spieler beim Betreten den Rahmen des ersten, und das Aufloesen eines Fokus reisst den
// Rahmen des anderen mit weg - das ist der zweite Teil von B4 ("der Rahmen des verschwundenen
// Spielers bleibt am Fenster registriert und laesst sich von niemandem mehr entfernen").
// --------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(EachPlayerGetsHisOwnFocusRing, PadViewFixture<2>)
{
    auto& wnd = static_cast<ButtonWnd&>(WINDOWMANAGER.Show(std::make_unique<ButtonWnd>()));
    pads.pickUp(10);
    step(16);
    pads.pickUp(11);
    step(16);
    pads.tap(10, PadButton::Y);
    pads.tap(11, PadButton::Y);
    step(16);
    BOOST_TEST_REQUIRE(view(0).GetFocus().IsActive());
    BOOST_TEST_REQUIRE(view(1).GetFocus().IsActive());

    // Zwei Spieler, ein Fenster, ZWEI Rahmen.
    BOOST_TEST(wnd.HasFocusRing(view(0).GetFocus()));
    BOOST_TEST(wnd.HasFocusRing(view(1).GetFocus()));

    // Spieler 0 verlaesst das Fenster: nur SEIN Rahmen geht.
    pads.tap(10, PadButton::B);
    step(16);
    BOOST_TEST(!wnd.HasFocusRing(view(0).GetFocus()));
    BOOST_TEST(wnd.HasFocusRing(view(1).GetFocus()));

    // Und Spieler 1 wird davon in keiner Weise gestoert.
    pads.tap(11, PadButton::A);
    step(16);
    BOOST_TEST(wnd.clicks.size() == 1u);

    wnd.Close();
    WINDOWMANAGER.Draw();
}

/// ZWEITER BEFUND der Nachpruefung: ctrlEdit::Activate() setzte ein GLOBALES Fokusbit.
///
/// focus_ haengt am Control, nicht am Spieler, und es entscheidet, wer die Tastatur bekommt.
/// Ein Padspieler, der ein Eingabefeld aktiviert, nahm damit dem Mausspieler die Tastatur weg -
/// unsichtbar fuer beide. Die Entscheidung: das Textfeld ist keine Fokusstation der
/// Padnavigation mehr (ctrlEdit::CanFocus). Der Mauspfad bleibt unangetastet.
BOOST_FIXTURE_TEST_CASE(PadNeverTakesTheKeyboardFocusFromTheMousePlayer, PadViewFixture<2>)
{
    auto& wnd = static_cast<EditWnd&>(WINDOWMANAGER.Show(std::make_unique<EditWnd>()));
    auto* edit = wnd.GetCtrl<ctrlEdit>(2);
    BOOST_TEST_REQUIRE(edit != nullptr);

    // Der Mausspieler (Spieler 0) klickt in das Feld und tippt - der Normalfall.
    edit->SetFocus(true);
    BOOST_TEST_REQUIRE(edit->HasFocus());

    // Spieler 1 betritt dasselbe Fenster und geht es KOMPLETT durch, in beide Richtungen,
    // und drueckt auf jeder Station A.
    pads.pickUp(10); // nimmt Slot 0 und ruehrt sich danach nie wieder
    step(16);
    pads.pickUp(11); // nimmt Slot 1 - das ist der Padspieler dieses Tests
    step(16);
    pads.tap(11, PadButton::Y);
    step(16);
    BOOST_TEST_REQUIRE(view(1).GetFocus().IsActive());
    BOOST_TEST_REQUIRE(!view(0).GetFocus().IsActive());

    // Die entscheidende, von der Geometrie unabhaengige Aussage: das Fenster hat drei
    // bedienbare Controls, aber nur ZWEI Stationen fuer das Pad. Das Textfeld ist keine.
    BOOST_TEST(view(1).GetFocus().Collect().size() == 2u);

    for(unsigned i = 0; i < 6u; ++i)
    {
        pads.tap(11, PadButton::A);
        pads.tap(11, PadButton::DpadDown);
        step(16);
        // Das Feld wird nie zur Station, und sein Fokusbit bleibt beim Mausspieler.
        BOOST_TEST(view(1).GetFocus().GetFocused() != static_cast<Window*>(edit));
        BOOST_TEST(edit->HasFocus());
    }
    for(unsigned i = 0; i < 6u; ++i)
    {
        pads.tap(11, PadButton::A);
        pads.tap(11, PadButton::DpadUp);
        step(16);
        BOOST_TEST(view(1).GetFocus().GetFocused() != static_cast<Window*>(edit));
        BOOST_TEST(edit->HasFocus());
    }

    // Gegenprobe, damit die Aussage nicht daran haengt, dass gar nichts passiert ist: die
    // beiden Knoepfe hat der Padspieler sehr wohl ausgeloest.
    BOOST_TEST(wnd.clicks.size() > 0u);

    // Und die Tastatur des Mausspielers landet weiterhin in SEINEM Feld.
    BOOST_TEST(edit->Msg_KeyDown(KeyEvent{KeyType::Space}));
    BOOST_TEST(edit->GetText() == " ");

    wnd.Close();
    WINDOWMANAGER.Draw();
}

BOOST_AUTO_TEST_SUITE_END()
