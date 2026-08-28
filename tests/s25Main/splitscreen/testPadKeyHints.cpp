// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

// DER AUSLOESER, woertlich vom Auftraggeber:
// "Um Eisenerz zu finden soll ich einen Gelehrten losschicken, da ist noch nicht genau klar
//  wie ich das mache."
//
// Er meint den GEOLOGEN, und die Funktion gibt es und sie ist am Pad erreichbar: Zeiger auf eine
// eigene Flagge, RB oeffnet das Aktionsfenster, Y hinein, Fokus auf den Knopf, A. Von diesen
// fuenf Schritten standen RB und Y nirgends auf dem Bildschirm, und der Knopf selbst zeigt nur
// ein Icon mit einem Zweiwort-Tooltip.
//
// Diese Datei bewacht die beiden Antworten darauf:
//
//  (a) DIE TASTENHINWEISLEISTE (CONTROLLER-UX.md 6.2). In jedem Zustand steht unter der eigenen
//      Ansicht, welche Tasten belegt sind und was sie tun.
//  (b) DER KLARTEXT ZU EINER HANDLUNG. Steht der Fokus auf "Geologen rufen", sagt der Kasten in
//      ganzen Saetzen, was der Mann tut, was er braucht und was danach zu tun ist.
//
// UND DIE HAERTERE HAELFTE, das eigentliche Abnahmekriterium: ein Hinweis, der luegt, ist
// schlimmer als keiner. Jede Zusicherung hier DRUECKT den Knopf, den die Leiste nennt, und misst
// die Wirkung - ueber den produktiven Weg (Padereignis in die Warteschlange des Treibers, dann
// dskGameInterface::UpdateInput). Kein Fall dieser Datei ruft HintsFor selbst auf, um zu
// erzeugen, was der Spieler angeblich sieht; wo eine reine Rechnung geprueft wird, steht es
// ausdruecklich daneben.

#include "GamePlayer.h"
#include "Loader.h"
#include "Settings.h"
#include "PointOutput.h"
#include "RttrForeachPt.h"
#include "WindowManager.h"
#include "buildings/nobBaseWarehouse.h"
#include "controls/ctrlButton.h"
#include "controls/ctrlGroup.h"
#include "controls/ctrlProgress.h"
#include "controls/ctrlTab.h"
#include "desktops/PlayerView.h"
#include "desktops/dskGameInterface.h"
#include "driver/PadEvent.h"
#include "ingameWindows/IngameWindow.h"
#include "ingameWindows/iwAction.h"
#include "ingameWindows/iwPadSystemMenu.h"
#include "input/FocusPath.h"
#include "input/PlayerBrief.h"
#include "languages.h"
#include "mygettext/mygettext.h"
#include "nodeObjs/noFlag.h"
#include "world/GameWorld.h"
#include "controls/ctrlOptionGroup.h"
#include "PadFixture.h"
#include "PadGameFixture.h"
#include <rttr/test/LocaleResetter.hpp>
#include <rttr/test/stubFunction.hpp>
#include <s25util/colors.h>
#include <s25util/warningSuppression.h>
#include <glad/glad.h>
#include "helpers/EnumRange.h"
#include "gameTypes/BuildingQuality.h"
#include "gameTypes/FlagType.h"
#include "gameTypes/RoadBuildMode.h"
#include "gameData/GameConsts.h"
#include "gameData/const_gui_ids.h"
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

using namespace rttr::test;

/// Damit Boost.Test einen Unterschied zweier Leisten AUSSCHREIBEN kann statt nur "false" zu
/// melden. Bewusst hier und nicht in PlayerBrief.h: der Produktivcode braucht keinen
/// Stromoperator, und <ostream> gehoert nicht in einen Kopf, der von fast allem eingelesen wird.
namespace brief {
inline std::ostream& operator<<(std::ostream& os, const KeyHint& hint)
{
    return os << PadButtonLabel(hint.button) << "=" << static_cast<int>(hint.action);
}
} // namespace brief

namespace {

/// Die Knopfbelegung dieses Befunds, an genau einer Stelle.
namespace padHint {
    constexpr PadButton OpenActions = PadButton::RightShoulder;
    constexpr PadButton Enter = PadButton::Y;
    constexpr PadButton Act = PadButton::A;
    constexpr PadButton Back = PadButton::B;
    constexpr PadButton SystemMenu = PadButton::Back;
    constexpr PadButton Flag = PadButton::X;
} // namespace padHint

/// Die Knopfnummern des Flaggenreiters (iwAction.cpp, Kopfkommentar).
constexpr unsigned kTabFlag = 4;
/// Der Anzeigereiter und sein erster Knopf, das Beobachtungsfenster (iwAction.cpp,
/// Msg_ButtonClick_TabWatch case 1).
constexpr unsigned kTabWatch = 3;
constexpr unsigned kWatchBtObserve = 1;
/// Der Knopf "Einstellungen" der Hauptauswahl (iwMainMenu, Kennung im Konstruktor sichtbar
/// vergeben) - derselbe Weg, den testPadSystemMenu.cpp schon faehrt.
constexpr unsigned kMainMenuOptionsButton = 30;
/// Der Musiklautstaerkeregler im Einstellungsfenster. iwOptionsWindow fuehrt seine Kennungen in
/// einer anonymen enum in der .cpp; von aussen sind sie nicht erreichbar, deshalb steht die Zahl
/// hier. Der Fall unten verankert sie DOPPELT: er prueft, dass dort wirklich ein Control mit
/// waagerechter Werteachse und OHNE Activate() sitzt. Verschiebt sich die enum, faellt das auf,
/// statt still das falsche Control zu messen.
constexpr unsigned kOptionsWndMusicVolume = 15;
constexpr unsigned kFlagBtRoad = 1;
constexpr unsigned kFlagBtWaterway = 2;
constexpr unsigned kFlagBtPullDown = 3;
constexpr unsigned kFlagBtGeologist = 4;
constexpr unsigned kFlagBtScout = 5;

/// Die Knoepfe, die der Flaggenreiter an einer GEWOEHNLICHEN Flagge traegt (iwAction.cpp,
/// FlagType::Normal). Kein Wasserweg - der steht nur an einer Wasserflagge.
brief::FlagMenuButtons plainFlagButtons()
{
    brief::FlagMenuButtons out;
    out.road = out.pullDown = out.geologist = out.scout = true;
    return out;
}

/// Die Knoepfe an einer WASSERFLAGGE: dieselben vier plus der Wasserweg (FlagType::WaterFlag).
brief::FlagMenuButtons waterFlagButtons()
{
    brief::FlagMenuButtons out = plainFlagButtons();
    out.waterway = true;
    return out;
}

/// Die Knoepfe an der HQ-Flagge: GENAU EINER (iwAction.cpp, FlagType::HQ).
brief::FlagMenuButtons hqFlagButtons()
{
    brief::FlagMenuButtons out;
    out.road = true;
    return out;
}

/// Was die Leiste zu DIESER Taste sagt - oder nullopt, wenn sie sie nicht nennt.
std::optional<brief::KeyAction> actionFor(const brief::Brief& b, const PadButton button)
{
    // `h.input == Button` gehoert dazu, seit die Leiste auch den linken STICK nennen kann
    // (Befund K2/4E): bei einem Stickhinweis traegt `button` keine Bedeutung, und ohne diese
    // Frage haelte ein Nachweis den Stick fuer PadButton::A.
    const auto it = std::find_if(b.keys.begin(), b.keys.end(), [button](const brief::KeyHint& h) {
        return h.input == brief::KeyInput::Button && h.button == button;
    });
    if(it == b.keys.end())
        return std::nullopt;
    return it->action;
}

/// Steht dieser Hinweis in der Leiste?
bool hasHint(const brief::Brief& b, const PadButton button, const brief::KeyAction action)
{
    return std::find(b.keys.begin(), b.keys.end(), brief::KeyHint{button, action}) != b.keys.end();
}

/// Ist diese Taste ueberhaupt genannt - egal mit welcher Wirkung?
bool namesButton(const brief::Brief& b, const PadButton button)
{
    return std::any_of(b.keys.begin(), b.keys.end(), [button](const brief::KeyHint& h) {
        return h.input == brief::KeyInput::Button && h.button == button;
    });
}

/// Die Leiste als Protokollzeile - fuer BOOST_TEST_MESSAGE, nicht fuer Zusicherungen.
std::string dumpKeys(const brief::Brief& b)
{
    std::string out;
    for(const brief::KeyHint& h : b.keys)
    {
        if(!out.empty())
            out += " | ";
        out += brief::KeyInputLabel(h);
        out += "=";
        out += std::to_string(static_cast<int>(h.action));
    }
    if(out.empty())
        return std::string("(leer)");
    // Zusaetzlich die Zeile, die wirklich unter der Ansicht steht - in der Sprache, die gerade
    // geladen ist. Sie ist AUSKUNFT und keine Zusicherung; gemessen wird oben ueber die Werte.
    return out + "   [" + brief::KeyLine(b.keys) + "]";
}

MapPoint hqFlagOf(const GameWorldBase& world, const unsigned char player)
{
    const MapPoint hqPos = world.GetPlayer(player).GetHQPos();
    const auto* hq = world.GetSpecObj<nobBaseWarehouse>(hqPos);
    BOOST_TEST_REQUIRE(hq != nullptr);
    return hq->GetFlagPos();
}

/// Ein Punkt im Gebiet dieses Spielers, auf dem eine GEWOEHNLICHE Flagge stehen kann - also
/// nicht die HQ-Flagge, deren Reiter nur einen einzigen Knopf traegt.
MapPoint findPlainFlagSpot(const GameWorld& world, const GameWorldViewer& viewer)
{
    const auto player = static_cast<unsigned char>(viewer.GetPlayerId());
    for(const MapPoint& pt : world.GetPointsInRadiusWithCenter(world.GetPlayer(player).GetHQPos(), 8))
    {
        if(!viewer.IsOwner(pt))
            continue;
        if(world.GetNO(pt)->GetType() != NodalObjectType::Nothing)
            continue;
        if(world.IsFlagAround(pt))
            continue;
        if(world.GetBQ(pt, player) < BuildingQuality::Flag)
            continue;
        if(world.GetNO(world.GetNeighbour(pt, Direction::NorthWest))->GetType() != NodalObjectType::Nothing)
            continue;
        return pt;
    }
    return MapPoint::Invalid();
}

/// Ein eigener, freier Knoten mit mindestens dieser Bauqualitaet.
MapPoint findBuildSpot(const GameWorldBase& world, const GameWorldViewer& viewer, const BuildingQuality minBQ)
{
    RTTR_FOREACH_PT(MapPoint, world.GetSize())
    {
        if(!viewer.IsOwner(pt))
            continue;
        if(viewer.GetBQ(pt) < minBQ)
            continue;
        if(world.GetNO(pt)->GetType() != NodalObjectType::Nothing)
            continue;
        return pt;
    }
    return MapPoint::Invalid();
}

/// Der Knopf `btId` im Flaggenreiter des Aktionsfensters dieser Ansicht - oder nullptr.
ctrlButton* flagTabButton(PlayerView& view, const unsigned btId)
{
    iwAction* const wnd = view.actionwindow;
    if(!wnd)
        return nullptr;
    auto* mainTab = wnd->GetCtrl<ctrlTab>(0);
    if(!mainTab)
        return nullptr;
    ctrlGroup* group = mainTab->GetGroup(kTabFlag);
    if(!group)
        return nullptr;
    return group->GetCtrl<ctrlButton>(btId);
}

/// Das Aktionsfenster dieser Ansicht sauber wegraeumen - Testaufbau, nicht Pruefgegenstand.
template<class T_Fixture>
void closeActionWindow(T_Fixture& f, PlayerView& view)
{
    // PHASE 13: das Aktionsfenster ist der Ring. Ihn hier mit zu schliessen ist keine Kosmetik -
    // ein stehengebliebener Ringzustand verschluckt jede weitere Weltflanke dieses Sitzplatzes,
    // und der naechste Abschnitt des Falls maesse dann etwas voellig anderes als er glaubt.
    if(view.GetRing().IsOpen())
        f.dsk->CloseRing(view, /*closeWindow*/ true);
    if(iwAction* const wnd = view.actionwindow)
    {
        if(!wnd->ShouldBeClosed())
            wnd->Close();
        f.dsk->Msg_WindowClosed(*wnd);
    }
    WINDOWMANAGER.Draw();
}

/// PadViewFixture mit dem Zugriff, den die Hilfen oben brauchen.
template<unsigned T_numViews, unsigned T_numPlayers = T_numViews>
struct HintFixture : PadViewFixture<T_numViews, T_numPlayers>
{
    /// Das Einstellungsfenster schreibt beim Schieben in SETTINGS.sound. Der Fall darf keinem
    /// anderen die Lautstaerke verstellen.
    decltype(SETTINGS.sound) savedSound_ = SETTINGS.sound;

    /// JEDES offene Fenster sofort freigeben, solange Welt und Desktop noch leben - dieselbe
    /// Begruendung wie in testPadSystemMenu.cpp: ein hier vergessenes Fenster stuerzt sonst in
    /// einem voellig anderen Testfall ab.
    ~HintFixture()
    {
        for(unsigned i = 0; i < 64u; ++i)
        {
            IngameWindow* wnd = WINDOWMANAGER.GetTopMostWindow();
            if(!wnd)
                break;
            WINDOWMANAGER.CloseNow(wnd);
        }
        WINDOWMANAGER.Draw();
        SETTINGS.sound = savedSound_;
    }

    PlayerView& playerViewOf(unsigned idx) { return this->view(idx); }

    /// Pad `dev` anstecken und Ansicht `viewIdx` zuordnen - der Vorspann jedes Falles.
    void takePad(const PadDeviceId dev, const unsigned viewIdx)
    {
        this->pads.connect(dev);
        this->step(16);
        BOOST_TEST_REQUIRE(this->dsk->GetPadRouter().AssignSlot(dev, viewIdx));
        this->step(16);
    }

    /// Fokus dieser Ansicht auf GENAU DIESES Control fahren - ausschliesslich mit Padereignissen
    /// (RB = FocusPath::Move(Dir::Next)).
    ///
    /// Ueber den ZEIGER und nicht ueber die Kennung: in einem Reiterfenster tragen der Reiterkopf
    /// und ein Knopf der Reitergruppe dieselbe Kennung, und eine Suche ueber die Kennung traefe
    /// den falschen.
    void focusToCtrl(const PadDeviceId dev, const unsigned viewIdx, const Window* const target)
    {
        BOOST_TEST_REQUIRE(target != static_cast<const Window*>(nullptr));
        // PHASE 13: im KREISMENUE wandert der Fokus mit dem Steuerkreuz (ein Sektor weiter),
        // waehrend die Schultern die SEITE wechseln; in einem gewoehnlichen Fenster ist es
        // umgekehrt. Gefragt wird der Ringzustand selbst - derselbe Wert, den auch
        // dskGameInterface::OnPadButton liest.
        for(unsigned i = 0; i < 40u; ++i)
        {
            if(this->view(viewIdx).GetFocus().GetFocused() == target)
                return;
            this->press(dev, this->view(viewIdx).GetRing().IsOpen() ? PadButton::DpadRight :
                                                                      PadButton::RightShoulder);
        }
        BOOST_FAIL("Das Control ist per Pad nicht erreichbar");
    }

    /// DEN RING BLAETTERN, bis das gesuchte Control ein Sektor ist - und dann darauf drehen.
    ///
    /// Der Ring zeigt nur eine Seite auf einmal, und die Reiter des Fensters SIND die weiteren
    /// Seiten (RingTurnPage). Ein Control, das auf einem anderen Reiter liegt, ist also nicht
    /// durch Drehen erreichbar, sondern durch Blaettern - woertlich die Tiefe, die
    /// CONTROLLER-UX.md Regel 3 vorschreibt (kein verschachtelter Ring, Tiefe durch LB/RB).
    /// Beides zusammen ist der volle produktive Weg zu jedem Knopf eines Ringfensters.
    void ringPageToCtrl(const PadDeviceId dev, const unsigned viewIdx, const Window* const target)
    {
        BOOST_TEST_REQUIRE(target != static_cast<const Window*>(nullptr));
        BOOST_TEST_REQUIRE(this->view(viewIdx).GetRing().IsOpen());
        for(unsigned page = 0; page < 40u; ++page)
        {
            unsigned numPages = 1;
            const std::vector<Window*> ctrls = dskGameInterface::RingPageCtrls(this->view(viewIdx), numPages);
            if(std::find(ctrls.begin(), ctrls.end(), target) != ctrls.end())
            {
                focusToCtrl(dev, viewIdx, target);
                return;
            }
            this->press(dev, PadButton::RightShoulder);
        }
        BOOST_FAIL("Das Control ist auch durch Blaettern nicht erreichbar");
    }

    /// Ein GEWOEHNLICHES Fenster dieses Sitzplatzes, geoeffnet ueber den vollen produktiven Weg
    /// und danach OHNE Fokus - der Zustand "ein Fenster steht offen, der Spieler ist in der
    /// Welt".
    ///
    /// PHASE 13: dafuer taugt das Aktionsfenster nicht mehr. Es ist jetzt ein Ring, wird sofort
    /// betreten und ist fuer seinen Sitzplatz modal. Das Postfenster ist ein gewoehnliches
    /// Fenster geblieben und traegt diese Faelle unveraendert.
    IngameWindow* openPlainWindowByPad(const PadDeviceId dev, const unsigned viewIdx)
    {
        this->press(dev, PadButton::Back);
        IngameWindow* const menu = WINDOWMANAGER.FindNonModalWindow(CGI_PADMENU, viewIdx);
        BOOST_TEST_REQUIRE(menu != static_cast<IngameWindow*>(nullptr));
        focusToCtrl(dev, viewIdx, menu->GetCtrl<Window>(iwPadSystemMenu::ID_POST));
        this->press(dev, PadButton::A);
        IngameWindow* const wnd = WINDOWMANAGER.FindNonModalWindow(CGI_POSTOFFICE, viewIdx);
        BOOST_TEST_REQUIRE(wnd != static_cast<IngameWindow*>(nullptr));
        BOOST_TEST_REQUIRE(!this->view(viewIdx).GetRing().IsOpen());
        // B in einem gewoehnlichen Fenster gibt nur den Fokus ab - die Staffelung, die es dort
        // weiterhin gibt.
        this->press(dev, PadButton::B);
        BOOST_TEST_REQUIRE(!this->view(viewIdx).GetFocus().IsActive());
        BOOST_TEST_REQUIRE(!wnd->ShouldBeClosed());
        return wnd;
    }

