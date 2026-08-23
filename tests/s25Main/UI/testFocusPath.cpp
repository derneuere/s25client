// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

// Phase 4, Schritt 2: Fokusnavigation innerhalb EINES Fensters, per Pad bedienbar.
//
// Ohne Pad, ohne Partie, ohne OpenGL. Die entscheidende Aussage steht in
// TwoFocusPathsOnTheSameWindowAreIndependent: vier Spieler sind vier Fokusse, und kein Control
// weiss davon. Die zweite entscheidende Aussage steht in NothingIsConsumedWithoutARoot: ohne
// gesetzte Wurzel verbraucht der Fokus GAR NICHTS, der Weltzeiger bewegt sich also wie vorher.

#include "Loader.h"
#include "controls/ctrlOptionGroup.h"
#include "controls/ctrlProgress.h"
#include "controls/ctrlTextButton.h"
#include "input/FocusPath.h"
#include "mockupDrivers/MockupVideoDriver.h"
#include "uiHelper/uiHelpers.hpp"
#include <boost/test/unit_test.hpp>
#include <vector>

namespace {
struct RecordingWnd : Window
{
    RecordingWnd() : Window(nullptr, 0, DrawPoint(0, 0), Extent(400, 400)) {}
    std::vector<unsigned> clicks;
    std::vector<unsigned short> progressChanges;
    void Msg_ButtonClick(unsigned id) override { clicks.push_back(id); }
    void Msg_ProgressChange(unsigned, unsigned short pos) override { progressChanges.push_back(pos); }
};

unsigned focusedId(const FocusPath& fp)
{
    const Window* w = fp.GetFocused();
    return w ? w->GetID() : 0xFFFFFFFFu;
}

/// Genau ein Rasterschritt in einer Richtung, unabhaengig von Wiederholrate und Rasterweite.
void oneStep(FocusPath& fp, const Position& dir)
{
    // Erst den Stick loslassen, damit die Wiederholsperre zurueckgesetzt wird...
    fp.OnPadMove(Position(0, 0), 16);
    // ...dann in einem Frame die volle Rasterweite zuruecklegen.
    fp.OnPadMove(dir * FocusPath::StepDistance, 16);
}
} // namespace

BOOST_FIXTURE_TEST_SUITE(FocusPathSuite, uiHelper::Fixture)

// --------------------------------------------------------------------------------------------
// HARTE RANDBEDINGUNG: ohne gesetzte Wurzel verbraucht der Fokus nichts. Ein Einzelspieler, der
// nie ein Fenster betritt, sieht exakt das Verhalten von Phase 3.
// --------------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(NothingIsConsumedWithoutARoot)
{
    FocusPath fp;
    BOOST_TEST(!fp.IsActive());
    BOOST_TEST(fp.GetRoot() == static_cast<Window*>(nullptr));
    BOOST_TEST(fp.GetFocused() == static_cast<Window*>(nullptr));
    BOOST_TEST(!fp.OnPadMove(Position(1000, 1000), 16));
    BOOST_TEST(!fp.OnPadButton(PadButton::A, true));
    BOOST_TEST(!fp.OnPadButton(PadButton::A, false));
    BOOST_TEST(!fp.Activate());
    BOOST_TEST(!fp.Move(FocusPath::Dir::Next));
    BOOST_TEST(fp.Collect().empty());
}

// Ein Fenster ohne bedienbares Control nimmt den Fokus gar nicht erst an - sonst haenge der
// Spieler darin fest.
BOOST_AUTO_TEST_CASE(WindowWithoutFocusableControlsIsNotEntered)
{
    RecordingWnd wnd;
    wnd.AddText(1, DrawPoint(5, 5), "nur Text", COLOR_YELLOW, FontStyle{}, NormalFont);
    FocusPath fp;
    BOOST_TEST(!fp.SetRoot(&wnd));
    BOOST_TEST(!fp.IsActive());
    BOOST_TEST(!fp.OnPadButton(PadButton::A, true)); // faellt an die Welt durch
}

BOOST_AUTO_TEST_CASE(SetRootFocusesTheFirstControlInIdOrder)
{
    RecordingWnd wnd;
    // Bewusst in verkehrter Reihenfolge angelegt: massgeblich ist die ID, nicht die
    // Anlegereihenfolge - das ist die Ordnung der std::map in Window und damit reproduzierbar.
    wnd.AddTextButton(30, DrawPoint(0, 80), Extent(60, 20), TextureColor::Green1, "C", NormalFont);
    wnd.AddTextButton(10, DrawPoint(0, 0), Extent(60, 20), TextureColor::Green1, "A", NormalFont);
    wnd.AddTextButton(20, DrawPoint(0, 40), Extent(60, 20), TextureColor::Green1, "B", NormalFont);

    FocusPath fp;
    BOOST_TEST_REQUIRE(fp.SetRoot(&wnd));
    BOOST_TEST(fp.IsActive());
    BOOST_TEST(focusedId(fp) == 10u);
    BOOST_TEST(fp.Collect().size() == 3u);
}

