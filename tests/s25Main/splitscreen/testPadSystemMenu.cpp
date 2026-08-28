// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Settings.h"
#include "WindowManager.h"
#include "controls/ctrlButton.h"
#include "controls/ctrlMultiline.h"
#include "controls/ctrlTextButton.h"
#include "desktops/PlayerView.h"
#include "desktops/dskGameInterface.h"
#include "ingameWindows/IngameWindow.h"
#include "ingameWindows/iwPadSystemMenu.h"
#include "input/FocusPath.h"
#include "postSystem/PostBox.h"
#include "postSystem/PostManager.h"
#include "world/GameWorld.h"
#include "world/GameWorldView.h"
#include "PadFixture.h"
#include "PadGameFixture.h"
#include "gameData/const_gui_ids.h"
#include <boost/test/unit_test.hpp>
#include <memory>
#include <string>

using rttr::test::PadViewFixture;

namespace {

/// Zwei Ansichten, zwei Pads - und die HUD-Einstellungen der ini bleiben unangetastet.
///
/// Warum die Sicherung: GameWorldView::ToggleShowBQ schreibt ueber SaveIngameSettingsValues in
/// SETTINGS.ingame zurueck, und ein GameWorldView liest denselben Wert beim BAU als Startwert.
/// Ohne die Ruecknahme entschiede die Reihenfolge der Testfaelle, mit welchem Zustand der
/// naechste anfaengt - genau die Art stiller Kopplung, an der Messungen zerbrechen.
struct PadMenuFixture : PadViewFixture<2>
{
    decltype(SETTINGS.ingame) savedIngame_ = SETTINGS.ingame;

    PadMenuFixture()
    {
        // Definierter Ausgangszustand statt "was der vorige Fall hinterlassen hat".
        for(unsigned i = 0; i < 2; ++i)
        {
            if(view(i).GetView().IsShowingBQ())
                view(i).GetView().ToggleShowBQ();
        }
        // Pad 10 nimmt Ansicht 0, Pad 11 danach Ansicht 1 (Reihenfolge der Benutzung,
        // PadRouter::FirstFreeSlot).
        pads.pickUp(10);
        step(16);
        pads.pickUp(11);
        step(16);
        BOOST_TEST_REQUIRE(view(0).HasPadCursor());
        BOOST_TEST_REQUIRE(view(1).HasPadCursor());
    }

    ~PadMenuFixture()
    {
        closeAllWindows();
        SETTINGS.ingame = savedIngame_;
    }

    /// Fokus DIESER Ansicht auf das Control mit dieser Kennung fahren - ausschliesslich mit
    /// Padereignissen. RB ist in FocusPath "eine Station weiter in ID-Reihenfolge"
    /// (FocusPath::OnPadButton -> Move(Dir::Next)).
    ///
    /// Bewusst ueber die KENNUNG und nicht ueber die Beschriftung: in frueheren Phasen sind
    /// dreimal Tests daran zerbrochen, dass sie englischen Text gegen einen uebersetzten
    /// Katalog gehalten haben.
    void focusTo(const PadDeviceId dev, const unsigned viewIdx, const unsigned ctrlId,
                 const Window* const expectedRoot)
    {
        // Die WURZEL wird mitgeprueft, sonst navigierte dieser Helfer im falschen Fenster
        // weiter und traefe dort eine zufaellig gleichlautende Kennung. Genau das ist beim
        // Bauen dieser Faelle einmal passiert, nachdem das Menue sich beim Waehlen schliesst.
        BOOST_TEST_REQUIRE(view(viewIdx).GetFocus().GetRoot() == expectedRoot);
        // PHASE 13: das Systemmenue IST ein Ring. Dort wandert der Fokus mit dem STEUERKREUZ
        // (ein Sektor weiter), waehrend die Schultern die SEITE wechseln; in einem gewoehnlichen
        // Fenster ist es umgekehrt. Gefragt wird der Ringzustand selbst und nicht das Fenster -
        // derselbe Wert, den auch dskGameInterface::OnPadButton liest.
        const PadButton nextCtrl =
          view(viewIdx).GetRing().IsOpen() ? PadButton::DpadRight : PadButton::RightShoulder;
        for(unsigned i = 0; i < 24u; ++i)
        {
            const Window* focused = view(viewIdx).GetFocus().GetFocused();
            BOOST_TEST_REQUIRE(focused != static_cast<const Window*>(nullptr));
            if(focused->GetID() == ctrlId)
                return;
            press(dev, nextCtrl);
        }
        BOOST_FAIL("Das Control ist per Pad nicht erreichbar");
    }

    /// JEDES offene Fenster sofort freigeben, solange Welt und Desktop noch leben.
    ///
    /// Wichtig ist das SOFORT (CloseNow statt Close): ein Fenster dieser Suite haelt
    /// Referenzen auf dskGameInterface und PlayerView, und uiHelper::Fixture raeumt die
    /// Fensterliste erst NACH der Zerstoerung von Welt und Desktop ab (Basisklassen sterben
    /// zuletzt). Ein hier vergessenes Fenster stuerzte dann in einem voellig anderen Testfall
    /// ab - genau das ist beim Bauen dieser Datei passiert, mit einem bad_alloc in
    /// PadWindowFocus.
    void closeAllWindows()
    {
        for(unsigned i = 0; i < 64u; ++i)
        {
            IngameWindow* wnd = WINDOWMANAGER.GetTopMostWindow();
            if(!wnd)
                break;
            WINDOWMANAGER.CloseNow(wnd);
        }
        WINDOWMANAGER.Draw();
        BOOST_TEST_REQUIRE(WINDOWMANAGER.GetTopMostWindow() == static_cast<IngameWindow*>(nullptr));
    }