    /// Der volle Padweg zum Musiklautstaerkeregler: Back, Hauptauswahl, Einstellungen, Y hinein.
    /// Liefert das Einstellungsfenster.
    IngameWindow* padWayToTheOptionsSlider(const PadDeviceId dev, const unsigned viewIdx)
    {
        this->press(dev, PadButton::Back);
        IngameWindow* const menu = WINDOWMANAGER.FindNonModalWindow(CGI_PADMENU, viewIdx);
        BOOST_TEST_REQUIRE(menu != static_cast<IngameWindow*>(nullptr));
        BOOST_TEST_REQUIRE(this->view(viewIdx).GetFocus().GetRoot() == static_cast<Window*>(menu));
        focusToCtrl(dev, viewIdx, menu->GetCtrl<Window>(iwPadSystemMenu::ID_MAIN_SELECTION));
        this->press(dev, PadButton::A);

        IngameWindow* const mainSel = WINDOWMANAGER.FindNonModalWindow(CGI_MAINSELECTION, viewIdx);
        BOOST_TEST_REQUIRE(mainSel != static_cast<IngameWindow*>(nullptr));
        WINDOWMANAGER.Draw();
        BOOST_TEST_REQUIRE(this->view(viewIdx).GetFocus().GetRoot() == static_cast<Window*>(mainSel));
        focusToCtrl(dev, viewIdx, mainSel->GetCtrl<Window>(kMainMenuOptionsButton));
        this->press(dev, PadButton::A);

        IngameWindow* const opts = WINDOWMANAGER.FindNonModalWindow(CGI_OPTIONSWINDOW, viewIdx);
        BOOST_TEST_REQUIRE(opts != static_cast<IngameWindow*>(nullptr));
        return opts;
    }
};

/// Ein Fenster ohne eine einzige Fokusstation.
///
/// Gebraucht fuer Befund B2: FocusPath::SetRoot scheitert an so einem Fenster, Y laesst den
/// Fokus also untaetig - und die Leiste nannte Y trotzdem.
struct WndWithoutControls : IngameWindow
{
    explicit WndWithoutControls(const DrawPoint& pos)
        : IngameWindow(CGI_MISSION_STATEMENT, pos, Extent(160, 100), "", nullptr, false, CloseBehavior::Regular)
    {}
};

/// WIE VIELE ZEICHEN steht in dieser Zeile - in Codepunkten und nicht in Bytes.
///
/// glFont::Draw dekodiert UTF-8 und legt fuer JEDEN Codepunkt vier Eckpunkte an (DrawChar).
/// Genau diese Zahl kommt unten bei glDrawArrays an; ein Vergleich ueber std::string::size()
/// waere in jeder Sprache mit Umlauten falsch.
int numCodepoints(const std::string& text)
{
    int n = 0;
    for(const char c : text)
    {
        if((static_cast<unsigned char>(c) & 0xC0u) != 0x80u)
            ++n;
    }
    return n;
}

/// DER MITSCHRIEB DES AUSGEBERS - Befund P2.
///
/// glFont::Draw endet fuer jede gezeichnete Zeile mit genau zwei Aufrufen, die hier
/// interessieren: glColor4ub setzt die Farbe der Zeile, glDrawArrays gibt ihre Eckpunkte aus
/// (vier je Zeichen). Der DummyRenderer verwirft das Bild, aber die Aufrufe laufen wirklich -
/// und damit ist messbar, WELCHE Zeilen den Weg bis nach draussen gefunden haben.
///
/// Freie Funktionen mit globalem Zustand, weil ein OpenGL-Funktionszeiger keine Fangliste
/// tragen kann. Eingehaengt wird mit RTTR_STUB_FUNCTION, das den alten Zeiger beim Verlassen
/// des Blocks zurueckschreibt.
namespace briefEmitTap {
    RTTR_IGNORE_DIAGNOSTIC("-Wmissing-declarations")

    struct Emitted
    {
        unsigned color;
        int glyphs;
    };
    std::vector<Emitted> emitted;
    unsigned curColor = 0;

    void reset()
    {
        emitted.clear();
        curColor = 0;
    }

    void APIENTRY glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a)
    {
        curColor = MakeColor(a, r, g, b);
    }

    void APIENTRY glDrawArrays(GLenum, GLint, GLsizei count)
    {
        emitted.push_back(Emitted{curColor, static_cast<int>(count) / 4});
    }

    RTTR_POP_DIAGNOSTIC
} // namespace briefEmitTap

/// Ein besitzerloses Fenster mit einem bedienbaren Knopf - eine Nachrichtenbox, wie sie der
/// Mausspieler ausserhalb jeder Besitzklammer oeffnet.
struct SharedWnd : IngameWindow
{
    explicit SharedWnd(const DrawPoint& pos)
        : IngameWindow(CGI_MSGBOX, pos, Extent(200, 120), "", nullptr, false, CloseBehavior::Regular)
    {
        AddTextButton(1, DrawPoint(10, 10), Extent(80, 20), TextureColor::Green1, "OK", NormalFont);
        SetOwner(SHARED_WINDOW_OWNER);
    }
};

} // namespace

BOOST_AUTO_TEST_SUITE(PadKeyHintTests)

// ============================================================================================
// 1. DIE LEISTE SAGT DIE WAHRHEIT - jeder genannte Knopf wird gedrueckt und gemessen
// ============================================================================================

/// Auf einer eigenen Flagge nennt die Leiste A (Strasse), RB (Aktionen) und Back (Menue) - und
/// jeder dieser drei Knoepfe tut danach genau das.
///
/// DAS IST DER KERN DES ABNAHMEKRITERIUMS: nicht "die Leiste enthaelt einen RB-Eintrag", sondern
/// "der RB-Eintrag stimmt". Sabotiert man den RB-Zweig in OnPadButton, wird dieser Fall rot -
/// die Gegenprobe steht im Bericht.
BOOST_FIXTURE_TEST_CASE(OnAnOwnFlagEveryHintedButtonReallyDoesWhatTheHintSays, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint flagPt = findPlainFlagSpot(world, view(1).GetViewer());
    BOOST_TEST_REQUIRE(flagPt.isValid());
    world.SetFlag(flagPt, 1); // Testaufbau
    BOOST_TEST_REQUIRE((world.GetSpecObj<noFlag>(flagPt)->GetFlagType() == FlagType::Normal));

    pads.connect(11);
    step(16);
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(11, 1));
    step(16);
    padSteerTo(11, 1, flagPt);

    const brief::Brief& b = view(1).GetBrief();
    BOOST_TEST_MESSAGE("AUDIT: Leiste auf eigener Flagge = " << dumpKeys(b));
    BOOST_TEST_REQUIRE(!b.keys.empty());
    BOOST_TEST(hasHint(b, PadButton::A, brief::KeyAction::StartRoad));
    BOOST_TEST(hasHint(b, PadButton::RightShoulder, brief::KeyAction::OpenActionMenu));
    BOOST_TEST(hasHint(b, PadButton::Back, brief::KeyAction::SystemMenu));
    // Was NICHT dasteht, darf auch nicht dastehen: auf einem Knoten mit Flagge passt keine
    // zweite (ComputeActionOptions::tabs.setflag ist dort falsch), und der Wasserweg beginnt
    // nur an einer Wasserflagge.
    BOOST_TEST(!namesButton(b, PadButton::X));
    BOOST_TEST(!namesButton(b, PadButton::LeftShoulder));
    // Kein Fenster offen -> Y und B versprechen nichts.
    BOOST_TEST(!namesButton(b, PadButton::Y));

    // (1) Der RB-Eintrag: das Aktionsfenster geht auf.
    press(11, padHint::OpenActions);
    BOOST_TEST_REQUIRE(view(1).actionwindow != static_cast<iwAction*>(nullptr));
    BOOST_TEST((view(1).GetRoad().mode == RoadBuildMode::Disabled));
    closeActionWindow(*this, view(1));
    step(16);

    // (2) Der A-Eintrag: der Strassenbau faengt an.
    press(11, padHint::Act);
    BOOST_TEST((view(1).GetRoad().mode == RoadBuildMode::Normal));
    BOOST_TEST((view(1).GetRoad().start == flagPt));
    press(11, padHint::Back); // leere Strecke -> Abbruch
    BOOST_TEST_REQUIRE((view(1).GetRoad().mode == RoadBuildMode::Disabled));
    step(16);

    // (3) Der Back-Eintrag: das Systemmenue geht auf.
    press(11, padHint::SystemMenu);
    BOOST_TEST(WINDOWMANAGER.FindNonModalWindow(CGI_PADMENU, 1u) != static_cast<IngameWindow*>(nullptr));
    press(11, padHint::SystemMenu);
    WINDOWMANAGER.Draw();
    BOOST_TEST(WINDOWMANAGER.FindNonModalWindow(CGI_PADMENU, 1u) == static_cast<IngameWindow*>(nullptr));
}

/// Der A-Eintrag ist eine KASKADE, und die Leiste muss sagen, welcher Zweig greift. Gemessen auf
/// drei Knoten: eigenes Gebaeude (Fenster), eigene Flagge (Strasse), freier Bauplatz (Menue).
BOOST_FIXTURE_TEST_CASE(TheOpenHintAppearsExactlyWhereAReallyOpensAWindow, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint hqPos = world.GetPlayer(1).GetHQPos();
    const MapPoint buildPt = findBuildSpot(world, view(1).GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(buildPt.isValid());

    pads.connect(11);
    step(16);
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(11, 1));
    step(16);

    // (a) Auf dem HQ: A oeffnet ein Fenster, und die Leiste sagt es.
    padSteerTo(11, 1, hqPos);
    BOOST_TEST_MESSAGE("AUDIT: Leiste auf dem eigenen HQ = " << dumpKeys(view(1).GetBrief()));
    BOOST_TEST_REQUIRE(hasHint(view(1).GetBrief(), PadButton::A, brief::KeyAction::OpenWindow));
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetTopMostWindow(1u) == static_cast<IngameWindow*>(nullptr));
    press(11, padHint::Act);
    BOOST_TEST(WINDOWMANAGER.GetTopMostWindow(1u) != static_cast<IngameWindow*>(nullptr));
    // Und es ist NICHT das Aktionsfenster - A hat den ersten Zweig der Kaskade genommen.
    BOOST_TEST(view(1).actionwindow == static_cast<iwAction*>(nullptr));
    // Wieder zu, sonst faelscht das offene Fenster die naechste Messung.
    press(11, padHint::Back);
    WINDOWMANAGER.Draw();
    step(16);

    // (b) Auf freiem eigenem Bauland: A oeffnet das Aktionsfenster.
    padSteerTo(11, 1, buildPt);
    BOOST_TEST_MESSAGE("AUDIT: Leiste auf freiem Bauland = " << dumpKeys(view(1).GetBrief()));
    BOOST_TEST_REQUIRE(hasHint(view(1).GetBrief(), PadButton::A, brief::KeyAction::OpenActionMenu));
    // Und RB steht dort NICHT nochmal - es taete dasselbe wie A.
    BOOST_TEST(!namesButton(view(1).GetBrief(), PadButton::RightShoulder));
    press(11, padHint::Act);
    BOOST_TEST(view(1).actionwindow != static_cast<iwAction*>(nullptr));
    closeActionWindow(*this, view(1));
}

/// Der Hinweis "X Flagge" steht NUR dort, wo wirklich eine Flagge entsteht - gemessen in einer
/// LAUFENDEN Partie, also nach einem echten Netzwerkumlauf und der Ausfuehrung in der
/// Simulation. Ohne laufende Partie waere das nur der Vergleich zweier Rechnungen.
BOOST_FIXTURE_TEST_CASE(TheFlagHintOnlyStandsWhereAFlagReallyAppears, PadGameFixture)
{
    setUpTwoLocalPlayers();
    const MapPoint flagPt = findExclusiveFlagSpot(world(), 1, 0);
    BOOST_TEST_REQUIRE(flagPt.isValid());

    // Reihenfolge zaehlt: der Router vergibt die Sitzplaetze in der Reihenfolge der Aufnahme.
    aimPadAt(10, 0, world().GetPlayer(0).GetHQPos());
    aimPadAt(11, 1, flagPt);
    BOOST_TEST_MESSAGE("AUDIT: Leiste auf einem freien Flaggenplatz = "
                       << dumpKeys(dsk->GetPlayerView(1).GetBrief()));
    BOOST_TEST_REQUIRE(hasHint(dsk->GetPlayerView(1).GetBrief(), PadButton::X, brief::KeyAction::PlaceFlag));

    press(11, padHint::Flag);
    pumpUntilGF(GAMECLIENT.GetGFNumber() + 15);
    BOOST_TEST_REQUIRE((world().GetNO(flagPt)->GetType() == NodalObjectType::Flag));
    BOOST_TEST(world().GetSpecObj<noFlag>(flagPt)->GetPlayer() == 1);

    // Und die Kehrseite: auf DERSELBEN Stelle passt jetzt keine zweite Flagge mehr, und die
    // Leiste bietet X folgerichtig nicht mehr an.
    step(16);
    BOOST_TEST_MESSAGE("AUDIT: dieselbe Stelle mit Flagge = " << dumpKeys(dsk->GetPlayerView(1).GetBrief()));
    BOOST_TEST(!namesButton(dsk->GetPlayerView(1).GetBrief(), PadButton::X));

    tearDownDesktop();
}

// ============================================================================================
// 2. DER ZUSTAND, IN DEM DER AUFTRAGGEBER STECKENGEBLIEBEN IST
// ============================================================================================

/// PHASE 13: das Aktionsfenster IST der Ring, und der Ring wird sofort betreten - Y hat hier
/// nichts mehr zu tun.
///
/// Was der Fall vorher mass: "Fenster offen, Fokus noch in der Welt, der einzige Weg hinein ist
/// Y, und das stand nirgends". Diesen Zustand gibt es nicht mehr; ihn weiter zu pruefen hiesse,
/// einen Weg zu sichern, den kein Spieler mehr geht. Der MASSSTAB bleibt aber woertlich
/// derselbe und ist genau das, was hier steht: die Leiste sagt, was IM RING gilt - nicht, was
/// im Fenster galt. Eine Leiste, die hier "Y Ins Fenster" oder "RB Weiter" verspraeche, waere
/// die Sorte Luege, die Phase 12 dreimal ausbauen musste.
BOOST_FIXTURE_TEST_CASE(WithTheRingOpenTheBarNamesTheRingKeysAndNotTheWindowKeys, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint flagPt = findPlainFlagSpot(world, view(1).GetViewer());
    BOOST_TEST_REQUIRE(flagPt.isValid());
    world.SetFlag(flagPt, 1);

    pads.connect(11);
    step(16);
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(11, 1));
    step(16);
    padSteerTo(11, 1, flagPt);

    // Vorher: kein Fenster, also auch kein Y-Eintrag.
    BOOST_TEST_REQUIRE(!namesButton(view(1).GetBrief(), PadButton::Y));

    press(11, padHint::OpenActions);
    BOOST_TEST_REQUIRE(view(1).actionwindow != static_cast<iwAction*>(nullptr));
    // Der Ring ist offen UND betreten - in EINEM Druck.
    BOOST_TEST_REQUIRE(view(1).GetRing().IsOpen());
    BOOST_TEST_REQUIRE(view(1).GetFocus().IsActive());
    BOOST_TEST(view(1).GetFocus().GetRoot() == static_cast<Window*>(view(1).actionwindow));

    const brief::Brief& b = view(1).GetBrief();
    BOOST_TEST_MESSAGE("AUDIT: Leiste mit offenem Ring = " << dumpKeys(b));
    // Was der Ring wirklich kann: A loest aus, das Steuerkreuz dreht, B schliesst.
    BOOST_TEST(hasHint(b, PadButton::A, brief::KeyAction::Choose));
    BOOST_TEST(hasHint(b, PadButton::DpadLeft, brief::KeyAction::TurnRing));
    BOOST_TEST(hasHint(b, PadButton::DpadRight, brief::KeyAction::TurnRing));
    BOOST_TEST(hasHint(b, PadButton::B, brief::KeyAction::CloseRing));
    // Und was er NICHT kann, steht auch nicht da.
    BOOST_TEST(!namesButton(b, PadButton::Y));
    BOOST_TEST(!hasHint(b, PadButton::RightShoulder, brief::KeyAction::NextControl));
    BOOST_TEST(!hasHint(b, PadButton::B, brief::KeyAction::LeaveFocus));

    // GEDRUECKT UND GEMESSEN: das Steuerkreuz dreht den Ring wirklich.
    const Window* const before = view(1).GetFocus().GetFocused();
    press(11, PadButton::DpadRight);
    BOOST_TEST(view(1).GetFocus().GetFocused() != before);
    // ... und B schliesst wirklich alles.
    press(11, padHint::Back);
    BOOST_TEST(!view(1).GetRing().IsOpen());
    BOOST_TEST(!view(1).GetFocus().IsActive());

    closeActionWindow(*this, view(1));
}

/// Im Fenster wechselt die Leiste: A waehlt, RB geht weiter, B gibt den Fokus ab. Alle drei
/// gedrueckt und gemessen.
///
/// PHASE 13: gemessen wird das an einem GEWOEHNLICHEN Fenster (dem Postfenster), denn das
/// Aktionsfenster ist jetzt ein Ring und hat eine eigene Belegung - der Fall darueber misst
/// die. Die Fensterbelegung gilt unveraendert weiter, sie hat nur einen anderen Traeger.
BOOST_FIXTURE_TEST_CASE(InsideAWindowTheHintsFollowTheFocusAndNotTheWorld, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint flagPt = findPlainFlagSpot(world, view(1).GetViewer());
    BOOST_TEST_REQUIRE(flagPt.isValid());
    world.SetFlag(flagPt, 1);

    pads.connect(11);
    step(16);
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(11, 1));
    step(16);
    padSteerTo(11, 1, flagPt);
    // Der volle produktive Weg zu einem gewoehnlichen Fenster: Back oeffnet den System-Ring,
    // sein Postsektor oeffnet das Postfenster, und dort steht der Fokus.
    press(11, PadButton::Back);
    BOOST_TEST_REQUIRE(view(1).GetRing().IsOpen());
    focusToCtrl(11, 1, WINDOWMANAGER.FindNonModalWindow(CGI_PADMENU, 1)
                         ->GetCtrl<Window>(iwPadSystemMenu::ID_POST));
    press(11, PadButton::A);
    IngameWindow* const postWnd = WINDOWMANAGER.FindNonModalWindow(CGI_POSTOFFICE, 1);
    BOOST_TEST_REQUIRE(postWnd != static_cast<IngameWindow*>(nullptr));
    BOOST_TEST_REQUIRE(!view(1).GetRing().IsOpen());
    BOOST_TEST_REQUIRE(view(1).GetFocus().IsActive());

    const brief::Brief& b = view(1).GetBrief();
    BOOST_TEST_MESSAGE("AUDIT: Leiste im Fenster = " << dumpKeys(b));
    BOOST_TEST(hasHint(b, PadButton::A, brief::KeyAction::Choose));
    BOOST_TEST(hasHint(b, PadButton::RightShoulder, brief::KeyAction::NextControl));
    BOOST_TEST(hasHint(b, PadButton::B, brief::KeyAction::LeaveFocus));
    // Die Weltzeilen sind weg - im Fenster sieht die Welt keine Flanke.
    BOOST_TEST(!hasHint(b, PadButton::A, brief::KeyAction::StartRoad));

    // RB geht wirklich eine Station weiter.
    const Window* const before = view(1).GetFocus().GetFocused();
    press(11, PadButton::RightShoulder);
    BOOST_TEST(view(1).GetFocus().GetFocused() != before);

    // B gibt wirklich den Fokus ab.
    press(11, padHint::Back);
    BOOST_TEST(!view(1).GetFocus().IsActive());

    if(!postWnd->ShouldBeClosed())
        postWnd->Close();
    WINDOWMANAGER.Draw();
}

// ============================================================================================
// 3. DER STRASSENBAU - der Modus, in dem dieselben Tasten etwas anderes tun
// ============================================================================================

/// Auf leerer Strecke verspricht die Leiste kein X (PadCommitRoad lehnt unter zwei Kanten mit
/// RoadTooShort ab) und nennt B als ABBRUCH. Beides gedrueckt.
BOOST_FIXTURE_TEST_CASE(InRoadModeTheHintsChangeWithTheRouteAndNotWithTheNode, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint flagPt = hqFlagOf(world, 1);

    pads.connect(11);
    step(16);
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(11, 1));
    step(16);
    padSteerTo(11, 1, flagPt);
    press(11, padRoad::Begin);
    BOOST_TEST_REQUIRE((view(1).GetRoad().mode == RoadBuildMode::Normal));
    BOOST_TEST_REQUIRE(view(1).GetRoad().route.empty());

    {
        const brief::Brief& b = view(1).GetBrief();
        BOOST_TEST_MESSAGE("AUDIT: Leiste im Baumodus, leere Strecke = " << dumpKeys(b));
        // BEFUND P1: der Zeiger steht noch auf der Startflagge, also AUF DEM WEGENDE. A tut
        // dort nichts - kein Schritt, keine Meldung -, und deshalb steht A hier NICHT mehr.
        BOOST_TEST(!namesButton(b, PadButton::A));
        BOOST_TEST(hasHint(b, PadButton::B, brief::KeyAction::CancelRoad));
        // X waere hier wirkungslos - also steht es nicht da.
        BOOST_TEST(!namesButton(b, PadButton::X));
        // Und Back auch nicht: im Baumodus ist der Modus die Bedeutung.
        BOOST_TEST(!namesButton(b, PadButton::Back));
    }

    // Zwei Stuecke legen - ueber den echten Weg.
    const MapPoint step1 = world.GetNeighbour(flagPt, Direction::East);
    const MapPoint step2 = world.GetNeighbour(step1, Direction::East);
    padSteerTo(11, 1, step1);
    // Erst JETZT - der Zeiger steht neben dem Wegende - verspricht die Leiste das Verlaengern.
    BOOST_TEST(hasHint(view(1).GetBrief(), PadButton::A, brief::KeyAction::ExtendRoad));
    press(11, padRoad::Extend);
    padSteerTo(11, 1, step2);
    press(11, padRoad::Extend);
    BOOST_TEST_REQUIRE(view(1).GetRoad().route.size() >= 2u);

    {
        const brief::Brief& b = view(1).GetBrief();
        BOOST_TEST_MESSAGE("AUDIT: Leiste im Baumodus, zwei Stuecke = " << dumpKeys(b));
        BOOST_TEST(hasHint(b, PadButton::B, brief::KeyAction::StepBackRoad));
        BOOST_TEST(hasHint(b, PadButton::X, brief::KeyAction::CommitRoad));
    }

    // B nimmt wirklich ein Stueck zurueck statt abzubrechen.
    const auto lenBefore = view(1).GetRoad().route.size();
    press(11, padRoad::StepBack);
    BOOST_TEST((view(1).GetRoad().mode == RoadBuildMode::Normal));
    BOOST_TEST(view(1).GetRoad().route.size() == lenBefore - 1u);
}