BOOST_AUTO_TEST_CASE(NextAndPrevDoNotWrapAround)
{
    RecordingWnd wnd;
    for(unsigned i = 1; i <= 4; i++)
        wnd.AddTextButton(i, DrawPoint(0, static_cast<int>(i) * 30), Extent(60, 20), TextureColor::Green1, "x",
                          NormalFont);
    FocusPath fp;
    BOOST_TEST_REQUIRE(fp.SetRoot(&wnd));
    BOOST_TEST(focusedId(fp) == 1u);
    // Am Anfang gibt es kein Zurueck - der Fokus bleibt stehen und rutscht NICHT ans Ende.
    BOOST_TEST(!fp.Move(FocusPath::Dir::Prev));
    BOOST_TEST(focusedId(fp) == 1u);

    for(unsigned i = 2; i <= 4; i++)
    {
        BOOST_TEST(fp.Move(FocusPath::Dir::Next));
        BOOST_TEST(focusedId(fp) == i);
    }
    // Am Ende genauso.
    BOOST_TEST(!fp.Move(FocusPath::Dir::Next));
    BOOST_TEST(focusedId(fp) == 4u);
    BOOST_TEST(fp.Move(FocusPath::Dir::Prev));
    BOOST_TEST(focusedId(fp) == 3u);
}

BOOST_AUTO_TEST_CASE(GeometricMoveFollowsTheLayoutNotTheId)
{
    RecordingWnd wnd;
    // Ein 2x2-Raster, dessen IDs BEWUSST quer zur Anordnung liegen. Waere Links/Rechts nur die
    // ID-Ordnung, muesste dieser Fall scheitern.
    //   ID 1 = oben links, ID 4 = oben rechts, ID 2 = unten links, ID 3 = unten rechts
    wnd.AddTextButton(1, DrawPoint(0, 0), Extent(60, 20), TextureColor::Green1, "ol", NormalFont);
    wnd.AddTextButton(4, DrawPoint(100, 0), Extent(60, 20), TextureColor::Green1, "or", NormalFont);
    wnd.AddTextButton(2, DrawPoint(0, 100), Extent(60, 20), TextureColor::Green1, "ul", NormalFont);
    wnd.AddTextButton(3, DrawPoint(100, 100), Extent(60, 20), TextureColor::Green1, "ur", NormalFont);

    FocusPath fp;
    BOOST_TEST_REQUIRE(fp.SetRoot(&wnd));
    BOOST_TEST(focusedId(fp) == 1u);
    BOOST_TEST(fp.Move(FocusPath::Dir::Right));
    BOOST_TEST(focusedId(fp) == 4u);
    BOOST_TEST(fp.Move(FocusPath::Dir::Down));
    BOOST_TEST(focusedId(fp) == 3u);
    BOOST_TEST(fp.Move(FocusPath::Dir::Left));
    BOOST_TEST(focusedId(fp) == 2u);
    BOOST_TEST(fp.Move(FocusPath::Dir::Up));
    BOOST_TEST(focusedId(fp) == 1u);
    // Ueber den Rand hinaus gibt es nichts - kein Umlauf, kein Sprung in ein fremdes Fenster.
    BOOST_TEST(!fp.Move(FocusPath::Dir::Up));
    BOOST_TEST(!fp.Move(FocusPath::Dir::Left));
    BOOST_TEST(focusedId(fp) == 1u);
}

BOOST_AUTO_TEST_CASE(DisabledAndInvisibleControlsAreSkipped)
{
    RecordingWnd wnd;
    auto* a = wnd.AddTextButton(1, DrawPoint(0, 0), Extent(60, 20), TextureColor::Green1, "A", NormalFont);
    auto* b = wnd.AddTextButton(2, DrawPoint(0, 40), Extent(60, 20), TextureColor::Green1, "B", NormalFont);
    wnd.AddTextButton(3, DrawPoint(0, 80), Extent(60, 20), TextureColor::Green1, "C", NormalFont);

    b->SetEnabled(false);
    FocusPath fp;
    BOOST_TEST_REQUIRE(fp.SetRoot(&wnd));
    BOOST_TEST(fp.Collect().size() == 2u);
    BOOST_TEST(focusedId(fp) == 1u);
    BOOST_TEST(fp.Move(FocusPath::Dir::Next));
    BOOST_TEST(focusedId(fp) == 3u); // B uebersprungen

    a->SetVisible(false);
    BOOST_TEST(fp.Collect().size() == 1u);
    BOOST_TEST(!fp.Move(FocusPath::Dir::Prev));
    BOOST_TEST(focusedId(fp) == 3u);
}

