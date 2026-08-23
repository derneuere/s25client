// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "PointOutput.h"
#include "WindowManager.h"
#include "desktops/Desktop.h"
#include "driver/KeyEvent.h"
#include "driver/MouseCoords.h"
#include "driver/PadEvent.h"
#include "drivers/VideoDriverWrapper.h"
#include "input/IPadTarget.h"
#include "input/PadRouter.h"
#include "world/ViewportLayout.h"
#include "PadFixture.h"
#include "mockupDrivers/MockupVideoDriver.h"
#include <boost/test/unit_test.hpp>
#include <cmath>
#include <string>
#include <tuple>
#include <vector>

using rttr::test::PadFeeder;
using rttr::test::PadViewFixture;

namespace {

/// Schreibt mit, was der Router zustellt. Kein Spiel, kein Bildschirm, keine Ansicht - damit
/// laesst sich die Zuordnungs- und Kennlinienlogik ohne jede weitere Beteiligung pruefen.
struct RecordingTarget : IPadTarget
{
    std::vector<std::pair<unsigned, bool>> assigns;
    std::vector<std::pair<unsigned, Position>> moves;
    std::vector<std::pair<unsigned, Position>> cameraMoves;
    std::vector<std::pair<unsigned, float>> zooms;
    std::vector<std::tuple<unsigned, PadButton, bool>> buttons;

    void OnPadAssigned(unsigned slot, bool assigned) override { assigns.emplace_back(slot, assigned); }
    void OnPadMove(unsigned slot, const Position& delta) override { moves.emplace_back(slot, delta); }
    void OnPadCamera(unsigned slot, const Position& delta) override { cameraMoves.emplace_back(slot, delta); }
    void OnPadZoom(unsigned slot, float step) override { zooms.emplace_back(slot, step); }
    void OnPadButton(unsigned slot, PadButton button, bool down) override
    {
        buttons.emplace_back(slot, button, down);
    }
    void clear()
    {
        assigns.clear();
        moves.clear();
        cameraMoves.clear();
        zooms.clear();
        buttons.clear();
    }
    /// Summe aller Bewegungen eines Slots seit dem letzten clear()
    Position totalMove(unsigned slot) const
    {
        Position sum(0, 0);
        for(const auto& m : moves)
        {
            if(m.first == slot)
                sum += m.second;
        }
        return sum;
    }
    /// Dasselbe fuer die Kamera
    Position totalCameraMove(unsigned slot) const
    {
        Position sum(0, 0);
        for(const auto& m : cameraMoves)
        {
            if(m.first == slot)
                sum += m.second;
        }
        return sum;
    }
    /// Summe aller Zoomschritte eines Slots seit dem letzten clear()
    float totalZoom(unsigned slot) const
    {
        float sum = 0.f;
        for(const auto& z : zooms)
        {
            if(z.first == slot)
                sum += z.second;
        }
        return sum;
    }
};

/// Zaehlt die Eingabe-Rueckrufe, die der WindowManager an seinen Desktop weiterreicht.
/// Bewusst handgeschrieben statt turtle: der Test soll ohne zusaetzliche Abhaengigkeit des
/// Splitscreen-Ziels laufen.
struct CountingDesktop : Desktop
{
    CountingDesktop() : Desktop(nullptr) {}
    unsigned numMouseMove = 0, numLeftDown = 0, numLeftUp = 0, numRightDown = 0, numKeyDown = 0;

    bool Msg_MouseMove(const MouseCoords&) override
    {
        ++numMouseMove;
        return true;
    }
    bool Msg_LeftDown(const MouseCoords&) override
    {
        ++numLeftDown;
        return true;
    }
    bool Msg_LeftUp(const MouseCoords&) override
    {
        ++numLeftUp;
        return true;
    }
    bool Msg_RightDown(const MouseCoords&) override
    {
        ++numRightDown;
        return true;
    }
    bool Msg_KeyDown(const KeyEvent&) override
    {
        ++numKeyDown;
        return true;
    }
    unsigned total() const { return numMouseMove + numLeftDown + numLeftUp + numRightDown + numKeyDown; }
};

/// Pixel je Sekunde bei Vollausschlag, als Zahl im Test festgenagelt. Bewegt sich der
/// Produktivwert, muss der Test bewusst nachgezogen werden.
constexpr float kPixelsPerSecond = 900.f;
int expectedMove(const float axis, const unsigned ms)
{
    return static_cast<int>(axis * kPixelsPerSecond * (static_cast<float>(ms) / 1000.f));
}

} // namespace

BOOST_AUTO_TEST_SUITE(GamepadInputTests)

// ============================================================================================
// 1. PadRouter pur: Zuordnung, Kennlinie, Flanken - ohne Spiel, ohne Treiber, ohne Ansicht
// ============================================================================================