// ============================================================================================
// 4. DER KLARTEXT ZUM GEOLOGEN - der Befund selbst
// ============================================================================================

/// Der ganze Weg vom Zeiger bis auf den Knopf "Geologen rufen", ausschliesslich mit
/// Padereignissen - und am Ende steht unter der Ansicht in ganzen Saetzen, was der Mann tut.
///
/// Angesteuert wird ueber die KENNUNG des Knopfes (kFlagBtGeologist), nie ueber seinen Text.
BOOST_FIXTURE_TEST_CASE(TheGeologistButtonExplainsItselfInPlainText, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint flagPt = findPlainFlagSpot(world, view(1).GetViewer());
    BOOST_TEST_REQUIRE(flagPt.isValid());
    world.SetFlag(flagPt, 1);

    pads.connect(11);
    step(16);
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(11, 1));
    step(16);
    padSteerTo(11, 1, flagPt);

    press(11, padHint::OpenActions);
    BOOST_TEST_REQUIRE(view(1).actionwindow != static_cast<iwAction*>(nullptr));
    ctrlButton* const geologist = flagTabButton(view(1), kFlagBtGeologist);
    BOOST_TEST_REQUIRE(geologist != static_cast<ctrlButton*>(nullptr));

    // PHASE 13: der Ring ist offen und betreten - kein Y noetig, und der Fokus steht auf einem
    // SEKTOR und nicht auf einem Reiterkopf. Der Kopftext des Flaggenmenues, den Phase 12
    // gebaut hat, faellt damit auf dem Ringweg weg: er zaehlte auf, welche Knoepfe der Reiter
    // hat - und genau die stehen jetzt gleichzeitig als Sektoren im Bild. Die Rechnung selbst
    // (brief::ForFlagMenu) bleibt und wird weiterhin gemessen; siehe Abschnitt 22.
    BOOST_TEST_REQUIRE(view(1).GetRing().IsOpen());
    BOOST_TEST_REQUIRE(view(1).GetFocus().IsActive());

    for(unsigned presses = 0; presses < 32u && view(1).GetFocus().GetFocused() != geologist; ++presses)
        press(11, PadButton::DpadRight);
    BOOST_TEST_REQUIRE(view(1).GetFocus().GetFocused() == static_cast<Window*>(geologist));

    const brief::Brief& b = view(1).GetBrief();
    BOOST_TEST_MESSAGE("AUDIT: Klartext auf 'Geologen rufen' = " << b.joined());
    const brief::Brief expected = brief::ForAction(brief::ActionBrief::CallGeologist);
    BOOST_TEST(b.title == expected.title);
    BOOST_TEST(b.lines == expected.lines, boost::test_tools::per_element());
    // DREI Saetze und nicht ein Wort: das ist der Unterschied zum Tooltip, den der Padspieler
    // vorher als einzige Auskunft bekam.
    BOOST_TEST(b.lines.size() >= 3u);
    BOOST_TEST(b.joined().size() > geologist->GetTooltip().size() * 5u);
    // Und die Leiste steht auch hier darunter.
    BOOST_TEST(hasHint(b, PadButton::A, brief::KeyAction::Choose));

    // Der Nachbarknopf sagt etwas ANDERES - sonst waere der Text eine Attrappe.
    ctrlButton* const scout = flagTabButton(view(1), kFlagBtScout);
    BOOST_TEST_REQUIRE(scout != static_cast<ctrlButton*>(nullptr));
    for(unsigned presses = 0; presses < 32u && view(1).GetFocus().GetFocused() != scout; ++presses)
        press(11, PadButton::DpadRight);
    BOOST_TEST_REQUIRE(view(1).GetFocus().GetFocused() == static_cast<Window*>(scout));
    BOOST_TEST(view(1).GetBrief().title == brief::ForAction(brief::ActionBrief::SendScout).title);
    BOOST_TEST(view(1).GetBrief().title != expected.title);

    closeActionWindow(*this, view(1));
}

/// Jeder Knopf des Flaggenreiters traegt einen EIGENEN Klartext - keiner faellt auf den
/// Tooltip zurueck und keine zwei sagen dasselbe.
BOOST_FIXTURE_TEST_CASE(EveryButtonOfTheFlagTabCarriesItsOwnSentences, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint flagPt = findPlainFlagSpot(world, view(1).GetViewer());
    BOOST_TEST_REQUIRE(flagPt.isValid());
    world.SetFlag(flagPt, 1);

    pads.connect(11);
    step(16);
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(11, 1));
    step(16);
    padSteerTo(11, 1, flagPt);
    press(11, padHint::OpenActions);
    BOOST_TEST_REQUIRE(view(1).actionwindow != static_cast<iwAction*>(nullptr));
    press(11, padHint::Enter);

    std::vector<std::string> seen;
    for(const unsigned btId : {kFlagBtRoad, kFlagBtPullDown, kFlagBtGeologist, kFlagBtScout})
    {
        ctrlButton* const bt = flagTabButton(view(1), btId);
        BOOST_TEST_REQUIRE(bt != static_cast<ctrlButton*>(nullptr));
        // Der Fokus dreht sich ueber das Steuerkreuz dorthin - der echte Weg IM RING.
        for(unsigned i = 0; i < 32u && view(1).GetFocus().GetFocused() != bt; ++i)
            press(11, PadButton::DpadRight);
        BOOST_TEST_REQUIRE(view(1).GetFocus().GetFocused() == static_cast<Window*>(bt));
        const brief::Brief& b = view(1).GetBrief();
        BOOST_TEST_CONTEXT("Knopf " << btId)
        {
            BOOST_TEST(!b.title.empty());
            // Mindestens ein ganzer Satz - der Tooltip allein liefert nur einen Titel.
            BOOST_TEST(!b.lines.empty());
            const bool isNewTitle = std::find(seen.begin(), seen.end(), b.title) == seen.end();
            BOOST_TEST(isNewTitle);
        }
        BOOST_TEST_MESSAGE("AUDIT: Knopf " << btId << " = " << b.joined());
        seen.push_back(b.title);
    }

    closeActionWindow(*this, view(1));
}

// ============================================================================================
// 5. DIE HQ-FLAGGE - der Hinweis, der bisher gelogen hat
// ============================================================================================

/// GEMESSEN IN DER VORBEREITUNG: an der HQ-Flagge enthaelt der Flaggenreiter GENAU EINEN Knopf.
/// Der Klartext versprach dort trotzdem "abreissen, Geologen rufen, Spaeher aussenden" - an der
/// einzigen Flagge, die ein Anfaenger zu Spielbeginn besitzt.
BOOST_FIXTURE_TEST_CASE(TheHeadquartersFlagNoLongerPromisesAGeologist, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint hqFlag = hqFlagOf(world, 1);

    pads.connect(11);
    step(16);
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(11, 1));
    step(16);
    padSteerTo(11, 1, hqFlag);

    // (a) Der Text ist der der HQ-Flagge und nicht der der gewoehnlichen.
    const brief::Brief& b = view(1).GetBrief();
    BOOST_TEST_MESSAGE("AUDIT: Klartext auf der HQ-Flagge = " << b.joined());
    BOOST_TEST(b.title == brief::ForNode(brief::NodeVerdict::OwnHQFlag).title);
    BOOST_TEST(b.title != brief::ForNode(brief::NodeVerdict::OwnFlag).title);
    BOOST_TEST(b.joined() != brief::ForNode(brief::NodeVerdict::OwnFlag).joined());

    // (b) UND DIE MESSUNG DAZU: das Fenster hinter RB hat wirklich nur den Strassenknopf.
    press(11, padHint::OpenActions);
    BOOST_TEST_REQUIRE(view(1).actionwindow != static_cast<iwAction*>(nullptr));
    BOOST_TEST(flagTabButton(view(1), kFlagBtRoad) != static_cast<ctrlButton*>(nullptr));
    BOOST_TEST(flagTabButton(view(1), kFlagBtPullDown) == static_cast<ctrlButton*>(nullptr));
    BOOST_TEST(flagTabButton(view(1), kFlagBtGeologist) == static_cast<ctrlButton*>(nullptr));
    BOOST_TEST(flagTabButton(view(1), kFlagBtScout) == static_cast<ctrlButton*>(nullptr));

    closeActionWindow(*this, view(1));
}

// ============================================================================================
// 6. SPLITSCREEN: die Hinweise gelten JE SITZPLATZ
// ============================================================================================

/// Steht Spieler 1 auf seiner Flagge und Spieler 0 auf freiem Bauland, lesen beide etwas
/// ANDERES - gleichzeitig, im selben Frame, aus derselben Rechnung.
BOOST_FIXTURE_TEST_CASE(TwoSeatsOnDifferentGroundReadDifferentHints, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint flagPt = findPlainFlagSpot(world, view(1).GetViewer());
    BOOST_TEST_REQUIRE(flagPt.isValid());
    world.SetFlag(flagPt, 1);
    const MapPoint buildPt = findBuildSpot(world, view(0).GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(buildPt.isValid());

    pads.connect(10);
    pads.connect(11);
    step(16);
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(10, 0));
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(11, 1));
    step(16);
    padSteerTo(10, 0, buildPt);
    padSteerTo(11, 1, flagPt);
    // Ein gemeinsamer Frame, damit beide Bloecke aus DERSELBEN Runde stammen.
    step(16);

    const brief::Brief& b0 = view(0).GetBrief();
    const brief::Brief& b1 = view(1).GetBrief();
    BOOST_TEST_MESSAGE("AUDIT: Sitz 0 = " << dumpKeys(b0));
    BOOST_TEST_MESSAGE("AUDIT: Sitz 1 = " << dumpKeys(b1));

    BOOST_TEST_REQUIRE(!b0.keys.empty());
    BOOST_TEST_REQUIRE(!b1.keys.empty());
    BOOST_TEST((b0.keys != b1.keys));
    BOOST_TEST(b0.title != b1.title);

    // Sitz 0 steht auf Bauland: A baut, X setzt eine Flagge, RB ist nicht noetig.
    BOOST_TEST(hasHint(b0, PadButton::A, brief::KeyAction::OpenActionMenu));
    BOOST_TEST(hasHint(b0, PadButton::X, brief::KeyAction::PlaceFlag));
    BOOST_TEST(!namesButton(b0, PadButton::RightShoulder));
    // Sitz 1 steht auf seiner Flagge: A baut eine Strasse, RB oeffnet das Flaggenmenue.
    BOOST_TEST(hasHint(b1, PadButton::A, brief::KeyAction::StartRoad));
    BOOST_TEST(hasHint(b1, PadButton::RightShoulder, brief::KeyAction::OpenActionMenu));
    BOOST_TEST(!namesButton(b1, PadButton::X));

    // Und die Trennung haelt auch, wenn EINER von beiden seinen RING oeffnet: nur SEINE Leiste
    // wechselt in die Ringbelegung, die des Nachbarn bleibt die der Welt.
    press(11, padHint::OpenActions);
    step(16);
    BOOST_TEST(view(1).GetRing().IsOpen());
    BOOST_TEST(!view(0).GetRing().IsOpen());
    BOOST_TEST(hasHint(view(1).GetBrief(), PadButton::B, brief::KeyAction::CloseRing));
    BOOST_TEST(!hasHint(view(0).GetBrief(), PadButton::B, brief::KeyAction::CloseRing));
    BOOST_TEST(hasHint(view(0).GetBrief(), PadButton::A, brief::KeyAction::OpenActionMenu));
    closeActionWindow(*this, view(1));
}

/// Eine Ansicht OHNE Pad bekommt keine Leiste. Der Mausspieler im Einzelspieler hat Tooltips,
/// und fuer ihn darf sich nichts aendern - kein Kasten, kein zusaetzlicher Zeichenaufruf.
BOOST_FIXTURE_TEST_CASE(AViewWithoutAPadGetsNoHintsAtAll, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint buildPt = findBuildSpot(world, view(1).GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(buildPt.isValid());

    pads.connect(11);
    step(16);
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(11, 1));
    step(16);
    padSteerTo(11, 1, buildPt);

    BOOST_TEST(!view(1).GetBrief().keys.empty());
    BOOST_TEST_REQUIRE(!view(0).HasPadCursor());
    BOOST_TEST(view(0).GetBrief().keys.empty());
    BOOST_TEST(view(0).GetBrief().empty());
}

// ============================================================================================
// 7. DIE ZEILE, DIE WIRKLICH DASTEHT
// ============================================================================================

/// KONSISTENZPRUEFUNG ZWEIER REINER RECHNUNGEN, kein Beleg dafuer, dass jemand etwas sieht:
/// jede Tastenwirkung und jede Taste hat einen nicht-leeren Text. Ohne diesen Fall koennte ein
/// neuer Enumwert stillschweigend als Leerstelle in der Leiste landen - der Knopf stuende dann
/// zwar da, aber ohne Bedeutung, und das faellt am Fernseher niemandem auf.
BOOST_AUTO_TEST_CASE(EveryKeyActionAndEveryButtonHasAWord)
{
    for(const auto action : helpers::enumRange<brief::KeyAction>())
    {
        BOOST_TEST_CONTEXT("KeyAction " << static_cast<int>(action))
        BOOST_TEST(std::string(brief::KeyLabel(action)) != "");
    }
    for(const auto button : helpers::enumRange<PadButton>())
    {
        BOOST_TEST_CONTEXT("PadButton " << static_cast<int>(button))
        BOOST_TEST(std::string(brief::PadButtonLabel(button)) != "");
    }
    // Und die zusammengesetzte Zeile nennt Taste UND Wirkung.
    const std::vector<brief::KeyHint> keys{{PadButton::A, brief::KeyAction::StartRoad},
                                           {PadButton::RightShoulder, brief::KeyAction::OpenActionMenu},
                                           {PadButton::Back, brief::KeyAction::SystemMenu}};
    const std::string line = brief::KeyLine(keys);
    BOOST_TEST_MESSAGE("AUDIT: gezeichnete Zeile = " << line);
    BOOST_TEST(line.find("A ") == 0u);
    BOOST_TEST(line.find("RB ") != std::string::npos);
    BOOST_TEST(line.find("Back ") != std::string::npos);
    BOOST_TEST(line.find(brief::KeyLabel(brief::KeyAction::StartRoad)) != std::string::npos);
    BOOST_TEST(line.find(brief::KeyLabel(brief::KeyAction::OpenActionMenu)) != std::string::npos);
}

/// Der Klartext einer Handlung ist NIE leer - sonst faellt der Kasten stumm auf den Tooltip
/// zurueck, und genau das war der Zustand vor dieser Phase.
BOOST_AUTO_TEST_CASE(EveryActionBriefCarriesATitleAndASentence)
{
    for(const auto action : helpers::enumRange<brief::ActionBrief>())
    {
        const brief::Brief b = brief::ForAction(action);
        BOOST_TEST_CONTEXT("ActionBrief " << static_cast<int>(action))
        {
            BOOST_TEST(!b.title.empty());
            BOOST_TEST(!b.lines.empty());
        }
    }
}

// ============================================================================================
// 9. BEFUND B1 - IM FENSTER FRAGT DIE LEISTE DAS FOKUSSIERTE CONTROL
// ============================================================================================

/// DER BLOCKIERENDE BEFUND, woertlich gemessen von zwei Pruefern: steht der Padfokus auf einem
/// SCHIEBEREGLER, zeigte die Leiste "A Waehlen". A tut dort nichts - ctrlProgress hat gar kein
/// Activate(). Was WIRKT, ist das Steuerkreuz, und das wurde nie genannt. Pruefer 2 hat
/// unabhaengig gemessen, dass die Leiste in JEDEM Fensterzustand woertlich dasselbe zeigte.
///
/// Gefahren wird der VOLLE Padweg zum Regler (Back, Hauptauswahl, Einstellungen, Y, RB bis zum
/// Regler), und danach wird JEDER genannte und jeder NICHT genannte Knopf gedrueckt und gemessen.
BOOST_FIXTURE_TEST_CASE(OnASliderTheBarNamesTheDpadAndNotA, HintFixture<2>)
{
    takePad(11, 1);
    IngameWindow* const opts = padWayToTheOptionsSlider(11, 1);
    press(11, padHint::Enter);
    BOOST_TEST_REQUIRE(view(1).GetFocus().GetRoot() == static_cast<Window*>(opts));

    auto* const slider = opts->GetCtrl<ctrlProgress>(kOptionsWndMusicVolume);
    BOOST_TEST_REQUIRE(slider != static_cast<ctrlProgress*>(nullptr));
    // ZWEITE VERANKERUNG, unabhaengig von der Kennung oben: dort sitzt wirklich ein Control mit
    // waagerechter Werteachse, und es hat wirklich kein Activate().
    BOOST_TEST_REQUIRE(slider->GetValueRange().has_value());
    BOOST_TEST_REQUIRE((slider->GetValueRange()->axis == Window::ValueAxis::Horizontal));
    BOOST_TEST_REQUIRE(!slider->CanActivate());
    // Nicht am Anschlag anfangen, sonst waere "der Wert aendert sich nicht" mehrdeutig.
    slider->SetPosition(50);

    focusToCtrl(11, 1, slider);
    const brief::Brief& b = view(1).GetBrief();
    BOOST_TEST_MESSAGE("AUDIT: Leiste auf dem Schieberegler = " << dumpKeys(b));
    BOOST_TEST_MESSAGE("AUDIT: gezeichnete Zeile = " << brief::KeyLine(b.keys));

    // (1) A steht NICHT da - das war die Luege.
    BOOST_TEST(!namesButton(b, PadButton::A));
    // (2) Das Steuerkreuz steht da, auf BEIDEN Richtungen der waagerechten Achse.
    BOOST_TEST(hasHint(b, PadButton::DpadLeft, brief::KeyAction::AdjustValue));
    BOOST_TEST(hasHint(b, PadButton::DpadRight, brief::KeyAction::AdjustValue));
    // ... und die senkrechte Achse aendert KEINEN Wert. Was sie stattdessen tut, steht seit
    // Befund N4 ebenfalls da: sie bewegt den Fokus, wenn dort ein Control liegt. Gemessen wird
    // beides unten, Richtung fuer Richtung.
    BOOST_TEST(!hasHint(b, PadButton::DpadUp, brief::KeyAction::AdjustValue));
    BOOST_TEST(!hasHint(b, PadButton::DpadDown, brief::KeyAction::AdjustValue));
    for(const PadButton dpad : {PadButton::DpadUp, PadButton::DpadDown})
    {
        const auto claimed = actionFor(b, dpad);
        BOOST_TEST_CONTEXT(brief::PadButtonLabel(dpad))
        BOOST_TEST((!claimed || *claimed == brief::KeyAction::MoveFocus));
    }
    // (3) Die Zeile zieht die beiden Richtungen zu einem Eintrag zusammen.
    BOOST_TEST(brief::KeyLine(b.keys).find(std::string("Left/Right ") + brief::KeyLabel(brief::KeyAction::AdjustValue))
               != std::string::npos);
    // (4) BEFUND B1, zweite Haelfte: der Kasten war an solchen Controls LEER. Der Regler traegt
    //     keinen Tooltip, also gibt es weiterhin weder Titel noch Zeile - aber es steht jetzt
    //     etwas da, naemlich die Leiste. Siehe Befund B7 weiter unten.
    BOOST_TEST(b.title.empty());
    BOOST_TEST(b.lines.empty());
    BOOST_TEST(!b.empty());

    // UND JETZT GEDRUECKT.
    const unsigned short before = slider->GetPosition();
    press(11, padHint::Act);
    BOOST_TEST(slider->GetPosition() == before); // A tut hier wirklich nichts
    BOOST_TEST(view(1).GetFocus().GetFocused() == static_cast<Window*>(slider));

    press(11, PadButton::DpadRight);
    const unsigned short afterRight = slider->GetPosition();
    BOOST_TEST(afterRight > before); // das Steuerkreuz tut wirklich etwas
    press(11, PadButton::DpadLeft);
    BOOST_TEST(slider->GetPosition() < afterRight);
    // Der Fokus ist dabei stehen geblieben - der Schritt war eine WERTAENDERUNG.
    BOOST_TEST(view(1).GetFocus().GetFocused() == static_cast<Window*>(slider));
}