BOOST_AUTO_TEST_CASE(ContainersAreTraversedButNotFocused)
{
    RecordingWnd wnd;
    auto* group = wnd.AddOptionGroup(5, GroupSelectType::Check);
    group->AddTextButton(1, DrawPoint(0, 0), Extent(60, 20), TextureColor::Green1, "A", NormalFont);
    group->AddTextButton(2, DrawPoint(0, 40), Extent(60, 20), TextureColor::Green1, "B", NormalFont);

    FocusPath fp;
    BOOST_TEST_REQUIRE(fp.SetRoot(&wnd));
    // Die Gruppe selbst ist keine Fokusstation, ihre Knoepfe schon.
    const auto candidates = fp.Collect();
    BOOST_TEST_REQUIRE(candidates.size() == 2u);
    BOOST_TEST(candidates[0].path.size() == 2u); // Gruppe -> Knopf
    BOOST_TEST(candidates[0].path[0] == 5u);
    BOOST_TEST(candidates[0].path[1] == 1u);

    // Optionsgruppen brauchen keinen Sonderfall: ctrlButton::Activate laeuft ueber
    // GetParent()->Msg_ButtonClick und landet damit in ctrlOptionGroup::Msg_ButtonClick.
    BOOST_TEST(fp.Move(FocusPath::Dir::Next));
    BOOST_TEST(focusedId(fp) == 2u);
    BOOST_TEST(fp.Activate());
    BOOST_TEST(group->GetSelection() == 2u);
}

// --------------------------------------------------------------------------------------------
// DER Nachweis fuer Negativkontrolle M6 (ein globaler Fokus statt einem je Spieler).
// Zwei Fokusse auf DEMSELBEN Fenster sind vollstaendig unabhaengig.
// --------------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(TwoFocusPathsOnTheSameWindowAreIndependent)
{
    RecordingWnd wnd;
    for(unsigned i = 1; i <= 4; i++)
        wnd.AddTextButton(i, DrawPoint(0, static_cast<int>(i) * 30), Extent(60, 20), TextureColor::Green1, "x",
                          NormalFont);

    FocusPath p0, p1;
    BOOST_TEST_REQUIRE(p0.SetRoot(&wnd));
    BOOST_TEST_REQUIRE(p1.SetRoot(&wnd));
    BOOST_TEST(focusedId(p0) == 1u);
    BOOST_TEST(focusedId(p1) == 1u);

    // Spieler 1 wandert. Spieler 0 bleibt, wo er ist.
    BOOST_TEST(p1.Move(FocusPath::Dir::Next));
    BOOST_TEST(p1.Move(FocusPath::Dir::Next));
    BOOST_TEST(focusedId(p1) == 3u);
    BOOST_TEST(focusedId(p0) == 1u);

    // Jeder loest SEINEN Knopf aus.
    BOOST_TEST(p0.Activate());
    BOOST_TEST(p1.Activate());
    BOOST_TEST_REQUIRE(wnd.clicks.size() == 2u);
    BOOST_TEST(wnd.clicks[0] == 1u);
    BOOST_TEST(wnd.clicks[1] == 3u);

    // Und der Zustand des Controls selbst traegt keinen Fokus: Spieler 0 kann danach immer noch
    // seinen eigenen Knopf bedienen.
    BOOST_TEST(p0.Activate());
    BOOST_TEST(wnd.clicks.back() == 1u);
}

BOOST_AUTO_TEST_CASE(BrokenPathLosesFocusWithoutCrashing)
{
    RecordingWnd wnd;
    wnd.AddTextButton(1, DrawPoint(0, 0), Extent(60, 20), TextureColor::Green1, "A", NormalFont);
    wnd.AddTextButton(2, DrawPoint(0, 40), Extent(60, 20), TextureColor::Green1, "B", NormalFont);

    FocusPath fp;
    BOOST_TEST_REQUIRE(fp.SetRoot(&wnd));
    BOOST_TEST(fp.Move(FocusPath::Dir::Next));
    BOOST_TEST(focusedId(fp) == 2u);

    // Das fokussierte Control verschwindet zur Laufzeit. Der Pfad ist eine ID-Kette, kein
    // Zeiger - hier darf nichts dereferenziert werden, was es nicht mehr gibt.
    wnd.DeleteCtrl(2);
    BOOST_TEST(fp.GetFocused() == static_cast<Window*>(nullptr));
    BOOST_TEST(!fp.Activate());
    BOOST_TEST(wnd.clicks.empty());

    // Der naechste Schritt faengt den Spieler wieder ein.
    BOOST_TEST(fp.Move(FocusPath::Dir::Next));
    BOOST_TEST(focusedId(fp) == 1u);
}