/// Grundregel der Zuordnung: nicht das Anstecken, sondern die erste BENUTZUNG vergibt den
/// Slot. Das ist die Stelle, an der aus "irgendein Geraet" ein bestimmter lokaler Spieler wird.
BOOST_AUTO_TEST_CASE(PadRouterAssignsSlotsInFirstUseOrderAndNotOnPlugIn)
{
    PadRouter router;
    router.SetNumSlots(2);
    RecordingTarget target;

    // Angesteckt, aber nicht angefasst: bekannt, ohne Slot, ohne jede Wirkung.
    router.OnEvent(PadEvent::Connected(10));
    router.OnEvent(PadEvent::Connected(11));
    BOOST_TEST(router.GetDevices().size() == 2u);
    BOOST_TEST(router.GetSlot(10) == PadRouter::NoSlot);
    BOOST_TEST(router.GetSlot(11) == PadRouter::NoSlot);
    BOOST_TEST(router.GetNumAssigned() == 0u);
    BOOST_TEST(!router.IsActive(10));
    router.UpdateMotion(1000, target);
    BOOST_TEST(target.assigns.empty());
    BOOST_TEST(target.moves.empty());
    // Auch ein Stickzappeln INNERHALB der Totzone ist keine Benutzung
    router.OnEvent(PadEvent::Axis(10, PadAxis::LeftX, 0.2f));
    BOOST_TEST(router.GetSlot(10) == PadRouter::NoSlot);

    // Wer zuerst benutzt, bekommt Slot 0 - unabhaengig von der Ansteckreihenfolge.
    router.OnEvent(PadEvent::Button(11, PadButton::Start, true));
    BOOST_TEST(router.GetSlot(11) == 0u);
    BOOST_TEST(router.IsActive(11));
    router.OnEvent(PadEvent::Axis(10, PadAxis::LeftX, 1.f)); // jenseits der Totzone
    BOOST_TEST(router.GetSlot(10) == 1u);
    BOOST_TEST(router.GetNumAssigned() == 2u);
    BOOST_TEST(router.GetSlot(12) == PadRouter::NoSlot); // unbekannt

    // Ein zweites Connected fuer dasselbe Geraet aendert nichts (idempotent)
    router.OnEvent(PadEvent::Connected(11));
    BOOST_TEST(router.GetSlot(11) == 0u);
    BOOST_TEST(router.GetDevices().size() == 2u);

    // Mehr benutzte Pads als Ansichten: das dritte bleibt unversorgt und erzeugt keine Wirkung.
    router.OnEvent(PadEvent::Connected(12));
    router.OnEvent(PadEvent::Button(12, PadButton::Start, true));
    BOOST_TEST(router.IsActive(12));
    BOOST_TEST(router.GetSlot(12) == PadRouter::NoSlot);
    BOOST_TEST(router.GetNumAssigned() == 2u);
    router.OnEvent(PadEvent::Axis(10, PadAxis::LeftX, 0.f)); // Pad 10 stillstellen
    router.UpdateMotion(0, target);
    router.DispatchButtons(target);
    target.clear();
    router.OnEvent(PadEvent::Axis(12, PadAxis::LeftX, 1.f));
    router.OnEvent(PadEvent::Button(12, PadButton::A, true));
    router.UpdateMotion(100, target);
    router.DispatchButtons(target);
    BOOST_TEST(target.moves.empty());
    BOOST_TEST(target.buttons.empty());

    // ... rueckt aber nach, sobald ein Slot frei wird. Nachruecken darf nur, wer benutzt hat.
    router.OnEvent(PadEvent::Disconnected(11));
    BOOST_TEST(router.GetSlot(12) == 0u);
    BOOST_TEST(router.GetSlot(11) == PadRouter::NoSlot);
}

/// Die Kehrseite: ein unbenutztes Pad rueckt NICHT nach, wenn ein Slot frei wird. Sonst waere
/// die Uebernahme durch Benutzung nur aufgeschoben - beim ersten Abziehen eines Nachbarn
/// haette das herumliegende Pad die Ansicht doch bekommen.
BOOST_AUTO_TEST_CASE(PadRouterDoesNotHandAFreedSlotToAnUnusedPad)
{
    PadRouter router;
    router.SetNumSlots(1);
    router.OnEvent(PadEvent::Connected(10));
    router.OnEvent(PadEvent::Button(10, PadButton::Start, true));
    router.OnEvent(PadEvent::Connected(11)); // steckt nur herum
    BOOST_TEST_REQUIRE(router.GetSlot(10) == 0u);
    BOOST_TEST_REQUIRE(router.GetSlot(11) == PadRouter::NoSlot);

    router.OnEvent(PadEvent::Disconnected(10));
    BOOST_TEST(router.GetSlot(11) == PadRouter::NoSlot);
    // Auch das Wachsen der Slotzahl darf ihm keinen geben
    router.SetNumSlots(4);
    BOOST_TEST(router.GetSlot(11) == PadRouter::NoSlot);
    BOOST_TEST(router.GetNumAssigned() == 0u);
    // Erst die Benutzung
    router.OnEvent(PadEvent::Button(11, PadButton::Start, true));
    BOOST_TEST(router.GetSlot(11) == 0u);
}

/// Die feste Zuordnung. Das ist die vorgesehene Stelle, an der spaeter Lobby oder Optionen
/// ansetzen ("dieses Pad ist Spieler 2").
BOOST_AUTO_TEST_CASE(PadRouterHonorsAnExplicitAssignment)
{
    PadRouter router;
    router.SetNumSlots(2);
    router.OnEvent(PadEvent::Connected(10));
    router.OnEvent(PadEvent::Button(10, PadButton::Start, true));
    router.OnEvent(PadEvent::Connected(11));
    router.OnEvent(PadEvent::Button(11, PadButton::Start, true));
    BOOST_TEST_REQUIRE(router.GetSlot(10) == 0u);

    BOOST_TEST(router.AssignSlot(10, 1));
    BOOST_TEST(router.GetSlot(10) == 1u);
    // Das verdraengte Geraet ist unversorgt, nicht etwa stillschweigend auf Slot 0 gerutscht:
    // sonst waere die ausdrueckliche Zuordnung wieder von der Ansteckreihenfolge abhaengig.
    BOOST_TEST(router.GetSlot(11) == PadRouter::NoSlot);

    BOOST_TEST(!router.AssignSlot(10, 2));  // Slot gibt es nicht
    BOOST_TEST(!router.AssignSlot(99, 0));  // Geraet gibt es nicht
    BOOST_TEST(router.GetSlot(10) == 1u);   // ... und nichts davon hat etwas veraendert
}

