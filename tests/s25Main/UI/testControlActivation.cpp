// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

// Phase 4, Schritt 1: Window::CanFocus()/Window::Activate().
//
// Die eine Aussage, an der die ganze Phase haengt: die rund 350 Klickstellen im Baum
// (AddImageButton/AddTextButton/AddColorButton/AddBuildingIcon) sind ueber GENAU EINE Methode
// erreichbar, OHNE dass der Mauszeiger gefragt wird - und der Mauspfad bleibt dabei
// unveraendert. Beides wird hier geprueft, und zwar im Control und nicht in einem
// Splitscreen-Szenario: die harte Randbedingung "Einzelspieler mit Maus verhaelt sich nicht
// anders" gilt ueberall, nicht nur im Spiel.

#include "Loader.h"
#include "controls/ctrlBuildingIcon.h"
#include "controls/ctrlCheck.h"
#include "controls/ctrlComboBox.h"
#include "controls/ctrlEdit.h"
#include "controls/ctrlList.h"
#include "controls/ctrlProgress.h"
#include "controls/ctrlScrollBar.h"
#include "controls/ctrlTable.h"
#include "controls/ctrlText.h"
#include "controls/ctrlTextButton.h"
#include "driver/KeyEvent.h"
#include "input/FocusPath.h"
#include "driver/MouseCoords.h"
#include "mockupDrivers/MockupVideoDriver.h"
#include "ogl/FontStyle.h"
#include "uiHelper/uiHelpers.hpp"
#include <boost/test/unit_test.hpp>
#include <vector>

namespace {
/// Elternfenster, das sich jeden Rueckruf merkt, den ein Control nach oben schickt.
/// Bewusst kein turtle-Mock: hier wird die REIHENFOLGE und die Gleichheit zweier
/// Aufzeichnungen verglichen, nicht eine Erwartung gesetzt.
struct RecordingWnd : Window
{
    RecordingWnd() : Window(nullptr, 0, DrawPoint(0, 0), Extent(400, 400)) {}

    std::vector<unsigned> clicks;
    std::vector<std::pair<unsigned, bool>> checkboxes;
    std::vector<unsigned> listChosen;
    std::vector<int> listSelected;
    std::vector<unsigned> tableChosen;
    std::vector<unsigned> comboSelected;
    std::vector<unsigned short> progressChanges;
    std::vector<unsigned short> scrollChanges;

    void Msg_ButtonClick(unsigned id) override { clicks.push_back(id); }
    void Msg_CheckboxChange(unsigned id, bool checked) override { checkboxes.emplace_back(id, checked); }
    void Msg_ListChooseItem(unsigned, unsigned sel) override { listChosen.push_back(sel); }
    void Msg_ListSelectItem(unsigned, int sel) override { listSelected.push_back(sel); }
    void Msg_TableChooseItem(unsigned, unsigned sel) override { tableChosen.push_back(sel); }
    void Msg_ComboSelectItem(unsigned, unsigned sel) override { comboSelected.push_back(sel); }
    void Msg_ProgressChange(unsigned, unsigned short pos) override { progressChanges.push_back(pos); }
    void Msg_ScrollChange(unsigned, unsigned short pos) override { scrollChanges.push_back(pos); }
};

/// Legt den Mauszeiger weit ausserhalb jedes Controls dieser Tests.
constexpr Position kMouseFarAway(10000, 10000);

void putMouseFarAway()
{
    uiHelper::GetVideoDriver()->SetMousePos(kMouseFarAway);
}
} // namespace

BOOST_FIXTURE_TEST_SUITE(ControlActivation, uiHelper::Fixture)

// --------------------------------------------------------------------------------------------
// Die Vorgaben der Basisklasse. Ohne sie waere jedes Bildchen und jeder Text fokussierbar und
// die Fokusnavigation unbenutzbar.
// --------------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(BaseClassDefaultsDoNothing)
{
    RecordingWnd wnd;
    Window plain(&wnd, 1, DrawPoint(0, 0), Extent(10, 10));
    BOOST_TEST(!plain.CanFocus());
    BOOST_TEST(!plain.Activate());
    BOOST_TEST(!plain.GetValueRange().has_value());
    BOOST_TEST(!plain.SetValue(3));
    BOOST_TEST(!plain.StepValue(Position(1, 1)));
    BOOST_TEST(!plain.WantsTextInput());

    // Reine Anzeige: ctrlText hat keine Ueberschreibung und darf keine bekommen.
    auto* txt = wnd.AddText(2, DrawPoint(5, 5), "Hallo", COLOR_YELLOW, FontStyle{}, NormalFont);
    BOOST_TEST(!txt->CanFocus());
    BOOST_TEST(!txt->Activate());

    BOOST_TEST(wnd.clicks.empty());
}

