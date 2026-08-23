// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

// BEFUND 1: Kamera und Zoom am Pad.
//
// Bis zu dieser Runde las der gesamte Padpfad ausschliesslich PadAxis::LeftX/LeftY, und
// PlayerView::ClampToView klemmte den Zeiger auf das eigene Viewport. Ein Padspieler sah damit
// fuer die ganze Partie genau den Ausschnitt um sein HQ (dskGameInterface.cpp, MoveToOwnHQ beim
// Partiestart) und konnte ihn nie verlassen.
//
// Jeder Fall hier nimmt AUSSCHLIESSLICH den Produktivweg: PadEvent in die Warteschlange des
// MockupVideoDriver, dskGameInterface::UpdateInput holt sie mit demselben Aufruf ab wie vom
// SDL2-Treiber (IVideoDriver::FetchPadEvents), PadRouter verteilt sie.
//
// Was hier mechanisch NICHT vorkommen darf:
//   * kein GameWorldView::MoveToMapPt, MoveBy, MoveTo, SetZoomFactor
//   * kein SetCursorPos / SetPadCursor
// Genau dieser Fehler hat die Padtests der letzten Runde entwertet: ihre Hilfsfunktion aimAt
// verschob die Ansicht mit MoveToMapPt - einem Aufruf, den kein Padknopf ausloest.
// Mit grep nachpruefbar.

#include "GamePlayer.h"
#include "PointOutput.h"
#include "buildings/nobBaseWarehouse.h"
#include "desktops/PlayerView.h"
#include "driver/PadEvent.h"
#include "input/PadRouter.h"
#include "world/GameWorld.h"
#include "PadFixture.h"
#include "gameData/GuiConsts.h"
#include <boost/test/unit_test.hpp>

using namespace rttr::test;

namespace {
/// Eine Ansicht, aber ZWEI Spieler auf der Karte: das HQ des zweiten liegt weit genug weg, um
/// beim Start sicher ausserhalb des Viewports zu sein. Eigener Name, weil das Komma sonst
/// innerhalb des BOOST_FIXTURE_TEST_CASE-Makros als Argumenttrenner gelesen wuerde.
using OneViewTwoPlayers = PadViewFixture<1, 2>;
} // namespace

BOOST_AUTO_TEST_SUITE(PadCameraTests)

// ============================================================================================
// 1. Der rechte Stick bewegt die Kamera der EIGENEN Ansicht
// ============================================================================================

BOOST_FIXTURE_TEST_CASE(TheRightStickMovesTheOwnCamera, PadViewFixture<1>)
{
    pads.pickUp(10);
    step(0);
    BOOST_TEST_REQUIRE(view(0).HasPadCursor());

    const DrawPoint before = gwv(0).GetOffset();
    pads.axis(10, PadAxis::RightX, 1.f);
    step(100);
    const DrawPoint afterRight = gwv(0).GetOffset();
    BOOST_TEST_INFO("Scrollstand vor dem Stick: " << before << ", danach: " << afterRight);
    BOOST_TEST((afterRight != before));
    BOOST_TEST(afterRight.x > before.x);
    BOOST_TEST(afterRight.y == before.y);

    // ... und die Gegenrichtung bringt ihn zurueck.
    pads.axis(10, PadAxis::RightX, -1.f);
    step(100);
    BOOST_TEST((gwv(0).GetOffset() == before));

    // Stick in Ruhelage: die Kamera steht still.
    pads.axis(10, PadAxis::RightX, 0.f);
    const DrawPoint parked = gwv(0).GetOffset();
    for(unsigned i = 0; i < 10u; ++i)
        step(100);
    BOOST_TEST((gwv(0).GetOffset() == parked));
}

BOOST_FIXTURE_TEST_CASE(TheRightStickMovesTheCameraOnBothAxes, PadViewFixture<1>)
{
    pads.pickUp(10);
    step(0);
    const DrawPoint before = gwv(0).GetOffset();
    pads.axis(10, PadAxis::RightY, 1.f);
    step(100);
    BOOST_TEST(gwv(0).GetOffset().y > before.y);
    BOOST_TEST(gwv(0).GetOffset().x == before.x);
}