/// M3a/M3b/M3c/M3d/M3e in einem: der Achsenwert wird tatsaechlich gelesen, mit Vorzeichen, die
/// Totzone wirkt, und die verstrichene Zeit geht linear ein.
BOOST_AUTO_TEST_CASE(PadRouterAppliesDeadzoneAxisValueAndElapsedTime)
{
    // Die Kennlinie fuer sich
    BOOST_TEST((PadRouter::FilterStick(PointF(0.05f, 0.f)) == PointF(0.f, 0.f)));  // unter der Totzone
    BOOST_TEST((PadRouter::FilterStick(PointF(0.5f, 0.f)) == PointF(0.5f, 0.f)));  // linear darueber
    BOOST_TEST((PadRouter::FilterStick(PointF(-1.f, 0.f)) == PointF(-1.f, 0.f)));  // Vorzeichen bleibt
    {
        // Diagonale darf nicht schneller sein als eine Achse
        const PointF diag = PadRouter::FilterStick(PointF(1.f, 1.f));
        BOOST_TEST(std::abs(std::sqrt(diag.x * diag.x + diag.y * diag.y) - 1.f) < 0.001f);
    }

    PadRouter router;
    router.SetNumSlots(1);
    RecordingTarget target;
    router.OnEvent(PadEvent::Connected(10));
    router.OnEvent(PadEvent::Button(10, PadButton::Start, true)); // Uebernahme durch Benutzung
    router.UpdateMotion(0, target);
    BOOST_TEST_REQUIRE(target.assigns.size() == 1u);
    BOOST_TEST((target.assigns[0] == std::make_pair(0u, true)));

    // Vollausschlag rechts, 100 ms
    target.clear();
    router.OnEvent(PadEvent::Axis(10, PadAxis::LeftX, 1.f));
    router.UpdateMotion(100, target);
    BOOST_TEST((target.totalMove(0) == Position(expectedMove(1.f, 100), 0)));

    // M3e: doppelte Zeit, doppelter Weg
    target.clear();
    router.UpdateMotion(200, target);
    BOOST_TEST((target.totalMove(0) == Position(expectedMove(1.f, 200), 0)));

    // M3a: halber Ausschlag, halber Weg
    target.clear();
    router.OnEvent(PadEvent::Axis(10, PadAxis::LeftX, 0.f)); // setzt den Subpixelrest zurueck
    router.UpdateMotion(100, target);
    BOOST_TEST(target.moves.empty());
    target.clear();
    router.OnEvent(PadEvent::Axis(10, PadAxis::LeftX, 0.5f));
    router.UpdateMotion(100, target);
    BOOST_TEST((target.totalMove(0) == Position(expectedMove(0.5f, 100), 0)));

    // M3c: Vorzeichen
    target.clear();
    router.OnEvent(PadEvent::Axis(10, PadAxis::LeftX, 0.f));
    router.UpdateMotion(1, target);
    target.clear();
    router.OnEvent(PadEvent::Axis(10, PadAxis::LeftX, -1.f));
    router.UpdateMotion(100, target);
    BOOST_TEST((target.totalMove(0) == Position(-expectedMove(1.f, 100), 0)));

    // M3d: unter der Totzone passiert exakt nichts - kein einziger Rueckruf
    target.clear();
    router.OnEvent(PadEvent::Axis(10, PadAxis::LeftX, 0.05f));
    router.UpdateMotion(1000, target);
    BOOST_TEST(target.moves.empty());
}

/// M9: das Konzept, das der Tastatur fehlt (VideoDriverLoaderInterface kennt kein Msg_KeyUp).
/// Zwei Druecken ohne Loslassen dazwischen sind EIN Ereignis; erst ein Loslassen macht das
/// naechste Druecken wieder zu einer Flanke.
BOOST_AUTO_TEST_CASE(PadRouterReportsButtonEdgesOnlyOnce)
{
    PadRouter router;
    router.SetNumSlots(1);
    RecordingTarget target;
    router.OnEvent(PadEvent::Connected(10));
    router.UpdateMotion(0, target);
    target.clear();

    router.OnEvent(PadEvent::Button(10, PadButton::A, true));
    router.OnEvent(PadEvent::Button(10, PadButton::A, true)); // Wiederholung, keine Flanke
    router.DispatchButtons(target);
    BOOST_TEST_REQUIRE(target.buttons.size() == 1u);
    BOOST_TEST((target.buttons[0] == std::make_tuple(0u, PadButton::A, true)));

    target.clear();
    router.OnEvent(PadEvent::Button(10, PadButton::A, false));
    router.OnEvent(PadEvent::Button(10, PadButton::A, true));
    router.DispatchButtons(target);
    BOOST_TEST_REQUIRE(target.buttons.size() == 2u);
    BOOST_TEST((target.buttons[0] == std::make_tuple(0u, PadButton::A, false)));
    BOOST_TEST((target.buttons[1] == std::make_tuple(0u, PadButton::A, true)));

    // Abziehen mitten im Druck: der Knopf wird kuenstlich losgelassen, sonst bliebe eine
    // Aktion fuer immer haengen.
    target.clear();
    router.OnEvent(PadEvent::Disconnected(10));
    router.DispatchButtons(target);
    BOOST_TEST_REQUIRE(target.buttons.size() == 1u);
    BOOST_TEST((target.buttons[0] == std::make_tuple(0u, PadButton::A, false)));
    router.UpdateMotion(0, target);
    BOOST_TEST_REQUIRE(!target.assigns.empty());
    BOOST_TEST((target.assigns.back() == std::make_pair(0u, false)));
}

// ============================================================================================
// 2. Zuordnung Pad -> Ansicht am echten dskGameInterface
// ============================================================================================

using TwoViews = PadViewFixture<2>;
using OneView = PadViewFixture<1>;
using FourViews = PadViewFixture<4>;

/// DER Nachweis dieser Phase, erste Haelfte: ein Gamepad bewegt den Zeiger SEINER Ansicht -
/// und nur diesen. M1 (Zuordnung vertauscht) und M2 (an alle Ansichten geleitet) fallen hier um.
BOOST_FIXTURE_TEST_CASE(PadMovesOnlyItsOwnViewCursor, TwoViews)
{
    pads.pickUp(10);
    pads.pickUp(11);
    step(0);

    // Ein frisch zugeordnetes Pad startet in der Mitte SEINER Ansicht
    BOOST_TEST_REQUIRE(view(0).HasPadCursor());
    BOOST_TEST_REQUIRE(view(1).HasPadCursor());
    const Position c0 = view(0).GetPadCursor();
    const Position c1 = view(1).GetPadCursor();
    BOOST_TEST((c0 == view(0).GetViewCenter()));
    BOOST_TEST((c1 == view(1).GetViewCenter()));
    // ... und die Ansicht rechnet mit genau diesem Zeiger
    BOOST_TEST_REQUIRE(gwv(0).GetCursorPos().has_value());
    BOOST_TEST((*gwv(0).GetCursorPos() == c0));
    BOOST_TEST((*gwv(1).GetCursorPos() == c1));

    // Pad 0 schiebt 100 ms lang voll nach rechts
    pads.axis(10, PadAxis::LeftX, 1.f);
    step(100);
    const int d = expectedMove(1.f, 100);
    BOOST_TEST((*gwv(0).GetCursorPos() == c0 + Position(d, 0)));
    BOOST_TEST((*gwv(1).GetCursorPos() == c1)); // <- die eigentliche Aussage

    // Und umgekehrt: Pad 1 nach oben, Pad 0 steht still
    pads.axis(10, PadAxis::LeftX, 0.f);
    pads.axis(11, PadAxis::LeftY, -1.f);
    const Position c0b = *gwv(0).GetCursorPos();
    step(100);
    BOOST_TEST((*gwv(0).GetCursorPos() == c0b));
    BOOST_TEST((*gwv(1).GetCursorPos() == c1 + Position(0, -d)));
}