// --------------------------------------------------------------------------------------------
// DER Kernnachweis: Activate() klickt, obwohl die Maus weit weg ist.
// Negativkontrolle M9 (Activate verlangt zusaetzlich IsMouseOver) wird hier rot.
// Negativkontrolle M8 (Activate prueft isEnabled nicht) wird hier rot.
// --------------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(ButtonActivatesWithoutAnyMouse)
{
    RecordingWnd wnd;
    auto* bt = wnd.AddTextButton(7, DrawPoint(10, 10), Extent(80, 20), TextureColor::Green1, "Klick", NormalFont);
    BOOST_TEST_REQUIRE(bt != nullptr);

    putMouseFarAway();
    BOOST_TEST(!bt->IsMouseOver()); // Beleg: der globale Maus-Singleton steht NICHT auf dem Knopf

    BOOST_TEST(bt->CanFocus());
    BOOST_TEST(bt->Activate());
    BOOST_TEST_REQUIRE(wnd.clicks.size() == 1u);
    BOOST_TEST(wnd.clicks[0] == 7u);

    // Abgeschaltet: weder fokussierbar noch aktivierbar, und es kommt NICHTS dazu.
    bt->SetEnabled(false);
    BOOST_TEST(!bt->CanFocus());
    BOOST_TEST(!bt->Activate());
    BOOST_TEST(wnd.clicks.size() == 1u);
    bt->SetEnabled(true);

    // Unsichtbar: dito.
    bt->SetVisible(false);
    BOOST_TEST(!bt->CanFocus());
    BOOST_TEST(!bt->Activate());
    BOOST_TEST(wnd.clicks.size() == 1u);
    bt->SetVisible(true);
    BOOST_TEST(bt->Activate());
    BOOST_TEST(wnd.clicks.size() == 2u);
}

// --------------------------------------------------------------------------------------------
// REGRESSIONSWACHE Einzelspieler: der Mauspfad ist unveraendert, VOR und NACH einem Activate().
// Negativkontrolle M10 (ctrlButton::Msg_LeftUp bekommt ein && IsFocused()) wird hier rot.
// --------------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(MousePathIsUnchangedByActivate)
{
    RecordingWnd wnd;
    auto* bt = wnd.AddTextButton(7, DrawPoint(10, 10), Extent(80, 20), TextureColor::Green1, "Klick", NormalFont);
    const MouseCoords over(20, 15);
    const MouseCoords away(1000, 1000);

    // 1. Maus weit weg -> kein Klick (unveraendert zu vorher).
    bt->Msg_LeftDown(away);
    bt->Msg_LeftUp(away);
    BOOST_TEST(wnd.clicks.empty());

    // 2. Maus drauf -> Klick.
    bt->Msg_LeftDown(over);
    BOOST_TEST(bt->Msg_LeftUp(over));
    BOOST_TEST_REQUIRE(wnd.clicks.size() == 1u);
    BOOST_TEST(wnd.clicks.back() == 7u);

    // 3. Runter auf dem Knopf, hoch daneben -> kein Klick (unveraendert).
    bt->Msg_LeftDown(over);
    BOOST_TEST(!bt->Msg_LeftUp(away));
    BOOST_TEST(wnd.clicks.size() == 1u);

    // 4. Nach einem Padklick muss der Mauspfad genau so weiterlaufen. Insbesondere darf der
    //    Knopf nicht in ButtonState::Pressed haengenbleiben - sonst loeste ein spaeteres
    //    Msg_LeftUp ohne vorheriges Msg_LeftDown aus.
    putMouseFarAway();
    BOOST_TEST(bt->Activate());
    BOOST_TEST(wnd.clicks.size() == 2u);
    BOOST_TEST(!bt->Msg_LeftUp(over)); // kein Msg_LeftDown davor -> darf NICHT klicken
    BOOST_TEST(wnd.clicks.size() == 2u);
    bt->Msg_LeftDown(over);
    BOOST_TEST(bt->Msg_LeftUp(over));
    BOOST_TEST(wnd.clicks.size() == 3u);
}