    iwPadSystemMenu* menuOf(const unsigned viewIdx)
    {
        return dynamic_cast<iwPadSystemMenu*>(WINDOWMANAGER.FindNonModalWindow(CGI_PADMENU, viewIdx));
    }
};

/// Dieselbe laufende Partie wie rttr::test::PadGameFixture - nur mit der Aufraeumung im
/// DESTRUKTOR statt im Rumpf des Testfalls.
///
/// BEFUND DIESER RUNDE, gemessen: der Fall unten hatte seine Aufraeumschleife im RUMPF. Ein
/// BOOST_TEST_REQUIRE WIRFT bei Fehlschlag - der Rumpf laeuft ab dieser Zeile nicht mehr weiter,
/// und das modale Tagebuch samt Postfenster bleibt in der Fensterliste des WindowManagers
/// stehen, waehrend Welt, Client und Desktop darunter weggeraeumt werden. Die naechste Suite
/// faellt dann aus dem falschen Grund um und verdeckt den echten Fehlschlag.
///
/// Genau diese Fehlerklasse war fuer PadMenuFixture darueber schon behoben (der bad_alloc in
/// PadWindowFocus) - fuer die Spielfassung aber nicht. Ein Destruktor laeuft auch beim Abbruch;
/// deshalb steht hier, was vorher im Rumpf stand.
///
/// BEWUSST OHNE BOOST_TEST_REQUIRE: ein Destruktor darf nicht werfen. Dass wirklich nichts
/// stehenbleibt, prueft der eigene Fall unmittelbar nach dem Spielfall.
struct PadMenuGameFixture : rttr::test::PadGameFixture
{
    ~PadMenuGameFixture()
    {
        for(unsigned i = 0; i < 64u; ++i)
        {
            IngameWindow* wnd = WINDOWMANAGER.GetTopMostWindow();
            if(!wnd)
                break;
            WINDOWMANAGER.CloseNow(wnd);
        }
        WINDOWMANAGER.Draw();
        tearDownDesktop();
    }
};

/// Die Knopfnummer des Tagebuchknopfes im Postfenster.
///
/// iwPostWindow fuehrt seine Kennungen in einer anonymen enum in der .cpp (ID_FIRST_FREE = 1,
/// danach ID_SHOW_ALL = 2, ID_SHOW_GOAL = 3). Sie sind von aussen nicht erreichbar, deshalb
/// steht die Zahl hier - mit der Zusicherung unten, dass an dieser Stelle wirklich ein
/// SICHTBARER Knopf sitzt. Verschiebt sich die enum, faellt der Fall auf, statt still etwas
/// anderes zu druecken.
constexpr unsigned postWndDiaryButton = 3;

/// Die Knopfnummern des Einstellungsfensters (iwOptionsWindow) und der Weg dorthin.
///
/// iwOptionsWindow fuehrt seine Kennungen - wie iwPostWindow - in einer anonymen enum in der
/// .cpp; von aussen sind sie nicht erreichbar. Deshalb stehen die Zahlen hier, und deshalb
/// verankert der Fall unten sie DOPPELT: er zaehlt zusaetzlich die Textknoepfe des Fensters.
/// Verschiebt sich die enum, fallen beide Zusicherungen auf, statt still das falsche Control zu
/// pruefen.
constexpr unsigned optionsWndSurrenderButton = 18;
constexpr unsigned optionsWndEndGameButton = 19;
/// Der Knopf "Einstellungen" im Hauptauswahlfenster (iwMainMenu, Kennung 30 - dort im
/// Konstruktor sichtbar vergeben, nicht in einer anonymen enum).
constexpr unsigned mainMenuOptionsButton = 30;
/// Der Knopf "Hauptauswahl" der EINEN Knopfleiste (dskGameInterface.cpp, anonyme enum:
/// ID_btMap = 0, ID_btOptions = 1). Der Fall unten prueft, dass dort wirklich ein Knopf sitzt.
constexpr unsigned buttonBarMainSelection = 1;

} // namespace

BOOST_AUTO_TEST_SUITE(PadSystemMenuTests)

/// DER Befund des Auftraggebers, Teil 1, woertlich: "Bitte in Phase 11 mir erlauben, dass ich
/// die Symbole auf der Karte an und ausschalten kann."
///
/// Der ganze Weg laeuft ueber die Warteschlange des Treibers: Back, dann RB bis zum Schalter,
/// dann A. Kein Aufruf von ToggleShowBQ, kein SetRoot, kein Msg_ButtonClick von Hand.
BOOST_FIXTURE_TEST_CASE(APadPlayerTurnsTheConstructionAidSymbolsOnAndOffWithTheGamepadAlone, PadMenuFixture)
{
    BOOST_TEST_REQUIRE(!view(1).GetView().IsShowingBQ());

    press(11, PadButton::Back);
    iwPadSystemMenu* menu = menuOf(1);
    BOOST_TEST_REQUIRE(menu != static_cast<iwPadSystemMenu*>(nullptr));
    // Das Menue wird SOFORT betreten - ohne das muesste der Spieler erst Y druecken, und genau
    // solche unausgesprochenen Regeln sind der Befund dieser Phase.
    BOOST_TEST_REQUIRE(view(1).GetFocus().IsActive());
    BOOST_TEST_REQUIRE(view(1).GetFocus().GetRoot() == static_cast<Window*>(menu));

    focusTo(11, 1, iwPadSystemMenu::ID_CONSTRUCTION_AID, menuOf(1));
    press(11, PadButton::A);
    BOOST_TEST(view(1).GetView().IsShowingBQ());
    // WELLE 14: der erste Druck landet auf der MITTLEREN Stufe, nicht auf "alles". Der Befund
    // des Auftraggebers war woertlich, dass "alles" die Karte erschlaegt; der Umlauf beginnt
    // deshalb bei dem, was er wirklich wollte.
    BOOST_TEST((view(1).GetView().GetBqMode() == BqMode::Cursor));

    press(11, PadButton::A);
    BOOST_TEST((view(1).GetView().GetBqMode() == BqMode::All));

    // ... und wieder AUS. Genau das war bisher unmoeglich: Phase 9 schaltet die Bauhilfe beim
    // Oeffnen des Baumenues ein (ForceShowBQ), und kein Padknopf schaltete sie je wieder aus.
    press(11, PadButton::A);
    BOOST_TEST((view(1).GetView().GetBqMode() == BqMode::Off));
    BOOST_TEST(!view(1).GetView().IsShowingBQ());
}

/// Der Besitznachweis, und der schaerfste Fall dieser Phase: schaltet Spieler 1 die Symbole,
/// darf sich die Ansicht von Spieler 0 NICHT aendern.
///
/// Wird die Besitzzuordnung entfernt (PadOpenSystemMenu auf primary() statt auf view), kippt
/// die Bauhilfe von Ansicht 0 und die von Ansicht 1 bleibt stehen - beide Zusicherungen unten
/// werden rot.
BOOST_FIXTURE_TEST_CASE(TheConstructionAidOfTheOtherPlayerIsNotTouched, PadMenuFixture)
{
    BOOST_TEST_REQUIRE(!view(0).GetView().IsShowingBQ());
    BOOST_TEST_REQUIRE(!view(1).GetView().IsShowingBQ());

    press(11, PadButton::Back);
    BOOST_TEST_REQUIRE(menuOf(1) != static_cast<iwPadSystemMenu*>(nullptr));
    // Das Menue gehoert dem Sitzplatz, der gedrueckt hat - und keinem anderen.
    BOOST_TEST(menuOf(1)->GetOwner() == 1u);
    BOOST_TEST(menuOf(0) == static_cast<iwPadSystemMenu*>(nullptr));

    focusTo(11, 1, iwPadSystemMenu::ID_CONSTRUCTION_AID, menuOf(1));
    press(11, PadButton::A);

    BOOST_TEST(view(1).GetView().IsShowingBQ());
    BOOST_TEST(!view(0).GetView().IsShowingBQ());
}