/// M6: ohne Klemme wanderte der Zeiger von Pad 0 sichtbar in das Bild von Spieler 1.
BOOST_FIXTURE_TEST_CASE(PadCursorNeverLeavesItsOwnViewport, TwoViews)
{
    const std::vector<Viewport> vps = CalcViewports(VIDEODRIVER.GetRenderSize(), 2);
    BOOST_TEST_REQUIRE(vps.size() == 2u);

    pads.pickUp(10);
    step(0);
    pads.axis(10, PadAxis::LeftX, 1.f);
    pads.axis(10, PadAxis::LeftY, 1.f);
    // 5000 ms Vollausschlag = 4500 Pixel, ein Vielfaches jeder Ansichtsbreite
    for(unsigned i = 0; i < 10; ++i)
        step(500);

    BOOST_TEST_REQUIRE(gwv(0).GetCursorPos().has_value());
    const Position pos = *gwv(0).GetCursorPos();
    BOOST_TEST(view(0).ContainsViewPos(pos));
    BOOST_TEST(!view(1).ContainsViewPos(pos));
    BOOST_TEST(pos.x < vps[1].origin.x);

    // ... und ebenso in die andere Richtung
    pads.axis(10, PadAxis::LeftX, -1.f);
    pads.axis(10, PadAxis::LeftY, -1.f);
    for(unsigned i = 0; i < 10; ++i)
        step(500);
    BOOST_TEST(view(0).ContainsViewPos(*gwv(0).GetCursorPos()));
    BOOST_TEST(gwv(0).GetCursorPos()->x >= vps[0].origin.x);
    BOOST_TEST(gwv(0).GetCursorPos()->y >= vps[0].origin.y);
}

/// Der Zeiger bedeutet auch fachlich etwas: er bestimmt den selektierten Kartenpunkt, und das
/// ist derselbe Wert, den eine Aktion benutzt. Ohne diese Zusicherung koennte der Zeiger sich
/// bewegen, ohne dass es irgendeine Wirkung haette.
BOOST_FIXTURE_TEST_CASE(PadCursorDrivesTheSelectedMapPointOfItsOwnView, TwoViews)
{
    pads.pickUp(10);
    pads.pickUp(11);
    step(0);
    BOOST_TEST_REQUIRE(gwv(0).GetSelectedPt().isValid());
    BOOST_TEST_REQUIRE(gwv(1).GetSelectedPt().isValid());

    const MapPoint before0 = gwv(0).GetSelectedPt();
    const MapPoint before1 = gwv(1).GetSelectedPt();

    pads.axis(10, PadAxis::LeftX, 1.f);
    step(100);
    BOOST_TEST((gwv(0).GetSelectedPt() != before0)); // der eigene Punkt wandert mit
    BOOST_TEST((gwv(1).GetSelectedPt() == before1)); // der des Nachbarn nicht
}

/// M7: solange eine Ansicht ein Pad hat, kann die Maus ihr den Zeiger nicht stehlen - auch
/// nicht, wenn sie genau darueber steht. Sonst zappelte der Zeiger von Spieler 1 zwischen
/// Pad und fremder Maus hin und her.
BOOST_FIXTURE_TEST_CASE(MouseDoesNotStealAPadOwnedViewCursor, TwoViews)
{
    pads.pickUp(10); // nur EIN benutztes Pad -> Ansicht 0 hat ein Pad, Ansicht 1 nicht
    step(0);
    BOOST_TEST_REQUIRE(view(0).HasPadCursor());
    BOOST_TEST_REQUIRE(!view(1).HasPadCursor());

    const Position padPos = view(0).GetPadCursor();
    // Die Maus steht mitten in Ansicht 0, also genau ueber der padbesetzten Ansicht
    const Position mouseInView0 = view(0).GetViewCenter() + Position(17, -23);
    BOOST_TEST_REQUIRE(view(0).ContainsViewPos(mouseInView0));
    step(0, mouseInView0);

    BOOST_TEST((*gwv(0).GetCursorPos() == padPos));       // Pad gewinnt
    BOOST_TEST((*gwv(0).GetCursorPos() != mouseInView0)); // ... und zwar sichtbar
    // BEFUND B: die padlose Ansicht bekommt die Maus HIER ausdruecklich NICHT.
    //
    // Bis zu dieser Runde stand hier das Gegenteil, mit der Begruendung "sonst haette Spieler 1
    // gar keinen Zeiger mehr, sobald Spieler 0 ein Pad ansteckt". Das war ein Fehlschluss: der
    // Zeiger, den Spieler 1 dann bekam, lag ausserhalb SEINES Viewports, UpdateSelection machte
    // daraus einen Randknoten, und ein Mausklick oeffnete dort ein Fenster - auf einem Knoten,
    // ueber dem die Maus sichtbar nicht stand. Ein Zeiger, der luegt, ist schlechter als keiner.
    // Sobald die Maus ueber Ansicht 1 steht, bekommt Ansicht 1 sie wieder (gleich darunter).
    BOOST_TEST(!gwv(1).GetCursorPos().has_value());

    // Steht die Maus ueber der padlosen Ansicht, gehoert sie natuerlich erst recht ihr
    const Position mouseInView1 = view(1).GetViewCenter();
    step(0, mouseInView1);
    BOOST_TEST((*gwv(0).GetCursorPos() == padPos));
    BOOST_TEST((*gwv(1).GetCursorPos() == mouseInView1));
}