// --------------------------------------------------------------------------------------------
// Ein Mausklick trifft das GEKLICKTE Control, nicht irgendein anderes, auch wenn ein anderes
// gerade per Pad benutzt wurde. Gegenstueck zu M10.
// --------------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(MouseClickHitsTheClickedControlNotThePadUsedOne)
{
    RecordingWnd wnd;
    auto* btA = wnd.AddTextButton(1, DrawPoint(0, 0), Extent(50, 20), TextureColor::Green1, "A", NormalFont);
    auto* btB = wnd.AddTextButton(2, DrawPoint(0, 40), Extent(50, 20), TextureColor::Green1, "B", NormalFont);

    putMouseFarAway();
    BOOST_TEST(btB->Activate());
    BOOST_TEST_REQUIRE(wnd.clicks.size() == 1u);
    BOOST_TEST(wnd.clicks.back() == 2u);

    const MouseCoords onA(10, 10);
    btA->Msg_LeftDown(onA);
    btA->Msg_LeftUp(onA);
    BOOST_TEST_REQUIRE(wnd.clicks.size() == 2u);
    BOOST_TEST(wnd.clicks.back() == 1u); // A, nicht B
}

// --------------------------------------------------------------------------------------------
// BEFUND B2: ein Padklick darf einen GLEICHZEITIG gehaltenen Mausklick nicht verschlucken.
//
// Der Mausklick besteht aus zwei Ereignissen (Msg_LeftDown/Msg_LeftUp) und lebt zwischen ihnen
// im Feld `state` desselben Controls. Wenn ein Padspieler in diesem Moment denselben Knopf
// aktiviert und dabei `state` anfasst, ist der Mausklick ersatzlos weg. Das ist genau die harte
// Randbedingung: der Mauspfad darf sich durch die Anwesenheit eines Pads nicht aendern.
//
// Negativkontrolle (die urspruengliche Fassung von ctrlButton::Activate, die `state` auf Up
// setzte, um ein Haengenbleiben zu verhindern) wird hier rot.
// --------------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(ActivateDoesNotSwallowAHeldMouseButton)
{
    RecordingWnd wnd;
    auto* bt = wnd.AddTextButton(7, DrawPoint(10, 10), Extent(80, 20), TextureColor::Green1, "Klick", NormalFont);
    const MouseCoords over(20, 15);

    // Der Mausspieler haelt die Taste auf dem Knopf ...
    BOOST_TEST_REQUIRE(bt->Msg_LeftDown(over));
    // ... und im selben Moment loest der Padspieler denselben Knopf aus.
    BOOST_TEST(bt->Activate());
    BOOST_TEST_REQUIRE(wnd.clicks.size() == 1u);

    // Der Mausspieler laesst los: SEIN Klick muss ebenfalls ankommen.
    BOOST_TEST(bt->Msg_LeftUp(over));
    BOOST_TEST(wnd.clicks.size() == 2u);

    // Gegenprobe in der anderen Richtung: nach einem Padklick OHNE gehaltene Maustaste darf ein
    // blosses Msg_LeftUp weiterhin nichts ausloesen (der Knopf haengt nicht in Pressed fest).
    putMouseFarAway();
    BOOST_TEST(bt->Activate());
    BOOST_TEST_REQUIRE(wnd.clicks.size() == 3u);
    BOOST_TEST(!bt->Msg_LeftUp(over));
    BOOST_TEST(wnd.clicks.size() == 3u);
}

// --------------------------------------------------------------------------------------------
// ctrlBuildingIcon, ctrlTextButton, ctrlImageButton und ctrlColorButton erben von ctrlButton.
// Eine einzige Methode deckt sie alle - genau das ist der Ertrag von Schritt 1.
// --------------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(ButtonSubclassesInheritActivation)
{
    RecordingWnd wnd;
    putMouseFarAway();

    auto* icon = wnd.AddBuildingIcon(11, DrawPoint(0, 0), BuildingType::Woodcutter, Nation::Africans);
    BOOST_TEST(icon->CanFocus());
    BOOST_TEST(icon->Activate());

    auto* colorBt = wnd.AddColorButton(12, DrawPoint(0, 40), Extent(30, 20), TextureColor::Green1, COLOR_RED);
    BOOST_TEST(colorBt->CanFocus());
    BOOST_TEST(colorBt->Activate());

    auto* imgBt = wnd.AddImageButton(13, DrawPoint(0, 80), Extent(30, 20), TextureColor::Green1,
                                     LOADER.GetImageN("io", 32));
    BOOST_TEST(imgBt->CanFocus());
    BOOST_TEST(imgBt->Activate());

    BOOST_TEST_REQUIRE(wnd.clicks.size() == 3u);
    BOOST_TEST(wnd.clicks[0] == 11u);
    BOOST_TEST(wnd.clicks[1] == 12u);
    BOOST_TEST(wnd.clicks[2] == 13u);
}