/// Die Gegenseite desselben Befunds: auf einem gewoehnlichen KNOPF steht A da und das
/// Steuerkreuz nicht - die Leiste zeigt also nicht mehr ueberall dasselbe.
BOOST_FIXTURE_TEST_CASE(TwoControlsOfTheSameWindowGiveTwoDifferentBars, HintFixture<2>)
{
    takePad(11, 1);
    IngameWindow* const opts = padWayToTheOptionsSlider(11, 1);
    press(11, padHint::Enter);
    BOOST_TEST_REQUIRE(view(1).GetFocus().GetRoot() == static_cast<Window*>(opts));

    auto* const slider = opts->GetCtrl<ctrlProgress>(kOptionsWndMusicVolume);
    BOOST_TEST_REQUIRE(slider != static_cast<ctrlProgress*>(nullptr));
    focusToCtrl(11, 1, slider);
    const std::vector<brief::KeyHint> onSlider = view(1).GetBrief().keys;

    // Der naechste Knopf hinter dem Regler ist der Musikspieler - ein ctrlTextButton.
    auto* const button = opts->GetCtrl<Window>(kOptionsWndMusicVolume + 1);
    BOOST_TEST_REQUIRE(button != static_cast<Window*>(nullptr));
    BOOST_TEST_REQUIRE(button->CanActivate());
    BOOST_TEST_REQUIRE(!button->GetValueRange().has_value());
    focusToCtrl(11, 1, button);
    const std::vector<brief::KeyHint> onButton = view(1).GetBrief().keys;

    BOOST_TEST_MESSAGE("AUDIT: Regler = " << brief::KeyLine(onSlider));
    BOOST_TEST_MESSAGE("AUDIT: Knopf  = " << brief::KeyLine(onButton));
    // GENAU DER BEFUND VON PRUEFER 2: vorher waren diese beiden Zeilen woertlich gleich.
    BOOST_TEST((onSlider != onButton));
    BOOST_TEST(brief::KeyLine(onSlider) != brief::KeyLine(onButton));
    BOOST_TEST(hasHint(view(1).GetBrief(), PadButton::A, brief::KeyAction::Choose));
    // Auf einem Knopf aendert das Steuerkreuz keinen Wert. Dass es dort den FOKUS bewegen kann,
    // ist Befund N4 und steht seitdem da - gemessen wird es im eigenen Fall weiter unten.
    BOOST_TEST(!hasHint(view(1).GetBrief(), PadButton::DpadLeft, brief::KeyAction::AdjustValue));
}

/// RB verspricht "eine Station weiter" nur, wo es eine gibt: FocusPath::Dir::Next kennt keinen
/// Umlauf, auf der LETZTEN Station tut die Schulter nichts.
BOOST_FIXTURE_TEST_CASE(OnTheLastFocusStopTheBarNoLongerPromisesTheShoulder, HintFixture<2>)
{
    takePad(11, 1);
    IngameWindow* const opts = padWayToTheOptionsSlider(11, 1);
    press(11, padHint::Enter);
    BOOST_TEST_REQUIRE(view(1).GetFocus().IsActive());

    // Ganz nach hinten fahren - ueber den echten Weg, bis die Schulter nichts mehr bewegt.
    const Window* focused = view(1).GetFocus().GetFocused();
    for(unsigned i = 0; i < 40u; ++i)
    {
        press(11, PadButton::RightShoulder);
        const Window* next = view(1).GetFocus().GetFocused();
        if(next == focused)
            break;
        focused = next;
    }
    BOOST_TEST_MESSAGE("AUDIT: Leiste auf der letzten Station = " << dumpKeys(view(1).GetBrief()));
    // Der Fokus bewegt sich nicht mehr ...
    const Window* const last = view(1).GetFocus().GetFocused();
    press(11, PadButton::RightShoulder);
    BOOST_TEST_REQUIRE(view(1).GetFocus().GetFocused() == last);
    // ... also nennt die Leiste RB auch nicht mehr.
    BOOST_TEST(!namesButton(view(1).GetBrief(), PadButton::RightShoulder));
    // B ist dagegen IMMER belegt und steht immer da.
    BOOST_TEST(hasHint(view(1).GetBrief(), PadButton::B, brief::KeyAction::LeaveFocus));
}

// ============================================================================================
// 10. BEFUND B2 - "B SCHLIESSEN" WAR FALSCH, UND Y AUCH
// ============================================================================================

/// DAS BEOBACHTUNGSFENSTER. Die Leiste versprach dort "B Schliessen"; das Fenster traegt
/// CloseBehavior::NoRightClick (iwObservate.cpp), PadCloseTopMostWindow lehnt also ab.
///
/// Gefahren wird der volle Padweg: Zeiger auf einen Knoten, RB, Y, Reiterkopf "Anzeige", A,
/// Knopf "Beobachtungsfenster", A.
BOOST_FIXTURE_TEST_CASE(TheWatchWindowNoLongerPromisesThatBClosesIt, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint spot = findBuildSpot(world, view(1).GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(spot.isValid());
    takePad(11, 1);
    padSteerTo(11, 1, spot);

    press(11, padHint::OpenActions);
    iwAction* const action = view(1).actionwindow;
    BOOST_TEST_REQUIRE(action != static_cast<iwAction*>(nullptr));
    // PHASE 13: das Aktionsfenster IST der Ring, und er ist sofort betreten - Y ist hier
    // wirkungslos. Der Reiterkopf "Anzeige" ist kein Sektor mehr, sondern eine SEITE: er wird
    // mit RB angeblaettert, nicht angesteuert und mit A umgeschaltet. Das ist derselbe Weg,
    // den der Spieler geht, und der Gegenstand dieses Falls (was die Leiste ueber das
    // Beobachtungsfenster behauptet) bleibt woertlich derselbe.
    BOOST_TEST_REQUIRE(view(1).GetRing().IsOpen());
    BOOST_TEST_REQUIRE(view(1).GetFocus().GetRoot() == static_cast<Window*>(action));

    auto* const mainTab = action->GetCtrl<ctrlTab>(0);
    BOOST_TEST_REQUIRE(mainTab != static_cast<ctrlTab*>(nullptr));
    ctrlGroup* const watchGroup = mainTab->GetGroup(kTabWatch);
    BOOST_TEST_REQUIRE(watchGroup != static_cast<ctrlGroup*>(nullptr));
    ringPageToCtrl(11, 1, watchGroup->GetCtrl<Window>(kWatchBtObserve));
    press(11, padHint::Act);

    // Das Aktionsfenster hat sich dabei selbst geschlossen; oben liegt jetzt das
    // Beobachtungsfenster, und der Fokus steht wieder in der Welt.
    //
    // BEWUSST CloseNow STATT WINDOWMANAGER.Draw(): das Beobachtungsfenster zeichnet in seinem
    // Inneren eine zweite GameWorldView (iwObservate::DrawBackground -> GameWorldView::Draw ->
    // TerrainRenderer), und die endet ohne GL-Kontext in einer Speicherschutzverletzung -
    // dieselbe Sperre, an der auch Msg_PaintBefore haengt (siehe testPadBrief.cpp). Der einzige
    // Zweck des Draw() waere gewesen, das tote Aktionsfenster aus der Liste zu raeumen; genau
    // das tut CloseNow, ohne irgendetwas zu zeichnen.
    WINDOWMANAGER.CloseNow(action);
    step(16);
    IngameWindow* const watch = WINDOWMANAGER.FindNonModalWindow(CGI_OBSERVATION, 1);
    BOOST_TEST_REQUIRE(watch != static_cast<IngameWindow*>(nullptr));
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetTopMostWindow(1u) == watch);
    BOOST_TEST_REQUIRE(!view(1).GetFocus().IsActive());

    const brief::Brief& b = view(1).GetBrief();
    BOOST_TEST_MESSAGE("AUDIT: Leiste mit offenem Beobachtungsfenster = " << dumpKeys(b));
    // DER BEFUND: B steht dort NICHT mehr.
    BOOST_TEST(!hasHint(b, PadButton::B, brief::KeyAction::CloseWindow));
    // Y dagegen schon - das Fenster hat vier bedienbare Knoepfe.
    BOOST_TEST(hasHint(b, PadButton::Y, brief::KeyAction::EnterWindow));

    // UND DIE MESSUNG DAZU: B tut dort wirklich nichts, Y tut wirklich etwas.
    press(11, padHint::Back);
    // Nicht einmal ZUM SCHLIESSEN VORGEMERKT ist es - PadCloseTopMostWindow ist an
    // getCloseBehavior() ausgestiegen, bevor es Close() rufen konnte.
    BOOST_TEST(!watch->ShouldBeClosed());
    BOOST_TEST(WINDOWMANAGER.GetTopMostWindow(1u) == watch);
    press(11, padHint::Enter);
    BOOST_TEST(view(1).GetFocus().GetRoot() == static_cast<Window*>(watch));
}

/// Ein Fenster OHNE eine einzige Fokusstation: FocusPath::SetRoot scheitert, Y laesst den Fokus
/// untaetig - also nennt die Leiste Y dort nicht mehr.
BOOST_FIXTURE_TEST_CASE(AWindowWithoutAnyControlIsNoLongerOfferedToY, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint spot = findBuildSpot(world, view(1).GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(spot.isValid());
    takePad(11, 1);
    padSteerTo(11, 1, spot);

    IngameWindow* wnd = nullptr;
    {
        // Testaufbau: das Fenster gehoert Sitzplatz 1, genau wie ein selbst geoeffnetes.
        const dskGameInterface::ViewScope scope(1);
        wnd = &WINDOWMANAGER.Show(std::make_unique<WndWithoutControls>(DrawPoint(20, 20)));
    }
    step(16);
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetTopMostWindow(1u) == wnd);
    BOOST_TEST_REQUIRE(!FocusPath::HasFocusableControl(wnd));

    const brief::Brief& b = view(1).GetBrief();
    BOOST_TEST_MESSAGE("AUDIT: Leiste an einem Fenster ohne Control = " << dumpKeys(b));
    BOOST_TEST(!namesButton(b, PadButton::Y));
    // B steht sehr wohl da - dieses Fenster laesst sich schliessen, und das ist der einzige
    // Ausweg, den es hier gibt.
    BOOST_TEST(hasHint(b, PadButton::B, brief::KeyAction::CloseWindow));

    // GEDRUECKT: Y laesst den Fokus wirklich untaetig, B schliesst wirklich.
    press(11, padHint::Enter);
    BOOST_TEST(!view(1).GetFocus().IsActive());
    press(11, padHint::Back);
    WINDOWMANAGER.Draw();
    BOOST_TEST(WINDOWMANAGER.GetTopMostWindow(1u) == static_cast<IngameWindow*>(nullptr));
}

/// EIN BESITZERLOSES FENSTER, und hier steht KEINE Aenderung, sondern eine MESSUNG.
///
/// Pruefer 2 hat gemeldet, dass ein Fenster mit SHARED_WINDOW_OWNER allen vier Sitzplaetzen Y
/// und B anbietet. Das stimmt - und es ist keine Luege, sondern das gemessene Verhalten des
/// Padpfades seit Phase 5: GetTopMostWindow(sitzIdx) liefert eigene UND besitzerlose Fenster,
/// und EnterTopMostWindow wie PadCloseTopMostWindow benutzen genau diese Ueberladung. Jeder
/// Sitzplatz darf ein solches Fenster wirklich betreten und wirklich schliessen.
///
/// Dieser Fall haelt das fest, damit es nicht unbemerkt kippt: die Leiste verspricht es beiden,
/// und beide koennen es. Wollte man das aendern, aenderte man die PADBELEGUNG - das ist nicht
/// Sache dieser Runde und stuende dann hier rot.
BOOST_FIXTURE_TEST_CASE(AnOwnerlessWindowIsOfferedToEverySeatAndEverySeatCanReallyUseIt, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint spot0 = findBuildSpot(world, view(0).GetViewer(), BuildingQuality::Hut);
    const MapPoint spot1 = findBuildSpot(world, view(1).GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(spot0.isValid());
    BOOST_TEST_REQUIRE(spot1.isValid());
    takePad(10, 0);
    takePad(11, 1);
    padSteerTo(10, 0, spot0);
    padSteerTo(11, 1, spot1);

    // Kein ViewScope: das Fenster gehoert niemandem - genau wie eine Nachrichtenbox, die der
    // Mausspieler ausserhalb einer Besitzklammer oeffnet.
    IngameWindow& shared = WINDOWMANAGER.Show(std::make_unique<SharedWnd>(DrawPoint(20, 20)));
    BOOST_TEST_REQUIRE(shared.GetOwner() == SHARED_WINDOW_OWNER);
    step(16);

    for(const unsigned seat : {0u, 1u})
    {
        BOOST_TEST_CONTEXT("Sitzplatz " << seat)
        {
            BOOST_TEST_MESSAGE("AUDIT: Sitz " << seat << " = " << dumpKeys(view(seat).GetBrief()));
            BOOST_TEST(hasHint(view(seat).GetBrief(), PadButton::Y, brief::KeyAction::EnterWindow));
            BOOST_TEST(hasHint(view(seat).GetBrief(), PadButton::B, brief::KeyAction::CloseWindow));
        }
    }
    // Und Sitzplatz 1 - dem es NICHT gehoert - kann wirklich hinein und wieder heraus.
    press(11, padHint::Enter);
    BOOST_TEST(view(1).GetFocus().GetRoot() == static_cast<Window*>(&shared));
    press(11, padHint::Back); // erst der Fokus
    press(11, padHint::Back); // dann das Fenster
    WINDOWMANAGER.Draw();
    BOOST_TEST(WINDOWMANAGER.GetTopMostWindow(1u) == static_cast<IngameWindow*>(nullptr));
}

// ============================================================================================
// 11. BEFUND B3 - Y IM FENSTER, DER FALL, DEN PHASE 11 EIGENS GEBAUT HAT
// ============================================================================================

/// Der Fokus steht im alten Fenster, ein NEUES geht obenauf. Y betritt es (die Vorabfrage in
/// dskGameInterface::OnPadButton laeuft vor dem Fokus) - und die Leiste sagte es nicht.
///
/// Gefahren wird der Weg, auf dem das im Spiel wirklich passiert: Hauptauswahl -> Einstellungen.
BOOST_FIXTURE_TEST_CASE(WithANewWindowOnTopTheBarNamesYEvenWhileTheFocusIsInTheOldOne, HintFixture<2>)
{
    takePad(11, 1);
    press(11, PadButton::Back);
    IngameWindow* const menu = WINDOWMANAGER.FindNonModalWindow(CGI_PADMENU, 1);
    BOOST_TEST_REQUIRE(menu != static_cast<IngameWindow*>(nullptr));
    focusToCtrl(11, 1, menu->GetCtrl<Window>(iwPadSystemMenu::ID_MAIN_SELECTION));
    press(11, padHint::Act);
    IngameWindow* const mainSel = WINDOWMANAGER.FindNonModalWindow(CGI_MAINSELECTION, 1);
    BOOST_TEST_REQUIRE(mainSel != static_cast<IngameWindow*>(nullptr));
    WINDOWMANAGER.Draw();
    step(16);
    BOOST_TEST_REQUIRE(view(1).GetFocus().GetRoot() == static_cast<Window*>(mainSel));

    // SOLANGE das oberste Fenster das ist, in dem der Fokus schon steht, tut Y nichts - und die
    // Leiste nennt es folgerichtig nicht.
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetTopMostWindow(1u) == mainSel);
    BOOST_TEST(!namesButton(view(1).GetBrief(), PadButton::Y));

    // Jetzt das zweite Fenster - der Fokus bleibt im ersten (iwMainMenu ruft ToggleWindow und
    // ruehrt den Fokus nicht an).
    focusToCtrl(11, 1, mainSel->GetCtrl<Window>(kMainMenuOptionsButton));
    press(11, padHint::Act);
    IngameWindow* const opts = WINDOWMANAGER.FindNonModalWindow(CGI_OPTIONSWINDOW, 1);
    BOOST_TEST_REQUIRE(opts != static_cast<IngameWindow*>(nullptr));
    BOOST_TEST_REQUIRE(view(1).GetFocus().GetRoot() == static_cast<Window*>(mainSel));
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetTopMostWindow(1u) == opts);

    const brief::Brief& b = view(1).GetBrief();
    BOOST_TEST_MESSAGE("AUDIT: Leiste im alten Fenster mit neuem obenauf = " << dumpKeys(b));
    BOOST_TEST(hasHint(b, PadButton::Y, brief::KeyAction::EnterWindow));
    // Die uebrigen Fenstereintraege stehen weiterhin da - Y verdraengt nichts.
    BOOST_TEST(hasHint(b, PadButton::A, brief::KeyAction::Choose));
    BOOST_TEST(hasHint(b, PadButton::B, brief::KeyAction::LeaveFocus));

    // UND Y TUT ES.
    press(11, padHint::Enter);
    BOOST_TEST(view(1).GetFocus().GetRoot() == static_cast<Window*>(opts));
}

// ============================================================================================
// 12. BEFUND B4 - DASS DIE LEISTE UEBERHAUPT GEZEICHNET WIRD
// ============================================================================================