/// M8, die harte Randbedingung dieser Phase, Teil 1: Einzelspieler mit Maus - mit oder ohne
/// angestecktes Pad - verhaelt sich wie vorher. Die eine Ansicht bekommt den Zeiger IMMER, egal
/// wo die Maus steht.
///
/// Der Padfall ist hier ausdruecklich EINGESCHLOSSEN und nicht, wie frueher im Testnamen
/// ("...WithoutPads..."), ausgenommen: ein blosses Anstecken darf nichts aendern. Siehe den
/// Regressionsnachweis darunter.
BOOST_FIXTURE_TEST_CASE(SingleViewFollowsTheMouseWhetherOrNotAPadIsPlugged, OneView)
{
    const Extent renderSize = VIDEODRIVER.GetRenderSize();
    const Position positions[] = {Position(0, 0),
                                  Position(renderSize / 2u),
                                  Position(renderSize.x - 1, renderSize.y - 1),
                                  Position(-500, -500),              // weit ausserhalb
                                  Position(renderSize.x + 99, 42)};  // rechts daneben
    for(const Position& p : positions)
    {
        BOOST_TEST_INFO("mouse at " << p);
        step(16, p);
        BOOST_TEST_REQUIRE(gwv(0).GetCursorPos().has_value());
        BOOST_TEST((*gwv(0).GetCursorPos() == p));
        BOOST_TEST(!view(0).HasPadCursor());
    }

    // Und jetzt dasselbe noch einmal, waehrend ein Pad steckt, das niemand anfasst.
    pads.connect(10);
    for(const Position& p : positions)
    {
        BOOST_TEST_INFO("idle pad plugged, mouse at " << p);
        step(16, p);
        BOOST_TEST_REQUIRE(gwv(0).GetCursorPos().has_value());
        BOOST_TEST((*gwv(0).GetCursorPos() == p));
        BOOST_TEST(!view(0).HasPadCursor());
    }
}

/// M8, Teil 2 - der scharfe Regressionsnachweis. Ein bloss ANGESTECKTES Pad hatte Slot 0
/// bekommen, den Zeiger in die Bildmitte gesetzt und der Maus die Karte weggenommen: jeder
/// Klick wirkte auf die Mitte statt auf den Mauszeiger, weil ContextClick ausschliesslich
/// gwv.GetSelectedPt() liest. Der SDL2-Treiber meldet auch BEREITS gesteckte Pads beim Start
/// als Connected - es genuegte also, dass ein Controller am PC haengt.
///
/// Geprueft wird deshalb der Wert, an dem die Regression haengt: der selektierte Kartenpunkt.
BOOST_FIXTURE_TEST_CASE(ConnectingAPadDoesNotMoveTheMapCursorOfAMouseControlledSingleView, OneView)
{
    const Position mousePos = view(0).GetViewCenter() + Position(97, -61);
    BOOST_TEST_REQUIRE(view(0).ContainsViewPos(mousePos));

    // 1. Ohne Pad: so soll es aussehen.
    step(16, mousePos);
    BOOST_TEST_REQUIRE(gwv(0).GetCursorPos().has_value());
    const MapPoint mouseSelPt = gwv(0).GetSelectedPt();
    BOOST_TEST_REQUIRE(mouseSelPt.isValid());
    // Gegenprobe, dass der Punkt ueberhaupt etwas mit dem Zeiger zu tun hat - sonst waere die
    // Gleichheit weiter unten wertlos.
    step(16, view(0).GetViewCenter());
    BOOST_TEST_REQUIRE((gwv(0).GetSelectedPt() != mouseSelPt));
    step(16, mousePos);
    BOOST_TEST_REQUIRE((gwv(0).GetSelectedPt() == mouseSelPt));

    // 2. Ein Pad wird angesteckt und NICHT angefasst. Danach muss alles unveraendert sein.
    pads.connect(10);
    step(16, mousePos);
    BOOST_TEST(!view(0).HasPadCursor());
    BOOST_TEST_REQUIRE(gwv(0).GetCursorPos().has_value());
    BOOST_TEST((*gwv(0).GetCursorPos() == mousePos));
    BOOST_TEST((gwv(0).GetSelectedPt() == mouseSelPt));

    // ... auch noch ein paar Frames spaeter, und auch dann, wenn der Stick INNERHALB der
    // Totzone zappelt (Drift eines liegenden Pads).
    pads.axis(10, PadAxis::LeftX, 0.1f);
    pads.axis(10, PadAxis::LeftY, -0.1f);
    for(unsigned i = 0; i < 5; ++i)
        step(100, mousePos);
    BOOST_TEST(!view(0).HasPadCursor());
    BOOST_TEST((gwv(0).GetSelectedPt() == mouseSelPt));
}

/// Die Gegenrichtung, damit die Behebung von M8 das eigentliche Ziel nicht verbaut: EIN Spieler
/// allein am Fernseher mit EINEM Pad. Sobald er das Pad tatsaechlich benutzt, gehoert ihm die
/// Ansicht - erst der Stick, und in einer zweiten Partie auch schon der blosse Knopfdruck.
BOOST_FIXTURE_TEST_CASE(SingleViewIsTakenOverByThePadAsSoonAsItIsActuallyUsed, OneView)
{
    const Position mousePos = view(0).GetViewCenter() + Position(97, -61);
    step(16, mousePos);
    BOOST_TEST_REQUIRE(!view(0).HasPadCursor());

    // Ein Ausschlag JENSEITS der Totzone ist die Uebernahme.
    pads.connect(10);
    pads.axis(10, PadAxis::LeftX, 1.f);
    step(100, mousePos);
    BOOST_TEST_REQUIRE(view(0).HasPadCursor());
    // Der Zeiger startet in der Mitte der Ansicht und ist in DIESEM Frame schon gelaufen.
    BOOST_TEST((view(0).GetPadCursor() == view(0).GetViewCenter() + Position(expectedMove(1.f, 100), 0)));
    BOOST_TEST((*gwv(0).GetCursorPos() == view(0).GetPadCursor()));
    // Die Maus kann ihn danach nicht mehr zurueckholen - die Ansicht gehoert dem Pad.
    step(16, mousePos);
    BOOST_TEST(view(0).HasPadCursor());
}