// --------------------------------------------------------------------------------------------
// Aequivalenztabelle: je Controltyp erzeugt Activate() genau den Rueckruf, den auch der
// Mauspfad erzeugt - ohne Mausposition.
// --------------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(CheckBoxActivateEqualsMouseClick)
{
    RecordingWnd wndMouse, wndPad;
    auto* cbMouse = wndMouse.AddCheckBox(1, DrawPoint(0, 0), Extent(60, 20), TextureColor::Green1, "x", NormalFont);
    auto* cbPad = wndPad.AddCheckBox(1, DrawPoint(0, 0), Extent(60, 20), TextureColor::Green1, "x", NormalFont);

    cbMouse->Msg_LeftDown(MouseCoords(5, 5));
    putMouseFarAway();
    BOOST_TEST(cbPad->CanFocus());
    BOOST_TEST(cbPad->Activate());

    BOOST_TEST_REQUIRE(wndMouse.checkboxes.size() == 1u);
    BOOST_TEST_REQUIRE(wndPad.checkboxes.size() == 1u);
    BOOST_TEST(wndPad.checkboxes[0].first == wndMouse.checkboxes[0].first);
    BOOST_TEST(wndPad.checkboxes[0].second == wndMouse.checkboxes[0].second);
    BOOST_TEST(cbPad->isChecked() == cbMouse->isChecked());
    BOOST_TEST(cbPad->isChecked());

    // Nur-lesend: kein Fokus, keine Wirkung.
    cbPad->setReadOnly(true);
    BOOST_TEST(!cbPad->CanFocus());
    BOOST_TEST(!cbPad->Activate());
    BOOST_TEST(wndPad.checkboxes.size() == 1u);
}

BOOST_AUTO_TEST_CASE(ListStepAndActivate)
{
    RecordingWnd wnd;
    auto* list = wnd.AddList(1, DrawPoint(0, 0), Extent(100, 40), TextureColor::Green1, NormalFont);
    putMouseFarAway();

    // Leere Liste: kein Fokus, kein Wert.
    BOOST_TEST(!list->CanFocus());
    BOOST_TEST(!list->GetValueRange().has_value());
    BOOST_TEST(!list->Activate());

    for(int i = 0; i < 10; i++)
        list->AddItem("Eintrag " + std::to_string(i));
    BOOST_TEST(list->CanFocus());

    // Waagerecht: nicht verbraucht -> der Fokus wandert weiter.
    BOOST_TEST(!list->StepValue(Position(1, 0)));
    // Senkrecht: Auswahl wandert und wird nach oben gemeldet, wie beim Mausklick.
    BOOST_TEST(list->StepValue(Position(0, 1)));
    BOOST_TEST_REQUIRE(list->GetSelection().has_value());
    BOOST_TEST(*list->GetSelection() == 0u);
    BOOST_TEST(list->StepValue(Position(0, 1)));
    BOOST_TEST(*list->GetSelection() == 1u);
    BOOST_TEST(wnd.listSelected.size() == 2u);

    // Die Scrollleiste zieht mit, wenn die Auswahl aus dem Sichtbereich laeuft.
    auto* scrollbar = list->GetCtrl<ctrlScrollBar>(0);
    BOOST_TEST_REQUIRE(scrollbar != nullptr);
    for(int i = 0; i < 8; i++)
        BOOST_TEST(list->StepValue(Position(0, 1)));
    BOOST_TEST(*list->GetSelection() == 9u);
    BOOST_TEST(scrollbar->GetScrollPos() + scrollbar->GetPageSize() > 9u);

    // Am Rand: verbraucht, aber keine Aenderung - der Fokus springt nicht aus der Liste.
    const auto numSelBefore = wnd.listSelected.size();
    BOOST_TEST(list->StepValue(Position(0, 1)));
    BOOST_TEST(*list->GetSelection() == 9u);
    BOOST_TEST(wnd.listSelected.size() == numSelBefore);

    // Activate == Doppelklick.
    BOOST_TEST(list->Activate());
    BOOST_TEST_REQUIRE(wnd.listChosen.size() == 1u);
    BOOST_TEST(wnd.listChosen.back() == 9u);
}