/// DER BEFUND: Pruefer 1 hat die Tastenzeile ersatzlos aus dem Zeichenweg entfernt, und 306
/// Faelle blieben gruen. Pruefer 2 hat nachgemessen, warum - der Zeichenweg lief in der GANZEN
/// Suite kein einziges Mal, weil WINDOWMANAGER.GetCurrentDesktop() != dsk in jeder
/// Splitscreen-Fixture ist und `paintForReal` damit wirkungslos blieb.
///
/// WAS DIESER FALL BEWEIST: der Zeichner laeuft (er wird hier DIREKT gerufen, dskGameInterface
/// ist dafuer oeffentlich), und die Tastenzeile ist Teil dessen, was er zeichnet - sie steht in
/// der Zeilenliste, sie traegt ihre eigene Farbe, und sie KOSTET PLATZ: der Kasten ist genau eine
/// Zeilenhoehe hoeher als derselbe Block ohne sie.
///
/// WAS ER NICHT BEWEIST: dass am Fernseher ein Buchstabe erscheint. Der DummyRenderer verwirft
/// jeden Zeichenaufruf. Mehr zu behaupten waere unehrlich - aber die Zeile kann jetzt nicht mehr
/// aus dem Zeichenweg fallen, ohne dass dieser Fall rot wird: DrawBrief kennt den Unterschied
/// zwischen einer Textzeile und der Tastenzeile gar nicht mehr, er zeichnet stur die Liste.
BOOST_FIXTURE_TEST_CASE(TheDrawnPanelReallyCarriesTheKeyLine, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint spot = findBuildSpot(world, view(1).GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(spot.isValid());
    takePad(11, 1);
    padSteerTo(11, 1, spot);

    const brief::Brief withKeys = view(1).GetBrief();
    BOOST_TEST_REQUIRE(!withKeys.keys.empty());
    BOOST_TEST_REQUIRE(!withKeys.title.empty());

    const auto layout = dsk->LayoutBrief(view(1));
    BOOST_TEST_REQUIRE(!layout.lines.empty());
    // Die letzten Zeilen des Kastens sind die Tastenzeile - erkennbar an ihrer eigenen Farbe.
    std::string drawnKeyText;
    unsigned numKeyLines = 0;
    for(const auto& line : layout.lines)
    {
        if(line.color != dskGameInterface::keyLineColor)
            continue;
        ++numKeyLines;
        if(!drawnKeyText.empty())
            drawnKeyText += ' ';
        drawnKeyText += line.text;
    }
    BOOST_TEST_MESSAGE("AUDIT: gezeichnete Tastenzeile = " << drawnKeyText);
    BOOST_TEST_REQUIRE(numKeyLines >= 1u);
    // Und es ist WIRKLICH die Leiste dieses Blocks: jeder genannte Knopf steht darin.
    for(const brief::KeyHint& hint : withKeys.keys)
    {
        BOOST_TEST_CONTEXT(brief::PadButtonLabel(hint.button))
        BOOST_TEST(drawnKeyText.find(brief::KeyLabel(hint.action)) != std::string::npos);
    }

    // SIE KOSTET PLATZ. Derselbe Block ohne Tasten ergibt einen Kasten, der genau um die Zeilen
    // der Leiste niedriger ist. Faellt die Zeile aus dem Zeichenweg, sind beide gleich hoch.
    brief::Brief withoutKeys = withKeys;
    withoutKeys.keys.clear();
    view(1).SetBrief(withoutKeys);
    const auto bare = dsk->LayoutBrief(view(1));
    BOOST_TEST_REQUIRE(!bare.lines.empty());
    BOOST_TEST(bare.lines.size() + numKeyLines == layout.lines.size());
    BOOST_TEST(layout.panel.getSize().y == bare.panel.getSize().y + numKeyLines * layout.lineHeight);
    for(const auto& line : bare.lines)
        BOOST_TEST(line.color != dskGameInterface::keyLineColor);

    // Und der ECHTE Zeichner laeuft ueber beides - direkt gerufen, weil der WindowManager in
    // dieser Fixture einen anderen Desktop zeichnet.
    view(1).SetBrief(withKeys);
    BOOST_TEST_CHECKPOINT("DrawBrief mit Tastenzeile");
    dsk->DrawBrief(view(1));
    BOOST_TEST_CHECKPOINT("DrawBrief ohne Tastenzeile");
    view(1).SetBrief(withoutKeys);
    dsk->DrawBrief(view(1));
    // ... und der ganze Zeichenschwanz, in dem DrawBrief haengt.
    view(1).SetBrief(withKeys);
    dsk->paintForReal = true;
    BOOST_TEST_CHECKPOINT("Msg_PaintAfter");
    dsk->Msg_PaintAfter();
    dsk->paintForReal = false;
}

// ============================================================================================
// 13. BEFUND B7 - EIN KASTEN, DER NUR AUS DER LEISTE BESTEHT
// ============================================================================================

/// Brief::empty() zaehlt die Tastenzeile mit. Ein Block ohne Titel und ohne Zeilen, aber mit
/// Leiste, gilt damit nicht mehr als leer, und der Zeichner zeichnet einen Kasten, wo Phase 9
/// gar nichts zeichnete.
///
/// DAS IST GEWOLLT, und die Begruendung steht bei brief::Brief::empty(): genau dieser Fall IST
/// der zweite Teil von Befund B1 ("zusaetzlich ist der Klartextkasten an solchen Controls
/// leer"). Ein Padspieler auf einem Regler ohne Tooltip bekam bisher gar nichts zu sehen; jetzt
/// liest er wenigstens, welche Knoepfe wirken.
///
/// DIE GRENZE DAZU, und die ist die harte Randbedingung des ganzen Vorhabens: eine Ansicht OHNE
/// Pad bekommt weiterhin GAR KEINEN Kasten.
BOOST_FIXTURE_TEST_CASE(ABoxThatIsNothingButTheKeyBarIsDrawnOnPurpose, HintFixture<2>)
{
    takePad(11, 1);
    IngameWindow* const opts = padWayToTheOptionsSlider(11, 1);
    press(11, padHint::Enter);
    auto* const slider = opts->GetCtrl<ctrlProgress>(kOptionsWndMusicVolume);
    BOOST_TEST_REQUIRE(slider != static_cast<ctrlProgress*>(nullptr));
    focusToCtrl(11, 1, slider);

    const brief::Brief& b = view(1).GetBrief();
    BOOST_TEST_REQUIRE(b.title.empty());
    BOOST_TEST_REQUIRE(b.lines.empty());
    BOOST_TEST_REQUIRE(!b.keys.empty());
    // Nicht leer - also wird gezeichnet.
    BOOST_TEST(!b.empty());
    const auto layout = dsk->LayoutBrief(view(1));
    BOOST_TEST_MESSAGE("AUDIT: Kasten = " << layout.lines.size() << " Zeile(n), "
                                          << layout.panel.getSize().x << "x" << layout.panel.getSize().y);
    BOOST_TEST_REQUIRE(!layout.lines.empty());
    // ... und zwar AUSSCHLIESSLICH die Leiste, keine Textzeile.
    for(const auto& line : layout.lines)
        BOOST_TEST(line.color == dskGameInterface::keyLineColor);
    BOOST_TEST(layout.panel.getSize().y > 0u);

    // DIE GRENZE: die Ansicht ohne Pad bekommt keinen Kasten - kein Titel, keine Zeile, keine
    // Leiste, kein Rechteck.
    BOOST_TEST_REQUIRE(!view(0).HasPadCursor());
    BOOST_TEST(view(0).GetBrief().empty());
    BOOST_TEST(dsk->LayoutBrief(view(0)).lines.empty());

    // Und ein Block, in dem wirklich nichts steht, ist weiterhin leer.
    BOOST_TEST(brief::Brief().empty());
}

// ============================================================================================
// 14. BEFUND B5 - DIE ERFUNDENE SPIELREGEL
// ============================================================================================

/// Im Geologentext stand "Ein Bergwerk foerdert nur dort, wo ein Schild steht". Das ist keine
/// Regel dieses Spiels, und der Satz widersprach seinem eigenen Nachsatz.
///
/// WAS DER QUELLTEXT SAGT, und was dieser Fall gegen den Text haelt:
///   - nofMiner::StartWorking -> nofWorkman::FindPointWithResource sucht
///     GetMatchingPointsInRadius(pos, MINER_RADIUS, NodeHasResource(res), true) - also im
///     Umkreis von MINER_RADIUS Knoten um das Bergwerk, einschliesslich seines eigenen, nach
///     Knoten, deren Node.resources den Rohstoff tragen. Von einem Schild ist dort nicht die
///     Rede, und in der ganzen Klasse noSign auch nicht: "As this is only for drawing".
///
/// Der Fall verankert die ZAHL an MINER_RADIUS statt an einem Wort im Text - steht der Radius
/// eines Tages anders im Quelltext, faellt der Satz auf.
BOOST_AUTO_TEST_CASE(TheGeologistTextNoLongerInventsAMiningRule)
{
    // Die Sprache wird AUSDRUECKLICH eingestellt und nie aus der Systemsprache uebernommen -
    // dieselbe Lehre wie in testPadBrief.cpp (Befund A der dritten Pruefung). Ohne das haengt
    // dieser Fall davon ab, welcher Katalog gerade geladen ist, und das ist in einer Suite, die
    // ihre Sprache selbst umstellt, ein Zufall. Der erste Anlauf dieses Falles ist genau daran
    // gescheitert: allein gruen, in der vollen Suite rot.
    static_cast<void>(LANGUAGES); // bindtextdomain, sonst liefert _() stumm die msgid

    // Der Radius im Text ist an den Quelltext gebunden und nicht an ein Wort.
    static_assert(MINER_RADIUS == 2, "Der Text nennt ZWEI Schritte - nofWorkman::FindPointWithResource");

    {
        // DURCHGANG 1: der Quelltext. "ja" hat keinen ausgelieferten Katalog, _() liefert also
        // die msgid - und das ist genau der englische Satz, der im Programm steht.
        const rttr::test::LocaleResetter noCatalog("ja");
        BOOST_TEST_REQUIRE(std::string(_("Call in a geologist")) == "Call in a geologist");
        const std::string all = brief::ForAction(brief::ActionBrief::CallGeologist).joined();
        BOOST_TEST_MESSAGE("AUDIT: Geologentext (Quelltext) = " << all);
        // Die erfundene Regel ist weg.
        BOOST_TEST(all.find("only digs where a sign stands") == std::string::npos);
        // Was stattdessen dasteht, ist im Quelltext nachgelesen: der Radius und das Schild als
        // blosse Markierung.
        BOOST_TEST(all.find("two steps") != std::string::npos);
        BOOST_TEST(all.find("sign or no sign") != std::string::npos);
    }
    {
        // DURCHGANG 2: der deutsche Katalog - das ist der Satz, den der Auftraggeber liest.
        const rttr::test::LocaleResetter german("de");
        BOOST_TEST_REQUIRE(std::string(_("Call in a geologist")) != "Call in a geologist");
        const std::string all = brief::ForAction(brief::ActionBrief::CallGeologist).joined();
        BOOST_TEST_MESSAGE("AUDIT: Geologentext (de) = " << all);
        BOOST_TEST(all.find("nur dort, wo ein Schild steht") == std::string::npos);
        BOOST_TEST(all.find("zwei Schritten") != std::string::npos);
        BOOST_TEST(all.find("mit oder ohne Schild") != std::string::npos);
    }
}

// ============================================================================================
// 15. DER DETERMINISMUS - Hinweise sind reine Anzeige
// ============================================================================================

/// Die Leiste und der Klartext werden je Frame und Ansicht neu gerechnet - und zwar unter
/// anderem aus ComputeActionOptions und CanOpenObjectWindow. Keine dieser Rechnungen darf ein
/// GameCommand erzeugen. Gezaehlt wird im REPLAY, also nur, was tatsaechlich vom Server zurueckkam.
BOOST_FIXTURE_TEST_CASE(ComputingAndDrawingTheHintsCreatesNoGameCommand, PadGameFixture)
{
    setUpTwoLocalPlayers();
    // BEFUND B4, gemessen von Pruefer 2: `paintForReal` ALLEIN reicht hier nicht. Der Schalter
    // legt die Ruempfe von Msg_PaintBefore/-After frei, aber WINDOWMANAGER.Draw() ruft sie nur
    // am AKTUELLEN Desktop - und das ist in dieser Fixture nicht dsk. Der Zeichenweg lief damit
    // kein einziges Mal, und das Merkmal war in diesem Fall wirkungslos.
    //
    // Deshalb wird Msg_PaintAfter unten ZUSAETZLICH direkt gerufen. Msg_PaintBefore bleibt aus
    // dem Spiel: es ruft Run() -> GameWorldView::Draw -> TerrainRenderer::Draw, und das endet
    // ohne GL-Kontext in einer Speicherschutzverletzung (nachgemessen, siehe testPadBrief.cpp).
    dsk->paintForReal = true;

    const MapPoint hqPos = world().GetPlayer(1).GetHQPos();
    const auto* hq = world().GetSpecObj<nobBaseWarehouse>(hqPos);
    BOOST_TEST_REQUIRE(hq != nullptr);
    const MapPoint hqFlag = hq->GetFlagPos();
    const MapPoint flagSpot = findExclusiveFlagSpot(world(), 1, 0);
    BOOST_TEST_REQUIRE(flagSpot.isValid());

    aimPadAt(10, 0, world().GetPlayer(0).GetHQPos());
    aimPadAt(11, 1, hqPos);
    for(const MapPoint& pt : {hqPos, hqFlag, flagSpot})
    {
        aimAt(1, pt);
        for(unsigned i = 0; i < 20u; ++i)
        {
            step(16);
            WINDOWMANAGER.Draw();
            // Der echte Zeichenschwanz mit der Schleife ueber alle Klartextkaesten.
            dsk->Msg_PaintAfter();
        }
        BOOST_TEST_MESSAGE("AUDIT: " << pt << " -> " << dumpKeys(dsk->GetPlayerView(1).GetBrief()));
        BOOST_TEST(!dsk->GetPlayerView(1).GetBrief().keys.empty());
    }
    pumpUntilGF(GAMECLIENT.GetGFNumber() + 20);

    tearDownDesktop();
    const auto replayPath = stopAndGetReplay();
    BOOST_TEST(numGCsForPlayer(replayPath, 1) == 0u);
    BOOST_TEST(numGCsForPlayer(replayPath, 0) == 0u);
}


// ============================================================================================
// 16. BEFUND N1 - "BACK MENUE" WAEHREND DER STRASSENBAU LAEUFT
// ============================================================================================

/// DER BEFUND, gemessen: dskGameInterface::OnPadButton faengt Back nur ab, solange
/// `view.GetRoad().mode == RoadBuildMode::Disabled`. Laeuft der Baumodus, faellt die Flanke in
/// den Fokus, und FocusPath::OnPadButton verschluckt sie. CanOpenSystemMenu fragte den Baumodus
/// nicht - "Back Menue" stand trotzdem da.
///
/// Erreichbar ist das auf dem Weg, den die LEISTE SELBST vorgibt, und genau so faehrt dieser
/// Fall ihn ab: eigene Flagge -> "RB Aktionen" -> "A Strasse" -> das Fenster bleibt stehen ->
/// "Y Ins Fenster" -> und dort stand "Back Menue". Das ist der Knopf, den ein festgefahrener
/// Anfaenger als Ausweg sucht.
BOOST_FIXTURE_TEST_CASE(WhileTheRoadIsBeingBuiltTheBarNoLongerPromisesTheMenu, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint flagPt = findPlainFlagSpot(world, view(1).GetViewer());
    BOOST_TEST_REQUIRE(flagPt.isValid());
    world.SetFlag(flagPt, 1); // Testaufbau
    takePad(11, 1);
    padSteerTo(11, 1, flagPt);

    // Ausgangslage: hier steht "Back Menue", und das stimmt auch.
    BOOST_TEST_REQUIRE(hasHint(view(1).GetBrief(), PadButton::Back, brief::KeyAction::SystemMenu));

    // Ein Fenster, das waehrend des Baumodus offen steht - PHASE 13 nimmt dafuer ein
    // GEWOEHNLICHES statt des Aktionsfensters, weil dieses jetzt ein Ring und damit modal ist.
    // An der geprueften Frage aendert das nichts: es geht um Back, nicht um das Fenster.
    IngameWindow* const plainWnd = openPlainWindowByPad(11, 1);
    padSteerTo(11, 1, flagPt);
    press(11, padHint::Act);
    BOOST_TEST_REQUIRE((view(1).GetRoad().mode == RoadBuildMode::Normal));
    BOOST_TEST_REQUIRE(!plainWnd->ShouldBeClosed());

    // (a) Baumodus, Fokus noch in der Welt: Back steht nicht mehr da.
    BOOST_TEST_MESSAGE("AUDIT: Leiste im Baumodus = " << dumpKeys(view(1).GetBrief()));
    BOOST_TEST(!namesButton(view(1).GetBrief(), PadButton::Back));

    // (b) Und im Fenster ebenfalls nicht - DAS war die gemessene Luege.
    press(11, padHint::Enter);
    BOOST_TEST_REQUIRE(view(1).GetFocus().IsActive());
    BOOST_TEST_REQUIRE((view(1).GetRoad().mode == RoadBuildMode::Normal));
    BOOST_TEST_MESSAGE("AUDIT: Leiste im Fenster waehrend des Baumodus = " << dumpKeys(view(1).GetBrief()));
    BOOST_TEST(!namesButton(view(1).GetBrief(), PadButton::Back));

    // UND GEDRUECKT: das Menue geht wirklich nicht auf. Ohne die Korrektur waere genau dieser
    // Druck der gemessene Widerspruch gewesen ("verspricht Back=Menue: true / danach offen:
    // false").
    press(11, padHint::SystemMenu);
    BOOST_TEST(WINDOWMANAGER.FindNonModalWindow(CGI_PADMENU, 1u) == static_cast<IngameWindow*>(nullptr));
    BOOST_TEST(view(1).GetFocus().IsActive()); // und der Fokus steht noch, wo er stand

    // DIE GEGENSEITE, damit der Hinweis nicht einfach verschwunden ist: sobald der Baumodus
    // endet, steht Back wieder da und tut wieder etwas.
    press(11, padHint::Back); // Fokus abgeben
    BOOST_TEST_REQUIRE(!view(1).GetFocus().IsActive());
    press(11, padHint::Back); // leere Strecke -> Baumodus aus
    BOOST_TEST_REQUIRE((view(1).GetRoad().mode == RoadBuildMode::Disabled));
    step(16);
    BOOST_TEST(hasHint(view(1).GetBrief(), PadButton::Back, brief::KeyAction::SystemMenu));
    press(11, padHint::SystemMenu);
    BOOST_TEST(WINDOWMANAGER.FindNonModalWindow(CGI_PADMENU, 1u) != static_cast<IngameWindow*>(nullptr));

    if(view(1).GetRing().IsOpen())
        dsk->CloseRing(view(1), true);
    if(!plainWnd->ShouldBeClosed())
        plainWnd->Close();
    WINDOWMANAGER.Draw();
}

// ============================================================================================
// 17. BEFUND N2 - Y WIRKT IM BAUMODUS, UND DAS STAND NICHT DA
// ============================================================================================

/// Die Y-Vorabfrage in dskGameInterface::OnPadButton hat KEINEN Baumodusschutz: steht ein
/// Fenster offen, fuehrt Y auch mitten im Strassenbau hinein. Die Leiste zeigte im Baumodus nur
/// "A Verlaengern - B Abbrechen".
BOOST_FIXTURE_TEST_CASE(InRoadModeTheBarNamesTheButtonThatEntersTheOpenWindow, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint flagPt = findPlainFlagSpot(world, view(1).GetViewer());
    BOOST_TEST_REQUIRE(flagPt.isValid());
    world.SetFlag(flagPt, 1);
    takePad(11, 1);
    padSteerTo(11, 1, flagPt);

    IngameWindow* const plainWnd = openPlainWindowByPad(11, 1);
    padSteerTo(11, 1, flagPt);
    press(11, padHint::Act);
    BOOST_TEST_REQUIRE((view(1).GetRoad().mode == RoadBuildMode::Normal));
    BOOST_TEST_REQUIRE(!view(1).GetFocus().IsActive());
    // Der Zeiger steht auf der Startflagge, also auf dem Wegende - dort tut A nichts
    // (Befund P1). Erst ein Knoten weiter verspricht die Leiste das Verlaengern.
    BOOST_TEST(!namesButton(view(1).GetBrief(), PadButton::A));
    padSteerTo(11, 1, world.GetNeighbour(flagPt, Direction::East));

    const brief::Brief& b = view(1).GetBrief();
    BOOST_TEST_MESSAGE("AUDIT: Leiste im Baumodus mit offenem Fenster = " << dumpKeys(b));
    BOOST_TEST(hasHint(b, PadButton::A, brief::KeyAction::ExtendRoad));
    BOOST_TEST(hasHint(b, PadButton::Y, brief::KeyAction::EnterWindow));

    // UND GEDRUECKT.
    press(11, padHint::Enter);
    BOOST_TEST(view(1).GetFocus().IsActive());
    BOOST_TEST(view(1).GetFocus().GetRoot() == static_cast<Window*>(plainWnd));

    if(!plainWnd->ShouldBeClosed())
        plainWnd->Close();
    WINDOWMANAGER.Draw();
}

/// Die Gegenprobe zu N2: OHNE offenes Fenster nennt der Baumodus Y NICHT - denn dann tut es
/// nichts. Sonst waere der neue Eintrag nur eine zweite Konstante.
BOOST_FIXTURE_TEST_CASE(InRoadModeWithoutAWindowTheBarStaysSilentAboutY, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint flagPt = findPlainFlagSpot(world, view(1).GetViewer());
    BOOST_TEST_REQUIRE(flagPt.isValid());
    world.SetFlag(flagPt, 1);
    takePad(11, 1);
    padSteerTo(11, 1, flagPt);

    press(11, padHint::Act); // A auf der Flagge: Strassenbau, kein Fenster
    BOOST_TEST_REQUIRE((view(1).GetRoad().mode == RoadBuildMode::Normal));
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetTopMostWindow(1u) == static_cast<IngameWindow*>(nullptr));
    BOOST_TEST_MESSAGE("AUDIT: Leiste im Baumodus ohne Fenster = " << dumpKeys(view(1).GetBrief()));
    BOOST_TEST(!namesButton(view(1).GetBrief(), PadButton::Y));
    press(11, padHint::Enter);
    BOOST_TEST(!view(1).GetFocus().IsActive());
}

