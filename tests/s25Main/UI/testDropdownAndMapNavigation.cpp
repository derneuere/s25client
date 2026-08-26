// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

// Phase 10, Bedienelementebene. Zwei Controls, an denen die Kampagnenauswahl haengt:
//
//  - ctrlComboBox. Der Auftraggeber sagte, das Aufklappmenue sei AUCH MIT DER MAUS schwierig zu
//    bedienen. Die Faelle unten frieren die drei Gruende ein, die dafuer gefunden wurden, und
//    die vier Schritte, die es am Pad jetzt gibt: aufklappen, blaettern, bestaetigen, verwerfen.
//
//  - ctrlMapSelection. Die Auswahl einer Mission auf der Weltkarte entstand ausschliesslich aus
//    einer PIXELFARBE UNTER DEM MAUSZEIGER (Msg_LeftUp). Ein Pad konnte die Karte nicht einmal
//    fokussieren, weil CanFocus() eine bestehende Auswahl verlangte, die nur die Maus erzeugen
//    konnte. Die Weltkampagne war damit am Pad nicht startbar.
//
// Bewusst auf Controlebene und nicht ueber den Treiber: hier wird die MECHANIK der Controls
// geprueft, nicht ihr Zusammenspiel mit dem Eingabeframe. Der Nachweis ueber die Treibernaht
// steht in tests/s25Main/splitscreen/testMenuPadDropdown.cpp und testMenuPadCampaign.cpp.

#include "Loader.h"
#include "Window.h"
#include "controls/ctrlComboBox.h"
#include "controls/ctrlGroup.h"
#include "controls/ctrlList.h"
#include "controls/ctrlMapSelection.h"
#include "controls/ctrlScrollBar.h"
#include "controls/ctrlTextButton.h"
#include "driver/MouseCoords.h"
#include "input/FocusPath.h"
#include "ogl/glFont.h"
#include "mockupDrivers/MockupVideoDriver.h"
#include "uiHelper/uiHelpers.hpp"
#include "gameData/SelectionMapInputData.h"
#include <boost/test/unit_test.hpp>
#include <helpers/optional_io.h>
#include <optional>
#include <vector>

namespace {

/// Elternfenster, das sich jede Meldung merkt, die ein Control nach oben schickt.
struct RecordingParent : Window
{
    RecordingParent() : Window(nullptr, 0, DrawPoint(0, 0), Extent(400, 400)) {}

    std::vector<unsigned> comboSelected;
    std::vector<unsigned> clicks;