/// Derselbe Nachweis ueber den Knopf statt ueber den Stick: wer nur A drueckt, hat das Pad
/// genauso in der Hand. Wichtig ist dabei die REIHENFOLGE - die Uebernahme muss noch im selben
/// Frame VOR der Knopfzustellung wirksam werden, sonst wirkte das erste A auf den Mauspunkt.
BOOST_FIXTURE_TEST_CASE(SingleViewIsTakenOverByAButtonPressBeforeThatPressIsActedOn, OneView)
{
    const Position mousePos = view(0).GetViewCenter() + Position(97, -61);
    step(16, mousePos);
    BOOST_TEST_REQUIRE(!view(0).HasPadCursor());

    pads.connect(10);
    pads.button(10, PadButton::A, true);
    step(16, mousePos);
    BOOST_TEST_REQUIRE(view(0).HasPadCursor());
    BOOST_TEST((view(0).GetPadCursor() == view(0).GetViewCenter()));
    // Der Zeiger, auf den der Knopf gewirkt hat, war bereits der des Pads
    BOOST_TEST((*gwv(0).GetCursorPos() == view(0).GetViewCenter()));
}

/// Befund 2: Pad-Ereignisse laufen ausserhalb der Partie auf. Einziger Abnehmer von
/// IVideoDriver::FetchPadEvents ist dskGameInterface; im Hauptmenue, in der Lobby und im
/// Ladebildschirm sammelt der Treiber weiter. Beim ersten UpdateInput der Partie wurden alle im
/// Menue gedrueckten Knoepfe als Flanken NACHGELIEFERT - wer im Menue A drueckte, setzte beim
/// Spielstart sofort eine Flagge, und seit der Uebernahme durch Benutzung nimmt derselbe
/// nachgelieferte Druck einem Mausspieler auch noch die Ansicht weg.
BOOST_FIXTURE_TEST_CASE(ButtonsPressedBeforeTheGameStartedAreNotReplayedIntoIt, OneView)
{
    // Alles hier passiert VOR der Partie: der Spieler steckt sein Pad an und navigiert damit
    // durch das Menue. Niemand holt in dieser Zeit ab.
    pads.connect(10);
    for(unsigned i = 0; i < 20; ++i)
        pads.tap(10, PadButton::A);
    pads.tap(10, PadButton::Start);
    pads.button(10, PadButton::B, true); // beim Spielstart noch gedrueckt
    pads.axis(10, PadAxis::LeftX, 1.f);  // ... und der Stick liegt an

    restartDesktop(); // <- jetzt beginnt die Partie

    const Position mousePos = view(0).GetViewCenter() + Position(97, -61);
    step(16, mousePos);

    // Kein einziger Menuedruck ist als Flanke angekommen: die Ansicht gehoert weiter der Maus.
    BOOST_TEST(!view(0).HasPadCursor());
    BOOST_TEST_REQUIRE(gwv(0).GetCursorPos().has_value());
    BOOST_TEST((*gwv(0).GetCursorPos() == mousePos));
    BOOST_TEST(!dsk->GetPadRouter().IsActive(10));

    // Das Geraet selbst ist aber sehr wohl bekannt: das Connected darf NICHT mit weggeworfen
    // werden, sonst waere ein vor dem Spielstart gestecktes Pad fuer den Rest der Partie tot.
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().GetDevices().size() == 1u);

    // ... und ab jetzt zaehlt jede Eingabe ganz normal.
    pads.tap(10, PadButton::Start);
    step(16, mousePos);
    BOOST_TEST(view(0).HasPadCursor());
}

/// Die Gegenprobe zum vorigen Fall: verworfen wird nur, was VOR der Partie aufgelaufen ist.
/// Ein Abziehen im Menue muss durchkommen, sonst fuehrte der Router ein Geraet, das gar nicht
/// mehr steckt, und dessen Slot bliebe fuer immer belegt.
BOOST_FIXTURE_TEST_CASE(DevicesPluggedAndUnpluggedBeforeTheGameStartedAreAccountedFor, OneView)
{
    pads.connect(10);
    pads.tap(10, PadButton::A);
    pads.disconnect(10); // im Menue wieder abgezogen
    pads.connect(11);
    restartDesktop();

    const Position mousePos = view(0).GetViewCenter();
    step(16, mousePos);
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().GetDevices().size() == 1u);
    BOOST_TEST(dsk->GetPadRouter().GetDevices()[0] == 11u);
    BOOST_TEST(!view(0).HasPadCursor());

    // Das abgezogene Pad ist unbekannt und bleibt wirkungslos
    pads.tap(10, PadButton::Start);
    step(16, mousePos);
    BOOST_TEST(!view(0).HasPadCursor());
    // Das steckende dagegen nicht
    pads.tap(11, PadButton::Start);
    step(16, mousePos);
    BOOST_TEST(view(0).HasPadCursor());
}