/// Beide Spieler gleichzeitig, jeder in seinem eigenen Menue. Zwei Fenster derselben GUI_ID mit
/// verschiedenen Besitzern - der Mechanismus aus Phase 4e traegt das, und hier wird er gebraucht.
BOOST_FIXTURE_TEST_CASE(TwoPlayersEachHaveTheirOwnMenuAndTheirOwnSymbols, PadMenuFixture)
{
    press(10, PadButton::Back);
    press(11, PadButton::Back);
    BOOST_TEST_REQUIRE(menuOf(0) != static_cast<iwPadSystemMenu*>(nullptr));
    BOOST_TEST_REQUIRE(menuOf(1) != static_cast<iwPadSystemMenu*>(nullptr));
    BOOST_TEST(menuOf(0) != menuOf(1));

    // Nur Spieler 0 schaltet.
    focusTo(10, 0, iwPadSystemMenu::ID_CONSTRUCTION_AID, menuOf(0));
    press(10, PadButton::A);
    BOOST_TEST(view(0).GetView().IsShowingBQ());
    BOOST_TEST(!view(1).GetView().IsShowingBQ());

    // Jetzt auch Spieler 1 - und Spieler 0 behaelt seinen Zustand.
    focusTo(11, 1, iwPadSystemMenu::ID_CONSTRUCTION_AID, menuOf(1));
    press(11, PadButton::A);
    BOOST_TEST(view(0).GetView().IsShowingBQ());
    BOOST_TEST(view(1).GetView().IsShowingBQ());
}

/// Zweiter Druck auf Back schliesst das eigene Menue wieder - und laesst das des Nachbarn stehen.
BOOST_FIXTURE_TEST_CASE(BackClosesTheOwnMenuAgainAndLeavesTheOtherOneAlone, PadMenuFixture)
{
    press(10, PadButton::Back);
    press(11, PadButton::Back);
    BOOST_TEST_REQUIRE(menuOf(0) != static_cast<iwPadSystemMenu*>(nullptr));
    BOOST_TEST_REQUIRE(menuOf(1) != static_cast<iwPadSystemMenu*>(nullptr));

    press(11, PadButton::Back);
    WINDOWMANAGER.Draw();
    BOOST_TEST(menuOf(1) == static_cast<iwPadSystemMenu*>(nullptr));
    BOOST_TEST(menuOf(0) != static_cast<iwPadSystemMenu*>(nullptr));
    // Der Fokus von Spieler 1 haengt danach nicht in einem zerfallenen Fenster.
    BOOST_TEST(!view(1).GetFocus().IsActive());
}

/// DER Befund des Auftraggebers, Teil 2, woertlich: "ansonsten kann ich aktuell auch nicht mit
/// den Tagebuch eintraegen im singleplayer interagieren."
///
/// Der vollstaendige Weg mit dem Pad allein: Back -> Postfenster -> Y hinein -> Tagebuchknopf ->
/// A -> LESEN -> Y hinein -> A auf "Weiter" -> zu.
///
/// Das Postfenster gehoert dabei DEM SPIELER, der gedrueckt hat, und liest SEIN Postfach. Ohne
/// das Fach je Ansicht (dskGameInterface::InitPlayer) gaebe es fuer Spieler 1 ueberhaupt keins,
/// PostManager::SetMissionGoal liefe ins Leere, und der Tagebuchknopf waere unsichtbar.
BOOST_FIXTURE_TEST_CASE(APadPlayerOpensThePostOfficeAndReadsAndClosesADiaryEntry, PadMenuFixture)
{
    const std::string goal = "Build a sawmill and connect it to your castle.";
    PostBox* box = worldFixture.world.GetPostMgr().GetPostBox(view(1).GetPlayerId());
    BOOST_TEST_REQUIRE(box != static_cast<PostBox*>(nullptr));
    worldFixture.world.GetPostMgr().SetMissionGoal(view(1).GetPlayerId(), goal);

    // 1. Menue auf, Postfenster auf.
    press(11, PadButton::Back);
    BOOST_TEST_REQUIRE(menuOf(1) != static_cast<iwPadSystemMenu*>(nullptr));
    focusTo(11, 1, iwPadSystemMenu::ID_POST, menuOf(1));
    press(11, PadButton::A);

    IngameWindow* post = WINDOWMANAGER.FindNonModalWindow(CGI_POSTOFFICE, 1);
    BOOST_TEST_REQUIRE(post != static_cast<IngameWindow*>(nullptr));
    BOOST_TEST(post->GetOwner() == 1u);
    // Das Postfenster des NACHBARN gibt es nicht - jeder liest sein eigenes Fach.
    BOOST_TEST(WINDOWMANAGER.FindNonModalWindow(CGI_POSTOFFICE, 0) == static_cast<IngameWindow*>(nullptr));

    // 2. Das Menue ist weg, und der Spieler steht bereits IM Postfenster - er hat den Punkt
    //    gewaehlt, also gehoert ihm der Fokus dort. Ein zusaetzliches Y waere eine
    //    unausgesprochene Regel.
    WINDOWMANAGER.Draw();
    BOOST_TEST(menuOf(1) == static_cast<iwPadSystemMenu*>(nullptr));
    BOOST_TEST_REQUIRE(view(1).GetFocus().GetRoot() == static_cast<Window*>(post));

    // 3. Auf den Tagebuchknopf. Er ist nur sichtbar, weil dieser Spieler ein Missionsziel in
    //    SEINEM Fach hat.
    const Window* diaryBtn = post->GetCtrl<Window>(postWndDiaryButton);
    BOOST_TEST_REQUIRE(diaryBtn != static_cast<const Window*>(nullptr));
    BOOST_TEST_REQUIRE(diaryBtn->IsVisible());
    focusTo(11, 1, postWndDiaryButton, post);
    press(11, PadButton::A);

    // 4. Das Tagebuch steht da - und es steht der Text DIESES Spielers darin.
    //    Es ist modal, taucht also in FindNonModalWindow bewusst nicht auf.
    IngameWindow* diary = WINDOWMANAGER.GetTopMostWindow();
    BOOST_TEST_REQUIRE(diary != static_cast<IngameWindow*>(nullptr));
    BOOST_TEST_REQUIRE(diary->GetID() == static_cast<unsigned>(CGI_MISSION_STATEMENT));
    BOOST_TEST(diary->GetOwner() == 1u);
    const auto* text = diary->GetCtrl<ctrlMultiline>(0);
    BOOST_TEST_REQUIRE(text != static_cast<const ctrlMultiline*>(nullptr));
    BOOST_TEST_REQUIRE(text->GetNumLines() >= 1u);
    BOOST_TEST(text->GetLine(0) == goal);

    // 5. Und wieder zu. B taete es NICHT (CloseBehavior::Custom, siehe PadCloseTopMostWindow) -
    //    genau das ist die Stelle, an der der Auftraggeber haengengeblieben ist. Der Weg ist Y
    //    (in das obenauf liegende Fenster hinein) und dann A auf "Weiter".
    //
    //    Dass Y hier ueberhaupt ankommt, ist neu: der Fokus steht im POSTfenster, und
    //    FocusPath verschluckte bisher jede Flanke, solange er steht. Genau deshalb wird Y in
    //    dskGameInterface::OnPadButton VOR dem Fokus abgefragt, wenn obenauf ein ANDERES
    //    eigenes Fenster liegt.
    press(11, PadButton::B);
    BOOST_TEST(!diary->ShouldBeClosed());
    press(11, PadButton::Y);
    BOOST_TEST_REQUIRE(view(1).GetFocus().GetRoot() == static_cast<Window*>(diary));
    press(11, PadButton::A);
    BOOST_TEST(diary->ShouldBeClosed());
}