/// Die Totzone gilt fuer den rechten Stick genauso wie fuer den linken: ein liegendes Pad mit
/// Drift darf die Kamera nicht wegwandern lassen. Am Fernseher liegt das Pad zwischen zwei
/// Zuegen auf dem Sofa - eine driftende Kamera waere dort besonders auffaellig.
BOOST_FIXTURE_TEST_CASE(TheRightStickIgnoresDriftInsideTheDeadzone, PadViewFixture<1>)
{
    pads.pickUp(10);
    step(0);
    const DrawPoint before = gwv(0).GetOffset();
    pads.axis(10, PadAxis::RightX, 0.15f);
    pads.axis(10, PadAxis::RightY, -0.1f);
    for(unsigned i = 0; i < 20u; ++i)
        step(100);
    BOOST_TEST((gwv(0).GetOffset() == before));
}

/// Ein Pad OHNE Ansicht bewegt gar nichts - auch nicht die Kamera.
BOOST_FIXTURE_TEST_CASE(AnUnassignedPadMovesNoCamera, PadViewFixture<1>)
{
    pads.connect(10); // nur angesteckt, nie benutzt
    step(0);
    const DrawPoint before = gwv(0).GetOffset();
    // Ein Stickausschlag IST eine Benutzung und nimmt die Ansicht in Besitz - genau deshalb
    // wird hier ein ZWEITES Pad benutzt, das keinen Slot mehr bekommen kann.
    pads.pickUp(11);
    step(0);
    const DrawPoint afterPickup = gwv(0).GetOffset();
    pads.axis(10, PadAxis::RightX, 1.f);
    for(unsigned i = 0; i < 10u; ++i)
        step(100);
    BOOST_TEST((gwv(0).GetOffset() == afterPickup));
    BOOST_TEST((afterPickup == before));
}

/// Splitscreen: der rechte Stick der einen Ansicht laesst die andere in Ruhe.
BOOST_FIXTURE_TEST_CASE(TheRightStickOfOneViewLeavesTheOtherAlone, PadViewFixture<2>)
{
    pads.pickUp(10);
    pads.pickUp(11);
    step(0);
    BOOST_TEST_REQUIRE(view(0).HasPadCursor());
    BOOST_TEST_REQUIRE(view(1).HasPadCursor());

    const DrawPoint before0 = gwv(0).GetOffset();
    const DrawPoint before1 = gwv(1).GetOffset();
    pads.axis(10, PadAxis::RightX, 1.f);
    for(unsigned i = 0; i < 5u; ++i)
        step(100);
    BOOST_TEST((gwv(0).GetOffset() != before0));
    BOOST_TEST((gwv(1).GetOffset() == before1));
}

// ============================================================================================
// 2. Der Zeiger bleibt am BILDSCHIRMpunkt, waehrend die Kamera faehrt
// ============================================================================================