/// M4 und M5: ein Padereignis fasst weder die Mausposition des Treibers noch den WindowManager
/// an. Beides zusammen ist die strukturelle Zusicherung "Maus und Tastatur bleiben, wie sie sind".
BOOST_FIXTURE_TEST_CASE(PadInputTouchesNeitherTheMouseNorTheWindowManager, TwoViews)
{
    MockupVideoDriver& video = *uiHelper::GetVideoDriver();
    const Position mousePos(123, 45);
    video.SetMousePos(mousePos);
    BOOST_TEST_REQUIRE((VIDEODRIVER.GetMousePos() == mousePos));

    auto* counting = static_cast<CountingDesktop*>(WINDOWMANAGER.Switch(std::make_unique<CountingDesktop>()));
    // Der Desktopwechsel wird erst im Draw() wirksam, und dabei stellt der WindowManager dem
    // neuen Desktop einmal die aktuelle Mausposition zu (genau wie in
    // tests/s25Main/UI/testWindowManager.cpp:49-53 erwartet). Danach bei null anfangen.
    WINDOWMANAGER.Draw();
    counting->numMouseMove = counting->numLeftDown = counting->numLeftUp = 0;
    counting->numRightDown = counting->numKeyDown = 0;
    BOOST_TEST_REQUIRE(counting->total() == 0u);

    pads.connect(10);
    pads.connect(11);
    pads.axis(10, PadAxis::LeftX, 1.f);
    pads.axis(11, PadAxis::LeftY, 1.f);
    pads.tap(10, PadButton::A);
    pads.tap(11, PadButton::A);
    step(100, Position(-10000, -10000));
    step(100, Position(-10000, -10000));

    // M4: der Padzeiger ist NICHT die Mausposition des Treibers
    BOOST_TEST((VIDEODRIVER.GetMousePos() == mousePos));
    // M5: kein einziges Padereignis hat den WindowManager erreicht
    BOOST_TEST(counting->total() == 0u);

    // Gegenprobe, dass die Zaehlung ueberhaupt zaehlt - sonst waere die Null wertlos
    WINDOWMANAGER.Msg_MouseMove(MouseCoords(Position(5, 5)));
    BOOST_TEST(counting->numMouseMove == 1u);

    // Aufgeraeumt wird von uiHelper::Fixture::~Fixture (uiHelpers.cpp:58-65): es schaltet auf
    // einen frischen DummyDesktop zurueck, sobald der aktuelle keiner ist.
}

/// Charakterisierung des Tastaturpfades: die heutigen Zahlen, damit ein spaeterer Umbau sie
/// nicht unbemerkt verschiebt. Bewusst am Hauptspieler - die Tastatur steuert weiterhin ihn.
BOOST_FIXTURE_TEST_CASE(KeyboardScrollingIsUnchangedByGamepadSupport, TwoViews)
{
    const DrawPoint before0 = gwv(0).GetOffset();
    const DrawPoint before1 = gwv(1).GetOffset();

    BOOST_TEST(dsk->Msg_KeyDown(KeyEvent(KeyType::Left)));
    BOOST_TEST((gwv(0).GetOffset() == before0 + DrawPoint(-30, 0)));
    BOOST_TEST(dsk->Msg_KeyDown(KeyEvent(KeyType::Right)));
    BOOST_TEST(dsk->Msg_KeyDown(KeyEvent(KeyType::Down)));
    BOOST_TEST((gwv(0).GetOffset() == before0 + DrawPoint(0, 30)));
    BOOST_TEST(dsk->Msg_KeyDown(KeyEvent(KeyType::Up)));
    BOOST_TEST((gwv(0).GetOffset() == before0));
    // Die zweite Ansicht bleibt davon unberuehrt - die Tastatur gehoert dem Hauptspieler
    BOOST_TEST((gwv(1).GetOffset() == before1));
}

/// Das Ziel des Gesamtprojekts, im Kleinen: vier Pads, vier Ansichten, jede fuer sich. Damit ist
/// die Zwei-Fall-Sonderloesung ausgeschlossen.
BOOST_FIXTURE_TEST_CASE(FourPadsDriveFourIndependentCursors, FourViews)
{
    for(PadDeviceId dev = 10; dev <= 13; ++dev)
        pads.pickUp(dev);
    step(0);

    std::vector<Position> start;
    for(unsigned i = 0; i < 4; ++i)
    {
        BOOST_TEST_REQUIRE(view(i).HasPadCursor());
        start.push_back(view(i).GetPadCursor());
        BOOST_TEST((start.back() == view(i).GetViewCenter()));
    }
    // Alle vier Startpunkte sind verschieden - sonst laegen die Ansichten uebereinander
    for(unsigned i = 0; i < 4; ++i)
        for(unsigned j = i + 1; j < 4; ++j)
            BOOST_TEST((start[i] != start[j]));

    // Nur Pad 2 bewegt sich
    pads.axis(12, PadAxis::LeftX, 1.f);
    step(100);
    const int d = expectedMove(1.f, 100);
    for(unsigned i = 0; i < 4; ++i)
    {
        BOOST_TEST_INFO("view " << i);
        const Position expected = (i == 2) ? start[i] + Position(d, 0) : start[i];
        BOOST_TEST((*gwv(i).GetCursorPos() == expected));
    }
    // ... und keiner der Zeiger hat seinen Viewport verlassen
    for(unsigned i = 0; i < 4; ++i)
    {
        BOOST_TEST_INFO("view " << i);
        BOOST_TEST(view(i).ContainsViewPos(*gwv(i).GetCursorPos()));
    }
}

/// Abziehen im laufenden Betrieb: die Ansicht verliert ihren Padzeiger und faellt an die Maus
/// zurueck. Der Spieler verlaesst dabei NICHT das Spiel - er ist nur bis zum Wiederanstecken
/// ohne eigenes Eingabegeraet.
BOOST_FIXTURE_TEST_CASE(UnpluggingAPadReturnsItsViewToTheMouse, TwoViews)
{
    pads.pickUp(10);
    pads.pickUp(11);
    step(0);
    BOOST_TEST_REQUIRE(view(0).HasPadCursor());
    BOOST_TEST_REQUIRE(view(1).HasPadCursor());

    pads.disconnect(10);
    const Position mousePos = view(0).GetViewCenter();
    step(16, mousePos);

    BOOST_TEST(!view(0).HasPadCursor());
    BOOST_TEST(view(1).HasPadCursor()); // der Nachbar ist voellig unbeeindruckt
    BOOST_TEST_REQUIRE(gwv(0).GetCursorPos().has_value());
    BOOST_TEST((*gwv(0).GetCursorPos() == mousePos));

    // Wieder anstecken - mit NEUER Kennung, so wie es ein echter Treiber liefert
    pads.pickUp(20);
    step(0, mousePos);
    BOOST_TEST(view(0).HasPadCursor());
    BOOST_TEST((view(0).GetPadCursor() == view(0).GetViewCenter()));
    // Ereignisse der ALTEN Kennung sind wirkungslos: eine Kennung wird nie wiederverwendet
    const Position afterReconnect = view(0).GetPadCursor();
    pads.axis(10, PadAxis::LeftX, 1.f);
    step(100, mousePos);
    BOOST_TEST((view(0).GetPadCursor() == afterReconnect));
}