/// Y erreicht ein Fenster, das sich UEBER dem geoeffnet hat, in dem der Spieler gerade steht.
///
/// Das ist der Fall aus dem Befund des Auftraggebers, isoliert: der Fokus steht im Postfenster,
/// ein Knopf darin oeffnet das Tagebuch obenauf - und FocusPath verschluckt bisher JEDE
/// Knopfflanke, solange er steht (FocusPath::OnPadButton gibt immer true zurueck). Y kam damit
/// nie an, und der einzige Ausweg war das ungeschriebene "erst B, dann Y".
///
/// Der Fall drueckt deshalb AUSDRUECKLICH kein B dazwischen. Nimmt man die Vorabfrage von Y in
/// dskGameInterface::OnPadButton wieder heraus, bleibt der Fokus im Postfenster und die
/// Zusicherung unten wird rot.
BOOST_FIXTURE_TEST_CASE(YReachesAWindowThatOpenedOnTopWhileTheFocusIsStillInTheOldOne, PadMenuFixture)
{
    worldFixture.world.GetPostMgr().SetMissionGoal(view(1).GetPlayerId(), "A goal for player one");

    press(11, PadButton::Back);
    focusTo(11, 1, iwPadSystemMenu::ID_POST, menuOf(1));
    press(11, PadButton::A);
    IngameWindow* post = WINDOWMANAGER.FindNonModalWindow(CGI_POSTOFFICE, 1);
    BOOST_TEST_REQUIRE(post != static_cast<IngameWindow*>(nullptr));
    WINDOWMANAGER.Draw();

    focusTo(11, 1, postWndDiaryButton, post);
    press(11, PadButton::A);
    IngameWindow* diary = WINDOWMANAGER.GetTopMostWindow();
    BOOST_TEST_REQUIRE(diary != static_cast<IngameWindow*>(nullptr));
    BOOST_TEST_REQUIRE(diary->GetID() == static_cast<unsigned>(CGI_MISSION_STATEMENT));
    // Der Fokus steht noch im Postfenster - das Tagebuch liegt sichtbar darueber.
    BOOST_TEST_REQUIRE(view(1).GetFocus().GetRoot() == static_cast<Window*>(post));

    // EIN Druck auf Y, kein B davor.
    press(11, PadButton::Y);
    BOOST_TEST(view(1).GetFocus().GetRoot() == static_cast<Window*>(diary));
    press(11, PadButton::A);
    BOOST_TEST(diary->ShouldBeClosed());
}

/// Solange ein MODALES Fenster steht, geht das Padmenue nicht auf.
///
/// Ohne diese Sperre entstuende das Menue HINTER dem modalen Fenster (WindowManager::DoShow
/// sortiert Nicht-Modale vor dem ersten Modalen ein), waere unsichtbar - und der Fokus dieses
/// Spielers spraenge trotzdem hinein. Er bediente dann blind ein Fenster, das er nicht sieht.
/// Der Fall ist konkret: das Tagebuch selbst ist modal.
BOOST_FIXTURE_TEST_CASE(BackDoesNotOpenTheMenuBehindAModalWindow, PadMenuFixture)
{
    worldFixture.world.GetPostMgr().SetMissionGoal(view(1).GetPlayerId(), "A goal");

    press(11, PadButton::Back);
    focusTo(11, 1, iwPadSystemMenu::ID_POST, menuOf(1));
    press(11, PadButton::A);
    IngameWindow* post = WINDOWMANAGER.FindNonModalWindow(CGI_POSTOFFICE, 1);
    BOOST_TEST_REQUIRE(post != static_cast<IngameWindow*>(nullptr));
    WINDOWMANAGER.Draw();
    focusTo(11, 1, postWndDiaryButton, post);
    press(11, PadButton::A);

    IngameWindow* diary = WINDOWMANAGER.GetTopMostWindow();
    BOOST_TEST_REQUIRE(diary != static_cast<IngameWindow*>(nullptr));
    BOOST_TEST_REQUIRE(diary->IsModal());

    // Back tut jetzt nichts - und vor allem wandert der Fokus nicht in ein verstecktes Menue.
    Window* const rootBefore = view(1).GetFocus().GetRoot();
    press(11, PadButton::Back);
    BOOST_TEST(menuOf(1) == static_cast<iwPadSystemMenu*>(nullptr));
    BOOST_TEST(view(1).GetFocus().GetRoot() == rootBefore);
    BOOST_TEST(WINDOWMANAGER.GetTopMostWindow() == diary);
}