// ============================================================================================
// 18. BEFUND N3 - LB GEHT EINE STATION ZURUECK
// ============================================================================================

/// LB wirkt seit Phase 4 (FocusPath::OnPadButton -> Move(Dir::Prev)), stand aber nie in der
/// Leiste, waehrend RB darin stand. Wer einen Knopf ueberschoss, musste ihn mit RB umrunden -
/// im Flaggenreiter viermal.
BOOST_FIXTURE_TEST_CASE(InsideAWindowTheBarNamesTheShoulderThatGoesBack, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint flagPt = findPlainFlagSpot(world, view(1).GetViewer());
    BOOST_TEST_REQUIRE(flagPt.isValid());
    world.SetFlag(flagPt, 1);
    takePad(11, 1);
    padSteerTo(11, 1, flagPt);
    // PHASE 13: gemessen an einem GEWOEHNLICHEN Fenster. Im Ring blaettern die Schultern die
    // Seite statt die Fokusstation zu wechseln - das misst der Ringfall in Abschnitt 2.
    IngameWindow* const plainWnd = openPlainWindowByPad(11, 1);
    press(11, padHint::Enter);
    BOOST_TEST_REQUIRE(view(1).GetFocus().IsActive());
    BOOST_TEST_REQUIRE(view(1).GetFocus().GetRoot() == static_cast<Window*>(plainWnd));

    // (a) Auf der ERSTEN Station gibt es kein Zurueck - also steht LB nicht da.
    const Window* const first = view(1).GetFocus().GetFocused();
    BOOST_TEST_MESSAGE("AUDIT: Leiste auf der ersten Station = " << dumpKeys(view(1).GetBrief()));
    BOOST_TEST(!namesButton(view(1).GetBrief(), PadButton::LeftShoulder));
    press(11, PadButton::LeftShoulder);
    BOOST_TEST(view(1).GetFocus().GetFocused() == first); // und LB tut dort wirklich nichts

    // (b) Eine Station weiter steht LB da ...
    press(11, PadButton::RightShoulder);
    const Window* const second = view(1).GetFocus().GetFocused();
    BOOST_TEST_REQUIRE(second != first);
    BOOST_TEST_MESSAGE("AUDIT: Leiste auf der zweiten Station = " << dumpKeys(view(1).GetBrief()));
    BOOST_TEST(hasHint(view(1).GetBrief(), PadButton::LeftShoulder, brief::KeyAction::PrevControl));
    // ... und tut genau das.
    press(11, PadButton::LeftShoulder);
    BOOST_TEST(view(1).GetFocus().GetFocused() == first);

    if(!plainWnd->ShouldBeClosed())
        plainWnd->Close();
    WINDOWMANAGER.Draw();
}

// ============================================================================================
// 19. BEFUND N4 - DAS STEUERKREUZ BEWEGT DEN FOKUS, UND DAS STAND NIRGENDS
// ============================================================================================

/// DIE FOLGENREICHSTE AUSLASSUNG fuer einen Anfaenger, so der Nachpruefer: er las in JEDEM
/// Fenster "A Waehlen - RB Weiter - B Zurueck - Back Menue" und erfuhr nirgends, dass das
/// Steuerkreuz (und der linke Stick, derselbe Weg durch FocusPath::Step) den Fokus bewegt.
///
/// Gemessen wird nicht "es steht etwas da", sondern die GLEICHHEIT von Behauptung und Wirkung:
/// fuer JEDE der vier Richtungen wird die Leiste gelesen, der Knopf gedrueckt und nachgesehen,
/// ob der Fokus wirklich gewandert ist. Beides muss uebereinstimmen - in beide Richtungen.
BOOST_FIXTURE_TEST_CASE(ForEveryDpadDirectionTheBarSaysExactlyWhatThePressDoes, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint flagPt = findPlainFlagSpot(world, view(1).GetViewer());
    BOOST_TEST_REQUIRE(flagPt.isValid());
    world.SetFlag(flagPt, 1);
    takePad(11, 1);
    padSteerTo(11, 1, flagPt);
    // PHASE 13: gemessen an einem GEWOEHNLICHEN Fenster. Im RING dreht das Steuerkreuz den Ring
    // (KeyAction::TurnRing) und bewegt gerade nicht den Fokus in ID- oder Lageordnung; die
    // Ringbelegung wird eigens in Abschnitt 2 gemessen. Die Frage dieses Falls - stimmt die
    // Behauptung der Leiste in JEDER Richtung mit der Wirkung ueberein? - bleibt woertlich
    // dieselbe, sie hat nur einen anderen Traeger.
    //
    // BEFUND K5: beim Umhaengen fielen hier drei Zeilen weg, die den Fokus auf den GEOLOGEN
    // setzten - die Knopfreihe des Flaggenreiters. Sie ist nicht verloren, sie steht in
    // Abschnitt 26 (OnTheFlagButtonRowTheBarSaysExactlyWhatEveryDpadPressDoes), auf dem
    // Traeger, den ein Padspieler heute wirklich erreicht. Warum sie nicht HIER wieder
    // stehen kann, ist dort gemessen: das Aktionsfenster geht fuer das Pad nur noch als RING
    // auf, und im Ring lautet die Wirkung TurnRing und nicht MoveFocus.
    IngameWindow* const plainWnd = openPlainWindowByPad(11, 1);
    press(11, padHint::Enter);
    BOOST_TEST_REQUIRE(view(1).GetFocus().IsActive());

    // DREI RUNDEN durch alle vier Richtungen. Der Fokus wandert dabei, jede Runde misst also
    // von einer anderen Stelle des Fensters aus - und die Leiste wird VOR jedem einzelnen Druck
    // neu gelesen. Nicht zurueckgesetzt wird bewusst: RB kennt keinen Umlauf, ein Zurueckfahren
    // waere ein zweiter Mechanismus im Nachweis.
    unsigned named = 0;
    unsigned moved = 0;
    unsigned presses = 0;
    for(unsigned round = 0; round < 3u; ++round)
    {
        for(const PadButton dpad : {PadButton::DpadLeft, PadButton::DpadRight, PadButton::DpadUp, PadButton::DpadDown})
        {
            const std::string bar = dumpKeys(view(1).GetBrief());
            const auto claimed = actionFor(view(1).GetBrief(), dpad);
            const Window* const before = view(1).GetFocus().GetFocused();
            press(11, dpad);
            const Window* const after = view(1).GetFocus().GetFocused();
            const bool reallyMoved = (after != before);
            ++presses;
            BOOST_TEST_CONTEXT("Runde " << round << " " << brief::PadButtonLabel(dpad) << "  Leiste=" << bar)
            {
                // Die Behauptung und die Wirkung sind DASSELBE - in beide Richtungen: was
                // wirkt, steht da, und was dasteht, wirkt.
                BOOST_TEST(reallyMoved == (claimed.has_value() && *claimed == brief::KeyAction::MoveFocus));
            }
            if(claimed)
                ++named;
            if(reallyMoved)
                ++moved;
        }
    }
    BOOST_TEST_MESSAGE("AUDIT: " << presses << " Steuerkreuzdruecke, genannt = " << named << ", gewirkt = " << moved);
    // Der Befund selbst: es wirkt wirklich etwas. Waere hier 0, waere der ganze Fall wertlos.
    BOOST_TEST(moved > 0u);
    BOOST_TEST(named == moved);

    if(!plainWnd->ShouldBeClosed())
        plainWnd->Close();
    WINDOWMANAGER.Draw();
}

// ============================================================================================
// 20. BEFUND N5 - Y WARF DEN SPIELER AUS SEINEM EIGENEN FENSTER
// ============================================================================================

/// DER BEFUND, gemessen: dskGameInterface::EnterWindow rief ReleaseFocus VOR SetRoot. Lag ein
/// Fenster OHNE Fokusstation obenauf, scheiterte SetRoot - und der Spieler stand danach ohne
/// Fokus da ("nach Y: Fokus noch aktiv = false"). Ein Knopf, der einen Spieler aus seinem
/// eigenen Fenster wirft, ohne ihn irgendwohin zu bringen, ist kein Hinweisproblem, sondern ein
/// FEHLER - deshalb ist EnterWindow repariert und nicht die Leiste angepasst worden.
///
/// Die Leiste nennt Y hier ohnehin nicht (Befund B2, bestaetigt). Dieser Fall misst das, was
/// danach passiert, wenn der Spieler ihn trotzdem drueckt - und ein Anfaenger drueckt ihn.
BOOST_FIXTURE_TEST_CASE(YNoLongerThrowsThePlayerOutOfHisOwnWindow, HintFixture<2>)
{
    takePad(11, 1);
    press(11, padHint::SystemMenu);
    IngameWindow* const menu = WINDOWMANAGER.FindNonModalWindow(CGI_PADMENU, 1);
    BOOST_TEST_REQUIRE(menu != static_cast<IngameWindow*>(nullptr));
    BOOST_TEST_REQUIRE(view(1).GetFocus().GetRoot() == static_cast<Window*>(menu));
    const Window* const focusedBefore = view(1).GetFocus().GetFocused();
    BOOST_TEST_REQUIRE(focusedBefore != static_cast<const Window*>(nullptr));

    // Ein Fenster ohne eine einzige Fokusstation legt sich obenauf - ihm gehoert derselbe
    // Sitzplatz, es ist also das, was Y ansteuern wuerde.
    IngameWindow* wnd = nullptr;
    {
        const dskGameInterface::ViewScope scope(1);
        wnd = &WINDOWMANAGER.Show(std::make_unique<WndWithoutControls>(DrawPoint(20, 20)));
    }
    step(16);
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetTopMostWindow(1u) == wnd);
    BOOST_TEST_REQUIRE(!FocusPath::HasFocusableControl(wnd));
    // Die Leiste verspricht Y hier nicht (Befund B2).
    BOOST_TEST(!namesButton(view(1).GetBrief(), PadButton::Y));

    // UND JETZT DER DRUCK, den sie nicht verspricht.
    press(11, padHint::Enter);
    BOOST_TEST_MESSAGE("AUDIT: nach Y - Fokus aktiv = " << view(1).GetFocus().IsActive());
    // GENAU DAS WAR FALSCH: der Fokus stand danach nirgends mehr.
    BOOST_TEST(view(1).GetFocus().IsActive());
    BOOST_TEST(view(1).GetFocus().GetRoot() == static_cast<Window*>(menu));
    BOOST_TEST(view(1).GetFocus().GetFocused() == focusedBefore);
    // Der Spieler kann also weiterarbeiten - die Leiste ist die des RINGES, in dem er steht
    // (PHASE 13: das Systemmenue ist der Ring; dort schliesst B, statt nur den Fokus abzugeben).
    BOOST_TEST(view(1).GetRing().IsOpen());
    BOOST_TEST(hasHint(view(1).GetBrief(), PadButton::B, brief::KeyAction::CloseRing));
}

// ============================================================================================
// 21. BEFUND N6 - DER KLARTEXT LOG EINE EBENE TIEFER ALS DIE KORREKTUR
// ============================================================================================

/// DER SCHWERSTE BEFUND DIESER RUNDE, und er trifft genau die Stelle, um die es dem
/// Auftraggeber ging.
///
/// brief::ForAction(FlagMenuTab) war ein KONSTANTER Satz und kannte die Flaggenart nicht:
/// "... eine Strasse von ihr aus bauen, sie abreissen, einen Geologen rufen, einen Spaeher
/// aussenden." Der Reiterkopf ist die ERSTE Fokusstation nach Y, also das Allererste, was der
/// Spieler im Fenster liest - und an der HQ-Flagge steht dort GENAU EIN Knopf.
///
/// Gemessen wurde: am Knoten stand richtig "An DIESER Flagge gibt es keinen Geologen und keinen
/// Spaeher", ein Knopfdruck spaeter im Fenster stand das Gegenteil.
BOOST_FIXTURE_TEST_CASE(TheFlagMenuHeadNamesOnlyTheButtonsThatAreReallyThere, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint hqFlag = hqFlagOf(world, 1);
    takePad(11, 1);
    padSteerTo(11, 1, hqFlag);

    // Der Knotentext sagt es richtig - das war schon vorher so.
    const std::string atNode = view(1).GetBrief().joined();
    BOOST_TEST_MESSAGE("AUDIT: am Knoten (HQ-Flagge) = " << atNode);

    // EINEN KNOPFDRUCK SPAETER, auf dem Reiterkopf.
    press(11, padHint::OpenActions);
    BOOST_TEST_REQUIRE(view(1).actionwindow != static_cast<iwAction*>(nullptr));
    // Die Messung dazu: der Reiter traegt wirklich nur den Strassenknopf.
    BOOST_TEST_REQUIRE(flagTabButton(view(1), kFlagBtRoad) != static_cast<ctrlButton*>(nullptr));
    BOOST_TEST_REQUIRE(flagTabButton(view(1), kFlagBtGeologist) == static_cast<ctrlButton*>(nullptr));
    BOOST_TEST_REQUIRE(flagTabButton(view(1), kFlagBtScout) == static_cast<ctrlButton*>(nullptr));
    BOOST_TEST_REQUIRE(flagTabButton(view(1), kFlagBtPullDown) == static_cast<ctrlButton*>(nullptr));

    // PHASE 13 - WIE DIESER BEFUND JETZT GESICHERT IST.
    //
    // Der Kopftext war die Antwort auf die Frage "welche Knoepfe gibt es hier?", und er stand
    // auf dem Reiterkopf, weil das die erste Fokusstation nach Y war. Im RING gibt es diese
    // Station nicht mehr - die Reiterkoepfe sind die Blaetterachse (LB/RB), und die Antwort auf
    // dieselbe Frage steht nicht mehr in einem Satz, sondern IM BILD: der Ring zeigt genau die
    // Knoepfe, die es wirklich gibt, alle gleichzeitig. Das ist die staerkere Zusicherung, und
    // sie wird hier gemessen.
    //
    // Die Rechnung selbst (brief::ForFlagMenu, aus den Knoepfen gebaut statt aus der Flaggenart
    // geraten) bleibt unveraendert und wird gleich darunter geprueft.
    BOOST_TEST_REQUIRE(view(1).GetRing().IsOpen());
    unsigned numPages = 1;
    const std::vector<Window*> ringCtrls = dskGameInterface::RingPageCtrls(view(1), numPages);
    BOOST_TEST_MESSAGE("AUDIT: Sektoren des Rings an der HQ-Flagge = " << ringCtrls.size());
    // GENAU EIN Sektor - der Strassenknopf. Kein Geologe, kein Spaeher, kein Abreissen.
    BOOST_TEST(ringCtrls.size() == 1u);
    BOOST_TEST_REQUIRE(!ringCtrls.empty());
    BOOST_TEST(ringCtrls.front() == static_cast<Window*>(flagTabButton(view(1), kFlagBtRoad)));
    BOOST_TEST(numPages == 1u);
    // Und der Klartext unter dem Ring ist der des gewaehlten Sektors - er verspricht nichts,
    // was der Ring nicht zeigt.
    const std::string inWindow = view(1).GetBrief().joined();
    BOOST_TEST_MESSAGE("AUDIT: Klartext unter dem Ring an der HQ-Flagge = " << inWindow);
    BOOST_TEST(inWindow.find("Geolog") == std::string::npos);

    // DIE RECHNUNG SELBST, unveraendert aus Phase 12: der Kopftext wird aus den KNOEPFEN
    // gebaut, nicht aus der Flaggenart geraten.
    BOOST_TEST(brief::ForFlagMenu(hqFlagButtons()).joined() != brief::ForFlagMenu(plainFlagButtons()).joined());
    BOOST_TEST(brief::ForFlagMenu(hqFlagButtons())
                 .joined()
                 .find(brief::ForFlagMenu(plainFlagButtons()).lines.front())
               == std::string::npos);

    closeActionWindow(*this, view(1));
}

/// Die Gegenseite desselben Befunds an einer GEWOEHNLICHEN Flagge: dort zaehlt der Kopf alle
/// vier Knoepfe auf, und zwar genau die vier, die dort stehen. Ohne diesen Fall koennte der
/// Text ueberall schweigen und trotzdem gruen sein.
BOOST_FIXTURE_TEST_CASE(AtAnOrdinaryFlagTheSameHeadNamesAllFourButtons, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint flagPt = findPlainFlagSpot(world, view(1).GetViewer());
    BOOST_TEST_REQUIRE(flagPt.isValid());
    world.SetFlag(flagPt, 1);
    BOOST_TEST_REQUIRE((world.GetSpecObj<noFlag>(flagPt)->GetFlagType() == FlagType::Normal));
    takePad(11, 1);
    padSteerTo(11, 1, flagPt);
    press(11, padHint::OpenActions);
    BOOST_TEST_REQUIRE(view(1).GetRing().IsOpen());

    // PHASE 13: die Gegenseite desselben Befunds - hier zeigt der Ring VIER Sektoren, und zwar
    // genau die vier Knoepfe, die es an einer gewoehnlichen Flagge gibt. Ohne diesen Fall
    // koennte der Ring ueberall einen einzigen Sektor zeigen und trotzdem gruen sein.
    unsigned numPages = 1;
    const std::vector<Window*> ringCtrls = dskGameInterface::RingPageCtrls(view(1), numPages);
    BOOST_TEST_MESSAGE("AUDIT: Sektoren des Rings an gewoehnlicher Flagge = " << ringCtrls.size());
    BOOST_TEST(ringCtrls.size() == 4u);
    BOOST_TEST(numPages == 1u);
    // Und die vier Knoepfe stehen wirklich da - jeder als Sektor.
    for(const unsigned btId : {kFlagBtRoad, kFlagBtPullDown, kFlagBtGeologist, kFlagBtScout})
    {
        ctrlButton* const bt = flagTabButton(view(1), btId);
        BOOST_TEST_REQUIRE(bt != static_cast<ctrlButton*>(nullptr));
        BOOST_TEST((std::find(ringCtrls.begin(), ringCtrls.end(), static_cast<Window*>(bt)) != ringCtrls.end()));
    }
    BOOST_TEST(flagTabButton(view(1), kFlagBtWaterway) == static_cast<ctrlButton*>(nullptr));
    // Und die Rechnung aus Phase 12 unterscheidet die drei Flaggenarten weiterhin.
    BOOST_TEST(brief::ForFlagMenu(plainFlagButtons()).joined() != brief::ForFlagMenu(waterFlagButtons()).joined());

    closeActionWindow(*this, view(1));
}

/// Die MILDERE Haelfte von N6: an einer WASSERFLAGGE traegt der Reiter einen fuenften Knopf
/// (Wasserweg), und der Kopftext liess ihn aus.
///
/// GEFAHREN WIRD HIER NICHT MIT DEM PAD, und das steht ausdruecklich da: eine Wasserflagge
/// setzt eine Kueste voraus, und die Karte dieser Fixture hat keine. Gemessen wird stattdessen
/// GENAU DIE ABLEITUNG, um die es geht - der echte Konstruktor von iwAction baut den Reiter mit
/// FlagType::WaterFlag, und die echte iwAction::GetPadBrief liest daraus den Text. Was hier
/// fehlt, ist allein der Weg des Zeigers dorthin.
BOOST_FIXTURE_TEST_CASE(AtAWaterFlagTheHeadAlsoNamesTheWaterway, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint flagPt = findPlainFlagSpot(world, view(1).GetViewer());
    BOOST_TEST_REQUIRE(flagPt.isValid());

    iwAction::Tabs tabs;
    tabs.flag = true;
    iwAction wnd(*dsk, view(1).GetView(), tabs, flagPt, DrawPoint(0, 0),
                 iwAction::Params(iwAction::FlagType::WaterFlag),
                 /*military_buildings*/ false, iwAction::MousePointer::LeaveAlone);

    auto* const mainTab = wnd.GetCtrl<ctrlTab>(0);
    BOOST_TEST_REQUIRE(mainTab != static_cast<ctrlTab*>(nullptr));
    ctrlGroup* const group = mainTab->GetGroup(kTabFlag);
    BOOST_TEST_REQUIRE(group != static_cast<ctrlGroup*>(nullptr));
    // Der Wasserwegknopf ist wirklich da - das ist der Unterschied zur gewoehnlichen Flagge.
    BOOST_TEST_REQUIRE(group->GetCtrl<ctrlButton>(kFlagBtWaterway) != static_cast<ctrlButton*>(nullptr));

    // Der Reiterkopf traegt als Kennung seine Position im ctrlTab.
    Window* const head = mainTab->GetCtrl<Window>(0);
    BOOST_TEST_REQUIRE(head != static_cast<Window*>(nullptr));
    const brief::Brief b = wnd.GetPadBrief(head);
    BOOST_TEST_MESSAGE("AUDIT: Reiterkopf an einer Wasserflagge = " << b.joined());
    BOOST_TEST(b.joined() == brief::ForFlagMenu(waterFlagButtons()).joined());
    BOOST_TEST(b.joined() != brief::ForFlagMenu(plainFlagButtons()).joined());
}