/// Die Begruendung steht in dskGameInterface::OnPadCamera. Kurz: der Padzeiger ist ein
/// Bildschirmzeiger und auf sein Viewport geklemmt (PlayerView::ClampToView). Bliebe er am
/// WELTpunkt haengen, waere er nach einem Sekundenbruchteil Kamerafahrt aus dem Viewport
/// heraus und muesste sowieso neu geklemmt werden - der Weltpunkt waere dann trotzdem weg,
/// nur unvorhersehbar. Zwei Sticks, zwei Groessen: rechts der Ausschnitt, links der Punkt darin.
BOOST_FIXTURE_TEST_CASE(TheCursorKeepsItsScreenPositionWhileTheCameraMoves, PadViewFixture<1>)
{
    pads.pickUp(10);
    step(0);
    // Zeiger aus der Mitte heraus, damit die Aussage nicht zufaellig auf der Mitte gilt.
    pads.axis(10, PadAxis::LeftX, 1.f);
    step(100);
    pads.axis(10, PadAxis::LeftX, 0.f);
    step(0);
    const Position cursorBefore = view(0).GetPadCursor();
    BOOST_TEST_REQUIRE((cursorBefore != view(0).GetViewCenter()));
    const MapPoint selBefore = gwv(0).GetSelectedPt();

    pads.axis(10, PadAxis::RightX, 1.f);
    step(200);
    pads.axis(10, PadAxis::RightX, 0.f);
    step(0);

    BOOST_TEST((view(0).GetPadCursor() == cursorBefore));
    // Gegenprobe: der Weltpunkt unter dem Zeiger hat sich sehr wohl geaendert - sonst waere die
    // Aussage oben leer.
    BOOST_TEST((gwv(0).GetSelectedPt() != selBefore));
}

// ============================================================================================
// 3. Die Trigger zoomen die EIGENE Ansicht - auf den Zeiger, nicht auf die Mitte
// ============================================================================================

BOOST_FIXTURE_TEST_CASE(TheTriggersZoomTheOwnView, PadViewFixture<1>)
{
    pads.pickUp(10);
    step(0);
    const float start = gwv(0).GetCurrentTargetZoomFactor();
    BOOST_TEST_REQUIRE(start == 1.f);

    pads.axis(10, PadAxis::TriggerRight, 1.f);
    step(200);
    const float zoomedIn = gwv(0).GetCurrentTargetZoomFactor();
    BOOST_TEST_INFO("Zoom nach 200ms rechtem Trigger: " << zoomedIn);
    BOOST_TEST(zoomedIn > start);

    pads.axis(10, PadAxis::TriggerRight, 0.f);
    pads.axis(10, PadAxis::TriggerLeft, 1.f);
    step(200);
    BOOST_TEST(gwv(0).GetCurrentTargetZoomFactor() < zoomedIn);

    // Ruhelage: kein Zoom mehr.
    pads.axis(10, PadAxis::TriggerLeft, 0.f);
    const float parked = gwv(0).GetCurrentTargetZoomFactor();
    for(unsigned i = 0; i < 10u; ++i)
        step(100);
    BOOST_TEST(gwv(0).GetCurrentTargetZoomFactor() == parked);
}

/// Der Zoom bleibt in denselben Grenzen wie beim Mausrad (ZOOM_FACTORS).
BOOST_FIXTURE_TEST_CASE(TheTriggerZoomStaysInsideTheKnownLimits, PadViewFixture<1>)
{
    pads.pickUp(10);
    step(0);
    pads.axis(10, PadAxis::TriggerRight, 1.f);
    for(unsigned i = 0; i < 60u; ++i)
        step(100);
    BOOST_TEST(gwv(0).GetCurrentTargetZoomFactor() == ZOOM_FACTORS.back());
    pads.axis(10, PadAxis::TriggerRight, 0.f);
    pads.axis(10, PadAxis::TriggerLeft, 1.f);
    for(unsigned i = 0; i < 60u; ++i)
        step(100);
    BOOST_TEST(gwv(0).GetCurrentTargetZoomFactor() == ZOOM_FACTORS.front());
}