/// DER BLOCKER DIESER RUNDE, gemessen von zwei Pruefern unabhaengig voneinander: die Sperre
/// darueber war GLOBAL statt besitzerbezogen.
///
/// Sitzplatz 1 laesst sein Tagebuch offen stehen (modal, Besitzer 1) - und danach bekam
/// Sitzplatz 0 auf Back kein Menue mehr. Das ist eine SACKGASSE OHNE AUSWEG: ein fremdes
/// modales Fenster kann Sitzplatz 0 nicht wegraeumen (GetTopMostWindow(0) liefert es nicht,
/// also betritt Y es nicht und schliesst B es nicht), er muesste warten, bis der Nachbar
/// handelt. Und das Padmenue ist sein EINZIGER Weg zu Karte, Post und Hauptauswahl.
///
/// Der Fall drueckt Back fuer Sitzplatz 0 zu einem Zeitpunkt, an dem das fremde Modale
/// nachweislich obenauf liegt (die Zusicherung steht direkt davor). Nimmt man den Besitzer in
/// dskGameInterface::PadOpenSystemMenu wieder heraus, wird die erste Zusicherung unten rot.
///
/// Die zweite Haelfte haelt fest, dass die BEGRUENDUNG der Sperre erhalten bleibt: fuer den
/// Sitzplatz, dem das Modale gehoert, bleibt Back weiter wirkungslos.
BOOST_FIXTURE_TEST_CASE(AModalWindowOfAnotherSeatDoesNotBlockTheMenuOfThisSeat, PadMenuFixture)
{
    worldFixture.world.GetPostMgr().SetMissionGoal(view(1).GetPlayerId(), "A goal for player one");

    // Sitzplatz 1 bringt sein Tagebuch auf den Schirm - ueber den Padweg, wie oben.
    press(11, PadButton::Back);
    focusTo(11, 1, iwPadSystemMenu::ID_POST, menuOf(1));
    press(11, PadButton::A);
    IngameWindow* post = WINDOWMANAGER.FindNonModalWindow(CGI_POSTOFFICE, 1);
    BOOST_TEST_REQUIRE(post != static_cast<IngameWindow*>(nullptr));
    WINDOWMANAGER.Draw();
    focusTo(11, 1, postWndDiaryButton, post);
    press(11, PadButton::A);

    IngameWindow* diary = WINDOWMANAGER.GetTopMostWindow();
    BOOST_TEST_REQUIRE(diary != static_cast<IngameWindow*>(nullptr));
    BOOST_TEST_REQUIRE(diary->IsModal());
    // Es gehoert DEM NACHBARN und nicht dem Bildschirm - das ist der ganze Punkt.
    BOOST_TEST_REQUIRE(diary->GetOwner() == 1u);
    // Sitzplatz 0 hat bis hierher nichts angefasst: kein Fenster, kein Fokus.
    BOOST_TEST_REQUIRE(!view(0).GetFocus().IsActive());
    BOOST_TEST_REQUIRE(menuOf(0) == static_cast<iwPadSystemMenu*>(nullptr));

    // JETZT drueckt Sitzplatz 0 seinen Back-Knopf, waehrend das fremde Modale obenauf liegt.
    press(10, PadButton::Back);
    iwPadSystemMenu* menu0 = menuOf(0);
    BOOST_TEST_REQUIRE(menu0 != static_cast<iwPadSystemMenu*>(nullptr));
    BOOST_TEST(menu0->GetOwner() == 0u);
    BOOST_TEST(view(0).GetFocus().GetRoot() == static_cast<Window*>(menu0));
    // Das Tagebuch des Nachbarn liegt unveraendert obenauf und ist nicht angefasst worden.
    BOOST_TEST(WINDOWMANAGER.GetTopMostWindow() == diary);
    BOOST_TEST(view(1).GetFocus().GetRoot() == static_cast<Window*>(post));

    // Und die Begruendung der Sperre bleibt: fuer den BESITZER des Modalen tut Back weiter
    // nichts. Er kommt mit Y hinein und mit A auf "Weiter" heraus - danach geht auch sein Menue
    // wieder auf. Die Sperre ist dort eine Reihenfolge, keine Falle.
    press(11, PadButton::Back);
    BOOST_TEST(menuOf(1) == static_cast<iwPadSystemMenu*>(nullptr));
    press(11, PadButton::Y);
    BOOST_TEST_REQUIRE(view(1).GetFocus().GetRoot() == static_cast<Window*>(diary));
    press(11, PadButton::A);
    BOOST_TEST_REQUIRE(diary->ShouldBeClosed());
    WINDOWMANAGER.Draw();
    press(11, PadButton::Back);
    BOOST_TEST(menuOf(1) != static_cast<iwPadSystemMenu*>(nullptr));
}

/// Die Gegenrichtung, ebenfalls gemessen (Pruefer 2): ein modales Fenster von Sitzplatz 0 sperrte
/// das Menue von Sitzplatz 1. Das trifft den MAUSSPIELER an einer Stelle, an der er es gar nicht
/// merkt - eine gewoehnliche Nachrichtenbox der Hauptansicht ist modal (iwMsgbox: modal = true)
/// und nahm damit allen Padspielern den einzigen Weg zu Karte, Post und Hauptauswahl.
BOOST_FIXTURE_TEST_CASE(AModalWindowOfTheMainSeatDoesNotBlockTheMenuOfASecondSeat, PadMenuFixture)
{
    worldFixture.world.GetPostMgr().SetMissionGoal(view(0).GetPlayerId(), "A goal for player zero");

    press(10, PadButton::Back);
    focusTo(10, 0, iwPadSystemMenu::ID_POST, menuOf(0));
    press(10, PadButton::A);
    IngameWindow* post = WINDOWMANAGER.FindNonModalWindow(CGI_POSTOFFICE, 0);
    BOOST_TEST_REQUIRE(post != static_cast<IngameWindow*>(nullptr));
    WINDOWMANAGER.Draw();
    focusTo(10, 0, postWndDiaryButton, post);
    press(10, PadButton::A);

    IngameWindow* diary = WINDOWMANAGER.GetTopMostWindow();
    BOOST_TEST_REQUIRE(diary != static_cast<IngameWindow*>(nullptr));
    BOOST_TEST_REQUIRE(diary->IsModal());
    BOOST_TEST_REQUIRE(diary->GetOwner() == 0u);

    press(11, PadButton::Back);
    BOOST_TEST(menuOf(1) != static_cast<iwPadSystemMenu*>(nullptr));
    BOOST_TEST(WINDOWMANAGER.GetTopMostWindow() == diary);
}

/// Ohne das Postfach je Ansicht waere der Befund oben nur zufaellig gruen. Deshalb hier
/// unmittelbar: JEDE dargestellte Ansicht hat nach dem Aufbau ein eigenes Fach.
///
/// Gemessen wurde vor dieser Phase: nur Spieler 0 hatte ein Fach, und PostManager::SendMsg gibt
/// bei fehlendem Fach STILL auf - jede Meldung an die Spieler 2 bis 4 wurde verworfen.
BOOST_FIXTURE_TEST_CASE(EveryViewHasItsOwnPostBox, PadMenuFixture)
{
    const PostManager& mgr = worldFixture.world.GetPostMgr();
    PostBox* box0 = mgr.GetPostBox(view(0).GetPlayerId());
    PostBox* box1 = mgr.GetPostBox(view(1).GetPlayerId());
    BOOST_TEST_REQUIRE(box0 != static_cast<PostBox*>(nullptr));
    BOOST_TEST_REQUIRE(box1 != static_cast<PostBox*>(nullptr));
    BOOST_TEST(box0 != box1);

    worldFixture.world.GetPostMgr().SetMissionGoal(view(1).GetPlayerId(), "Only player 1 has this goal");
    BOOST_TEST(box1->GetCurrentMissionGoal() == "Only player 1 has this goal");
    BOOST_TEST(box0->GetCurrentMissionGoal() == "");
}