// ============================================================================================
// Rechter Stick und Trigger - die Kennlinien, ohne Ansicht und ohne Spiel
// ============================================================================================

/// Die Triggerkennlinie: unter der Totzone genau 0, darueber auf [0,1] GEDEHNT. Ohne die
/// Dehnung sprungt der Zoom beim Ueberschreiten der Totzone von 0 auf 15 Prozent - fuehlbar
/// als Ruck, gerade am Fernseher.
BOOST_AUTO_TEST_CASE(TheTriggerCurveStartsAtZeroAndReachesOne)
{
    BOOST_TEST(PadRouter::FilterTrigger(0.f) == 0.f);
    BOOST_TEST(PadRouter::FilterTrigger(PadRouter::TriggerDeadzone) == 0.f);
    BOOST_TEST(PadRouter::FilterTrigger(0.1f) == 0.f);
    BOOST_TEST(PadRouter::FilterTrigger(1.f) == 1.f);
    // Knapp ueber der Totzone: knapp ueber null, nicht knapp ueber der Totzone selbst.
    BOOST_TEST(PadRouter::FilterTrigger(PadRouter::TriggerDeadzone + 0.001f) < 0.01f);
    BOOST_TEST(PadRouter::FilterTrigger(PadRouter::TriggerDeadzone + 0.001f) > 0.f);
    // Monoton
    BOOST_TEST(PadRouter::FilterTrigger(0.5f) < PadRouter::FilterTrigger(0.8f));
    // Verrutschte Kalibrierung ueber 1 wird gekappt
    BOOST_TEST(PadRouter::FilterTrigger(1.7f) == 1.f);
}

/// Zeiger und Kamera sind zwei getrennte Meldungen mit zwei Geschwindigkeiten - und der eine
/// Stick erzeugt nie die Meldung des anderen.
BOOST_AUTO_TEST_CASE(TheTwoSticksAreReportedSeparately)
{
    PadRouter router;
    RecordingTarget target;
    router.SetNumSlots(2);
    router.OnEvent(PadEvent::Connected(1));
    router.OnEvent(PadEvent::Button(1, PadButton::Start, true));
    router.UpdateMotion(0, target);
    target.clear();

    router.OnEvent(PadEvent::Axis(1, PadAxis::LeftX, 1.f));
    router.UpdateMotion(1000, target);
    BOOST_TEST(target.totalMove(0).x == static_cast<int>(PadRouter::PixelsPerSecond));
    BOOST_TEST(target.cameraMoves.empty());

    target.clear();
    router.OnEvent(PadEvent::Axis(1, PadAxis::LeftX, 0.f));
    router.OnEvent(PadEvent::Axis(1, PadAxis::RightY, -1.f));
    router.UpdateMotion(1000, target);
    BOOST_TEST(target.moves.empty());
    BOOST_TEST(target.totalCameraMove(0).y == -static_cast<int>(PadRouter::CameraPixelsPerSecond));
}

/// Der rechte Stick gehoert dem Slot seines eigenen Geraets - so wie der linke.
BOOST_AUTO_TEST_CASE(TheCameraGoesToTheOwnSlotOnly)
{
    PadRouter router;
    RecordingTarget target;
    router.SetNumSlots(2);
    for(const PadDeviceId dev : {1u, 2u})
    {
        router.OnEvent(PadEvent::Connected(dev));
        router.OnEvent(PadEvent::Button(dev, PadButton::Start, true));
    }
    router.UpdateMotion(0, target);
    target.clear();

    router.OnEvent(PadEvent::Axis(2, PadAxis::RightX, 1.f));
    router.UpdateMotion(100, target);
    BOOST_TEST(target.totalCameraMove(0) == Position(0, 0));
    BOOST_TEST(target.totalCameraMove(1).x > 0);
}

/// Beide Trigger gleichzeitig heben sich auf - das faellt aus der Differenz und ist kein
/// Sonderfall im Code.
BOOST_AUTO_TEST_CASE(BothTriggersTogetherCancelOut)
{
    PadRouter router;
    RecordingTarget target;
    router.SetNumSlots(1);
    router.OnEvent(PadEvent::Connected(1));
    router.OnEvent(PadEvent::Button(1, PadButton::Start, true));
    router.UpdateMotion(0, target);
    target.clear();

    router.OnEvent(PadEvent::Axis(1, PadAxis::TriggerRight, 1.f));
    router.UpdateMotion(100, target);
    BOOST_TEST(target.totalZoom(0) > 0.f);

    target.clear();
    router.OnEvent(PadEvent::Axis(1, PadAxis::TriggerLeft, 1.f));
    router.UpdateMotion(100, target);
    BOOST_TEST(target.zooms.empty());

    target.clear();
    router.OnEvent(PadEvent::Axis(1, PadAxis::TriggerRight, 0.f));
    router.UpdateMotion(100, target);
    BOOST_TEST(target.totalZoom(0) < 0.f);
}

/// Auch der rechte Stick nimmt eine Ansicht in Besitz - "bewegt die Kamera" und "uebernimmt
/// die Ansicht" duerfen nicht auseinanderfallen. Ein blosses Zittern in der Totzone aber nicht.
BOOST_AUTO_TEST_CASE(TheRightStickAlsoClaimsAViewButOnlyBeyondTheDeadzone)
{
    PadRouter router;
    RecordingTarget target;
    router.SetNumSlots(1);
    router.OnEvent(PadEvent::Connected(1));
    BOOST_TEST_REQUIRE(router.GetSlot(1) == PadRouter::NoSlot);

    router.OnEvent(PadEvent::Axis(1, PadAxis::RightX, 0.2f)); // innerhalb der Totzone
    BOOST_TEST(router.GetSlot(1) == PadRouter::NoSlot);
    BOOST_TEST(!router.IsActive(1));

    router.OnEvent(PadEvent::Axis(1, PadAxis::RightX, 0.9f));
    BOOST_TEST(router.GetSlot(1) == 0u);
    BOOST_TEST(router.IsActive(1));
}

BOOST_AUTO_TEST_SUITE_END()