BOOST_AUTO_TEST_CASE(TableStepAndActivate)
{
    RecordingWnd wnd;
    std::vector<TableColumn> cols{TableColumn{"A", 100, TableSortType::String}};
    auto* table = wnd.AddTable(1, DrawPoint(0, 0), Extent(100, 60), TextureColor::Green1, NormalFont, cols);
    putMouseFarAway();

    BOOST_TEST(!table->CanFocus());
    // ctrlTable belegt selection_ im Konstruktor mit -1. Als std::optional<unsigned> ist das
    // BELEGT und enthaelt 4294967295 - has_value() ist also true, obwohl nichts ausgewaehlt ist.
    // Bestehender Fehler im Baum; Activate() darf darauf nicht hereinfallen.
    BOOST_TEST(table->GetSelection().has_value());
    BOOST_TEST(!table->Activate());

    for(int i = 0; i < 5; i++)
        table->AddRow({"Zeile " + std::to_string(i)});
    BOOST_TEST(table->CanFocus());
    // Immer noch der Unsinnswert -> weiterhin kein Activate.
    BOOST_TEST(!table->Activate());
    BOOST_TEST(wnd.tableChosen.empty());

    BOOST_TEST(!table->StepValue(Position(1, 0)));
    BOOST_TEST(table->StepValue(Position(0, 1)));
    BOOST_TEST_REQUIRE(table->GetSelection().has_value());
    BOOST_TEST(*table->GetSelection() == 0u); // (unsigned)-1 + 1 == 0, exakt wie die Pfeiltaste
    BOOST_TEST(table->StepValue(Position(0, 1)));
    BOOST_TEST(*table->GetSelection() == 1u);
    BOOST_TEST(table->Activate());
    BOOST_TEST_REQUIRE(wnd.tableChosen.size() == 1u);
    BOOST_TEST(wnd.tableChosen.back() == 1u);
}

// Die Pfeiltasten der Tabelle duerfen sich NICHT geaendert haben: dskSelectMap, dskLAN und
// dskLobby haengen daran, und dort sitzt der Maus-und-Tastatur-Spieler.
BOOST_AUTO_TEST_CASE(TableArrowKeysAreUnchanged)
{
    RecordingWnd wnd;
    std::vector<TableColumn> cols{TableColumn{"A", 100, TableSortType::String}};
    auto* table = wnd.AddTable(1, DrawPoint(0, 0), Extent(100, 60), TextureColor::Green1, NormalFont, cols);
    for(int i = 0; i < 5; i++)
        table->AddRow({"Zeile " + std::to_string(i)});

    // Kein Fokus, nirgends gesetzt - und die Taste wirkt trotzdem. Genau wie heute.
    // Die Zahlen sind die des Ist-Zustands: selection_ startet als (unsigned)-1, das erste
    // Runter laeuft also auf 0 ueber. Genau dieses Verhalten friert der Fall ein.
    BOOST_TEST(table->Msg_KeyDown(KeyEvent{KeyType::Down}));
    BOOST_TEST_REQUIRE(table->GetSelection().has_value());
    BOOST_TEST(*table->GetSelection() == 0u);
    BOOST_TEST(table->Msg_KeyDown(KeyEvent{KeyType::Down}));
    BOOST_TEST(*table->GetSelection() == 1u);
    BOOST_TEST(table->Msg_KeyDown(KeyEvent{KeyType::Up}));
    BOOST_TEST(*table->GetSelection() == 0u);
    // Am oberen Rand passiert nichts mehr.
    BOOST_TEST(table->Msg_KeyDown(KeyEvent{KeyType::Up}));
    BOOST_TEST(*table->GetSelection() == 0u);
    BOOST_TEST(!table->Msg_KeyDown(KeyEvent{KeyType::Left}));
}

BOOST_AUTO_TEST_CASE(ComboBoxStepAndActivate)
{
    RecordingWnd wnd;
    auto* combo = wnd.AddComboBox(1, DrawPoint(0, 0), Extent(80, 20), TextureColor::Green1, NormalFont, 60, false);
    putMouseFarAway();
    for(int i = 0; i < 4; i++)
        combo->AddItem("Wahl " + std::to_string(i));

    BOOST_TEST(combo->CanFocus());
    BOOST_TEST(!combo->StepValue(Position(1, 0)));
    BOOST_TEST(combo->StepValue(Position(0, 1)));
    BOOST_TEST_REQUIRE(combo->GetSelection().has_value());
    BOOST_TEST(*combo->GetSelection() == 0u);
    BOOST_TEST(combo->StepValue(Position(0, 1)));
    BOOST_TEST(*combo->GetSelection() == 1u);
    // Der Rueckruf ist derselbe, den ein Mausklick auf den Listeneintrag ausloest.
    BOOST_TEST_REQUIRE(wnd.comboSelected.size() == 2u);
    BOOST_TEST(wnd.comboSelected.back() == 1u);

    // Activate klappt die Liste auf und wieder zu - genau der Rumpf von Msg_LeftDown beim
    // Klick auf das Feld.
    auto* list = combo->GetCtrl<ctrlList>(0);
    BOOST_TEST_REQUIRE(list != nullptr);
    BOOST_TEST(!list->IsVisible());
    BOOST_TEST(combo->Activate());
    BOOST_TEST(list->IsVisible());
    BOOST_TEST(combo->Activate());
    BOOST_TEST(!list->IsVisible());

    // Nur-lesend: kein Fokus, keine Wirkung.
    auto* ro = wnd.AddComboBox(2, DrawPoint(0, 40), Extent(80, 20), TextureColor::Green1, NormalFont, 60, true);
    ro->AddItem("x");
    BOOST_TEST(!ro->CanFocus());
    BOOST_TEST(!ro->StepValue(Position(0, 1)));
    BOOST_TEST(!ro->Activate());
}