/// Die Uebersichtskarte und die Hauptauswahl gehen denselben Weg - und sie gehoeren ebenfalls
/// dem druckenden Sitzplatz. Damit ist die vierte Leistenfunktion (Speichern, Aufgeben,
/// Statistik liegen hinter der Hauptauswahl) am Pad erreichbar.
BOOST_FIXTURE_TEST_CASE(TheMenuAlsoReachesTheOutlineMapAndTheMainSelection, PadMenuFixture)
{
    press(11, PadButton::Back);
    BOOST_TEST_REQUIRE(menuOf(1) != static_cast<iwPadSystemMenu*>(nullptr));
    focusTo(11, 1, iwPadSystemMenu::ID_MINIMAP, menuOf(1));
    press(11, PadButton::A);
    IngameWindow* minimap = WINDOWMANAGER.FindNonModalWindow(CGI_MINIMAP, 1);
    BOOST_TEST_REQUIRE(minimap != static_cast<IngameWindow*>(nullptr));
    BOOST_TEST(minimap->GetOwner() == 1u);
    BOOST_TEST(WINDOWMANAGER.FindNonModalWindow(CGI_MINIMAP, 0) == static_cast<IngameWindow*>(nullptr));
    // Nach der Wahl ist das Menue zu und der Spieler steht in der Uebersichtskarte.
    WINDOWMANAGER.Draw();
    BOOST_TEST_REQUIRE(menuOf(1) == static_cast<iwPadSystemMenu*>(nullptr));
    BOOST_TEST(view(1).GetFocus().GetRoot() == static_cast<Window*>(minimap));

    // Also noch einmal Back - und das geht, obwohl der Fokus in einem Fenster steht. Genau
    // dafuer wird Back in dskGameInterface::OnPadButton VOR dem Fokus abgefragt.
    press(11, PadButton::Back);
    BOOST_TEST_REQUIRE(menuOf(1) != static_cast<iwPadSystemMenu*>(nullptr));
    focusTo(11, 1, iwPadSystemMenu::ID_MAIN_SELECTION, menuOf(1));
    press(11, PadButton::A);
    IngameWindow* mainSel = WINDOWMANAGER.FindNonModalWindow(CGI_MAINSELECTION, 1);
    BOOST_TEST_REQUIRE(mainSel != static_cast<IngameWindow*>(nullptr));
    BOOST_TEST(mainSel->GetOwner() == 1u);
    BOOST_TEST(WINDOWMANAGER.FindNonModalWindow(CGI_MAINSELECTION, 0) == static_cast<IngameWindow*>(nullptr));
}

/// BEFUND DIESER RUNDE: ab jetzt ist "Spiel beenden" von JEDEM Sitzplatz drei Knopfdruecke
/// entfernt - Back, Hauptauswahl, Einstellungen. Der Bestaetigungsdialog dahinter gehoert
/// absichtlich dem BILDSCHIRM (iwEndgame: SetOwner(SHARED_WINDOW_OWNER)) und liegt damit fuer
/// alle vier obenauf; ein A auf "OK" ruft GAMEMANAGER.ShowMenu() und die Partie ist fuer alle
/// weg. Vor dieser Phase war das fuer die Sitzplaetze 1 bis 3 unerreichbar (sie kamen an die
/// Knopfleiste nicht heran), es ist also ein NEUES Risiko dieser Phase.
///
/// Die Regel: die Partie beenden darf nur der Sitzplatz, dem auch Maus, Tastatur und
/// Knopfleiste gehoeren. Die uebrigen finden den Knopf gar nicht erst - "Aufgeben" bleibt
/// ihnen, und das ist der richtige Knopf fuer sie: iwSurrender erzeugt ein GameCommand, das
/// ueber den Fensterbesitz auf IHREN Spieler bucht, die Partie laeuft weiter.
///
/// Gefahren wird der volle Padweg. Nimmt man die Klammer in iwOptionsWindow wieder heraus, wird
/// die vorletzte Zusicherung rot (der Knopf ist dann da).
BOOST_FIXTURE_TEST_CASE(ASecondSeatCannotReachEndGameButKeepsSurrender, PadMenuFixture)
{
    press(11, PadButton::Back);
    BOOST_TEST_REQUIRE(menuOf(1) != static_cast<iwPadSystemMenu*>(nullptr));
    focusTo(11, 1, iwPadSystemMenu::ID_MAIN_SELECTION, menuOf(1));
    press(11, PadButton::A);
    IngameWindow* mainSel = WINDOWMANAGER.FindNonModalWindow(CGI_MAINSELECTION, 1);
    BOOST_TEST_REQUIRE(mainSel != static_cast<IngameWindow*>(nullptr));
    WINDOWMANAGER.Draw();
    BOOST_TEST_REQUIRE(view(1).GetFocus().GetRoot() == static_cast<Window*>(mainSel));

    focusTo(11, 1, mainMenuOptionsButton, mainSel);
    press(11, PadButton::A);
    IngameWindow* opts = WINDOWMANAGER.FindNonModalWindow(CGI_OPTIONSWINDOW, 1);
    BOOST_TEST_REQUIRE(opts != static_cast<IngameWindow*>(nullptr));
    BOOST_TEST(opts->GetOwner() == 1u);

    // "Aufgeben" ist da - der Sitzplatz kann sehr wohl SEIN Volk aufgeben.
    BOOST_TEST_REQUIRE(opts->GetCtrl<ctrlTextButton>(optionsWndSurrenderButton)
                       != static_cast<ctrlTextButton*>(nullptr));
    // "Spiel beenden" gibt es fuer ihn nicht.
    BOOST_TEST(opts->GetCtrl<Window>(optionsWndEndGameButton) == static_cast<Window*>(nullptr));
    // Zweite Verankerung, unabhaengig von den Zahlen oben: DREI Textknoepfe (Musikspieler,
    // Erweitert, Aufgeben) statt vier.
    BOOST_TEST(opts->GetCtrls<ctrlTextButton>().size() == 3u);
}