/// DER Punkt an Befund 1: der Zoom zentriert auf den ZEIGER, nicht auf die Viewportmitte.
/// GameWorldView::SetZoomFactor allein schneidet links und rechts gleich viel weg
/// (CalcFxLx: diff/2 von beiden Seiten). Der Padzeiger steht am Fernseher dauernd am Rand
/// seines Viertelbildschirms - ein Zoom auf die Mitte schoebe genau den Knoten aus dem Bild,
/// auf den der Spieler gerade zielt.
BOOST_FIXTURE_TEST_CASE(TheTriggerZoomKeepsTheNodeUnderTheCursor, PadViewFixture<1>)
{
    pads.pickUp(10);
    step(0);
    // Zeiger deutlich aus der Mitte heraus - sonst waere die Aussage trivial wahr.
    pads.axis(10, PadAxis::LeftX, 1.f);
    pads.axis(10, PadAxis::LeftY, -1.f);
    step(120);
    releaseSticks(10);
    const Position cursor = view(0).GetPadCursor();
    const Position center = view(0).GetViewCenter();
    BOOST_TEST_REQUIRE(std::abs(cursor.x - center.x) > 50);
    const MapPoint under = gwv(0).GetSelectedPt();
    BOOST_TEST_REQUIRE(under.isValid());

    pads.axis(10, PadAxis::TriggerRight, 1.f);
    for(unsigned i = 0; i < 5u; ++i)
        step(60);
    pads.axis(10, PadAxis::TriggerRight, 0.f);
    step(0);
    BOOST_TEST_REQUIRE(gwv(0).GetCurrentTargetZoomFactor() > 1.f);
    BOOST_TEST_INFO("Zoomfaktor: " << gwv(0).GetCurrentTargetZoomFactor());
    BOOST_TEST((view(0).GetPadCursor() == cursor));
    BOOST_TEST((gwv(0).GetSelectedPt() == under));
}

// ============================================================================================
// 3b. BEFUND C: der Padzoom haelt eine laufende WEICHE Zoomfahrt wirklich an
// ============================================================================================

/// GameWorldView::SetZoomFactor(x, /*smoothTransition*/ false) setzt zoomSpeed_ auf 0. Diese
/// eine Zeile war bisher durch keinen Nachweis gedeckt - sie liess sich vollstaendig
/// zuruecknehmen, ohne dass ein Test rot wurde.
///
/// Warum sie noetig ist: der Padtrigger schaltet den Zoom HART um (SetZoomFactorAt, sonst liesse
/// sich der Anker unter dem Zeiger nicht festhalten). Ohne den Reset bleibt die Geschwindigkeit
/// einer noch laufenden Mausrad-Fahrt stehen. Sichtbar wird das beim NAECHSTEN weichen Zoom: der
/// startet dann mit einer geerbten Geschwindigkeit, unter Umstaenden in die falsche Richtung -
/// der Spieler zoomt heraus und das Bild geht erst einmal hinein.
///
/// Gemessen wird der EFFEKTIVE Zoomfaktor ueber MapPosToView - genau der Wert, mit dem der
/// Zeichenpfad und die Zeigerumrechnung arbeiten. Einen Getter fuer zoomFactor_ gibt es nicht,
/// und dafuer einen einzufuehren waere eine Naht, die es nur fuer den Test gaebe.
BOOST_FIXTURE_TEST_CASE(APadZoomStopsARunningSmoothZoomInsteadOfInheritingItsSpeed, PadViewFixture<1>)
{
    // Der effektive Zoomfaktor, in ganzen Pixeln je 1000 Weltpixel.
    const auto zoomProbe = [this] {
        return gwv(0).MapPosToView(Position(1000, 0)).x - gwv(0).MapPosToView(Position(0, 0)).x;
    };

    pads.pickUp(10);
    step(0);
    const int atRest = zoomProbe();

    // 1. Mausrad: eine WEICHE Zoomfahrt nach oben. Das ist der Produktivweg des Mausspielers.
    for(unsigned i = 0; i < 12u; ++i)
        BOOST_TEST_REQUIRE(dsk->Msg_WheelUp(MouseCoords(view(0).GetViewCenter())));
    BOOST_TEST_REQUIRE(gwv(0).GetCurrentTargetZoomFactor() > 1.f);

    // 2. Ein paar Frames der Fahrt - genau das, was Draw() je Frame tut. Danach hat zoomSpeed_
    //    einen deutlichen Wert nach OBEN.
    for(unsigned i = 0; i < 12u; ++i)
        gwv(0).SetNextZoomFactor();
    const int running = zoomProbe();
    BOOST_TEST_REQUIRE(running > atRest); // die Fahrt laeuft wirklich

    // 3. Mitten hinein greift der Padtrigger. Er schaltet hart um.
    pads.axis(10, PadAxis::TriggerRight, 1.f);
    step(60);
    pads.axis(10, PadAxis::TriggerRight, 0.f);
    step(0);
    const float padTarget = gwv(0).GetCurrentTargetZoomFactor();
    const int afterPad = zoomProbe();

    // 4. Und jetzt eine NEUE weiche Fahrt, diesmal nach UNTEN (Mausrad zurueck).
    BOOST_TEST_REQUIRE(dsk->Msg_WheelDown(MouseCoords(view(0).GetViewCenter())));
    BOOST_TEST_REQUIRE(gwv(0).GetCurrentTargetZoomFactor() < padTarget);
    for(unsigned i = 0; i < 3u; ++i)
        gwv(0).SetNextZoomFactor();

    // Mit dem Reset faehrt sie sofort in die richtige Richtung. Ohne ihn erbt sie die
    // Aufwaertsgeschwindigkeit aus Schritt 2 und das Bild zoomt erst einmal weiter HINEIN.
    BOOST_TEST_INFO("Zoomprobe: Ruhe " << atRest << ", Fahrt " << running << ", nach Pad " << afterPad
                                       << ", nach dem Richtungswechsel " << zoomProbe());
    BOOST_TEST(zoomProbe() <= afterPad);
}