// --------------------------------------------------------------------------------------------
// Padbedienung
// --------------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(StickNeedsTheFullStepDistanceForOneStep)
{
    RecordingWnd wnd;
    for(unsigned i = 1; i <= 3; i++)
        wnd.AddTextButton(i, DrawPoint(0, static_cast<int>(i) * 30), Extent(60, 20), TextureColor::Green1, "x",
                          NormalFont);
    FocusPath fp;
    BOOST_TEST_REQUIRE(fp.SetRoot(&wnd));

    // Kleine Ausschlaege sammeln sich, loesen aber noch nichts aus.
    const int small = FocusPath::StepDistance / 4;
    for(int i = 0; i < 3; i++)
    {
        BOOST_TEST(fp.OnPadMove(Position(0, small), 16)); // verbraucht - der Weltzeiger bleibt stehen
        BOOST_TEST(focusedId(fp) == 1u);
    }
    // Der vierte Teilschritt macht das Raster voll.
    BOOST_TEST(fp.OnPadMove(Position(0, small), 16));
    BOOST_TEST(focusedId(fp) == 2u);

    // Direkt danach greift die Wiederholsperre: derselbe Weg bewegt nichts.
    BOOST_TEST(fp.OnPadMove(Position(0, FocusPath::StepDistance), 16));
    BOOST_TEST(focusedId(fp) == 2u);

    // Nach Ablauf der Sperre geht es weiter.
    BOOST_TEST(fp.OnPadMove(Position(0, FocusPath::StepDistance), FocusPath::RepeatDelayMs + 1));
    BOOST_TEST(focusedId(fp) == 3u);
}

BOOST_AUTO_TEST_CASE(PadButtonsDriveTheFocus)
{
    RecordingWnd wnd;
    wnd.AddTextButton(1, DrawPoint(0, 0), Extent(60, 20), TextureColor::Green1, "A", NormalFont);
    wnd.AddTextButton(2, DrawPoint(0, 40), Extent(60, 20), TextureColor::Green1, "B", NormalFont);
    FocusPath fp;
    BOOST_TEST_REQUIRE(fp.SetRoot(&wnd));

    BOOST_TEST(fp.OnPadButton(PadButton::DpadDown, true));
    BOOST_TEST(focusedId(fp) == 2u);
    BOOST_TEST(fp.OnPadButton(PadButton::DpadUp, true));
    BOOST_TEST(focusedId(fp) == 1u);
    BOOST_TEST(fp.OnPadButton(PadButton::RightShoulder, true));
    BOOST_TEST(focusedId(fp) == 2u);

    BOOST_TEST(fp.OnPadButton(PadButton::A, true));
    BOOST_TEST_REQUIRE(wnd.clicks.size() == 1u);
    BOOST_TEST(wnd.clicks.back() == 2u);

    // Ein unbelegter Knopf wird trotzdem verbraucht: die Welt darf nicht reagieren, solange der
    // Spieler in einem Fenster steht.
    BOOST_TEST(fp.OnPadButton(PadButton::Y, true));
    BOOST_TEST(wnd.clicks.size() == 1u);

    // B verlaesst das Fenster. Ab da faellt alles wieder an die Welt durch.
    BOOST_TEST(fp.OnPadButton(PadButton::B, true));
    BOOST_TEST(!fp.IsActive());
    BOOST_TEST(!fp.OnPadButton(PadButton::A, true));
    BOOST_TEST(wnd.clicks.size() == 1u);
}

BOOST_AUTO_TEST_CASE(ValueControlsConsumeTheirOwnAxisOnly)
{
    RecordingWnd wnd;
    auto* prog = wnd.AddProgress(1, DrawPoint(0, 0), Extent(120, 20), TextureColor::Green1, 0, 0, 10);
    wnd.AddTextButton(2, DrawPoint(0, 60), Extent(60, 20), TextureColor::Green1, "unten", NormalFont);

    FocusPath fp;
    BOOST_TEST_REQUIRE(fp.SetRoot(&wnd));
    BOOST_TEST(focusedId(fp) == 1u);

    // Waagerecht: der Balken verbraucht den Schritt, der Fokus bleibt.
    oneStep(fp, Position(1, 0));
    BOOST_TEST(focusedId(fp) == 1u);
    BOOST_TEST(prog->GetPosition() == 1);
    BOOST_TEST_REQUIRE(wnd.progressChanges.size() == 1u);
    BOOST_TEST(wnd.progressChanges.back() == 1);

    // Senkrecht: der Balken verbraucht nichts, der Fokus wandert weiter.
    oneStep(fp, Position(0, 1));
    BOOST_TEST(focusedId(fp) == 2u);
    BOOST_TEST(prog->GetPosition() == 1); // unveraendert
}

BOOST_AUTO_TEST_SUITE_END()