/// Und die Gegenprobe zum MAUSSPIELER, gemessen und nicht angenommen: sein Weg ist voellig
/// unveraendert.
///
/// Gefahren wird der Mausweg von vorn bis hinten - Klick auf den Knopf "Hauptauswahl" der EINEN
/// Knopfleiste (das ist der Knopf, ueber den dskGameInterface::Msg_ButtonClick ueberhaupt erst
/// gerufen wird, und der klammert auf primary()), dann ein echter Mausklick auf
/// "Einstellungen" ueber den WindowManager - genau der setzt die Besitzklammer aus dem Fenster,
/// in das zugestellt wird.
BOOST_FIXTURE_TEST_CASE(TheMousePlayerStillReachesEndGame, PadMenuFixture)
{
    // Im echten Spiel aktiviert WindowManager::Switch den Desktop und damit seine Controls; die
    // Fixture baut das dskGameInterface direkt und laesst es neben dem Desktop des
    // WindowManagers stehen. Nachgeholt wird ausschliesslich diese Aktivierung - genau der
    // Aufruf, den Window::SetActive macht.
    dsk->ActivateControls(true);

    auto* barBt = dsk->GetCtrl<ctrlButton>(buttonBarMainSelection);
    BOOST_TEST_REQUIRE(barBt != static_cast<ctrlButton*>(nullptr));
    const Position barBtPos = barBt->GetDrawPos() + DrawPoint(barBt->GetSize().x / 2, barBt->GetSize().y / 2);
    // Der Weg, den WindowManager::RelayMouseMessage dem Desktop gegenueber nimmt.
    dsk->RelayMouseMessage(&Window::Msg_LeftDown, MouseCoords(barBtPos));
    dsk->RelayMouseMessage(&Window::Msg_LeftUp, MouseCoords(barBtPos));

    IngameWindow* mainSel = WINDOWMANAGER.FindNonModalWindow(CGI_MAINSELECTION, 0);
    BOOST_TEST_REQUIRE(mainSel != static_cast<IngameWindow*>(nullptr));
    BOOST_TEST(mainSel->GetOwner() == 0u);

    auto* optBt = mainSel->GetCtrl<ctrlButton>(mainMenuOptionsButton);
    BOOST_TEST_REQUIRE(optBt != static_cast<ctrlButton*>(nullptr));
    const Position optBtPos = optBt->GetDrawPos() + DrawPoint(optBt->GetSize().x / 2, optBt->GetSize().y / 2);
    // Der erste Losklick raeumt nur die Sperre gegen den Durchrutschklick (siehe
    // testMouseRoadBuilding.cpp).
    WINDOWMANAGER.Msg_LeftUp(MouseCoords(optBtPos));
    WINDOWMANAGER.Msg_LeftDown(MouseCoords(optBtPos));
    WINDOWMANAGER.Msg_LeftUp(MouseCoords(optBtPos));

    IngameWindow* opts = WINDOWMANAGER.FindNonModalWindow(CGI_OPTIONSWINDOW, 0);
    BOOST_TEST_REQUIRE(opts != static_cast<IngameWindow*>(nullptr));
    BOOST_TEST(opts->GetOwner() == 0u);
    // Beide Knoepfe stehen, wie eh und je - VIER Textknoepfe.
    BOOST_TEST(opts->GetCtrl<ctrlTextButton>(optionsWndSurrenderButton) != static_cast<ctrlTextButton*>(nullptr));
    BOOST_TEST(opts->GetCtrl<ctrlTextButton>(optionsWndEndGameButton) != static_cast<ctrlTextButton*>(nullptr));
    BOOST_TEST(opts->GetCtrls<ctrlTextButton>().size() == 4u);
}

/// Namen sind der zweite Anzeigeschalter im Menue - und ebenfalls je Ansicht.
///
/// PHASE 13: Namen und Auslastung sind hier GETRENNT (ID_NAMES / ID_PRODUCTIVITY). Der
/// gekoppelte Schalter war ein Platzkompromiss der Knopfliste; der Ring hat den Platz. Der
/// Nachweis prueft deshalb beides einzeln - und ausdruecklich auch, dass der eine den anderen
/// NICHT mitzieht.
BOOST_FIXTURE_TEST_CASE(NamesAndOutputAreSwitchedPerViewAndSeparately, PadMenuFixture)
{
    const bool names0 = view(0).GetView().IsShowingNames();
    const bool names1 = view(1).GetView().IsShowingNames();
    const bool prod1 = view(1).GetView().IsShowingProductivity();

    press(11, PadButton::Back);
    focusTo(11, 1, iwPadSystemMenu::ID_NAMES, menuOf(1));
    press(11, PadButton::A);

    BOOST_TEST(view(1).GetView().IsShowingNames() == !names1);
    BOOST_TEST(view(0).GetView().IsShowingNames() == names0);
    // Die Auslastung ist NICHT mitgegangen - das ist der ganze Punkt der Trennung.
    BOOST_TEST(view(1).GetView().IsShowingProductivity() == prod1);

    // Und der zweite Schalter wirkt fuer sich.
    focusTo(11, 1, iwPadSystemMenu::ID_PRODUCTIVITY, menuOf(1));
    press(11, PadButton::A);
    BOOST_TEST(view(1).GetView().IsShowingProductivity() == !prod1);
    BOOST_TEST(view(1).GetView().IsShowingNames() == !names1);
}

/// Ein Fenster, das ein Padspieler geoeffnet hat, darf sich die gemerkten Fenstereinstellungen
/// des HAUPTSPIELERS nicht unter den Nagel reissen.
///
/// SETTINGS.windows.persistentSettings ist allein nach GUI_ID geschluesselt; vor dieser Phase
/// war das unerreichbar, weil es nur Fenster der Hauptansicht gab. Jetzt gibt es sie - und ohne
/// die Klemme in IngameWindow schriebe das Postfenster von Spieler 1 Position, Minimierzustand
/// und "war offen" von Spieler 0 um.
BOOST_FIXTURE_TEST_CASE(AWindowOfASecondSeatDoesNotOverwriteTheRememberedSettings, PadMenuFixture)
{
    auto& persisted = SETTINGS.windows.persistentSettings[CGI_POSTOFFICE];
    const auto savedPos = persisted.lastPos;
    persisted.isOpen = false;
    persisted.isMinimized = false;

    press(11, PadButton::Back);
    focusTo(11, 1, iwPadSystemMenu::ID_POST, menuOf(1));
    press(11, PadButton::A);
    BOOST_TEST_REQUIRE(WINDOWMANAGER.FindNonModalWindow(CGI_POSTOFFICE, 1) != static_cast<IngameWindow*>(nullptr));

    // Das Fenster des zweiten Sitzplatzes hat NICHTS gemerkt.
    BOOST_TEST(!persisted.isOpen);
    BOOST_TEST((persisted.lastPos == savedPos));

    // Und die Hauptansicht merkt sich weiterhin alles - der Einzelspieler mit Maus aendert sich
    // nicht.
    {
        const dskGameInterface::ViewScope ownerScope(0);
        dsk->OpenPostOfficeFor(view(0));
    }
    BOOST_TEST(persisted.isOpen);
}