/// DER TEXT SELBST, in der Sprache, die der Auftraggeber liest. Reine Rechnung, ausdruecklich -
/// hier wird kein Knopf gedrueckt, sondern nachgelesen, was auf dem Schirm steht.
BOOST_AUTO_TEST_CASE(TheFlagMenuTextIsBuiltFromTheButtonsAndNotFromAFixedSentence)
{
    static_cast<void>(LANGUAGES); // bindtextdomain, sonst liefert _() stumm die msgid
    {
        const rttr::test::LocaleResetter noCatalog("ja"); // kein Katalog -> die msgid, also Englisch
        const std::string hq = brief::ForFlagMenu(hqFlagButtons()).joined();
        const std::string plain = brief::ForFlagMenu(plainFlagButtons()).joined();
        const std::string water = brief::ForFlagMenu(waterFlagButtons()).joined();
        BOOST_TEST_MESSAGE("AUDIT: HQ     = " << hq);
        BOOST_TEST_MESSAGE("AUDIT: Flagge = " << plain);
        BOOST_TEST_MESSAGE("AUDIT: Wasser = " << water);
        // Die HQ-Flagge nennt weder Geologen noch Spaeher noch Abreissen als HANDLUNG ...
        BOOST_TEST(hq.find("call in a geologist") == std::string::npos);
        BOOST_TEST(hq.find("send out a scout") == std::string::npos);
        BOOST_TEST(hq.find("tear it down") == std::string::npos);
        // ... und schickt den Spieler dorthin, wo es sie gibt.
        BOOST_TEST(hq.find("ordinary flag") != std::string::npos);
        // Die gewoehnliche Flagge nennt sie, den Wasserweg aber nicht.
        BOOST_TEST(plain.find("call in a geologist") != std::string::npos);
        BOOST_TEST(plain.find("send out a scout") != std::string::npos);
        BOOST_TEST(plain.find("waterway") == std::string::npos);
        BOOST_TEST(plain.find("ordinary flag") == std::string::npos);
        // Die Wasserflagge nennt ihn.
        BOOST_TEST(water.find("waterway") != std::string::npos);
    }
    {
        const rttr::test::LocaleResetter german("de");
        BOOST_TEST_REQUIRE(std::string(_("Flag menu")) != "Flag menu");
        const std::string hq = brief::ForFlagMenu(hqFlagButtons()).joined();
        const std::string plain = brief::ForFlagMenu(plainFlagButtons()).joined();
        BOOST_TEST_MESSAGE("AUDIT (de): HQ     = " << hq);
        BOOST_TEST_MESSAGE("AUDIT (de): Flagge = " << plain);
        BOOST_TEST(hq.find("Geologen rufen") == std::string::npos);
        BOOST_TEST(plain.find("Geologen rufen") != std::string::npos);
    }
}

// ============================================================================================
// 22. BEFUND N7 - DIE SCHLEIFE, DIE WIRKLICH ZEICHNET
// ============================================================================================