/// Ein Textfeld ist bewusst KEINE Fokusstation der Padnavigation.
///
/// focus_ ist ein Bit am Control, nicht am Spieler, und es entscheidet, wer die Tastatur
/// bekommt (WindowManager::RelayKeyboardMessage -> oberstes Fenster -> das Feld mit focus_).
/// Duerfte ein Padspieler es setzen, wanderte die Eingabe des Tastaturspielers stillschweigend
/// in sein Feld. Tippen kann ein Pad ohnehin nicht - der Tausch waere reiner Verlust.
BOOST_AUTO_TEST_CASE(EditIsNoPadFocusStop)
{
    RecordingWnd wnd;
    auto* edit = wnd.AddEdit(1, DrawPoint(0, 0), Extent(100, 20), TextureColor::Green1, NormalFont);
    putMouseFarAway();

    BOOST_TEST(!edit->CanFocus());
    // Kein Weg ueber die Padnavigation: weder als Kandidat noch ueber Activate().
    BOOST_TEST(!edit->Activate());
    BOOST_TEST(!edit->HasFocus());
    BOOST_TEST(!edit->WantsTextInput());

    // Der MAUSPFAD ist unveraendert - das ist die harte Randbedingung fuer den Einzelspieler.
    edit->SetFocus(true);
    BOOST_TEST(edit->HasFocus());
    BOOST_TEST(edit->WantsTextInput());
    BOOST_TEST(edit->Msg_KeyDown(KeyEvent{KeyType::Space}));
    BOOST_TEST(edit->GetText() == " ");
}

/// FocusPath sammelt nur Controls mit CanFocus(). Ein Fenster, in dem es nichts ausser einem
/// Textfeld gibt, ist damit fuer ein Pad nicht betretbar - und genau das ist gewollt.
BOOST_AUTO_TEST_CASE(EditIsNotCollectedByFocusPath)
{
    RecordingWnd wnd;
    wnd.AddEdit(1, DrawPoint(0, 0), Extent(100, 20), TextureColor::Green1, NormalFont);
    putMouseFarAway();

    FocusPath fp;
    BOOST_TEST(!fp.SetRoot(&wnd));
    BOOST_TEST(!fp.IsActive());

    // Mit einem Knopf daneben ist das Fenster betretbar - der Fokus landet aber auf dem Knopf
    // und kann von dort aus nicht auf das Textfeld wandern.
    wnd.AddTextButton(2, DrawPoint(0, 40), Extent(60, 20), TextureColor::Green1, "A", NormalFont);
    BOOST_TEST(fp.SetRoot(&wnd));
    BOOST_TEST(fp.Collect().size() == 1u);
    BOOST_TEST(!fp.Move(FocusPath::Dir::Up));
    BOOST_TEST(!fp.Move(FocusPath::Dir::Down));
}

BOOST_AUTO_TEST_CASE(ProgressIsHorizontalValue)
{
    RecordingWnd wnd;
    auto* prog = wnd.AddProgress(1, DrawPoint(0, 0), Extent(120, 20), TextureColor::Green1, 0, 0, 10);
    putMouseFarAway();

    BOOST_TEST(prog->CanFocus());
    const auto range = prog->GetValueRange();
    BOOST_TEST_REQUIRE(range.has_value());
    BOOST_TEST((range->axis == Window::ValueAxis::Horizontal));
    BOOST_TEST(range->max == 10u);

    // Senkrecht wandert der Fokus weiter, waagerecht aendert den Wert.
    BOOST_TEST(!prog->StepValue(Position(0, 1)));
    BOOST_TEST(prog->StepValue(Position(1, 0)));
    BOOST_TEST(prog->GetPosition() == 1);
    BOOST_TEST(prog->StepValue(Position(-1, 0)));
    BOOST_TEST(prog->GetPosition() == 0);

    BOOST_TEST(prog->SetValue(7));
    BOOST_TEST(prog->GetPosition() == 7);
    BOOST_TEST_REQUIRE(!wnd.progressChanges.empty());
    BOOST_TEST(wnd.progressChanges.back() == 7);

    // Kein neuer Wert -> keine Meldung (sonst Flut von GameCommands bei Vollausschlag).
    const auto numBefore = wnd.progressChanges.size();
    BOOST_TEST(prog->SetValue(7));
    BOOST_TEST(wnd.progressChanges.size() == numBefore);

    // Ueber das Maximum wird geklemmt.
    BOOST_TEST(prog->SetValue(1000));
    BOOST_TEST(prog->GetPosition() == 10);
}