// ============================================================================================
// 4. Die Aussage, an der alles haengt: ein Padspieler ERREICHT einen fernen Punkt
// ============================================================================================

/// Ohne Kamera am Pad ist alles, was in den letzten Phasen gebaut wurde, nicht spielbar: eine
/// Strasse ueber mehr als eine Bildschirmbreite ist unmoeglich, weil der Zeiger auf das eigene
/// Viewport geklemmt ist.
///
/// Gemessen wird deshalb genau das: ein Knoten, der beim Partiestart NICHT im gezeichneten
/// Ausschnitt liegt, wird ausschliesslich mit Padereignissen ausgewaehlt.
BOOST_FIXTURE_TEST_CASE(APadPlayerReachesAPointOutsideHisStartingView, OneViewTwoPlayers)
{
    pads.pickUp(10);
    step(0);
    BOOST_TEST_REQUIRE(view(0).HasPadCursor());

    // Das HQ des zweiten Spielers - CreateEmptyWorld setzt die HQs weit auseinander.
    const MapPoint farPt = worldFixture.world.GetPlayer(1).GetHQPos();
    BOOST_TEST_REQUIRE(farPt.isValid());

    // Vorbedingung: der Punkt liegt beim Start WIRKLICH ausserhalb des Viewports, sonst waere
    // der Nachweis leer.
    const Position atStart = nodeViewPos(0, farPt);
    const bool visibleAtStart = view(0).ContainsViewPos(atStart);
    BOOST_TEST_INFO("Zielknoten " << farPt << " liegt beim Start bei " << atStart
                                  << ", Viewport " << gwv(0).GetPos() << " + " << gwv(0).GetSize());
    BOOST_TEST_REQUIRE(!visibleAtStart);
    BOOST_TEST_REQUIRE((gwv(0).GetSelectedPt() != farPt));

    aimAt(0, farPt);
    BOOST_TEST((gwv(0).GetSelectedPt() == farPt));
}

// ============================================================================================
// 5. Randschub: der AUSSCHLAG nach aussen schiebt die Kamera, die blosse LAGE nicht
// ============================================================================================

/// Der Zeiger in der Mitte: kein Schub, egal wie lange gefahren wird.
BOOST_FIXTURE_TEST_CASE(TheLeftStickInsideTheInnerFrameLeavesTheCameraAlone, PadViewFixture<1>)
{
    pads.pickUp(10);
    step(0);
    const DrawPoint before = gwv(0).GetOffset();
    // 20 ms Vollausschlag = 18 Pixel. Das Viewport ist um ein Vielfaches groesser, der Zeiger
    // bleibt also weit innerhalb des Innenrahmens.
    pads.axis(10, PadAxis::LeftX, 1.f);
    step(20);
    releaseSticks(10);
    BOOST_TEST((view(0).GetPadCursor() != view(0).GetViewCenter()));
    BOOST_TEST((gwv(0).GetOffset() == before));
}