/// DER BEFUND: der Waechter aus B4 deckte LayoutBrief ab, nicht den Zeichner. Der Nachpruefer
/// hat LayoutBrief unangetastet gelassen und in DrawBrief die LETZTE Zeile nicht mehr
/// gezeichnet - 316 Faelle blieben gruen.
///
/// Der Grund war, dass es nichts zu messen gab: der DummyRenderer verwirft jeden Zeichenaufruf.
/// Jetzt gibt es etwas zu messen - dskGameInterface::EmitBriefLines IST die Schleife, die im
/// Spiel den Zeichenaufruf ausloest, und dieser Fall reicht ihr seinen eigenen Ausgeber herein.
///
/// WAS DAMIT UNGEDECKT BLEIBT, und das ist der ehrliche Rest: der Ausgeber, den DrawBrief selbst
/// hereinreicht, ist ein einziger font.Draw-Aufruf ohne Verzweigung. Dass daraus am Fernseher
/// Buchstaben werden, kann in dieser Umgebung kein Fall sehen.
BOOST_FIXTURE_TEST_CASE(TheDrawingLoopItselfEmitsEveryLineIncludingTheKeyBar, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint spot = findBuildSpot(world, view(1).GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(spot.isValid());
    takePad(11, 1);
    padSteerTo(11, 1, spot);
    BOOST_TEST_REQUIRE(!view(1).GetBrief().keys.empty());

    const auto layout = dsk->LayoutBrief(view(1));
    BOOST_TEST_REQUIRE(layout.lines.size() >= 2u);

    // Der eigene Ausgeber an derselben Schleife, die DrawBrief benutzt.
    std::vector<std::string> emittedText;
    std::vector<unsigned> emittedColor;
    std::vector<int> emittedY;
    dskGameInterface::EmitBriefLines(layout, [&](const DrawPoint& pos, const dskGameInterface::BriefLine& line) {
        emittedText.push_back(line.text);
        emittedColor.push_back(line.color);
        emittedY.push_back(pos.y);
    });
    BOOST_TEST_MESSAGE("AUDIT: ausgegeben = " << emittedText.size() << " von " << layout.lines.size() << " Zeilen");

    // (1) JEDE Zeile geht hindurch - keine faellt hinten (oder vorn) heraus.
    BOOST_TEST_REQUIRE(emittedText.size() == layout.lines.size());
    for(std::size_t i = 0; i < layout.lines.size(); ++i)
    {
        BOOST_TEST_CONTEXT("Zeile " << i)
        {
            BOOST_TEST(emittedText[i] == layout.lines[i].text);
            BOOST_TEST(emittedColor[i] == layout.lines[i].color);
            // ... und an der Stelle, an der sie stehen soll.
            BOOST_TEST(emittedY[i] == layout.textOrigin.y + static_cast<int>(i * layout.lineHeight));
        }
    }

    // (2) DIE TASTENZEILE ist eine davon - erkennbar an ihrer Farbe - und sie ist die LETZTE.
    //     Genau die hat die Sabotage des Nachpruefers weggelassen.
    BOOST_TEST_REQUIRE(!emittedColor.empty());
    BOOST_TEST(emittedColor.back() == dskGameInterface::keyLineColor);
    const auto numKeyLines =
      static_cast<unsigned>(std::count(emittedColor.begin(), emittedColor.end(), dskGameInterface::keyLineColor));
    BOOST_TEST(numKeyLines >= 1u);
    std::string drawn;
    for(std::size_t i = 0; i < emittedText.size(); ++i)
    {
        if(emittedColor[i] == dskGameInterface::keyLineColor)
            drawn += emittedText[i] + " ";
    }
    BOOST_TEST_MESSAGE("AUDIT: ausgegebene Tastenzeile = " << drawn);
    for(const brief::KeyHint& hint : view(1).GetBrief().keys)
    {
        BOOST_TEST_CONTEXT(brief::PadButtonLabel(hint.button))
        BOOST_TEST(drawn.find(brief::KeyLabel(hint.action)) != std::string::npos);
    }

    // (3) Und der ECHTE Zeichner laeuft ueber dieselbe Schleife.
    BOOST_TEST_CHECKPOINT("DrawBrief");
    dsk->DrawBrief(view(1));
}


// ============================================================================================
// 23. DER WEG ZUM GEOLOGEN, LAUT VORGELESEN
// ============================================================================================

/// KEIN NEUER MECHANISMUS - dieser Fall ist die ABNAHME. Er faehrt den Weg ab, an dem der
/// Auftraggeber steckengeblieben ist ("Um Eisenerz zu finden soll ich einen Gelehrten
/// losschicken, da ist noch nicht genau klar wie ich das mache"), und schreibt in das Protokoll,
/// was auf dem Schirm steht - Schritt fuer Schritt, auf Deutsch, in der Reihenfolge, in der er
/// es liest.
///
/// Gefahren wird BEIDES: die HQ-Flagge, die er zu Spielbeginn als einzige hat und an der es den
/// Geologen NICHT gibt, und eine gewoehnliche Flagge, an der es ihn gibt. Der Unterschied
/// zwischen den beiden Protokollen ist Befund N6.
BOOST_FIXTURE_TEST_CASE(TheWholeWayToTheGeologistReadAloud, HintFixture<2>)
{
    static_cast<void>(LANGUAGES);
    const rttr::test::LocaleResetter german("de");

    GameWorld& world = worldFixture.world;
    takePad(11, 1);

    // --- (I) DIE HQ-FLAGGE -------------------------------------------------------------------
    const MapPoint hqFlag = hqFlagOf(world, 1);
    padSteerTo(11, 1, hqFlag);
    BOOST_TEST_MESSAGE("=== HQ-FLAGGE, Schritt 1: der Zeiger steht auf der Flagge ===");
    BOOST_TEST_MESSAGE("  " << view(1).GetBrief().joined());
    BOOST_TEST_MESSAGE("  [" << brief::KeyLine(view(1).GetBrief().keys) << "]");

    press(11, padHint::OpenActions); // "RB Aktionen"
    BOOST_TEST_REQUIRE(view(1).actionwindow != static_cast<iwAction*>(nullptr));
    BOOST_TEST_REQUIRE(view(1).GetFocus().IsActive()); // PHASE 13: der Ring ist sofort betreten
    BOOST_TEST_MESSAGE("=== HQ-FLAGGE, Schritt 2: RB - der Ring steht offen, der Fokus im Ring ===");
    BOOST_TEST_MESSAGE("  " << view(1).GetBrief().joined());
    BOOST_TEST_MESSAGE("  [" << brief::KeyLine(view(1).GetBrief().keys) << "]");
    // Hier gibt es keinen Geologenknopf - und der Text sagt es jetzt auch.
    BOOST_TEST(flagTabButton(view(1), kFlagBtGeologist) == static_cast<ctrlButton*>(nullptr));
    BOOST_TEST(view(1).GetBrief().joined().find("Geologen rufen") == std::string::npos);
    closeActionWindow(*this, view(1));
    step(16);

    // --- (II) EINE GEWOEHNLICHE FLAGGE -------------------------------------------------------
    const MapPoint flagPt = findPlainFlagSpot(world, view(1).GetViewer());
    BOOST_TEST_REQUIRE(flagPt.isValid());
    world.SetFlag(flagPt, 1); // Testaufbau: die Flagge, die der Spieler sich vorher baut
    padSteerTo(11, 1, flagPt);
    BOOST_TEST_MESSAGE("=== FLAGGE, Schritt 1: der Zeiger steht auf der eigenen Flagge ===");
    BOOST_TEST_MESSAGE("  " << view(1).GetBrief().joined());
    BOOST_TEST_MESSAGE("  [" << brief::KeyLine(view(1).GetBrief().keys) << "]");

    press(11, padHint::OpenActions);
    BOOST_TEST_REQUIRE(view(1).actionwindow != static_cast<iwAction*>(nullptr));
    BOOST_TEST_REQUIRE(view(1).GetRing().IsOpen());
    BOOST_TEST_MESSAGE("=== FLAGGE, Schritt 2: RB - der Ring steht offen und ist betreten ===");
    BOOST_TEST_MESSAGE("  " << view(1).GetBrief().joined());
    BOOST_TEST_MESSAGE("  [" << brief::KeyLine(view(1).GetBrief().keys) << "]");

    ctrlButton* const geologist = flagTabButton(view(1), kFlagBtGeologist);
    BOOST_TEST_REQUIRE(geologist != static_cast<ctrlButton*>(nullptr));
    unsigned steps = 0;
    while(view(1).GetFocus().GetFocused() != geologist && steps < 32u)
    {
        press(11, PadButton::DpadRight);
        ++steps;
        BOOST_TEST_MESSAGE("=== FLAGGE, Schritt 2+" << steps << ": der Ring dreht sich ===");
        BOOST_TEST_MESSAGE("  " << view(1).GetBrief().joined());
        BOOST_TEST_MESSAGE("  [" << brief::KeyLine(view(1).GetBrief().keys) << "]");
    }
    BOOST_TEST_REQUIRE(view(1).GetFocus().GetFocused() == static_cast<Window*>(geologist));
    BOOST_TEST_MESSAGE("=== FLAGGE: der Sektor des Geologen ist gewaehlt nach " << steps
                                                                               << " x Steuerkreuz ===");
    // Dass A hier wirklich den Mann losschickt, misst testPadFlagActions.cpp in einer laufenden
    // Partie. Dieser Fall misst, was der Spieler dabei LIEST - das ist der Gegenstand dieser Phase.
    BOOST_TEST(view(1).GetBrief().joined() == brief::ForAction(brief::ActionBrief::CallGeologist).joined());
    BOOST_TEST(hasHint(view(1).GetBrief(), PadButton::A, brief::KeyAction::Choose));

    closeActionWindow(*this, view(1));
}

// ============================================================================================
// 24. BEFUND P1 - "A VERLAENGERN" AM WEGENDE
// ============================================================================================

/// DER HAEUFIGSTE WEG EINES ANFAENGERS UEBERHAUPT: A auf der eigenen Flagge, der Baumodus laeuft,
/// der Zeiger steht noch auf der Flagge - und A tut dort GAR NICHTS.
///
/// Gemessen wird das Nichts vollstaendig: Modus, Streckenlaenge, Wegende, Startpunkt, der
/// Ablehnungszaehler und der Klartext samt Tastenzeile bleiben Zeichen fuer Zeichen dieselben.
/// Erst wenn der Zeiger einen Knoten weiter steht, verspricht die Leiste "A Verlaengern" - und
/// dann waechst die Strecke auch wirklich.
BOOST_FIXTURE_TEST_CASE(AtTheRoadEndTheBarKeepsQuietAboutAAndAReallyDoesNothing, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint flagPt = hqFlagOf(world, 1);
    takePad(11, 1);
    padSteerTo(11, 1, flagPt);
    press(11, padRoad::Begin);
    BOOST_TEST_REQUIRE((view(1).GetRoad().mode == RoadBuildMode::Normal));
    // GENAU DER ZUSTAND DES BEFUNDS: der Zeiger steht auf dem Wegende.
    BOOST_TEST_REQUIRE((view(1).GetView().GetSelectedPt() == view(1).GetRoad().point));

    const brief::Brief& b = view(1).GetBrief();
    BOOST_TEST_MESSAGE("AUDIT: Leiste am Wegende = " << dumpKeys(b));
    BOOST_TEST(!namesButton(b, PadButton::A));

    // UND JETZT WIRD A GEDRUECKT. Alles, was ein Spieler daran merken koennte, vorher gemerkt.
    const RoadBuildMode modeBefore = view(1).GetRoad().mode;
    const auto lenBefore = view(1).GetRoad().route.size();
    const MapPoint pointBefore = view(1).GetRoad().point;
    const MapPoint startBefore = view(1).GetRoad().start;
    const unsigned rejBefore = view(1).GetRejectionCount();
    const std::string textBefore = b.joined();
    const std::string lineBefore = brief::KeyLine(b.keys);

    press(11, padRoad::Extend);

    BOOST_TEST((view(1).GetRoad().mode == modeBefore));
    BOOST_TEST(view(1).GetRoad().route.size() == lenBefore);
    BOOST_TEST((view(1).GetRoad().point == pointBefore));
    BOOST_TEST((view(1).GetRoad().start == startBefore));
    // Kein Schritt UND keine Ablehnungsmeldung - genau das macht es zur Luege und nicht zur
    // blossen Unvollstaendigkeit.
    BOOST_TEST(view(1).GetRejectionCount() == rejBefore);
    BOOST_TEST(!view(1).GetRejection().has_value());
    BOOST_TEST(view(1).GetBrief().joined() == textBefore);
    BOOST_TEST(brief::KeyLine(view(1).GetBrief().keys) == lineBefore);

    // DIE GEGENRICHTUNG, damit der neue Eintrag nicht einfach ein weggelassener ist: einen
    // Knoten weiter steht A wieder da, und dann waechst die Strecke auch.
    padSteerTo(11, 1, world.GetNeighbour(flagPt, Direction::East));
    BOOST_TEST_MESSAGE("AUDIT: Leiste einen Knoten weiter = " << dumpKeys(view(1).GetBrief()));
    BOOST_TEST_REQUIRE(hasHint(view(1).GetBrief(), PadButton::A, brief::KeyAction::ExtendRoad));
    press(11, padRoad::Extend);
    BOOST_TEST(view(1).GetRoad().route.size() == lenBefore + 1u);
}

// ============================================================================================
// 25. BEFUND P1b - AUF EINEM SCHON GELEGTEN STUECK BAUT A ZURUECK
// ============================================================================================

/// In derselben Messung gefunden: zeigt der Spieler auf ein Stueck, das er selbst schon gelegt
/// hat, baut A die Vorschau BIS DORTHIN ZURUECK (PadExtendRoad -> GetIdInCurBuildRoad ->
/// DemolishRoad). "A Verlaengern" war dort genauso falsch wie am Wegende - nur in die andere
/// Richtung.
BOOST_FIXTURE_TEST_CASE(PointingBackAtALaidPieceTheBarSaysBackToHereAndAReallyShortens, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint flagPt = hqFlagOf(world, 1);
    const MapPoint step1 = world.GetNeighbour(flagPt, Direction::East);
    const MapPoint step2 = world.GetNeighbour(step1, Direction::East);
    takePad(11, 1);
    padSteerTo(11, 1, flagPt);
    press(11, padRoad::Begin);
    padSteerTo(11, 1, step1);
    press(11, padRoad::Extend);
    padSteerTo(11, 1, step2);
    press(11, padRoad::Extend);
    BOOST_TEST_REQUIRE(view(1).GetRoad().route.size() == 2u);

    // Zurueck auf das erste Stueck - es liegt auf der eigenen Strecke und ist NICHT das Wegende.
    padSteerTo(11, 1, step1);
    BOOST_TEST_REQUIRE((view(1).GetRoad().point != step1));
    const brief::Brief& b = view(1).GetBrief();
    BOOST_TEST_MESSAGE("AUDIT: Leiste auf dem eigenen Wegstueck = " << dumpKeys(b));
    BOOST_TEST(hasHint(b, PadButton::A, brief::KeyAction::ShortenRoad));
    BOOST_TEST(!hasHint(b, PadButton::A, brief::KeyAction::ExtendRoad));

    // UND GEDRUECKT: die Strecke wird kuerzer, der Modus laeuft weiter.
    press(11, padRoad::Extend);
    BOOST_TEST(view(1).GetRoad().route.size() == 1u);
    BOOST_TEST((view(1).GetRoad().point == step1));
    BOOST_TEST((view(1).GetRoad().mode == RoadBuildMode::Normal));
}

// ============================================================================================
// 26. BEFUND P3 - "A WAEHLEN" AUF DEM SCHON GEWAEHLTEN REITERKOPF
// ============================================================================================

/// Der Reiterkopf ist die ERSTE Fokusstation nach Y in jedem Aktionsfenster - das Allererste
/// also, was ein Padspieler dort liest und ausprobiert. Auf dem SCHON GEWAEHLTEN Kopf bewirkt A
/// nichts: ctrlTab::SetSelection setzt Schritt fuer Schritt dieselben Werte noch einmal.
///
/// Gemessen wird beides: auf dem gewaehlten Kopf schweigt die Leiste und der Druck laesst das
/// Fenster unveraendert; auf dem NACHBARKOPF steht A da und schaltet den Reiter wirklich um.
BOOST_FIXTURE_TEST_CASE(OnTheAlreadyChosenTabHeadTheBarKeepsQuietAboutA, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint flagPt = findPlainFlagSpot(world, view(1).GetViewer());
    BOOST_TEST_REQUIRE(flagPt.isValid());
    world.SetFlag(flagPt, 1);
    takePad(11, 1);
    padSteerTo(11, 1, flagPt);
    // PHASE 13: der Ring zeigt keine Reiterkoepfe mehr - er BLAETTERT sie (LB/RB). Der Fall
    // misst weiterhin den Reiterkopf selbst, und dafuer muss das Aktionsfenster wie ein
    // gewoehnliches betreten werden. Erreichbar ist das ueber den ANGEHEFTETEN Zustand: B
    // schliesst dann nur den Ring, das Fenster bleibt sichtbar stehen, und Y fuehrt hinein.
    press(11, padHint::OpenActions);
    BOOST_TEST_REQUIRE(view(1).actionwindow != static_cast<iwAction*>(nullptr));
    view(1).actionwindow->SetPinned(true);
    press(11, padHint::Back);
    BOOST_TEST_REQUIRE(!view(1).GetRing().IsOpen());
    BOOST_TEST_REQUIRE(view(1).actionwindow->IsVisible());
    press(11, padHint::Enter);
    BOOST_TEST_REQUIRE(view(1).GetFocus().IsActive());
    BOOST_TEST_REQUIRE(view(1).GetFocus().GetRoot() == static_cast<Window*>(view(1).actionwindow));

    auto* const mainTab = view(1).actionwindow->GetCtrl<ctrlTab>(0);
    BOOST_TEST_REQUIRE(mainTab != static_cast<ctrlTab*>(nullptr));
    BOOST_TEST_REQUIRE(mainTab->GetNumTabs() >= 2u);
    // Die erste Fokusstation nach Y IST der Kopf des gewaehlten Reiters - gemessen, nicht
    // angenommen.
    const Window* const firstStation = view(1).GetFocus().GetFocused();
    BOOST_TEST_REQUIRE(firstStation == static_cast<const Window*>(mainTab->GetCtrl<ctrlButton>(0)));
    BOOST_TEST_REQUIRE(mainTab->GetCurrentTab() == mainTab->GetTabIdAt(0));

    const brief::Brief& b = view(1).GetBrief();
    BOOST_TEST_MESSAGE("AUDIT: Leiste auf dem gewaehlten Reiterkopf = " << dumpKeys(b));
    BOOST_TEST(!namesButton(b, PadButton::A));

    // UND GEDRUECKT: nichts aendert sich.
    const unsigned tabBefore = mainTab->GetCurrentTab();
    const Extent sizeBefore = view(1).actionwindow->GetSize();
    const std::string textBefore = b.joined();
    press(11, padHint::Act);
    BOOST_TEST(mainTab->GetCurrentTab() == tabBefore);
    BOOST_TEST((view(1).actionwindow->GetSize() == sizeBefore));
    BOOST_TEST(view(1).GetFocus().GetFocused() == firstStation);
    BOOST_TEST(view(1).GetBrief().joined() == textBefore);

    // DIE GEGENRICHTUNG: auf dem NACHBARKOPF steht A sehr wohl - und schaltet wirklich um.
    focusToCtrl(11, 1, mainTab->GetCtrl<ctrlButton>(1));
    BOOST_TEST_MESSAGE("AUDIT: Leiste auf dem zweiten Reiterkopf = " << dumpKeys(view(1).GetBrief()));
    BOOST_TEST_REQUIRE(hasHint(view(1).GetBrief(), PadButton::A, brief::KeyAction::Choose));
    press(11, padHint::Act);
    BOOST_TEST(mainTab->GetCurrentTab() == mainTab->GetTabIdAt(1));
    BOOST_TEST(mainTab->GetCurrentTab() != tabBefore);

    view(1).actionwindow->SetPinned(false);
    closeActionWindow(*this, view(1));
}

// ============================================================================================
// 27. BEFUND P4 - DER ANGRIFFSREITER OHNE EINEN EINZIGEN SOLDATEN
// ============================================================================================

/// DERSELBE BAU WIE N6, AN EINER ZWEITEN STELLE. Der Kopftext des Angriffsreiters versprach eine
/// Soldatenwahl. Bei null erreichbaren Soldaten traegt der Reiter aber GAR KEINEN Knopf -
/// iwAction::AddAttackControls legt dann nur einen ctrlText an ("Attack not possible.").
///
/// Der Aufbau ist das feindliche Hauptquartier: ComputeActionOptions setzt tabs.attack an einem
/// sichtbaren fremden HQ, und der Spieler in dieser Ansicht hat kein einziges Militaergebaeude
/// in Reichweite - GetNumSoldiersForAttack liefert null.
BOOST_FIXTURE_TEST_CASE(TheAttackTabWithoutSoldiersNoLongerPromisesAChoice, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint enemyHQ = world.GetPlayer(0).GetHQPos();
    // Testaufbau: der Spieler muss das fremde HQ SEHEN, sonst gibt es den Reiter gar nicht.
    world.MakeVisibleAroundPoint(enemyHQ, 3, 1);
    BOOST_TEST_REQUIRE((view(1).GetViewer().GetVisibility(enemyHQ) == Visibility::Visible));
    BOOST_TEST_REQUIRE(view(1).GetViewer().GetNumSoldiersForAttack(enemyHQ) == 0u);

    takePad(11, 1);
    padSteerTo(11, 1, enemyHQ);
    press(11, padHint::OpenActions);
    BOOST_TEST_REQUIRE(view(1).actionwindow != static_cast<iwAction*>(nullptr));
    auto* const mainTab = view(1).actionwindow->GetCtrl<ctrlTab>(0);
    BOOST_TEST_REQUIRE(mainTab != static_cast<ctrlTab*>(nullptr));

    press(11, padHint::Enter);
    BOOST_TEST_REQUIRE(view(1).GetFocus().IsActive());
    // Die erste Station ist der Kopf des Angriffsreiters.
    BOOST_TEST_REQUIRE(view(1).GetFocus().GetFocused()
                       == static_cast<Window*>(mainTab->GetCtrl<ctrlButton>(0)));

    // UND DIE MESSUNG, die den Befund ueberhaupt erst zu einem macht: hinter diesem Kopf steht
    // kein einziger Knopf.
    const ctrlGroup* const group = mainTab->GetGroup(mainTab->GetTabIdAt(0));
    BOOST_TEST_REQUIRE(group != static_cast<const ctrlGroup*>(nullptr));
    for(const unsigned id : {1u, 2u, 4u, 10u, 11u, 12u, 13u})
        BOOST_TEST(group->GetCtrl<ctrlButton>(id) == static_cast<const ctrlButton*>(nullptr));
    BOOST_TEST(group->GetCtrl<ctrlOptionGroup>(3) == static_cast<const ctrlOptionGroup*>(nullptr));

    const brief::Brief& b = view(1).GetBrief();
    BOOST_TEST_MESSAGE("AUDIT: Klartext auf dem Angriffsreiter ohne Soldaten = " << b.joined());
    const brief::Brief expected = brief::ForAttackMenu(brief::AttackMenuButtons());
    BOOST_TEST(b.title == expected.title);
    BOOST_TEST(b.lines == expected.lines, boost::test_tools::per_element());
    // UND ES IST NICHT MEHR DER SATZ MIT DER SOLDATENWAHL: derselbe Reiter mit Knoepfen sagt
    // etwas anderes.
    brief::AttackMenuButtons full;
    full.fewer = full.more = full.strength = full.attack = true;
    full.quickPicks = 4;
    BOOST_TEST(b.joined() != brief::ForAttackMenu(full).joined());
    BOOST_TEST(brief::ForAttackMenu(full).title == expected.title);

    closeActionWindow(*this, view(1));
}

// ============================================================================================
// 28. BEFUND P2 - WAS DER AUSGEBER WIRKLICH AUSGIBT
// ============================================================================================

/// DER BEFUND, woertlich vom Nachpruefer: der Waechter aus B4 deckt LayoutBrief, nicht DrawBrief.
/// Seine Gegenprobe - "LayoutBrief unangetastet, in DrawBrief die letzte Zeile nicht mehr
/// zeichnen" - blieb in allen 328 Faellen gruen. Ungedeckt war namentlich der font.Draw-Aufruf.
///
/// DIESER FALL SCHLIESST DIE LUECKE, und zwar am tiefstmoeglichen Punkt: er faengt die
/// OpenGL-Aufrufe ab, mit denen glFont::Draw jede einzelne Zeile ausgibt (glColor4ub setzt die
/// Farbe, glDrawArrays gibt vier Eckpunkte je Zeichen aus - glFont.cpp, Ende von Draw). Der
/// DummyRenderer verwirft zwar jedes Bild, aber die AUFRUFE laufen wirklich, und sie tragen
/// Farbe und Zeichenzahl. Genau daran ist abzulesen, welche Zeilen den Bildschirm erreicht
/// haben.
///
/// Gestubbt wird mit RTTR_STUB_FUNCTION - derselbe Weg, den tests/s25Main/UI/testSmartBitmap.cpp
/// schon geht; die Attrappe wird beim Verlassen des Blocks wieder abgeraeumt.
BOOST_FIXTURE_TEST_CASE(TheDrawnLinesReallyLeaveTheEmitter, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint spot = findBuildSpot(world, view(1).GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(spot.isValid());
    takePad(11, 1);
    padSteerTo(11, 1, spot);
    BOOST_TEST_REQUIRE(!view(1).GetBrief().keys.empty());

    // Einmal warmzeichnen: der erste Aufruf legt die Schrifttextur an (glFont::Draw ->
    // GetTexture), und das soll die Messung nicht mitzaehlen.
    dsk->DrawBrief(view(1));

    const auto layout = dsk->LayoutBrief(view(1));
    BOOST_TEST_REQUIRE(layout.lines.size() >= 2u);

    briefEmitTap::reset();
    {
        RTTR_STUB_FUNCTION(glColor4ub, briefEmitTap::glColor4ub);
        RTTR_STUB_FUNCTION(glDrawArrays, briefEmitTap::glDrawArrays);
        dsk->DrawBrief(view(1));
    }

    // Was LayoutBrief liefert, und was der Ausgeber wirklich losgeschickt hat.
    std::vector<briefEmitTap::Emitted> expected;
    for(const auto& line : layout.lines)
    {
        if(line.text.empty())
            continue; // glFont::Draw steigt bei leerem Text aus, bevor es zeichnet
        expected.push_back(briefEmitTap::Emitted{line.color, numCodepoints(line.text)});
    }
    for(const auto& e : briefEmitTap::emitted)
        BOOST_TEST_MESSAGE("AUDIT: gezeichnet - Farbe " << e.color << ", " << e.glyphs << " Zeichen");
    BOOST_TEST_REQUIRE(briefEmitTap::emitted.size() == expected.size());
    for(std::size_t i = 0; i < expected.size(); ++i)
    {
        BOOST_TEST_CONTEXT("Zeile " << i << " (" << layout.lines[i].text << ")")
        {
            BOOST_TEST(briefEmitTap::emitted[i].color == expected[i].color);
            BOOST_TEST(briefEmitTap::emitted[i].glyphs == expected[i].glyphs);
        }
    }

    // UND DIE TASTENZEILE IST DARUNTER: die LETZTEN ausgegebenen Zeilen tragen die Farbe der
    // Leiste. Genau die hat die Gegenprobe des Nachpruefers entfernt. Es koennen mehrere sein -
    // LayoutBrief bricht die Leiste um, statt sie abzuschneiden.
    const auto numKeyLines = static_cast<std::size_t>(
      std::count_if(layout.lines.begin(), layout.lines.end(),
                    [](const auto& l) { return l.color == dskGameInterface::keyLineColor; }));
    BOOST_TEST_REQUIRE(numKeyLines >= 1u);
    BOOST_TEST_REQUIRE(briefEmitTap::emitted.size() > numKeyLines);
    BOOST_TEST(briefEmitTap::emitted.back().color == dskGameInterface::keyLineColor);
    for(std::size_t i = 0; i < briefEmitTap::emitted.size(); ++i)
    {
        const bool isKeyLine = i >= briefEmitTap::emitted.size() - numKeyLines;
        BOOST_TEST_CONTEXT("Ausgegebene Zeile " << i)
        BOOST_TEST((briefEmitTap::emitted[i].color == dskGameInterface::keyLineColor) == isKeyLine);
    }

    // Und ein Block OHNE Leiste gibt genau diese Zeilen NICHT aus - sonst waere die Messung oben
    // blind gegen eine Zeile, die immer da ist.
    brief::Brief withoutKeys = view(1).GetBrief();
    withoutKeys.keys.clear();
    view(1).SetBrief(withoutKeys);
    briefEmitTap::reset();
    {
        RTTR_STUB_FUNCTION(glColor4ub, briefEmitTap::glColor4ub);
        RTTR_STUB_FUNCTION(glDrawArrays, briefEmitTap::glDrawArrays);
        dsk->DrawBrief(view(1));
    }
    BOOST_TEST(briefEmitTap::emitted.size() + numKeyLines == expected.size());
    for(const auto& e : briefEmitTap::emitted)
        BOOST_TEST(e.color != dskGameInterface::keyLineColor);
}

// ============================================================================================
// 26. BEFUND K5 - DIE PHASE-12-DECKUNG AUF DER KNOPFREIHE DES FLAGGENREITERS
// ============================================================================================

/// DER BEFUND: vier Phase-12-Nachweise wurden vom AKTIONSFENSTER auf das POSTFENSTER umgehaengt,
/// und im letzten fielen dabei die drei Zeilen weg, die den Fokus auf den GEOLOGENKNOPF
/// setzten - die Knopfreihe des Flaggenreiters, vier Knoepfe nebeneinander. Genau der Knopf,
/// um den Phase 12 gebaut wurde. Die Zusicherungen liefen weiter, aber nicht mehr auf dem
/// Fenster, dem ein Anfaenger begegnet.
///
/// WARUM DIE VIER FAELLE TROTZDEM AUF DEM POSTFENSTER BLEIBEN, und das ist gemessen und nicht
/// gemeint: das Aktionsfenster ist fuer einen Padspieler seit Phase 13 KEIN Gitterfenster mehr.
/// Es geht nur noch als RING auf (dskGameInterface::PadOpenActionWindow -> OpenRing), und
/// solange der Ring offen ist, ist er fuer seinen Sitzplatz modal - auch Y wird verbraucht.
/// Die erste Haelfte dieses Falls MISST das. Ein Nachweis, der im Aktionsfenster
/// KeyAction::MoveFocus erwartet, kann dort also nicht mehr laufen; er wuerde nicht die alte
/// Frage stellen, sondern eine falsche. Das Postfenster ist damit nicht der "bessere" Traeger,
/// sondern der einzige verbliebene Gitterweg, den ein Anfaenger nimmt - er liegt hinter dem
/// Postsektor des Back-Rings.
///
/// UND DIE VERLORENE DECKUNG KEHRT HIER ZURUECK, auf ihrem eigenen Traeger: die vier Knoepfe des
/// Flaggenreiters, mit dem Geologen darunter, so wie ein Padspieler sie HEUTE erreicht. Die
/// Frage ist woertlich die aus Phase 12 - stimmt die Behauptung der Leiste in JEDER Richtung mit
/// der Wirkung ueberein? -, nur heisst die Wirkung im Ring TurnRing statt MoveFocus.
BOOST_FIXTURE_TEST_CASE(OnTheFlagButtonRowTheBarSaysExactlyWhatEveryDpadPressDoes, HintFixture<2>)
{
    GameWorld& world = worldFixture.world;
    const MapPoint flagPt = findPlainFlagSpot(world, view(1).GetViewer());
    BOOST_TEST_REQUIRE(flagPt.isValid());
    world.SetFlag(flagPt, 1);
    takePad(11, 1);
    padSteerTo(11, 1, flagPt);

    press(11, padHint::OpenActions);
    BOOST_TEST_REQUIRE(view(1).actionwindow != static_cast<iwAction*>(nullptr));
    BOOST_TEST_REQUIRE(view(1).GetRing().IsOpen());

    // --- (a) DIE MESSUNG, die die Umhaengung begruendet: das Aktionsfenster ist fuer das Pad
    //         kein Gitterfenster mehr. Y kommt nicht hinein, weil es schon drin ist - und der
    //         Ring verbraucht die Flanke.
    const Window* const rootBefore = view(1).GetFocus().GetRoot();
    const Window* const focusBefore = view(1).GetFocus().GetFocused();
    BOOST_TEST_REQUIRE(rootBefore == static_cast<Window*>(view(1).actionwindow));
    BOOST_TEST(!namesButton(view(1).GetBrief(), PadButton::Y));
    press(11, padHint::Enter);
    BOOST_TEST(view(1).GetRing().IsOpen());
    BOOST_TEST(view(1).GetFocus().GetRoot() == rootBefore);
    BOOST_TEST(view(1).GetFocus().GetFocused() == focusBefore);

    // --- (b) DIE KNOPFREIHE selbst: vier Knoepfe, und der Geologe ist einer davon.
    unsigned numPages = 1;
    const std::vector<Window*> ringCtrls = dskGameInterface::RingPageCtrls(view(1), numPages);
    BOOST_TEST_MESSAGE("AUDIT: Sektoren des Flaggenreiters = " << ringCtrls.size() << ", Seiten = " << numPages);
    BOOST_TEST_REQUIRE(ringCtrls.size() == 4u);
    ctrlButton* const geologist = flagTabButton(view(1), kFlagBtGeologist);
    BOOST_TEST_REQUIRE(geologist != static_cast<ctrlButton*>(nullptr));
    BOOST_TEST_REQUIRE(
      (std::find(ringCtrls.begin(), ringCtrls.end(), static_cast<Window*>(geologist)) != ringCtrls.end()));

    // DER FOKUS AUF DEN GEOLOGEN - woertlich die drei Zeilen, die beim Umhaengen gestrichen
    // wurden. Ohne sie faengt die Runde unten an einer beliebigen Stelle an.
    for(unsigned presses = 0; presses < 8u && view(1).GetFocus().GetFocused() != geologist; ++presses)
        press(11, PadButton::DpadRight);
    BOOST_TEST_REQUIRE(view(1).GetFocus().GetFocused() == static_cast<Window*>(geologist));
    // Und der Klartext ist wirklich der des Geologen - der Fall misst also die Reihe, um die
    // Phase 12 gebaut wurde, und nicht irgendeine.
    BOOST_TEST(view(1).GetBrief().title == brief::ForAction(brief::ActionBrief::CallGeologist).title);

    // --- (c) DIE PHASE-12-FRAGE, Richtung fuer Richtung, drei Runden ---------------------
    unsigned named = 0;
    unsigned turned = 0;
    unsigned presses = 0;
    bool sawGeologist = false;
    for(unsigned round = 0; round < 3u; ++round)
    {
        for(const PadButton dpad : {PadButton::DpadLeft, PadButton::DpadRight, PadButton::DpadUp, PadButton::DpadDown})
        {
            const std::string bar = dumpKeys(view(1).GetBrief());
            const auto claimed = actionFor(view(1).GetBrief(), dpad);
            const Window* const before = view(1).GetFocus().GetFocused();
            press(11, dpad);
            const Window* const after = view(1).GetFocus().GetFocused();
            const bool reallyTurned = (after != before);
            ++presses;
            if(after == geologist)
                sawGeologist = true;
            BOOST_TEST_CONTEXT("Runde " << round << " " << brief::PadButtonLabel(dpad) << "  Leiste=" << bar)
            {
                // Die Behauptung und die Wirkung sind DASSELBE - in beide Richtungen.
                BOOST_TEST(reallyTurned == (claimed.has_value() && *claimed == brief::KeyAction::TurnRing));
            }
            if(claimed)
                ++named;
            if(reallyTurned)
                ++turned;
        }
    }
    BOOST_TEST_MESSAGE("AUDIT: " << presses << " Steuerkreuzdruecke auf der Flaggenreihe, genannt = " << named
                                 << ", gewirkt = " << turned);
    // Waere hier 0, waere der ganze Fall wertlos - genau die Sorte stiller Schrumpfung, die
    // Befund K5 aufgedeckt hat.
    BOOST_TEST(turned > 0u);
    BOOST_TEST(named == turned);
    BOOST_TEST(sawGeologist);

    closeActionWindow(*this, view(1));
}

BOOST_AUTO_TEST_SUITE_END()