/// DER ABNAHMEFALL IN EINER ECHTEN PARTIE.
///
/// Alles darueber laeuft ohne Netz (PadViewFixture). Hier steht ein echter GameServer und ein
/// echter GameClient ueber Loopback, mit zwei lokalen Menschen - also genau die Lage, in der
/// Vorbereiter B gemessen hat, dass es nur EIN Postfach gibt (das des Hauptspielers) und dass
/// die Post der uebrigen Spieler spurlos verschwindet.
///
/// Gemessen wird derselbe Weg wie oben: Padereignisse in die Warteschlange des Treibers, dann
/// der normale Bildlauf (dskGameInterface::UpdateInput, das ist der Aufruf, den Run() macht).
BOOST_FIXTURE_TEST_CASE(InARunningGameThePadMenuServesTheSeatThatPressed, PadMenuGameFixture)
{
    setUpTwoLocalPlayers();
    PlayerView& v0 = dsk->GetPlayerView(0);
    PlayerView& v1 = dsk->GetPlayerView(1);
    if(v0.GetView().IsShowingBQ())
        v0.GetView().ToggleShowBQ();
    if(v1.GetView().IsShowingBQ())
        v1.GetView().ToggleShowBQ();

    pads.pickUp(10);
    step(16);
    pads.pickUp(11);
    step(16);
    BOOST_TEST_REQUIRE(v1.HasPadCursor());

    // Jeder der beiden Menschen hat ein eigenes Postfach - in der laufenden Partie.
    BOOST_TEST_REQUIRE(world().GetPostMgr().GetPostBox(0) != static_cast<PostBox*>(nullptr));
    BOOST_TEST_REQUIRE(world().GetPostMgr().GetPostBox(1) != static_cast<PostBox*>(nullptr));
    world().GetPostMgr().SetMissionGoal(1, "Player one has a goal in a real game");

    // 1. Menue auf, Bauhilfe an - nur bei Spieler 1.
    press(11, PadButton::Back);
    auto* menu = dynamic_cast<iwPadSystemMenu*>(WINDOWMANAGER.FindNonModalWindow(CGI_PADMENU, 1));
    BOOST_TEST_REQUIRE(menu != static_cast<iwPadSystemMenu*>(nullptr));
    BOOST_TEST(menu->GetOwner() == 1u);
    // Fokus wandern lassen - im RING mit dem Steuerkreuz (Phase 13), in einem gewoehnlichen
    // Fenster mit den Schultern (RB vorwaerts, LB rueckwaerts, in ID-Reihenfolge). FocusPath
    // laeuft ausdruecklich NICHT um, deshalb braucht der Fensterfall beide Richtungen; der Ring
    // laeuft um und kommt mit einer aus.
    const auto focusToId = [&](const unsigned target) {
        for(unsigned i = 0; i < 24u; ++i)
        {
            const Window* focused = v1.GetFocus().GetFocused();
            BOOST_TEST_REQUIRE(focused != static_cast<const Window*>(nullptr));
            if(focused->GetID() == target)
                return;
            if(v1.GetRing().IsOpen())
                press(11, PadButton::DpadRight);
            else
                press(11, focused->GetID() < target ? PadButton::RightShoulder : PadButton::LeftShoulder);
        }
        BOOST_FAIL("Das Control ist per Pad nicht erreichbar");
    };

    focusToId(iwPadSystemMenu::ID_CONSTRUCTION_AID);
    press(11, PadButton::A);
    BOOST_TEST(v1.GetView().IsShowingBQ());
    BOOST_TEST(!v0.GetView().IsShowingBQ());

    // 2. Postfenster auf, Tagebuch lesen und schliessen.
    focusToId(iwPadSystemMenu::ID_POST);
    press(11, PadButton::A);
    IngameWindow* post = WINDOWMANAGER.FindNonModalWindow(CGI_POSTOFFICE, 1);
    BOOST_TEST_REQUIRE(post != static_cast<IngameWindow*>(nullptr));
    BOOST_TEST(post->GetOwner() == 1u);
    WINDOWMANAGER.Draw();
    BOOST_TEST_REQUIRE(v1.GetFocus().GetRoot() == static_cast<Window*>(post));

    focusToId(postWndDiaryButton);
    press(11, PadButton::A);

    IngameWindow* diary = WINDOWMANAGER.GetTopMostWindow();
    BOOST_TEST_REQUIRE(diary != static_cast<IngameWindow*>(nullptr));
    BOOST_TEST_REQUIRE(diary->GetID() == static_cast<unsigned>(CGI_MISSION_STATEMENT));
    BOOST_TEST(diary->GetOwner() == 1u);
    const auto* text = diary->GetCtrl<ctrlMultiline>(0);
    BOOST_TEST_REQUIRE(text != static_cast<const ctrlMultiline*>(nullptr));
    BOOST_TEST(text->GetLine(0) == "Player one has a goal in a real game");

    press(11, PadButton::Y);
    BOOST_TEST_REQUIRE(v1.GetFocus().GetRoot() == static_cast<Window*>(diary));
    press(11, PadButton::A);
    BOOST_TEST(diary->ShouldBeClosed());

    // KEINE Aufraeumung im Rumpf: sie steht im Destruktor von PadMenuGameFixture und laeuft
    // damit auch dann, wenn dieser Fall vorher an einem BOOST_TEST_REQUIRE abbricht.
}

/// Der Nachweis fuer den Destruktor darueber, und er ist bewusst der NAECHSTE Fall in derselben
/// Suite: die Fensterliste des WindowManagers ist leer, wenn hier angefangen wird.
///
/// Bleibt in der Spielfassung ein Fenster stehen, haelt es Referenzen auf einen bereits toten
/// dskGameInterface, eine tote PlayerView und eine tote Welt. Der Schaden trifft nicht den Fall,
/// der ihn anrichtet, sondern den naechsten - genau darum steht die Messung hier und nicht dort.
///
/// Gegenprobe: zieht man die Aufraeumung zurueck in den Rumpf des Spielfalls und laesst diesen
/// vorher abbrechen, wird DIESE Zusicherung rot.
BOOST_AUTO_TEST_CASE(TheRunningGameCaseLeavesNoWindowBehind)
{
    BOOST_TEST(WINDOWMANAGER.GetTopMostWindow() == static_cast<IngameWindow*>(nullptr));
}

BOOST_AUTO_TEST_SUITE_END()