    void Msg_ComboSelectItem(unsigned, unsigned sel) override { comboSelected.push_back(sel); }
    void Msg_ButtonClick(unsigned id) override { clicks.push_back(id); }
};

/// Die Maus liegt bewusst ausserhalb von allem: Controls lesen in ihren Zeichen- und
/// Mausmethoden den globalen Mauszeiger, und ein zufaellig darueber liegender Zeiger wuerde
/// Zustaende erzeugen, die der Test nicht gesetzt hat.
void putMouseFarAway()
{
    uiHelper::GetVideoDriver()->SetMousePos(Position(-10000, -10000));
}

ctrlComboBox* makeCombo(RecordingParent& parent, unsigned numItems, unsigned short maxListHeight = 200)
{
    auto* combo =
      parent.AddComboBox(1, DrawPoint(0, 0), Extent(80, 20), TextureColor::Green1, NormalFont, maxListHeight, false);
    for(unsigned i = 0; i < numItems; ++i)
        combo->AddItem("Wahl " + std::to_string(i));
    // Window::active_ ist per Vorgabe FALSE, und Window::RelayMouseMessage stellt nur an aktive
    // Kinder zu. Im Spiel erledigt das der Desktop bzw. das Ingamefenster; hier muss es der
    // Test tun, sonst erreicht kein Mausklick die aufgeklappte Liste.
    parent.SetActive(true);
    return combo;
}

/// Ein Klick genau auf den Eintrag mit dem gegebenen Index in der aufgeklappten Liste.
MouseCoords posOfListItem(const ctrlList& list, unsigned idx)
{
    const auto* scrollbar = list.GetCtrl<ctrlScrollBar>(0);
    const unsigned visibleIdx = idx - scrollbar->GetScrollPos();
    return MouseCoords(list.GetDrawPos() + DrawPoint(4, 3 + static_cast<int>(visibleIdx * NormalFont->getHeight())));
}

/// Eine Auswahlkarte mit `numMissions` Marken auf einer Linie, Abstand 2 in x.
/// Die Bilder kommen aus Loader::LoadDummyMapSelectionFiles.
SelectionMapInputData makeSelectionMap(unsigned numMissions)
{
    SelectionMapInputData data;
    data.background = ImageResource("selmap.lbm", 0);
    data.map = ImageResource("selmap.lbm", 1);
    data.missionMapMask = ImageResource("selmap.lbm", 2);
    data.marker = ImageResource("selmap.lbm", 3);
    data.conquered = ImageResource("selmap.lbm", 4);
    for(unsigned i = 0; i < numMissions; ++i)
    {
        MissionSelectionInfo info;
        info.maskAreaColor = 0xFF000001u + i;
        info.ankerPos = Position(2 * static_cast<int>(i), 0);
        data.missionSelectionInfos.push_back(info);
    }
    return data;
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(DropdownNavigation, uiHelper::Fixture)

/// FEHLER 1, reiner MAUSFEHLER: ein Klick auf den BEREITS AUSGEWAEHLTEN Eintrag liess die Liste
/// offen stehen.
///
/// ctrlList::SetSelection steigt bei unveraenderter Auswahl sofort aus, meldet also nichts, und
/// damit lief das einzige ShowList(false) des Auswahlpfads (ctrlComboBox::Msg_ListSelectItem)
/// nie. Der Benutzer klappte auf, klickte den Wert an, der schon drinstand - und nichts
/// passierte. Mit der RECHTEN Maustaste ging es, weil Msg_RightDown diese Absicherung hatte.
BOOST_AUTO_TEST_CASE(ClickingTheAlreadySelectedItemClosesTheList)
{
    RecordingParent parent;
    auto* combo = makeCombo(parent, 4);
    putMouseFarAway();
    combo->SetSelection(2);

    // Aufklappen wie mit der Maus: Klick auf das Feld.
    combo->Msg_LeftDown(MouseCoords(combo->GetDrawPos() + DrawPoint(2, 2)));
    auto* list = combo->GetCtrl<ctrlList>(0);
    BOOST_TEST_REQUIRE(list->IsVisible());

    // Klick auf den Eintrag, der ohnehin schon gewaehlt ist.
    combo->Msg_LeftDown(posOfListItem(*list, 2));
    BOOST_TEST(!list->IsVisible());
    BOOST_TEST(combo->GetSelection() == 2u);
    // Und es wurde nichts gemeldet - es hat sich ja auch nichts geaendert.
    BOOST_TEST(parent.comboSelected.empty());

    // Die Wahl eines ANDEREN Eintrags meldet unveraendert nach oben.
    combo->Msg_LeftDown(MouseCoords(combo->GetDrawPos() + DrawPoint(2, 2)));
    BOOST_TEST_REQUIRE(list->IsVisible());
    combo->Msg_LeftDown(posOfListItem(*list, 0));
    BOOST_TEST(!list->IsVisible());
    BOOST_TEST(combo->GetSelection() == 0u);
    BOOST_TEST_REQUIRE(parent.comboSelected.size() == 1u);
    BOOST_TEST(parent.comboSelected.back() == 0u);
}

/// FEHLER 2, ebenfalls MAUS: ein Klick DANEBEN schloss die Liste UND drueckte, was darunter lag.
///
/// Window::RelayMouseMessage bricht nicht beim ersten Treffer ab, und die Sperre der Combobox
/// umfasste nur die Flaeche der Liste selbst. Wer die Liste durch Danebenklicken loswerden
/// wollte, loeste damit gleichzeitig den Knopf unter dem Zeiger aus. Jetzt sperrt eine offene
/// Liste den ganzen Bildschirm - das ist es, was ein Aufklappmenue ueberall sonst auch tut.
BOOST_AUTO_TEST_CASE(AnOpenListSwallowsClicksElsewhere)
{
    RecordingParent parent;
    auto* combo = makeCombo(parent, 4);
    putMouseFarAway();

    const Position farAway(300, 300);
    BOOST_TEST(!parent.IsInLockedRegion(farAway));

    combo->Msg_LeftDown(MouseCoords(combo->GetDrawPos() + DrawPoint(2, 2)));
    BOOST_TEST_REQUIRE(combo->IsListOpen());
    // Solange die Liste offen ist, nimmt kein anderes Kind des Elternfensters Mausereignisse an.
    BOOST_TEST(parent.IsInLockedRegion(farAway));
    // Fuer die Combobox selbst gilt die Sperre nicht - sonst koennte niemand die Liste bedienen.
    BOOST_TEST(!parent.IsInLockedRegion(farAway, combo));

    // Der Klick daneben klappt zu und gibt die Flaeche wieder frei.
    combo->Msg_LeftDown(MouseCoords(farAway));
    BOOST_TEST(!combo->IsListOpen());
    BOOST_TEST(!parent.IsInLockedRegion(farAway));
}

/// FEHLER 3, ebenfalls MAUS: die Liste klappte immer bei Scrollposition 0 auf.
///
/// In dskOptions hat die Sprachliste rund 30 Eintraege bei etwa acht sichtbaren Zeilen. Wer
/// weiter unten stand, sah beim Aufklappen seine eingestellte Sprache gar nicht und wusste
/// nicht, wo er ist.
BOOST_AUTO_TEST_CASE(OpeningScrollsToTheCurrentSelection)
{
    RecordingParent parent;
    // 30 Eintraege, aber nur wenige Zeilen hoch.
    auto* combo = makeCombo(parent, 30, 60);
    putMouseFarAway();
    auto* list = combo->GetCtrl<ctrlList>(0);
    auto* scrollbar = list->GetCtrl<ctrlScrollBar>(0);
    const unsigned pagesize = scrollbar->GetPageSize();
    BOOST_TEST_REQUIRE(pagesize > 0u);
    BOOST_TEST_REQUIRE(pagesize < 30u); // sonst prueft der Fall nichts

    combo->SetSelection(25);
    BOOST_TEST_REQUIRE(scrollbar->GetScrollPos() == 0u);

    combo->Msg_LeftDown(MouseCoords(combo->GetDrawPos() + DrawPoint(2, 2)));
    BOOST_TEST_REQUIRE(combo->IsListOpen());
    // Der gewaehlte Eintrag ist sichtbar.
    BOOST_TEST(scrollbar->GetScrollPos() <= 25u);
    BOOST_TEST(25u < scrollbar->GetScrollPos() + pagesize);
}

/// AM PAD sind AUFKLAPPEN, BLAETTERN, BESTAETIGEN und VERWERFEN vier verschiedene Dinge.
///
/// Vorher gab es nur eins: jeder Steuerkreuzschritt setzte den Wert sofort, meldete ihn nach
/// oben und klappte die Liste dabei zu. In dskOptions hiess ein einziger Schritt in der
/// Sprachliste: Sprache umstellen und den ganzen Bildschirm neu bauen. Ein Abbrechen gab es
/// nicht, und die aufgeklappte Liste hatte fuer das Pad ueberhaupt keine Funktion.
BOOST_AUTO_TEST_CASE(PadOpensBrowsesAndConfirms)
{
    RecordingParent parent;
    auto* combo = makeCombo(parent, 4);
    putMouseFarAway();
    combo->SetSelection(1);
    auto* list = combo->GetCtrl<ctrlList>(0);

    // A klappt auf - und aendert KEINEN Wert.
    BOOST_TEST(combo->Activate());
    BOOST_TEST(list->IsVisible());
    BOOST_TEST(combo->GetSelection() == 1u);
    BOOST_TEST(parent.comboSelected.empty());

    // Steuerkreuz blaettert. Die Liste bleibt OFFEN und das Elternfenster hoert NICHTS.
    BOOST_TEST(combo->StepValue(Position(0, 1)));
    BOOST_TEST(list->IsVisible());
    BOOST_TEST(combo->GetSelection() == 2u);
    BOOST_TEST(parent.comboSelected.empty());
    BOOST_TEST(combo->StepValue(Position(0, 1)));
    BOOST_TEST(combo->GetSelection() == 3u);
    BOOST_TEST(parent.comboSelected.empty());
    // Waagerecht ist bei offener Liste ebenfalls verbraucht: der Fokus soll nicht unter der
    // Liste wegrutschen und sie offen zuruecklassen.
    BOOST_TEST(combo->StepValue(Position(1, 0)));
    BOOST_TEST(list->IsVisible());

    // A bestaetigt: zu, und JETZT genau eine Meldung.
    BOOST_TEST(combo->Activate());
    BOOST_TEST(!list->IsVisible());
    BOOST_TEST(combo->GetSelection() == 3u);
    BOOST_TEST_REQUIRE(parent.comboSelected.size() == 1u);
    BOOST_TEST(parent.comboSelected.back() == 3u);
}

/// B verwirft: der alte Wert steht wieder da, und es wurde nichts gemeldet.
BOOST_AUTO_TEST_CASE(PadCancelRestoresTheOldValue)
{
    RecordingParent parent;
    auto* combo = makeCombo(parent, 4);
    putMouseFarAway();
    combo->SetSelection(1);

    // Bei GESCHLOSSENER Liste verbraucht B nichts - sonst koennte man mit B kein Fenster mehr
    // schliessen, nur weil der Fokus zufaellig auf einem Aufklappmenue steht.
    BOOST_TEST(!combo->CancelInput());

    BOOST_TEST(combo->Activate());
    BOOST_TEST(combo->StepValue(Position(0, 1)));
    BOOST_TEST(combo->GetSelection() == 2u);

    BOOST_TEST(combo->CancelInput());
    BOOST_TEST(!combo->IsListOpen());
    BOOST_TEST(combo->GetSelection() == 1u);
    BOOST_TEST(parent.comboSelected.empty());
    BOOST_TEST(!parent.IsInLockedRegion(Position(300, 300)));
}

/// Der Fokus wandert weiter, waehrend die Liste offen steht. Sie muss mit - sonst bliebe sie
/// sichtbar ueber allem stehen UND ihre Sperre liegen.
BOOST_AUTO_TEST_CASE(LeavingWithTheFocusDiscardsAndCloses)
{
    RecordingParent parent;
    auto* combo = makeCombo(parent, 4);
    parent.AddTextButton(2, DrawPoint(0, 200), Extent(80, 20), TextureColor::Green1, "Weiter", NormalFont);
    putMouseFarAway();
    combo->SetSelection(1);

    FocusPath focus;
    BOOST_TEST_REQUIRE(focus.SetRoot(&parent));
    BOOST_TEST_REQUIRE(focus.GetFocused() == static_cast<Window*>(combo));

    BOOST_TEST(focus.Activate()); // A: aufklappen
    BOOST_TEST_REQUIRE(combo->IsListOpen());
    BOOST_TEST(combo->StepValue(Position(0, 1)));
    BOOST_TEST(combo->GetSelection() == 2u);

    // Schulterknopf: Fokus auf den naechsten Kandidaten.
    BOOST_TEST(focus.Move(FocusPath::Dir::Next));
    BOOST_TEST(focus.GetFocused() != static_cast<Window*>(combo));
    BOOST_TEST(!combo->IsListOpen());
    BOOST_TEST(combo->GetSelection() == 1u); // verworfen
    BOOST_TEST(parent.comboSelected.empty());
    BOOST_TEST(!parent.IsInLockedRegion(Position(300, 300)));
}

/// Wird das Control weggeblendet, waehrend die Liste offen ist, muss die Sperre weg. Sonst
/// haette ein Reiterwechsel in dskOptions einen Bildschirm hinterlassen, der keine Maustaste
/// mehr annimmt.
BOOST_AUTO_TEST_CASE(HidingReleasesTheLock)
{
    RecordingParent parent;
    auto* combo = makeCombo(parent, 4);
    putMouseFarAway();
    combo->SetSelection(1);

    BOOST_TEST(combo->Activate());
    BOOST_TEST_REQUIRE(parent.IsInLockedRegion(Position(300, 300)));
    combo->SetVisible(false);
    BOOST_TEST(!combo->IsListOpen());
    BOOST_TEST(!parent.IsInLockedRegion(Position(300, 300)));
    BOOST_TEST(combo->GetSelection() == 1u);
}

/// Ein Reiterwechsel blendet die GRUPPE weg, nicht das Control darin - die Combobox erfaehrt
/// davon nichts. Window::Msg_PaintAfter laeuft trotzdem fuer sie (die Schleife prueft keine
/// Sichtbarkeit), und dort faellt es auf.
BOOST_AUTO_TEST_CASE(AHiddenParentAlsoReleasesTheLock)
{
    RecordingParent parent;
    auto* group = parent.AddGroup(3);
    auto* combo = group->AddComboBox(1, DrawPoint(0, 0), Extent(80, 20), TextureColor::Green1, NormalFont, 200, false);
    for(int i = 0; i < 4; i++)
        combo->AddItem("Wahl " + std::to_string(i));
    putMouseFarAway();
    combo->SetSelection(1);

    BOOST_TEST(combo->Activate());
    BOOST_TEST_REQUIRE(parent.IsInLockedRegion(Position(300, 300)));

    group->SetVisible(false);
    parent.Msg_PaintAfter();
    BOOST_TEST(!combo->IsListOpen());
    BOOST_TEST(!parent.IsInLockedRegion(Position(300, 300)));
    BOOST_TEST(combo->GetSelection() == 1u);
}

/// DIE ZWILLINGSPRUEFUNG: derselbe Wert, ueber die Maus und ueber das Pad gewaehlt, hinterlaesst
/// denselben Zustand und dieselbe Meldung. Ohne sie faellt es nie auf, wenn der Padweg etwas
/// anderes tut als der Mausweg.
BOOST_AUTO_TEST_CASE(MouseAndPadEndInTheSameState)
{
    putMouseFarAway();

    RecordingParent mouseParent;
    auto* mouseCombo = makeCombo(mouseParent, 4);
    mouseCombo->SetSelection(0);
    auto* mouseList = mouseCombo->GetCtrl<ctrlList>(0);
    mouseCombo->Msg_LeftDown(MouseCoords(mouseCombo->GetDrawPos() + DrawPoint(2, 2)));
    mouseCombo->Msg_LeftDown(posOfListItem(*mouseList, 2));

    RecordingParent padParent;
    auto* padCombo = makeCombo(padParent, 4);
    padCombo->SetSelection(0);
    padCombo->Activate();
    padCombo->StepValue(Position(0, 1));
    padCombo->StepValue(Position(0, 1));
    padCombo->Activate();

    BOOST_TEST(padCombo->GetSelection() == mouseCombo->GetSelection());
    BOOST_TEST(padCombo->IsListOpen() == mouseCombo->IsListOpen());
    BOOST_TEST(padParent.comboSelected == mouseParent.comboSelected, boost::test_tools::per_element());
    BOOST_TEST_REQUIRE(padParent.comboSelected.size() == 1u);
    BOOST_TEST(padParent.comboSelected.back() == 2u);
}

/// Bei GESCHLOSSENER Liste bleibt der Steuerkreuzschritt, was er war: er setzt den Wert direkt
/// und meldet ihn. Das ist derselbe Weg wie das Mausrad ueber dem Feld
/// (ctrlComboBox::Msg_WheelDown), und beides wird gebraucht - ein Lautstaerkeregler soll sich
/// nicht erst aufklappen lassen muessen.
BOOST_AUTO_TEST_CASE(SteppingOnAClosedBoxIsUnchanged)
{
    RecordingParent parent;
    auto* combo = makeCombo(parent, 4);
    putMouseFarAway();

    BOOST_TEST(!combo->IsListOpen());
    BOOST_TEST(!combo->StepValue(Position(1, 0))); // waagerecht: der Fokus wandert weiter
    BOOST_TEST(combo->StepValue(Position(0, 1)));
    BOOST_TEST(combo->GetSelection() == 0u);
    BOOST_TEST(!combo->IsListOpen());
    BOOST_TEST_REQUIRE(parent.comboSelected.size() == 1u);
    BOOST_TEST(parent.comboSelected.back() == 0u);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(MapSelectionNavigation, uiHelper::Fixture)

/// Die Karte ist eine Fokusstation, AUCH OHNE bestehende Auswahl - das war die Sackgasse.
BOOST_AUTO_TEST_CASE(TheMapIsAFocusStopWithoutASelection)
{
    LOADER.LoadDummyMapSelectionFiles();
    putMouseFarAway();

    RecordingParent parent;
    auto* map = parent.AddMapSelection(1, DrawPoint(0, 0), Extent(200, 200), makeSelectionMap(4));
    map->setMissionsStatus(std::vector<MissionStatus>(4, {true, false}));

    BOOST_TEST(!map->getSelection());
    BOOST_TEST(map->CanFocus());

    // Die VORSCHAU in dskCampaignSelection bleibt aussen vor: dort ist die Karte ein Bild.
    map->setPreview(true);
    BOOST_TEST(!map->CanFocus());
    BOOST_TEST(!map->StepValue(Position(0, 1)));
    map->setPreview(false);
}

/// Der erste Schritt setzt die Marke, die folgenden navigieren geometrisch - und jeder Schritt
/// meldet sich nach oben, genau wie der Mausklick (Msg_LeftUp). Ohne diese Meldung zoege
/// dskCampaignMissionSelection den Startknopf nie scharf.
BOOST_AUTO_TEST_CASE(TheDpadWalksFromMissionToMission)
{
    LOADER.LoadDummyMapSelectionFiles();
    putMouseFarAway();

    RecordingParent parent;
    // Vier Missionen auf einer waagerechten Linie bei x = 0, 2, 4, 6.
    auto* map = parent.AddMapSelection(1, DrawPoint(0, 0), Extent(200, 200), makeSelectionMap(4));
    map->setMissionsStatus(std::vector<MissionStatus>(4, {true, false}));

    BOOST_TEST(map->StepValue(Position(1, 0)));
    BOOST_TEST_REQUIRE(!!map->getSelection());
    BOOST_TEST(*map->getSelection() == 0u);
    BOOST_TEST_REQUIRE(parent.clicks.size() == 1u);
    BOOST_TEST(parent.clicks.back() == 1u); // die Id der Karte

    BOOST_TEST(map->StepValue(Position(1, 0)));
    BOOST_TEST(*map->getSelection() == 1u);
    BOOST_TEST(map->StepValue(Position(1, 0)));
    BOOST_TEST(*map->getSelection() == 2u);
    BOOST_TEST(map->StepValue(Position(-1, 0)));
    BOOST_TEST(*map->getSelection() == 1u);

    // Am linken Rand liegt nichts mehr: das Control verbraucht den Schritt NICHT, der Fokus
    // wandert also normal aus der Karte heraus.
    BOOST_TEST(map->StepValue(Position(-1, 0)));
    BOOST_TEST(*map->getSelection() == 0u);
    BOOST_TEST(!map->StepValue(Position(-1, 0)));
    BOOST_TEST(*map->getSelection() == 0u);
    // Senkrecht liegt hier ueberhaupt nichts.
    BOOST_TEST(!map->StepValue(Position(0, 1)));

    // Und A ist "diese Mission" - dieselbe Meldung wie der Mausklick.
    const size_t clicksBefore = parent.clicks.size();
    BOOST_TEST(map->Activate());
    BOOST_TEST(parent.clicks.size() == clicksBefore + 1u);
}

/// NICHT SPIELBARE Missionen werden uebersprungen. Eine Kampagne gibt ihre Missionen der Reihe
/// nach frei; ein Pad, das auf einer gesperrten Marke landen koennte, waere schlechter als die
/// Maus, die sie ebenfalls nicht anwaehlen kann (ctrlMapSelection::setSelection).
BOOST_AUTO_TEST_CASE(LockedMissionsAreSkipped)
{
    LOADER.LoadDummyMapSelectionFiles();
    putMouseFarAway();

    RecordingParent parent;
    auto* map = parent.AddMapSelection(1, DrawPoint(0, 0), Extent(200, 200), makeSelectionMap(4));
    // Nur 0 und 2 sind spielbar.
    map->setMissionsStatus({{true, false}, {false, false}, {true, false}, {false, false}});

    BOOST_TEST(map->StepValue(Position(1, 0)));
    BOOST_TEST(*map->getSelection() == 0u);
    BOOST_TEST(map->StepValue(Position(1, 0)));
    BOOST_TEST(*map->getSelection() == 2u); // 1 uebersprungen
    BOOST_TEST(!map->StepValue(Position(1, 0)));
    BOOST_TEST(*map->getSelection() == 2u); // 3 ist gesperrt, rechts liegt nichts mehr
}

BOOST_AUTO_TEST_SUITE_END()