/// Am Rand angekommen faehrt die Kamera mit - und der Zeiger bleibt trotzdem erreichbar bis in
/// die aeusserste Ecke (er wird NICHT am Innenrahmen festgehalten).
BOOST_FIXTURE_TEST_CASE(TheLeftStickPushesTheCameraBeyondTheInnerFrame, PadViewFixture<1>)
{
    pads.pickUp(10);
    step(0);
    const DrawPoint before = gwv(0).GetOffset();

    // Lange genug nach rechts, um sicher an den Viewportrand zu kommen.
    pads.axis(10, PadAxis::LeftX, 1.f);
    for(unsigned i = 0; i < 20u; ++i)
        step(100);
    releaseSticks(10);

    const Position origin = gwv(0).GetPos();
    const int right = origin.x + static_cast<int>(gwv(0).GetSize().x) - 1;
    BOOST_TEST(view(0).GetPadCursor().x == right); // ClampToView haelt ihn genau dort
    BOOST_TEST(gwv(0).GetOffset().x > before.x);   // ... und die Kamera ist mitgefahren
}

/// DIE Unterscheidung, an der die Entscheidung haengt: der Zeiger LIEGT am Rand, der Stick ist
/// aber losgelassen (oder zittert in der Totzone) - dann faehrt nichts. Ohne diese Bedingung
/// driftete die Kamera nach jedem uebersteuerten Zielen weiter, im Splitscreen ausgerechnet an
/// der Kante zum Bild des Nachbarn.
BOOST_FIXTURE_TEST_CASE(ACursorRestingAtTheEdgeDoesNotPushTheCamera, PadViewFixture<1>)
{
    pads.pickUp(10);
    step(0);
    pads.axis(10, PadAxis::LeftX, 1.f);
    for(unsigned i = 0; i < 20u; ++i)
        step(100);
    pads.axis(10, PadAxis::LeftX, 0.f);
    step(16);

    const DrawPoint parked = gwv(0).GetOffset();
    const Position cursor = view(0).GetPadCursor();
    // Losgelassen ...
    for(unsigned i = 0; i < 20u; ++i)
        step(100);
    BOOST_TEST((gwv(0).GetOffset() == parked));
    // ... und mit Drift innerhalb der Totzone.
    pads.axis(10, PadAxis::LeftX, 0.2f);
    pads.axis(10, PadAxis::LeftY, 0.1f);
    for(unsigned i = 0; i < 20u; ++i)
        step(100);
    BOOST_TEST((gwv(0).GetOffset() == parked));
    BOOST_TEST((view(0).GetPadCursor() == cursor));
}

/// Und der Schub gilt nur nach AUSSEN: vom Rand weg zurueck in die Mitte zu ziehen bewegt die
/// Kamera nicht.
BOOST_FIXTURE_TEST_CASE(PullingBackFromTheEdgeDoesNotPushTheCamera, PadViewFixture<1>)
{
    pads.pickUp(10);
    step(0);
    pads.axis(10, PadAxis::LeftX, 1.f);
    for(unsigned i = 0; i < 20u; ++i)
        step(100);
    pads.axis(10, PadAxis::LeftX, 0.f);
    step(16);
    const DrawPoint atEdge = gwv(0).GetOffset();

    pads.axis(10, PadAxis::LeftX, -1.f);
    step(20); // kurz, damit der Zeiger den Innenrahmen noch nicht verlaesst
    releaseSticks(10);
    BOOST_TEST((gwv(0).GetOffset() == atEdge));
}

BOOST_AUTO_TEST_SUITE_END()