BOOST_AUTO_TEST_CASE(ScrollBarIsVerticalValue)
{
    RecordingWnd wnd;
    auto* bar = wnd.AddScrollBar(1, DrawPoint(0, 0), Extent(20, 100), 20, TextureColor::Green1, 5);
    putMouseFarAway();

    // Ohne ueberhaengenden Inhalt ist die Leiste unsichtbar und nicht fokussierbar.
    BOOST_TEST(!bar->CanFocus());
    bar->SetRange(20);
    BOOST_TEST(bar->IsVisible());
    BOOST_TEST(bar->CanFocus());

    const auto range = bar->GetValueRange();
    BOOST_TEST_REQUIRE(range.has_value());
    BOOST_TEST((range->axis == Window::ValueAxis::Vertical));
    BOOST_TEST(range->max == 15u);

    BOOST_TEST(!bar->StepValue(Position(1, 0)));
    BOOST_TEST(bar->StepValue(Position(0, 1)));
    BOOST_TEST(bar->GetScrollPos() == 1);
    // Scroll() meldet nach oben - eine angehaengte Liste kommt damit mit.
    BOOST_TEST_REQUIRE(!wnd.scrollChanges.empty());
    BOOST_TEST(wnd.scrollChanges.back() == 1);

    BOOST_TEST(bar->SetValue(9));
    BOOST_TEST(bar->GetScrollPos() == 9);
    BOOST_TEST(wnd.scrollChanges.back() == 9);
}


// --------------------------------------------------------------------------------------------
// BEFUND N8 - DIE STEUERKREUZFRAGE UND DER STEUERKREUZSCHRITT KOENNEN NICHT MEHR AUSEINANDER
//
// DER BEFUND, gemessen vom Nachpruefer der zweiten Runde: die Tastenhinweisleiste fragte
// Window::GetValueRange(), gewirkt hat aber Window::StepValue() - zwei Funktionen ohne
// gemeinsame Bedingung. Seine zwei Belege:
//
//   - ctrlMapSelection hat StepValue, aber KEIN GetValueRange: das Steuerkreuz wirkt, die
//     Leiste haette geschwiegen.
//   - ctrlComboBox mit readonly hat einen Wertebereich, StepValue steigt aber sofort aus: die
//     Leiste haette "Einstellen" versprochen, der Druck haette nichts getan.
//
// DIE ANTWORT IST KEINE ZWEITE ABSCHRIFT, sondern eine Bauform: Window::StepValue ist NICHT
// MEHR VIRTUELL und liefert woertlich das Ergebnis von CanStepValue (Window.h). Eine Klasse
// sagt in CanStepValue, WANN sie den Schritt annimmt, und in DoStepValue, WAS dann geschieht.
// Damit ist die Gleichheit keine Zusicherung mehr, sondern eine Zeile Quelltext.
//
// Dieser Fall haelt sie trotzdem fest - und zwar an jedem Control des Baums, das den Schritt
// ueberhaupt annimmt. Er ist die Wache fuer den Tag, an dem jemand StepValue wieder virtuell
// macht: dann kann er hier auseinanderlaufen, und dann wird er hier rot.
// --------------------------------------------------------------------------------------------

/// Fragt beide Seiten fuer alle vier Richtungen und vergleicht sie.
///
/// GEDRUECKT WIRD WIRKLICH: StepValue veraendert das Control. Genau deshalb wird CanStepValue
/// unmittelbar VOR jedem Schritt gelesen - beide sehen denselben Zustand.
template<class T_Ctrl>
void checkStepQuestionMatchesTheStep(T_Ctrl& ctrl, const char* what)
{
    for(const Position dir : {Position(-1, 0), Position(1, 0), Position(0, -1), Position(0, 1)})
    {
        const bool asked = ctrl.CanStepValue(dir);
        const bool done = ctrl.StepValue(dir);
        BOOST_TEST_CONTEXT(what << " dir=(" << dir.x << "," << dir.y << ")")
        BOOST_TEST(asked == done);
    }
}

BOOST_AUTO_TEST_CASE(TheDpadQuestionAndTheDpadStepAreTheSameCondition)
{
    RecordingWnd wnd;
    putMouseFarAway();

    // (1) Der Schieberegler: waagerecht ja, senkrecht nein.
    auto* prog = wnd.AddProgress(1, DrawPoint(0, 0), Extent(120, 20), TextureColor::Green1, 0, 0, 10);
    prog->SetPosition(5);
    checkStepQuestionMatchesTheStep(*prog, "ctrlProgress");
    BOOST_TEST(prog->CanStepValue(Position(1, 0)));
    BOOST_TEST(!prog->CanStepValue(Position(0, 1)));

    // (2) Die Bildlaufleiste: senkrecht ja, waagerecht nein.
    auto* bar = wnd.AddScrollBar(2, DrawPoint(0, 30), Extent(20, 100), 20, TextureColor::Green1, 5);
    bar->SetRange(20);
    checkStepQuestionMatchesTheStep(*bar, "ctrlScrollBar");
    BOOST_TEST(bar->CanStepValue(Position(0, 1)));
    BOOST_TEST(!bar->CanStepValue(Position(1, 0)));

    // (3) Die LEERE Liste: nirgends. Und die gefuellte: senkrecht.
    auto* empty = wnd.AddList(3, DrawPoint(0, 140), Extent(100, 60), TextureColor::Green1, NormalFont);
    checkStepQuestionMatchesTheStep(*empty, "ctrlList (leer)");
    BOOST_TEST(!empty->CanStepValue(Position(0, 1)));
    auto* list = wnd.AddList(4, DrawPoint(0, 210), Extent(100, 60), TextureColor::Green1, NormalFont);
    for(int i = 0; i < 4; i++)
        list->AddItem("Zeile " + std::to_string(i));
    checkStepQuestionMatchesTheStep(*list, "ctrlList");
    BOOST_TEST(list->CanStepValue(Position(0, 1)));

    // (4) Die Tabelle, leer und gefuellt.
    const std::vector<TableColumn> cols{TableColumn{"A", 100, TableSortType::String}};
    auto* table = wnd.AddTable(5, DrawPoint(120, 0), Extent(200, 100), TextureColor::Green1, NormalFont, cols);
    checkStepQuestionMatchesTheStep(*table, "ctrlTable (leer)");
    BOOST_TEST(!table->CanStepValue(Position(0, 1)));
    table->AddRow({"eins"});
    table->AddRow({"zwei"});
    checkStepQuestionMatchesTheStep(*table, "ctrlTable");
    BOOST_TEST(table->CanStepValue(Position(0, 1)));

    // (5) DIE AUSWAHLLISTE - der erste Beleg des Befunds. Sie hat einen Wertebereich, auch
    //     schreibgeschuetzt; das Steuerkreuz wirkt dort aber nicht.
    auto* combo = wnd.AddComboBox(6, DrawPoint(120, 120), Extent(80, 20), TextureColor::Green1, NormalFont, 60, false);
    for(int i = 0; i < 4; i++)
        combo->AddItem("Wahl " + std::to_string(i));
    checkStepQuestionMatchesTheStep(*combo, "ctrlComboBox");
    // ... auch AUFGEKLAPPT, wo sie sogar waagerecht verbraucht.
    BOOST_TEST_REQUIRE(combo->Activate());
    BOOST_TEST_REQUIRE(combo->GetCtrl<ctrlList>(0)->IsVisible());
    checkStepQuestionMatchesTheStep(*combo, "ctrlComboBox (offen)");
    BOOST_TEST(combo->CanStepValue(Position(1, 0)));
    BOOST_TEST(combo->Activate()); // wieder zu

    auto* ro = wnd.AddComboBox(7, DrawPoint(120, 150), Extent(80, 20), TextureColor::Green1, NormalFont, 60, true);
    ro->AddItem("x");
    ro->AddItem("y");
    // DER BELEG: der Wertebereich sagt "hier gibt es einen Wert" ...
    BOOST_TEST(ro->GetValueRange().has_value());
    // ... und das Steuerkreuz tut trotzdem nichts. Genau deshalb ist GetValueRange nicht die
    // Frage, an der die Leiste haengen darf.
    BOOST_TEST(!ro->CanStepValue(Position(0, 1)));
    checkStepQuestionMatchesTheStep(*ro, "ctrlComboBox (readonly)");
}

BOOST_AUTO_TEST_SUITE_END()
