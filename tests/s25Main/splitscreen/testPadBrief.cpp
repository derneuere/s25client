// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

// DER AUSLOESER, woertlich vom Auftraggeber nach seinem ersten Spieltest am Fernseher:
// "Ich hab den Steinbruch und den Holzfaeller verwechselt" und "als Anfaenger ist auch nicht
// klar, wann Flagge und wann Gebaeude kommt".
//
// Beides ist nachvollziehbar und war vom Datenbestand her vorgezeichnet: Holzfaeller und
// Steinbruch kosten identisch {2, 0} Bretter (BUILDING_COSTS), sind beide eine Huette, und der
// einzige Text, den es dazu ueberhaupt gab, war ein TOOLTIP mit Namen und Kosten - also zwei
// Zeilen, die sich in genau einem Wort unterscheiden. Und Tooltips erreicht ein Padspieler
// grundsaetzlich nicht: es gibt genau einen fuer den ganzen Bildschirm, er wird an der
// MAUSposition gezeichnet, und ohne je bewegte Maus gar nicht.
//
// Gemessen wird hier ausschliesslich am PRODUKTIVEN Weg: Padereignisse hinein, PlayerView::
// GetBrief() heraus - und GetBrief() ist genau der Wert, den dskGameInterface::DrawBrief
// EINLIEST. Nicht, was es malt: dazwischen liegt noch der Zeilenumbruch auf die Kastenbreite,
// und ob am Fernseher wirklich etwas steht, kann kein Fall dieser Datei sehen (der
// DummyRenderer verwirft jeden Zeichenaufruf). Kein Test ruft RefreshBrief oder ForNode selbst
// auf, um zu erzeugen, was der Spieler angeblich sieht; wo eine Rechnung direkt geprueft wird,
// ist der Fall ausdruecklich als
// KONSISTENZPRUEFUNG ZWEIER RECHNUNGEN gekennzeichnet und nicht als Beleg dafuer, dass jemand
// etwas zu Gesicht bekommt.

#include "GamePlayer.h"
#include "Loader.h"
#include "NodalObjectTypes.h"
#include "PadFixture.h"
#include "PointOutput.h"
#include "RttrConfig.h"
#include "RttrForeachPt.h"
#include "Settings.h"
#include "TvDisplay.h"
#include "WindowManager.h"
#include "buildings/nobBaseWarehouse.h"
#include "controls/ctrlBuildingIcon.h"
#include "controls/ctrlButton.h"
#include "controls/ctrlGroup.h"
#include "controls/ctrlTab.h"
#include "desktops/PlayerView.h"
#include "desktops/dskGameInterface.h"
#include "drivers/VideoDriverWrapper.h"
#include "files.h"
#include "helpers/EnumRange.h"
#include "helpers/OptionalEnum.h"
#include "ingameWindows/IngameWindow.h"
#include "ingameWindows/iwAction.h"
#include "input/PlayerBrief.h"
#include "languages.h"
#include "mygettext/mygettext.h"
#include "world/GameWorld.h"
#include "world/GameWorldViewer.h"
#include "world/ViewportLayout.h"
#include "gameTypes/BuildingQuality.h"
#include "gameTypes/BuildingType.h"
#include "gameData/BuildingBriefs.h"
#include "gameData/BuildingConsts.h"
#include "gameData/BuildingProperties.h"
#include "gameData/GoodConsts.h"
#include <rttr/test/LocaleResetter.hpp>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>

using namespace rttr::test;

namespace {

/// Bindet das Katalogverzeichnis genau so, wie es der Produktivcode tut.
///
/// Der Konstruktor des Languages-Singletons ruft bindtextdomain und textdomain
/// (languages.cpp:26-30). Ohne ihn suchte mygettext unter "/usr/share/locale" nach einem Katalog
/// namens "messages", faende keinen und lieferte stumm die msgid zurueck - jeder
/// Uebersetzungsnachweis waere dann still statt rot. Im Spiel besorgt das die erste Benutzung
/// von SETTINGS; hier steht es ausdruecklich, damit kein Nachweis eine Fixture dafuer braucht.
bool bindCatalogDir()
{
    static_cast<void>(LANGUAGES);
    return true;
}

/// Der Katalog, in dem ein Nachweis rechnet - AUSDRUECKLICH eingestellt, nie aus der
/// Systemsprache uebernommen.
///
/// BEFUND A DER DRITTEN PRUEFUNG, in seiner ganzen Breite: mehrere Faelle dieser Datei halten
/// einen UEBERSETZTEN Satz gegen UEBERSETZTE Namen. Das ergibt nur INNERHALB EINES Katalogs
/// einen Sinn. Welchen Katalog mygettext geladen hatte, entschied bis hierher die
/// Spracheinstellung des Rechners (Settings::LoadDefaults -> LANGUAGES.setLanguage("")).
/// Nachgemessen an derselben Binaerdatei:
///
///     LC_ALL=en_US.UTF-8  -> kein rttr-en_US.mo, kein Katalog  -> lief
///     LC_ALL=ja_JP.UTF-8  -> kein rttr-ja.mo,    kein Katalog  -> lief
///     LC_ALL=fr_FR.UTF-8  -> rttr-fr.mo IST da                 -> 78 Zusicherungen rot
///
/// Der franzoesische Katalog uebersetzt die Gebaeudenamen, aber keinen dieser Saetze. Links
/// stand dann Englisch und rechts Franzoesisch, und die Faelle waren rot, ohne dass an diesem
/// Zweig irgendetwas kaputt gewesen waere. Ein Nachweis, der so von der Maschine abhaengt,
/// bewacht nichts und blockiert alle.
///
/// Deutsch ist der Katalog, den dieser Zweig pflegt, und der einzige, in dem diese Saetze
/// ueberhaupt stehen. Faellt er weg, faellt der Nachweis - laut und nicht still.
struct GermanCatalog
{
    // Reihenfolge zaehlt: erst das Verzeichnis binden, dann die Sprache setzen.
    const bool bound = bindCatalogDir();
    const rttr::test::LocaleResetter locale{"de"};

    GermanCatalog() { BOOST_TEST_REQUIRE(std::string(_("Woodcutter")) != "Woodcutter"); }
};

/// Ein eigener, freier Knoten mit MINDESTENS dieser Bauqualitaet, IN DER NAEHE des eigenen HQ.
///
/// Die Naehe ist keine Bequemlichkeit: der Zeiger dieser Ansicht muss ihn mit dem Stick
/// erreichen koennen, und fuer den Mausvergleich muss er im Viewport DIESER Ansicht liegen. Ein
/// Treffer irgendwo auf der Karte erfuellt beides nicht.
MapPoint findBuildSpot(const GameWorldBase& world, const GameWorldViewer& viewer, const BuildingQuality minBQ)
{
    const MapPoint hqPos = world.GetPlayer(viewer.GetPlayerId()).GetHQPos();
    if(!hqPos.isValid())
        return MapPoint::Invalid();
    for(const MapPoint& pt : world.GetPointsInRadiusWithCenter(hqPos, 8))
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

/// Ein Knoten NEBEN dem eigenen Gebiet, der niemandem gehoert.
MapPoint findNoMansLand(const GameWorldBase& world, const GameWorldViewer& viewer)
{
    const MapPoint hqPos = world.GetPlayer(viewer.GetPlayerId()).GetHQPos();
    if(!hqPos.isValid())
        return MapPoint::Invalid();
    for(const MapPoint& pt : world.GetPointsInRadiusWithCenter(hqPos, 16))
    {
        if(viewer.IsOwner(pt))
            continue;
        if(world.GetNode(pt).owner != 0)
            continue;
        if(world.GetNO(pt)->GetType() != NodalObjectType::Nothing)
            continue;
        return pt;
    }
    return MapPoint::Invalid();
}

/// Der RUECKSCHLAG: aus einem angezeigten Titel wieder den Gebaeudetyp machen.
///
/// Warum rueckwaerts und nicht vorwaerts: ein Test, der ForBuilding(icon->GetType()).title mit
/// dem angezeigten Titel vergleicht, laesst zweimal denselben Code laufen und wuerde auch dann
/// gruen bleiben, wenn die Anzeige systematisch das Nachbaricon benennt. Der Rueckschlag ueber
/// BUILDING_NAMES faellt daran (NK2, unten ausdruecklich mutiert).
///
/// Verglichen wird gegen den UEBERSETZTEN Namen: die Testumgebung laedt den deutschen Katalog,
/// der Titel heisst dort "Holzfaeller" und nicht "Woodcutter". Das ist ein Nebenbefund mit
/// Gewicht - es heisst, dass diese Nachweise die Uebersetzung mitpruefen und ein Text ohne
/// msgstr hier als ENGLISCHER Satz auffiele.
helpers::OptionalEnum<BuildingType> buildingFromTitle(const std::string& title)
{
    if(title.empty())
        return helpers::OptionalEnum<BuildingType>{};
    for(const BuildingType bld : helpers::enumRange<BuildingType>())
    {
        const char* name = BUILDING_NAMES[bld];
        if(name && *name && title == _(name))
            return bld;
    }
    return helpers::OptionalEnum<BuildingType>{};
}

/// Alle Bauicons des GERADE gewaehlten Baureiters, in Fokusreihenfolge.
std::vector<const ctrlBuildingIcon*> iconsOfCurrentBuildTab(iwAction& wnd)
{
    std::vector<const ctrlBuildingIcon*> out;
    auto* mainTab = wnd.GetCtrl<ctrlTab>(0);
    if(!mainTab)
        return out;
    ctrlGroup* buildGroup = mainTab->GetGroup(1); // TAB_BUILD
    if(!buildGroup)
        return out;
    auto* buildTab = buildGroup->GetCtrl<ctrlTab>(1);
    if(!buildTab)
        return out;
    ctrlGroup* tabGroup = buildTab->GetGroup(static_cast<int>(buildTab->GetCurrentTab()));
    if(!tabGroup)
        return out;
    for(const ctrlBuildingIcon* icon : tabGroup->GetCtrls<ctrlBuildingIcon>())
        out.push_back(icon);
    return out;
}

/// Nimmt Pad `dev` fuer GENAU DIESE Ansicht in die Hand und zielt auf `pt`.
///
/// Nicht PadViewFixture::aimPadAt: das ruft pads.pickUp(), und die Uebernahme durch Benutzung
/// vergibt den ERSTEN freien Slot. Fuer Ansicht 1 muss der Router ausdruecklich gefragt werden -
/// sonst landet das Pad bei Ansicht 0, und der Nachweis prueft die falsche Haelfte des Bildes.
void takePad(PadViewFixture<2>& f, const PadDeviceId dev, const unsigned viewIdx, const MapPoint pt)
{
    f.pads.connect(dev);
    f.step(16);
    BOOST_TEST_REQUIRE(f.dsk->GetPadRouter().AssignSlot(dev, viewIdx));
    f.step(16);
    BOOST_TEST_REQUIRE(f.view(viewIdx).HasPadCursor());
    f.padSteerTo(dev, viewIdx, pt);
}

/// Faehrt mit dem Pad so lange nach unten, bis der Fokus auf einem Bauicon steht.
const ctrlBuildingIcon* focusFirstIcon(PadViewFixture<2>& f, const PadDeviceId dev, const unsigned viewIdx)
{
    for(unsigned i = 0; i < 8u; ++i)
    {
        if(const auto* icon = dynamic_cast<const ctrlBuildingIcon*>(f.view(viewIdx).GetFocus().GetFocused()))
            return icon;
        f.press(dev, PadButton::DpadDown);
    }
    return dynamic_cast<const ctrlBuildingIcon*>(f.view(viewIdx).GetFocus().GetFocused());
}

} // namespace

BOOST_AUTO_TEST_SUITE(PadBriefTests)

// ============================================================================================
// 1. Die Textquelle selbst: vollstaendig, und ueber der RICHTIGEN Menge
// ============================================================================================

/// NK6, Quantorenkontrolle: das naive "jeder BuildingType hat einen Text" ist ohne jeden Defekt
/// rot - BuildingType::Nothing9 ist ein Platzhalter und hat auch in BUILDING_NAMES keinen
/// Namen. Die Menge ist also "alle ausser Nothing9", und die ZAHL wird mitgeprueft: sonst
/// schrumpfte ein Tippfehler in der Schleifenbedingung die Abdeckung still auf null, und eine
/// Schleife ueber die leere Menge ist gruen.
BOOST_AUTO_TEST_CASE(EveryBuildingWithANameAlsoHasAPurposeSentence)
{
    unsigned named = 0;
    for(const BuildingType bld : helpers::enumRange<BuildingType>())
    {
        const char* name = BUILDING_NAMES[bld];
        if(!name || !*name)
        {
            BOOST_TEST((bld == BuildingType::Nothing9));
            // Der Platzhalter traegt auch keinen Zweck - und ForBuilding faellt darueber nicht.
            BOOST_TEST(std::string(BUILDING_PURPOSE_STRINGS[bld]).empty());
            BOOST_TEST(brief::ForBuilding(bld).title.empty());
            continue;
        }
        ++named;
        BOOST_TEST_CONTEXT(name)
        {
            BOOST_TEST(!std::string(BUILDING_PURPOSE_STRINGS[bld]).empty());
            const brief::Brief b = brief::ForBuilding(bld);
            BOOST_TEST(b.title == _(name));
            // Zweck immer; Kosten bei allem, was man bauen kann. Das Hauptquartier kostet
            // nichts, weil man es nicht baut - es steht zu Spielbeginn da.
            BOOST_TEST(b.lines.size() >= (bld == BuildingType::Headquarters ? 1u : 2u));
        }
    }
    BOOST_TEST(named == 39u);
}

/// Die Zahlen im Text muessen die Zahlen des Spiels sein. Geprueft wird durch HERAUSPARSEN aus
/// dem fertigen Satz und nicht durch Nachbauen derselben Formatierung - sonst liefe zweimal
/// derselbe Code.
BOOST_AUTO_TEST_CASE(TheCostLineCarriesTheRealBuildingCosts)
{
    for(const BuildingType bld : helpers::enumRange<BuildingType>())
    {
        if(bld == BuildingType::Nothing9)
            continue;
        const BuildingCost cost = BUILDING_COSTS[bld];
        const std::string text = brief::ForBuilding(bld).joined();
        BOOST_TEST_CONTEXT(BUILDING_NAMES[bld] << " -> " << text)
        {
            if(cost.boards == 0 && cost.stones == 0)
            {
                // Nur HQ und der Platzhalter; beide sind nicht baubar.
                BOOST_TEST((bld == BuildingType::Headquarters));
                BOOST_TEST(text.find(_("Costs: ")) == std::string::npos);
            } else
            {
                BOOST_TEST(text.find(_("Costs: ")) != std::string::npos);
                if(cost.boards > 0)
                    BOOST_TEST(text.find(std::to_string(cost.boards) + _(" boards")) != std::string::npos);
                if(cost.stones > 0)
                    BOOST_TEST(text.find(std::to_string(cost.stones) + _(" stones")) != std::string::npos);
            }
        }
    }
}

/// Der Ausloeser selbst, als Zusicherung: Holzfaeller und Steinbruch sind im Klartext
/// unterscheidbar. Vor dieser Phase war der einzige Text zu beiden
/// "<Name>\nCosts: 2 boards" - identisch bis auf ein Wort.
BOOST_AUTO_TEST_CASE(WoodcutterAndQuarryReadDifferently)
{
    const brief::Brief wood = brief::ForBuilding(BuildingType::Woodcutter);
    const brief::Brief quarry = brief::ForBuilding(BuildingType::Quarry);
    // Die Kosten sind wirklich gleich - das ist die Ausgangslage, nicht eine Annahme.
    BOOST_TEST_REQUIRE(
      (BUILDING_COSTS[BuildingType::Woodcutter].boards == BUILDING_COSTS[BuildingType::Quarry].boards));
    BOOST_TEST_REQUIRE(
      (BUILDING_COSTS[BuildingType::Woodcutter].stones == BUILDING_COSTS[BuildingType::Quarry].stones));
    BOOST_TEST(wood.title != quarry.title);
    BOOST_TEST(wood.lines != quarry.lines);
    // Und beide sagen, WORAN sie haengen - das ist die Auskunft, die es sonst nirgends gibt.
    BOOST_TEST(!std::string(BUILDING_SITE_STRINGS[BuildingType::Woodcutter]).empty());
    BOOST_TEST(!std::string(BUILDING_SITE_STRINGS[BuildingType::Quarry]).empty());
}

/// UEBERSETZUNGSWAECHTER: der deutsche Katalog kennt JEDEN Satz, den der Kasten zeigt - sonst
/// stuenden beim deutschen Spieler mitten im Kasten englische Zeilen.
///
/// WAS HIER FRUEHER STAND und warum es weg ist: der Fall hiess
/// "WhereACatalogIsLoadedItAlsoCarriesTheNewSentences" und begann mit
///
///     const bool catalogLoaded = std::string(_("Woodcutter")) != "Woodcutter";
///     if(!catalogLoaded) return;   // "still statt falsch rot"
///
/// Beides war falsch. Ohne Katalog war er STILL - auf einem englischen Rechner bewachte er
/// nichts und niemandem fiel es auf. Und mit einem ANDEREN Katalog war er falsch rot: unter
/// LC_ALL=fr_FR.UTF-8 laedt rttr-fr.mo, uebersetzt die Namen und keinen dieser Saetze, und der
/// Fall verlangte von ihm 39 Uebersetzungen, die dort niemand versprochen hat. Jetzt sagt er,
/// welchen Katalog er meint, und dieser Katalog ist Pflicht (GermanCatalog).
///
/// HINWEIS: Die Kataloge liegen unter data/RTTR/languages im Hauptprojekt, nicht mehr im
/// Submodul external/languages. Damit ist dieser Fall die Zusicherung, dass die deutschen
/// Uebersetzungen mit dem Branch mitkommen: wer ihn auscheckt und baut, muss sie haben.
BOOST_FIXTURE_TEST_CASE(TheGermanCatalogCarriesEverySentenceOfThePanel, PadViewFixture<1>)
{
    const GermanCatalog catalog;
    unsigned checked = 0;
    for(const BuildingType bld : helpers::enumRange<BuildingType>())
    {
        const char* purpose = BUILDING_PURPOSE_STRINGS[bld];
        if(!purpose || !*purpose)
            continue;
        BOOST_TEST_CONTEXT(purpose)
        {
            BOOST_TEST(std::string(_(purpose)) != purpose);
        }
        const char* site = BUILDING_SITE_STRINGS[bld];
        if(site && *site)
        {
            BOOST_TEST_CONTEXT(site)
            {
                BOOST_TEST(std::string(_(site)) != site);
            }
        }
        ++checked;
    }
    BOOST_TEST(checked == 39u);
    // Und die Bausteine, die der Kasten selbst zusammensetzt.
    for(const char* piece : {"Costs: ", "Supplies needed: ", "Room for a small hut", "No man's land",
                             "Press A for the build menu.", "Building a road"})
    {
        BOOST_TEST_CONTEXT(piece)
        {
            BOOST_TEST(std::string(_(piece)) != piece);
        }
    }

    // Woertliche Ausgabe fuer den Bericht - der Kasten, wie ihn der Spieler liest. VOLLZAEHLIG
    // und nicht in Beispielen: die Widersprueche zwischen Satz und Nachschubzeile sind zweimal
    // dadurch gefunden worden, dass jemand die ganze Liste gelesen hat, und beide Male hat die
    // Stichprobe sie nicht gezeigt.
    for(const BuildingType bld : helpers::enumRange<BuildingType>())
    {
        if(!BUILDING_NAMES[bld] || !*BUILDING_NAMES[bld])
            continue;
        const brief::Brief b = brief::ForBuilding(bld);
        BOOST_TEST_MESSAGE("--- " << b.title);
        for(const std::string& line : b.lines)
            BOOST_TEST_MESSAGE("    " << line);
    }
    for(const brief::NodeVerdict v :
        {brief::NodeVerdict::Hut, brief::NodeVerdict::FlagOnly, brief::NodeVerdict::NoSpace,
         brief::NodeVerdict::NoMansLand, brief::NodeVerdict::Mine})
    {
        const brief::Brief b = brief::ForNode(v);
        BOOST_TEST_MESSAGE("--- " << b.title);
        for(const std::string& line : b.lines)
            BOOST_TEST_MESSAGE("    " << line);
    }
}

// ============================================================================================
// 2. Die Auskunft folgt dem FOKUS, nicht der Maus
// ============================================================================================

/// Z1 - ANSCHLUSS. Der Padspieler fokussiert ein Bauicon, und die Auskunft zu GENAU DIESEM
/// Gebaeude steht bereit, ohne dass je ein Msg_MouseMove gesendet wurde.
///
/// Dieser Fall war vor der Aenderung ZWINGEND rot, und zwar konstruktiv: es gab keinen zweiten
/// Traeger fuer die Auskunft ausser WindowManager::curTooltip, und der wird an lastMousePos
/// gezeichnet, das ohne Mausbewegung Position::Invalid() ist.
BOOST_FIXTURE_TEST_CASE(ThePadPlayerReadsTheFocusedBuildingWithoutAnyMouse, PadViewFixture<2>)
{
    const MapPoint spot = findBuildSpot(worldFixture.world, view(1).GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(spot.isValid());
    const Position mouseBefore = VIDEODRIVER.GetMousePos();

    takePad(*this, 11, 1, spot);
    press(11, PadButton::A);
    iwAction* const wnd = view(1).actionwindow;
    BOOST_TEST_REQUIRE(wnd != static_cast<iwAction*>(nullptr));
    press(11, PadButton::Y);

    const ctrlBuildingIcon* icon = focusFirstIcon(*this, 11, 1);
    BOOST_TEST_REQUIRE(icon != static_cast<const ctrlBuildingIcon*>(nullptr));

    const brief::Brief& b = view(1).GetBrief();
    BOOST_TEST_REQUIRE(!b.empty());
    const auto named = buildingFromTitle(b.title);
    BOOST_TEST_REQUIRE(named.has_value());
    BOOST_TEST((*named == icon->GetType()));
    // ... und der Text sagt wirklich etwas ueber das Gebaeude aus, nicht nur seinen Namen.
    BOOST_TEST(b.lines.size() >= 2u);

    // NK3, der Maus-Warp als naheliegende Fehlimplementierung: die eine echte Maus hat sich in
    // diesem ganzen Ablauf nicht bewegt.
    BOOST_TEST((VIDEODRIVER.GetMousePos() == mouseBefore));

    // NK5, Besitzkontrolle: der Nachbar ohne Pad liest gar nichts.
    BOOST_TEST(view(0).GetBrief().empty());

    view(1).actionwindow->Close();
    WINDOWMANAGER.Draw();
}

/// NK2 - MUTATIONSPROBE auf die Zuordnung. Es wird nicht EIN Icon geprueft, sondern JEDES des
/// Reiters: eine Anzeige, die um eins verrutscht ist, faellt hier an mehreren Stellen auf, und
/// eine, die immer denselben festen Namen zeigt, ebenfalls (die Titel muessen paarweise
/// verschieden sein).
BOOST_FIXTURE_TEST_CASE(EveryIconOfTheTabNamesItselfAndNotItsNeighbour, PadViewFixture<2>)
{
    const MapPoint spot = findBuildSpot(worldFixture.world, view(1).GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(spot.isValid());

    takePad(*this, 11, 1, spot);
    press(11, PadButton::A);
    iwAction* const wnd = view(1).actionwindow;
    BOOST_TEST_REQUIRE(wnd != static_cast<iwAction*>(nullptr));
    press(11, PadButton::Y);
    BOOST_TEST_REQUIRE(focusFirstIcon(*this, 11, 1) != static_cast<const ctrlBuildingIcon*>(nullptr));

    const std::vector<const ctrlBuildingIcon*> icons = iconsOfCurrentBuildTab(*wnd);
    BOOST_TEST_REQUIRE(icons.size() >= 5u); // die Huettenreihe hat je nach Addons 6 bis 10

    std::vector<std::string> seenTitles;
    std::vector<const Window*> visited;
    unsigned checked = 0;
    for(unsigned i = 0; i < icons.size(); ++i)
    {
        const auto* focused = dynamic_cast<const ctrlBuildingIcon*>(view(1).GetFocus().GetFocused());
        if(!focused)
            break; // der Fokus hat die Reihe verlassen - der Rest ist eine andere Zeile
        // Das Gitter hat fuenf Spalten; ein Schritt nach rechts am Zeilenende landet wieder auf
        // einem schon besuchten Icon. Der Lauf endet dort, damit die Eindeutigkeitspruefung
        // unten eine Aussage ueber die ANZEIGE macht und nicht ueber die Navigation.
        if(std::find(visited.begin(), visited.end(), static_cast<const Window*>(focused)) != visited.end())
            break;
        visited.push_back(focused);
        const brief::Brief& b = view(1).GetBrief();
        BOOST_TEST_CONTEXT("Icon " << i << " ist " << BUILDING_NAMES[focused->GetType()] << ", Titel \"" << b.title
                                   << "\"")
        {
            const auto named = buildingFromTitle(b.title);
            BOOST_TEST_REQUIRE(named.has_value());
            BOOST_TEST((*named == focused->GetType()));
        }
        seenTitles.push_back(b.title);
        ++checked;
        press(11, PadButton::DpadRight);
    }
    BOOST_TEST(checked >= 5u);
    // Eine Anzeige, die immer denselben Satz zeigt, faellt hier.
    std::vector<std::string> unique = seenTitles;
    std::sort(unique.begin(), unique.end());
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
    BOOST_TEST(unique.size() == seenTitles.size());

    view(1).actionwindow->Close();
    WINDOWMANAGER.Draw();
}

/// NK4 - die Falle "aendert sich beim Fokuswechsel". Diese Zusicherung allein bestuende auch
/// eine Anzeige, die den Framezaehler zeigt. Deshalb die Umkehrung: EIN Schritt zurueck stellt
/// den vorigen Text WORTGLEICH wieder her.
BOOST_FIXTURE_TEST_CASE(SteppingBackRestoresThePreviousTextWordForWord, PadViewFixture<2>)
{
    const MapPoint spot = findBuildSpot(worldFixture.world, view(1).GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(spot.isValid());

    takePad(*this, 11, 1, spot);
    press(11, PadButton::A);
    BOOST_TEST_REQUIRE(view(1).actionwindow != static_cast<iwAction*>(nullptr));
    press(11, PadButton::Y);
    BOOST_TEST_REQUIRE(focusFirstIcon(*this, 11, 1) != static_cast<const ctrlBuildingIcon*>(nullptr));

    const brief::Brief first = view(1).GetBrief();
    press(11, PadButton::DpadRight);
    const brief::Brief second = view(1).GetBrief();
    BOOST_TEST_REQUIRE(second.title != first.title);
    press(11, PadButton::DpadLeft);
    const brief::Brief again = view(1).GetBrief();
    BOOST_TEST(again.title == first.title);
    BOOST_TEST(again.lines == first.lines);

    view(1).actionwindow->Close();
    WINDOWMANAGER.Draw();
}

/// Z2 - UNABHAENGIGKEIT VON DER MAUS. Die Maus steht auf einem ANDEREN Icon desselben Fensters,
/// waehrend der Padfokus auf unserem steht. Gelesen werden muss das FOKUSSIERTE.
///
/// Ohne diesen Fall waere Z1 auch von einer Implementierung erfuellt, die bei jedem Fokuswechsel
/// die Maus auf das Control zieht und den alten Tooltipweg weiterbenutzt - plausibel, falsch,
/// und ein Bruch der harten Randbedingung.
BOOST_FIXTURE_TEST_CASE(TheTextFollowsTheFocusEvenWhenTheMouseSitsOnAnotherIcon, PadViewFixture<2>)
{
    const MapPoint spot = findBuildSpot(worldFixture.world, view(1).GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(spot.isValid());

    takePad(*this, 11, 1, spot);
    press(11, PadButton::A);
    iwAction* const wnd = view(1).actionwindow;
    BOOST_TEST_REQUIRE(wnd != static_cast<iwAction*>(nullptr));
    press(11, PadButton::Y);
    const ctrlBuildingIcon* focused = focusFirstIcon(*this, 11, 1);
    BOOST_TEST_REQUIRE(focused != static_cast<const ctrlBuildingIcon*>(nullptr));

    const std::vector<const ctrlBuildingIcon*> icons = iconsOfCurrentBuildTab(*wnd);
    BOOST_TEST_REQUIRE(icons.size() >= 2u);
    const ctrlBuildingIcon* other = nullptr;
    for(const ctrlBuildingIcon* icon : icons)
    {
        if(icon != focused)
        {
            other = icon;
            break;
        }
    }
    BOOST_TEST_REQUIRE(other != static_cast<const ctrlBuildingIcon*>(nullptr));

    // Der PRODUKTIVE Mauseingang, genau der, den auch der Treiber nimmt.
    const Position otherPos = other->GetDrawPos() + DrawPoint(other->GetSize() / 2u);
    WINDOWMANAGER.Msg_MouseMove(MouseCoords(otherPos));
    step(16, otherPos);

    const auto named = buildingFromTitle(view(1).GetBrief().title);
    BOOST_TEST_REQUIRE(named.has_value());
    BOOST_TEST((*named == focused->GetType()));
    BOOST_TEST((*named != other->GetType()));

    view(1).actionwindow->Close();
    WINDOWMANAGER.Draw();
}

/// Z3 - VIER FOKUSSE, VIER AUSKUENFTE. Zwei Ansichten, zwei Pads, zwei GLEICHZEITIG offene
/// Aktionsfenster; jedes Pad steht auf einem anderen Gebaeude, jede Ansicht liest ihres.
///
/// Mit WindowManager::curTooltip ist dieser Fall KONSTRUKTIV unmoeglich - es gibt genau einen
/// Tooltip, der zweite SetToolTip ueberschreibt den ersten. Dieser Nachweis ist damit zugleich
/// die Begruendung dafuer, warum die Auskunft an der PlayerView haengt und nicht am Control.
BOOST_FIXTURE_TEST_CASE(TwoViewsReadTwoDifferentBuildingsAtTheSameTime, PadViewFixture<2>)
{
    const MapPoint spot0 = findBuildSpot(worldFixture.world, view(0).GetViewer(), BuildingQuality::Hut);
    const MapPoint spot1 = findBuildSpot(worldFixture.world, view(1).GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(spot0.isValid());
    BOOST_TEST_REQUIRE(spot1.isValid());

    aimPadAt(10, 0, spot0);
    aimPadAt(11, 1, spot1);

    for(const auto& [dev, idx] : {std::pair<PadDeviceId, unsigned>{10, 0}, std::pair<PadDeviceId, unsigned>{11, 1}})
    {
        press(dev, PadButton::A);
        BOOST_TEST_REQUIRE(view(idx).actionwindow != static_cast<iwAction*>(nullptr));
        press(dev, PadButton::Y);
        BOOST_TEST_REQUIRE(focusFirstIcon(*this, dev, idx) != static_cast<const ctrlBuildingIcon*>(nullptr));
    }
    // Beide Fenster stehen wirklich gleichzeitig offen.
    BOOST_TEST_REQUIRE(view(0).actionwindow != static_cast<iwAction*>(nullptr));
    BOOST_TEST_REQUIRE(view(1).actionwindow != static_cast<iwAction*>(nullptr));

    // Ansicht 1 einen Schritt weiter, damit die beiden auf VERSCHIEDENEN Icons stehen.
    press(11, PadButton::DpadRight);

    const auto* icon0 = dynamic_cast<const ctrlBuildingIcon*>(view(0).GetFocus().GetFocused());
    const auto* icon1 = dynamic_cast<const ctrlBuildingIcon*>(view(1).GetFocus().GetFocused());
    BOOST_TEST_REQUIRE(icon0 != static_cast<const ctrlBuildingIcon*>(nullptr));
    BOOST_TEST_REQUIRE(icon1 != static_cast<const ctrlBuildingIcon*>(nullptr));
    BOOST_TEST_REQUIRE((icon0->GetType() != icon1->GetType()));

    const auto named0 = buildingFromTitle(view(0).GetBrief().title);
    const auto named1 = buildingFromTitle(view(1).GetBrief().title);
    BOOST_TEST_REQUIRE(named0.has_value());
    BOOST_TEST_REQUIRE(named1.has_value());
    BOOST_TEST((*named0 == icon0->GetType()));
    BOOST_TEST((*named1 == icon1->GetType()));
    // Und damit gleichzeitig zwei verschiedene Texte auf einem Bildschirm.
    BOOST_TEST(view(0).GetBrief().title != view(1).GetBrief().title);

    view(0).actionwindow->Close();
    view(1).actionwindow->Close();
    WINDOWMANAGER.Draw();
}

/// Z4 / HARTE RANDBEDINGUNG: eine Ansicht OHNE Pad bekommt keinen Klartext - auch dann nicht,
/// wenn die Maus mitten in ihr steht und ein Aktionsfenster offen ist. Fuer den Mausspieler
/// aendert sich nichts, und "nichts" heisst hier: kein zusaetzlicher Kasten, kein zusaetzlicher
/// Zeichenaufruf.
BOOST_FIXTURE_TEST_CASE(TheMouseOnlyViewGetsNoPanelAtAll, PadViewFixture<2>)
{
    const MapPoint spot = findBuildSpot(worldFixture.world, view(0).GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(spot.isValid());

    // Ansicht 1 bekommt ein Pad, damit die Maus wirklich bei Ansicht 0 landet.
    pads.connect(11);
    step(16);
    BOOST_TEST_REQUIRE(dsk->GetPadRouter().AssignSlot(11, 1));
    step(16);
    const Position mousePos = nodeViewPos(0, spot);
    BOOST_TEST_REQUIRE(view(0).ContainsViewPos(mousePos));
    step(16, mousePos);
    BOOST_TEST_REQUIRE((gwv(0).GetSelectedPt() == spot));
    BOOST_TEST_REQUIRE(!view(0).HasPadCursor());

    BOOST_TEST_REQUIRE(dsk->ContextClick(MouseCoords(mousePos)));
    BOOST_TEST_REQUIRE(view(0).actionwindow != static_cast<iwAction*>(nullptr));
    step(16, mousePos);

    BOOST_TEST(view(0).GetBrief().empty());
    // Die Ansicht MIT Pad dagegen liest etwas - sonst waere dieser Nachweis auch von einer
    // Fassung erfuellt, die den Klartext gar nicht erst gebaut hat.
    BOOST_TEST(!view(1).GetBrief().empty());

    view(0).actionwindow->Close();
    WINDOWMANAGER.Draw();
}

// ============================================================================================
// 3. "Wann Flagge, wann Gebaeude" - und warum es hier gerade nicht geht
// ============================================================================================

/// Der zweite Halbsatz des Auftraggebers. Waehrend der Padspieler mit dem Zeiger ueber die Welt
/// faehrt - ohne jedes Fenster, ohne einen Knopf zu druecken - sagt ihm die Zeile unter seiner
/// Ansicht, was auf dem Knoten unter dem Zeiger geht.
BOOST_FIXTURE_TEST_CASE(TheLineUnderTheViewSaysWhatFitsOnThisNode, PadViewFixture<2>)
{
    const GameWorldBase& world = worldFixture.world;
    const MapPoint hut = findBuildSpot(world, view(1).GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(hut.isValid());

    takePad(*this, 11, 1, hut);
    // Kein Knopfdruck - nur zielen.
    BOOST_TEST_REQUIRE(view(1).actionwindow == static_cast<iwAction*>(nullptr));
    const brief::Brief onHut = view(1).GetBrief();
    BOOST_TEST_REQUIRE(!onHut.empty());
    // Die Zusicherung ist inhaltlich, nicht woertlich: der Text muss der Auskunft entsprechen,
    // die dasselbe Regelwerk gleich darauf ins Fenster schreibt.
    const brief::NodeVerdict verdict = dsk->JudgeNode(view(1), hut);
    BOOST_TEST(((verdict == brief::NodeVerdict::Hut) || (verdict == brief::NodeVerdict::House)
                || (verdict == brief::NodeVerdict::Castle)));
    BOOST_TEST(onHut.title == brief::ForNode(verdict).title);

    // Und auf dem eigenen HQ steht etwas ANDERES - eine Zeile, die sich nie aendert, faellt hier.
    const MapPoint hqPos = world.GetPlayer(view(1).GetPlayerId()).GetHQPos();
    BOOST_TEST_REQUIRE(hqPos.isValid());
    aimAt(1, hqPos);
    BOOST_TEST(view(1).GetBrief().title != onHut.title);
    BOOST_TEST((dsk->JudgeNode(view(1), hqPos) == brief::NodeVerdict::OwnBuilding));
}

/// Der Fall, den es bis Phase 9 nur als EINEN Satz gab. Ein A auf Niemandsland sagte woertlich
/// dasselbe wie ein A auf einem zu engen eigenen Knoten - "Nothing can be done here." Ein
/// Anfaenger konnte daraus nicht ableiten, ob er einen Knoten weiterruecken oder erst Gebiet
/// erobern muss. Das sind zwei voellig verschiedene Handlungen.
BOOST_FIXTURE_TEST_CASE(NoMansLandGivesItsOwnReasonAndNotTheCollectiveSentence, PadViewFixture<2>)
{
    const MapPoint empty = findNoMansLand(worldFixture.world, view(1).GetViewer());
    BOOST_TEST_REQUIRE(empty.isValid());

    takePad(*this, 11, 1, empty);
    // Schon ohne Knopfdruck steht die Auskunft da.
    BOOST_TEST((dsk->JudgeNode(view(1), empty) == brief::NodeVerdict::NoMansLand));
    BOOST_TEST(view(1).GetBrief().title == brief::ForNode(brief::NodeVerdict::NoMansLand).title);

    BOOST_TEST_REQUIRE(!view(1).GetRejection().has_value());
    press(11, PadButton::A);
    BOOST_TEST(view(1).actionwindow == static_cast<iwAction*>(nullptr));
    BOOST_TEST_REQUIRE(view(1).GetRejection().has_value());
    // DER Punkt: nicht mehr der Sammelgrund.
    BOOST_TEST((*view(1).GetRejection() == PadRejection::NoMansLand));
    BOOST_TEST((*view(1).GetRejection() != PadRejection::NothingHere));
    // NK5: der Nachbar bleibt unberuehrt.
    BOOST_TEST(!view(0).GetRejection().has_value());
}

/// KONSISTENZPRUEFUNG ZWEIER RECHNUNGEN (NK8), ausdruecklich KEIN Beleg dafuer, dass ein
/// Spieler irgendetwas sieht: ueber JEDEN Knoten der Welt muss gelten, dass der genannte Grund
/// und das tatsaechliche Angebot des Aktionsfensters dasselbe sagen. Ein Text, der "hier passt
/// eine Huette" behauptet, waehrend ComputeActionOptions keinen Baureiter hergibt, waere ein
/// zweites, danebenlaufendes Regelwerk - genau das, was das Herausziehen von
/// ComputeActionOptions einmal vermieden hat.
///
/// Der Vollkartenlauf ist hier bezahlbar, weil PadViewFixture weder Server noch NWF braucht.
BOOST_FIXTURE_TEST_CASE(TheStatedReasonAgreesWithTheActionWindowOnEveryNodeOfTheMap, PadViewFixture<2>)
{
    const GameWorldBase& world = worldFixture.world;
    unsigned buildable = 0, notBuildable = 0;
    RTTR_FOREACH_PT(MapPoint, world.GetSize())
    {
        const brief::NodeVerdict verdict = dsk->JudgeNode(view(1), pt);
        const bool saysBuildable = (verdict == brief::NodeVerdict::Hut || verdict == brief::NodeVerdict::House
                                    || verdict == brief::NodeVerdict::Castle || verdict == brief::NodeVerdict::Mine
                                    || verdict == brief::NodeVerdict::Harbor);
        const auto opts = dsk->ComputeActionOptions(view(1), pt);
        // Ein eigenes Gebaeude faengt A vor dem Aktionsfenster ab (OpenObjectWindow); dort sagt
        // JudgeNode bewusst OwnBuilding, obwohl tabs.build false ist. Das ist der einzige
        // erlaubte Unterschied, und er wird hier ausgesprochen statt uebersehen.
        if(verdict == brief::NodeVerdict::OwnBuilding)
            continue;
        BOOST_TEST_CONTEXT("Knoten " << pt << ", Grund " << static_cast<int>(verdict))
        {
            BOOST_TEST(saysBuildable == opts.tabs.build);
        }
        if(saysBuildable)
            ++buildable;
        else
            ++notBuildable;
        // Und die Rechnung hat keine Nebenwirkung: sie darf beliebig oft laufen. Frueher zeigte
        // sie im Handelsfall ein Fenster - ein Lauf ueber jeden Knoten haette Fenster geoeffnet.
        BOOST_TEST(WINDOWMANAGER.GetTopMostWindow() == static_cast<IngameWindow*>(nullptr));
    }
    // NK7, Abdeckungskontrolle: dass beide Seiten wirklich vorkamen.
    BOOST_TEST(buildable > 0u);
    BOOST_TEST(notBuildable > 0u);
}

// ============================================================================================
// 4. Die erzwungene Bauhilfe
// ============================================================================================

/// Die Spezifikation empfiehlt, die Bauhilfe beim Oeffnen des Baumenues zu erzwingen. Sie wirkt
/// aber NUR auf die Ansicht, die gedrueckt hat - und sie schreibt die Einstellung NICHT.
///
/// Beides ist die harte Randbedingung: SETTINGS.ingame.showBQ ist die dauerhafte Vorgabe aller
/// Ansichten und aller kuenftigen Partien. Wuerde ein Padspieler sie umstellen, aenderte er dem
/// Mausspieler eine Einstellung, die dieser nie angefasst hat - und sie bliebe nach dem Spiel in
/// der ini stehen.
BOOST_FIXTURE_TEST_CASE(OpeningTheBuildMenuTurnsOnTheBuildingAidForThatViewOnly, PadViewFixture<2>)
{
    const MapPoint spot = findBuildSpot(worldFixture.world, view(1).GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(spot.isValid());

    const bool settingBefore = SETTINGS.ingame.showBQ;
    BOOST_TEST_REQUIRE(!gwv(1).IsShowingBQ()); // Auslieferungszustand: aus
    BOOST_TEST_REQUIRE(!gwv(0).IsShowingBQ());

    takePad(*this, 11, 1, spot);
    press(11, PadButton::A);
    BOOST_TEST_REQUIRE(view(1).actionwindow != static_cast<iwAction*>(nullptr));

    BOOST_TEST(gwv(1).IsShowingBQ());
    // Die Nachbaransicht nicht ...
    BOOST_TEST(!gwv(0).IsShowingBQ());
    // ... und die dauerhafte Einstellung erst recht nicht.
    BOOST_TEST(SETTINGS.ingame.showBQ == settingBefore);

    view(1).actionwindow->Close();
    WINDOWMANAGER.Draw();
}

/// BEFUND 2 der Pruefung, und das eigentliche Leck: der Test darueber schaut nur auf den
/// Augenblick DIREKT nach dem Druck. Gemessen wurde aber ein SPAETERER Weg.
///
/// GameWorldView::SaveIngameSettingsValues schreibt ALLE DREI HUD-Werte dieser Ansicht in
/// SETTINGS - nicht nur den, den der Aufrufer gerade geaendert hat. Und einen dieser Aufrufer
/// erreicht der Padspieler selbst: iwAction, Reiter "Anzeigeoptionen", Knopf 2 ruft
/// gwv.ToggleShowNamesAndProductivity() - auf der GameWorldView, mit der das Fenster gebaut
/// wurde (dskGameInterface::ShowActionWindow), also auf SEINER. Damit wanderte die ihm
/// aufgezwungene Bauhilfe in die ini, und der Mausspieler fand sie beim naechsten Start
/// eingeschaltet vor.
///
/// Der Nachweis geht deshalb den ganzen Weg: Baumenue oeffnen (Zwang entsteht), dann den
/// HUD-Umschalter DERSELBEN Ansicht benutzen (das Leck), dann die Einstellung ansehen.
BOOST_FIXTURE_TEST_CASE(TheForcedBuildingAidNeverReachesTheSettingsFile, PadViewFixture<2>)
{
    const MapPoint spot = findBuildSpot(worldFixture.world, view(1).GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(spot.isValid());

    SETTINGS.ingame.showBQ = false; // der Auslieferungszustand, den der Mausspieler vorfindet
    BOOST_TEST_REQUIRE(!gwv(1).IsShowingBQ());

    takePad(*this, 11, 1, spot);
    press(11, PadButton::A);
    BOOST_TEST_REQUIRE(view(1).actionwindow != static_cast<iwAction*>(nullptr));
    BOOST_TEST_REQUIRE(gwv(1).IsShowingBQ()); // der Zwang ist da ...

    // ... und jetzt der HUD-Umschalter, den der Padspieler ueber sein eigenes Aktionsfenster
    // erreicht. Genau hier lief die Einstellung frueher aus.
    gwv(1).ToggleShowNamesAndProductivity();
    BOOST_TEST(SETTINGS.ingame.showBQ == false);
    // Der Zwang wirkt weiter - er ist ja nicht das Problem, sondern nur sein Weg in die ini.
    BOOST_TEST(gwv(1).IsShowingBQ());
    // Und der andere Umschalter derselben Ansicht ebenso.
    gwv(1).ToggleShowProductivity();
    BOOST_TEST(SETTINGS.ingame.showBQ == false);
    gwv(1).ToggleShowNames();
    BOOST_TEST(SETTINGS.ingame.showBQ == false);

    // Gegenprobe: der ausdrueckliche Wille eines Menschen wird sehr wohl gespeichert - sonst
    // waere die Trennung eine Sperre statt einer Trennung. Er schaltet dabei die erzwungene
    // Bauhilfe ab; ein Knopf, der sichtbar nichts tut, waere die naechste Beschwerde.
    gwv(1).ToggleShowBQ();
    BOOST_TEST(!gwv(1).IsShowingBQ());
    BOOST_TEST(SETTINGS.ingame.showBQ == false);
    gwv(1).ToggleShowBQ();
    BOOST_TEST(gwv(1).IsShowingBQ());
    BOOST_TEST(SETTINGS.ingame.showBQ == true);

    SETTINGS.ingame.showBQ = false;
    view(1).actionwindow->Close();
    WINDOWMANAGER.Draw();
}

/// Der MECHANISMUS hinter Befund 2, ausgesprochen statt vorausgesetzt - und der Grund, warum
/// die erzwungene Bauhilfe ein eigenes Feld braucht und nicht einfach show_bq setzen darf.
///
/// GameWorldView::SaveIngameSettingsValues schreibt showBQ, showNames und showProductivity
/// GEMEINSAM. Ein Umschalter, der nur die Gebaeudenamen meint, traegt die Bauhilfe deshalb mit
/// in die Einstellung. Solange nur Menschen die Werte setzen, ist das bloss unsauber; sobald ein
/// Automatismus einen davon aufzwingt, ist es ein Leck.
///
/// Dieser Fall ist bewusst KEINE Regression, die behoben wurde - er haelt den vorhandenen
/// Zustand fest, damit die Trennung nebenan begruendet bleibt. Wer SaveIngameSettingsValues
/// eines Tages auf den einzelnen geaenderten Wert umstellt, sieht hier, dass er dabei den Grund
/// fuer forcedShowBQ_ mit aufloest.
BOOST_FIXTURE_TEST_CASE(OneHudToggleWritesAllThreeSettingsAtOnce, PadViewFixture<2>)
{
    const bool oldBQ = SETTINGS.ingame.showBQ;
    const bool oldNames = SETTINGS.ingame.showNames;
    const bool oldProd = SETTINGS.ingame.showProductivity;

    SETTINGS.ingame.showBQ = false;
    gwv(1).ToggleShowBQ(); // ausdruecklicher Wille eines Menschen - der DARF gespeichert werden
    BOOST_TEST(SETTINGS.ingame.showBQ == true);

    // Jemand stellt die Einstellung von aussen zurueck ...
    SETTINGS.ingame.showBQ = false;
    // ... und ein Umschalter, der mit der Bauhilfe GAR NICHTS zu tun hat, schreibt sie wieder.
    gwv(1).ToggleShowNames();
    BOOST_TEST(SETTINGS.ingame.showBQ == true);

    SETTINGS.ingame.showBQ = oldBQ;
    SETTINGS.ingame.showNames = oldNames;
    SETTINGS.ingame.showProductivity = oldProd;
}

// ============================================================================================
// 5. Geometrie des Kastens - eine Layoutaussage ohne OpenGL
// ============================================================================================

/// NK9 und seine Grenze: dass der Kasten tatsaechlich GEMALT wird, kann kein Test beweisen - der
/// Baureiter laesst sich in der Testumgebung nicht zeichnen (Loader::GetNationIcon). Was
/// beweisbar ist, ist seine LAGE, und die ist eine reine Funktion.
BOOST_AUTO_TEST_CASE(ThePanelStaysInsideItsOwnViewportAndInsideTheSafeArea)
{
    // Viertelbildschirm oben links auf 1920x1080, Safe Area 5% je Seite.
    const Rect screen(0, 0, 1920, 1080);
    const Rect safe(96, 54, 1728, 972);
    const Rect topLeft(0, 0, 960, 540);
    const Rect bottomRight(960, 540, 960, 540);

    for(const Rect& viewport : {topLeft, bottomRight})
    {
        const Rect panel = brief::PanelRect(viewport, safe, 4, 12);
        BOOST_TEST_CONTEXT("Viewport " << viewport.left << "," << viewport.top)
        {
            // Im eigenen Viewport - ein Kasten, der in das Bild des Nachbarn ragte, waere ein
            // Eingriff in dessen Partie.
            BOOST_TEST(panel.left >= viewport.left);
            BOOST_TEST(panel.right <= viewport.right);
            BOOST_TEST(panel.top >= viewport.top);
            BOOST_TEST(panel.bottom <= viewport.bottom);
            // Und im Bildschirmkasten - der Overscan schneidet an den vier KANTEN des Bildes ab.
            BOOST_TEST(panel.left >= safe.left);
            BOOST_TEST(panel.right <= safe.right);
            BOOST_TEST(panel.bottom <= safe.bottom);
            // Er hat Flaeche, und die Hoehe traegt die vier Zeilen.
            BOOST_TEST(panel.getSize().x > 100u);
            BOOST_TEST(panel.getSize().y >= 4u * 12u);
        }
    }

    // Der Kasten sitzt UNTEN in seiner Ansicht - dort verdeckt er am wenigsten von der Karte.
    const Rect panel = brief::PanelRect(topLeft, safe, 4, 12);
    BOOST_TEST(panel.top > topLeft.top + (topLeft.bottom - topLeft.top) / 2);

    // Der Rand WIRKT auch wirklich - sonst waere die Zusicherung oben von einer Fassung
    // erfuellt, die die Safe Area gar nicht liest. Geprueft an der Ansicht UNTEN rechts, denn
    // nur deren Unterkante liegt ueberhaupt im Overscanbereich (1080 gegen 972).
    const Rect clamped = brief::PanelRect(bottomRight, safe, 4, 12);
    const Rect unclamped = brief::PanelRect(bottomRight, screen, 4, 12);
    BOOST_TEST(clamped.bottom < unclamped.bottom);
    BOOST_TEST(unclamped.bottom <= bottomRight.bottom);
    // ... und die Ansicht OBEN links wird davon nicht angefasst: die Naht zwischen zwei
    // Ansichten schneidet kein Fernseher ab.
    BOOST_TEST(panel.bottom == brief::PanelRect(topLeft, screen, 4, 12).bottom);
}

/// NK9 in der Fassung, die hier moeglich ist. BEWEISEN, dass der Kasten auf dem Fernseher
/// erscheint, kann kein Test - der Zeichenweg des Baureiters ist in der Testumgebung gesperrt
/// (Loader::GetNationIcon), und der Renderer ist ohnehin der DummyRenderer, dessen Methoden
/// allesamt No-Ops sind.
///
/// Was der Aufruf trotzdem abdeckt: dass DrawBrief mit einem echten, vollen Klartextblock durch
/// den Umbruch (glFont::GetWrapInfo auf den Dummy-Schriften), durch PanelRect und durch alle
/// Zeichenaufrufe laeuft, ohne zu stolpern - also die Rechnung DAVOR, nicht das Bild danach.
/// Mehr zu behaupten waere unehrlich.
BOOST_FIXTURE_TEST_CASE(DrawingTheFullPanelRunsThroughWithoutStumbling, PadViewFixture<2>)
{
    const MapPoint spot = findBuildSpot(worldFixture.world, view(1).GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(spot.isValid());
    takePad(*this, 11, 1, spot);

    // Der laengste Block, den es gibt: ein Gebaeude mit Zweck, Standortbedingung, drei
    // Nachschubwaren und Kosten.
    view(1).SetBrief(brief::ForBuilding(BuildingType::GraniteMine));
    BOOST_TEST_REQUIRE(view(1).GetBrief().lines.size() == 4u);
    dsk->DrawBrief(view(1));

    // ... und der leere, den jede Ansicht ohne Pad hat.
    view(0).SetBrief(brief::Brief());
    dsk->DrawBrief(view(0));

    // Der Knotenblock, den der Spieler beim Erkunden am haeufigsten sieht.
    view(1).SetBrief(brief::ForNode(brief::NodeVerdict::FlagOnly));
    dsk->DrawBrief(view(1));
}

// ============================================================================================
// 6. Was die Saetze BEHAUPTEN, gegen das, was in den Spieldaten steht
// ============================================================================================

namespace {

/// Kommt dieser Gebaeudename in dem Satz vor?
///
/// Zwei Schreibungen, weil ein Name mitten im Satz klein anfaengt, wo die Sprache es so will
/// (englisch "woodcutter"), und gross, wo sie es anders will (deutsch "Holzfaeller"). Beide
/// Seiten laufen durch _(), der Vergleich stimmt damit in JEDEM Katalog - und nicht nur in dem,
/// in dem der Test zufaellig laeuft.
bool namesBuilding(const std::string& sentence, const std::string& name)
{
    if(name.empty())
        return false;
    if(sentence.find(name) != std::string::npos)
        return true;
    std::string lowered = name;
    // Nur das erste Zeichen, und nur wenn es ASCII ist: jeder Gebaeudename faengt mit einem
    // Buchstaben aus dem ASCII-Bereich an, der REST kann UTF-8 sein (Holzfaeller) und darf
    // deshalb nicht byteweise angefasst werden.
    const auto first = static_cast<unsigned char>(lowered[0]);
    if(first >= 0x80)
        return false;
    lowered[0] = static_cast<char>(std::tolower(first));
    return sentence.find(lowered) != std::string::npos;
}

} // namespace

namespace {

/// Zaehlt dieses Byte zu einem Wort?
///
/// Bytes >= 0x80 ja: die Umlaute des deutschen Katalogs sind UTF-8 und duerfen keine Wortgrenze
/// sein, sonst waere "Muenz" ein Treffer in "Muenzpraegerei".
bool isWordByte(const char c)
{
    const auto u = static_cast<unsigned char>(c);
    return u >= 0x80 || std::isalpha(u) != 0;
}

/// Alle Stellen, an denen `word` als EIGENES Wort in `text` steht.
///
/// GANZWORT und nicht Teilzeichenkette, und daran haengt der halbe Nutzen dieses Abschnitts:
/// "Golderz" enthaelt "Gold", "Fleischerei" enthaelt "Fleisch", "Holzfaeller" enthaelt "Holz".
/// Wer solche Treffer zaehlt, prueft nicht mehr, ob ein Satz DAS WORT DER WARE benutzt, sondern
/// nur noch, ob irgendwo etwas Aehnliches steht - und genau dieser Unterschied ist der Befund.
///
/// Der erste Buchstabe wird zusaetzlich klein probiert: derselbe Name steht mitten im englischen
/// Satz klein ("boards") und im deutschen gross ("Bretter").
std::vector<size_t> wordHits(const std::string& text, const std::string& word)
{
    std::vector<size_t> hits;
    if(word.empty())
        return hits;
    std::vector<std::string> forms{word};
    if(const auto first = static_cast<unsigned char>(word[0]); first < 0x80 && std::isupper(first) != 0)
    {
        std::string lowered = word;
        lowered[0] = static_cast<char>(std::tolower(first));
        forms.push_back(lowered);
    }
    for(const std::string& form : forms)
    {
        for(size_t pos = text.find(form); pos != std::string::npos; pos = text.find(form, pos + 1))
        {
            if(pos > 0 && isWordByte(text[pos - 1]))
                continue;
            const size_t end = pos + form.size();
            if(end < text.size() && isWordByte(text[end]))
                continue;
            hits.push_back(pos);
        }
    }
    return hits;
}

/// Nennt `text` dieses Wort - und zwar so, dass der Treffer nicht bloss in einem LAENGEREN Namen
/// derselben Liste steckt?
///
/// Ohne diese Ausnahme waere "the pig farm" ein Treffer fuer das Gebaeude "Farm" und "iron ore"
/// einer fuer die Ware "Iron". Beide kuerzeren Namen gibt es wirklich, deshalb genuegt es nicht,
/// sie wegzulassen; der laengere Name gewinnt an der Stelle, an der er steht.
bool namesWord(const std::string& text, const std::string& word, const std::vector<std::string>& vocabulary)
{
    for(const size_t pos : wordHits(text, word))
    {
        bool masked = false;
        for(const std::string& other : vocabulary)
        {
            if(other.size() <= word.size())
                continue;
            for(const size_t otherPos : wordHits(text, other))
            {
                if(otherPos <= pos && pos + word.size() <= otherPos + other.size())
                {
                    masked = true;
                    break;
                }
            }
            if(masked)
                break;
        }
        if(!masked)
            return true;
    }
    return false;
}

/// Die Woerter, gegen die geprueft wird. `translate` schaltet zwischen Quelltext (msgid) und
/// geladenem Katalog um - dieselbe Rechnung, zwei Sprachen.
std::vector<std::string> wareVocabulary(const bool translate)
{
    std::vector<std::string> out;
    for(const GoodType good : helpers::enumRange<GoodType>())
    {
        const std::string& name = WARE_NAMES[good];
        if(!name.empty())
            out.push_back(translate ? _(name) : name);
    }
    return out;
}

std::vector<std::string> buildingVocabulary(const bool translate)
{
    std::vector<std::string> out;
    for(const BuildingType bld : helpers::enumRange<BuildingType>())
    {
        const char* name = BUILDING_NAMES[bld];
        if(name && *name)
            out.emplace_back(translate ? _(name) : name);
    }
    return out;
}

/// Der von HAND geschriebene Teil eines Gebaeudeblocks: alle Zeilen ausser den beiden, die
/// PlayerBrief.cpp aus den Spieldaten zusammensetzt (Nachschub und Kosten).
///
/// Erkannt an ihren Praefixen, nicht an ihrer Position - und der Aufrufer rechnet gegen, wie
/// viele Zeilen wegfallen MUESSEN. Ohne diese Gegenrechnung koennte der Filter still zu viel
/// oder zu wenig entfernen, und der Fall waere wieder so leer wie sein Vorgaenger.
std::vector<std::string> proseLines(const brief::Brief& b, unsigned& removed)
{
    const std::string supplyPrefix = _("Supplies needed: ");
    const std::string costPrefix = _("Costs: ");
    std::vector<std::string> out;
    removed = 0;
    for(const std::string& line : b.lines)
    {
        if(line.rfind(supplyPrefix, 0) == 0 || line.rfind(costPrefix, 0) == 0)
            ++removed;
        else
            out.push_back(line);
    }
    return out;
}

std::string joinLines(const std::vector<std::string>& lines)
{
    std::string out;
    for(const std::string& line : lines)
    {
        if(!out.empty())
            out += ' ';
        out += line;
    }
    return out;
}

/// Alle Kataloge, die dieser Bau ausliefert - als Locale-Code, wie mygettext ihn versteht.
///
/// Aus den DATEINAMEN und nicht aus einer Liste im Testcode: mygettext sucht seinen Katalog
/// unter genau diesem Namen (GetText::getCatalogFilePath, Zweig "dirname/rttr-<locale>.mo").
/// Eine Liste hier veraltete beim ersten neuen Katalog, ohne dass es jemandem auffiele.
std::vector<std::string> shippedCatalogs()
{
    std::vector<std::string> out;
    const boost::filesystem::path dir = RTTRCONFIG.ExpandPath(s25::folders::languages);
    for(const auto& entry : boost::filesystem::directory_iterator(dir))
    {
        if(!is_regular_file(entry.status()) || entry.path().extension() != ".mo")
            continue;
        const std::string stem = entry.path().stem().string();
        if(stem.rfind("rttr-", 0) == 0)
            out.push_back(stem.substr(5));
    }
    std::sort(out.begin(), out.end());
    return out;
}

/// Alle Gebaeude, die diese Ware herstellen - aus BLD_WORK_DESC und aus nichts sonst.
std::vector<BuildingType> producersOf(const GoodType good)
{
    std::vector<BuildingType> out;
    for(const BuildingType bld : helpers::enumRange<BuildingType>())
    {
        const auto& made = BLD_WORK_DESC[bld].producedWare;
        if(holds_alternative<GoodType>(made) && get<GoodType>(made) == good)
            out.push_back(bld);
    }
    return out;
}

/// Was ein Durchgang wirklich angefasst hat. Ohne diese Zahlen koennte der Fall eines Tages
/// still schrumpfen und bliebe trotzdem gruen - genau das ist seinem Vorgaenger passiert.
struct PassCount
{
    unsigned buildings = 0, produced = 0, consumed = 0, suppliers = 0, names = 0;
};

} // namespace

/// BEFUND 1 UND BEFUND 2 der zweiten Pruefung und BEFUND A/B der dritten in einem Fall. Der Kopf
/// ist lang, weil zwei Vorgaenger dieses Falls GRUEN waren und trotzdem nichts bewacht haben -
/// das ist die schlimmste Fehlerklasse, die dieses Verzeichnis kennt, und sie soll nachlesbar
/// bleiben.
///
/// GEMESSEN AM BILDSCHIRM, deutscher Katalog, vor der ersten Korrektur:
///
///     Fleischerei   Macht aus dem Schinken der Schweinezucht Fleisch, eine der drei Speisen
///                   deiner Bergleute.   Nachschub: Fleisch   Kostet: 2 Bretter, 2 Steine
///
/// Der Satz sagt, das Gebaeude ERZEUGE Fleisch; die Zeile darunter - aus BLD_WORK_DESC und
/// WARE_NAMES, also aus den Spieldaten - sagt, es BRAUCHE Fleisch. Ursache ist, dass der
/// deutsche Katalog die beiden Fleischwaren umgekehrt benennt (msgid "Ham" -> "Fleisch",
/// msgid "Meat" -> "Schinken").
///
/// WARUM DIE VORGAENGER GENAU DAS DURCHLIESSEN - vier Loecher, alle vier nachgemessen:
///
///  1. Der erste las brief::ForBuilding(bld).joined(), also EINSCHLIESSLICH der Nachschubzeile.
///     Wo eine Ware angeliefert wird, steht ihr Wort dort ohnehin - die Forderung "dieser Block
///     nennt diese Ware" war fuer jede Eingangsware also leer.
///  2. Er kannte zwei Waren (Ham, Meat) von den 24, die in einem Block ueberhaupt vorkommen
///     koennen, und sechs Gebaeude von 39.
///  3. Er hatte KEINE Fixture, also keinen geladenen Katalog: _() lieferte die englische msgid
///     zurueck, und der Fall lief in der Sprache, in der es den Fehler gar nicht gibt.
///  4. BEFUND A DER DRITTEN PRUEFUNG: der zweite Anlauf holte sich den Katalog ueber eine
///     Fixture - und damit ueber die SYSTEMSPRACHE (Settings::LoadDefaults ruft
///     LANGUAGES.setLanguage("") und das ist die Spracheinstellung des Rechners). Auf einer
///     deutschen Maschine lief er, auf jeder anderen brach er ab:
///
///         LC_ALL=en_US.UTF-8 ./Test_splitscreen.exe --run_test=".../EverySentence..."
///         testPadBrief.cpp(1126): fatal error: critical check
///             std::string(_("Woodcutter")) != "Woodcutter" has failed
///
///     Ein Nachweis, der nur auf einem Rechner laeuft, ist schlimmer als keiner: er blockiert
///     alle anderen und bewacht dort nichts. Deshalb stellt dieser Fall den Katalog SELBST ein
///     (rttr::test::LocaleResetter, dieselbe Hilfe, die testLocalization.cpp benutzt) und
///     rechnet ueber JEDEN ausgelieferten Katalog. Der deutsche Durchgang ist hart gefordert.
///
/// WAS GEPRUEFT WIRD, ueber ALLE 39 benannten Gebaeude und ALLE 24 Waren, die in einem Block
/// vorkommen koennen:
///
///  R1  Der ZWECKSATZ nennt die Ware, die das Gebaeude herstellt (BLD_WORK_DESC::producedWare),
///      mit genau dem Wort aus WARE_NAMES, als ganzes Wort.
///  R2  Die Prosa nennt KEINE Ware, die das Gebaeude anliefern laesst
///      (BLD_WORK_DESC::waresNeeded). Diese Woerter stehen bereits in der Nachschubzeile
///      darunter; ein zweites Mal von Hand geschrieben sind sie genau die Stelle, an der der
///      Ausloeser entstanden ist. Was nur einmal und nur aus den Daten kommt, kann sich nicht
///      widersprechen.
///  R2b Die Prosa nennt AUCH NICHT das Gebaeude, das eine ihrer Eingangswaren herstellt.
///
///      BEFUND B DER DRITTEN PRUEFUNG, und der Grund, warum es diese Regel gibt: R2 ist eine
///      Regel ueber ein WORT, und Woerter haben Synonyme. Zwei ausgelieferte Saetze haben R2
///      nicht erfuellt, sondern UMSCHRIEBEN, und sind gruen durchgelaufen:
///
///          Saegewerk  "Cuts logs into boards ... right after your first woodcutter."
///                     ueber "Nachschub: Holz"    ("logs"/"Staemme" = Wood)
///          Abdecker   "Delivers skins from what the pig farm raises."
///                     ueber "Nachschub: Fleisch" ("was die Schweinezucht heranzieht" = Ham)
///
///      Beide nehmen denselben Weg: sie schreiben die LIEFERBEZIEHUNG von Hand neben die
///      Nachschubzeile, die sie schon aus den Daten sagt. Genau das faengt R2b, ohne
///      Ausnahmeliste und in jeder Sprache - denn das Gebaeude hat einen Namen in BUILDING_NAMES
///      und der ist nachschlagbar. Ein Gebaeude als ABNEHMER zu nennen bleibt erlaubt
///      ("Grinds flour for the bakery"): das ist keine Aussage ueber den eigenen Nachschub.
///  R3  Nennt der ENGLISCHE Quelltext ein Gebaeude beim Namen, dann nennt der uebersetzte Satz
///      es mit dem Namen aus BUILDING_NAMES - also mit dem Wort, das im Baumenue steht.
///      (Gemessen: der Tempelsatz sagte "Goldmine", das Menue sagt "Goldbergwerk".)
///
/// WAS DIESER FALL NICHT KANN, damit niemand mehr verspricht als der Code haelt:
///
///  a) Ein reines SYNONYM fuer eine Ware, das kein Gebaeude nennt, faellt durch alle vier
///     Regeln. Der Katapultsatz war so einer - "Hurls rocks"/"Schleudert Felsbrocken" ueber
///     "Nachschub: Steine". Er ist von Hand umgeschrieben worden ("Fires at ..."), und kein
///     Nachweis kann verhindern, dass morgen jemand ein neues Ersatzwort erfindet. Die Regel im
///     Kopf von BuildingBriefs.cpp verbietet es; nachrechnen laesst sich nur die Haelfte davon.
///  b) Er faellt nicht auf ein FALSCHES Wort fuer eine Ware herein, das gar keiner Ware gehoert.
///     Sagte ein deutscher Satz "Leder", wo die Ware "Leather" heisst, waere das ein
///     Widerspruch, den R2 nicht sieht - R2 verbietet nur das RICHTIGE Wort der Eingangsware.
///     Aufgefangen wird der Fall dadurch, dass R2 im QUELLTEXTdurchgang laeuft: dort heisst die
///     Ware "Leather", der englische Satz darf sie also gar nicht erst nennen.
///  c) Ein Katalog, der einen Satz NICHT uebersetzt, wird fuer diesen Satz uebersprungen - der
///     Spieler liest dort den englischen, und der steht im Quelltextdurchgang. Geprueft wird je
///     SATZ und nicht je Katalog, damit eine halb fertige Uebersetzung weder blockiert noch
///     ungeprueft durchrutscht.
///  d) Alle vier Regeln suchen GANZE WOERTER. Eine gebeugte Form entkommt ihnen: nachgemessen
///     faellt R2b auf "nach deinem ersten Holzfaeller" herein, auf "aus dem Holz deines ersten
///     Holzfaellers" dagegen nicht - das Genitiv-s ist ein Wortbyte und beendet den Treffer.
///     Ein Stammformenabgleich stuende dem gegenueber, was diese Regeln wert sind: er braeuchte
///     je Sprache eine Liste, und die veraltet. Die Ganzwortsuche faengt die Form, die ein
///     Uebersetzer natuerlich schreibt; mehr behauptet sie nicht.
BOOST_AUTO_TEST_CASE(EverySentenceAboutAWareUsesTheWordTheSupplyLineUses)
{
    // Ohne dies suchte mygettext unter "/usr/share/locale" und lieferte stumm die msgid zurueck
    // - der Katalogdurchgang waere eine Kopie des Quelltextdurchgangs. WELCHE Sprache es wird,
    // sagt gleich der LocaleResetter und sonst niemand.
    bindCatalogDir();

    std::set<GoodType> waresSeen;

    // EIN Durchgang ueber alle benannten Gebaeude. `translate == false` rechnet auf dem
    // Quelltext (den msgids), `true` auf dem Katalog, den der AUFRUFER vorher eingestellt hat -
    // dieser Fall liest die Systemsprache an keiner Stelle mehr.
    const auto runPass = [&](const std::string& pass, const bool translate) {
        PassCount count;
        const std::vector<std::string> wares = wareVocabulary(translate);
        const std::vector<std::string> buildings = buildingVocabulary(translate);
        const std::vector<std::string> sourceBuildings = buildingVocabulary(false);
        for(const BuildingType bld : helpers::enumRange<BuildingType>())
        {
            const char* name = BUILDING_NAMES[bld];
            if(!name || !*name)
                continue; // Nothing9 - kein Name, kein Icon, kein Block
            const char* purposeSrc = BUILDING_PURPOSE_STRINGS[bld];
            const char* siteSrc = BUILDING_SITE_STRINGS[bld];
            BOOST_TEST_REQUIRE((purposeSrc && *purposeSrc));

            std::string sourceProse = purposeSrc;
            if(siteSrc && *siteSrc)
                sourceProse += std::string(" ") + siteSrc;

            std::string purpose, prose;
            if(translate)
            {
                // Siehe (c) im Kopf: was dieser Katalog nicht uebersetzt hat, liest der Spieler
                // dort englisch - und der englische Satz steht im Quelltextdurchgang.
                if(std::string(_(purposeSrc)) == purposeSrc)
                    continue;
                const brief::Brief b = brief::ForBuilding(bld);
                unsigned removed = 0;
                const std::vector<std::string> lines = proseLines(b, removed);
                const BuildingCost cost = BUILDING_COSTS[bld];
                const unsigned expectRemoved =
                  (BLD_WORK_DESC[bld].waresNeeded.empty() ? 0u : 1u) + ((cost.boards > 0 || cost.stones > 0) ? 1u : 0u);
                BOOST_TEST_CONTEXT(pass << " | " << name << " -> " << b.joined())
                {
                    // Genau die maschinellen Zeilen sind weg - keine mehr, keine weniger.
                    BOOST_TEST_REQUIRE(removed == expectRemoved);
                    BOOST_TEST_REQUIRE(!lines.empty());
                    // Die erste Prosazeile IST der Zwecksatz. Nachgeschlagen, nicht angenommen.
                    BOOST_TEST_REQUIRE(lines.front() == std::string(_(purposeSrc)));
                }
                purpose = lines.front();
                prose = joinLines(lines);
            } else
            {
                purpose = purposeSrc;
                prose = sourceProse;
            }
            ++count.buildings;

            const BldWorkDescription& work = BLD_WORK_DESC[bld];

            // R1 - was es herstellt, steht mit dem Wort der Ware im Zwecksatz.
            if(holds_alternative<GoodType>(work.producedWare))
            {
                if(const GoodType made = get<GoodType>(work.producedWare); made != GoodType::Nothing)
                {
                    const std::string word = translate ? _(WARE_NAMES[made]) : WARE_NAMES[made];
                    BOOST_TEST_CONTEXT(pass << " | " << name << " stellt her: " << word << " | " << purpose)
                    {
                        BOOST_TEST(namesWord(purpose, word, wares));
                    }
                    waresSeen.insert(made);
                    ++count.produced;
                }
            }

            // R2 - was angeliefert wird, steht NICHT in der Prosa; es steht in der Nachschubzeile.
            for(const GoodType needed : work.waresNeeded)
            {
                const std::string word = translate ? _(WARE_NAMES[needed]) : WARE_NAMES[needed];
                BOOST_TEST_CONTEXT(pass << " | " << name << " bekommt geliefert: " << word << " | " << prose)
                {
                    BOOST_TEST(!namesWord(prose, word, wares));
                }
                // R2b - und auch nicht das Gebaeude, aus dem sie kommt.
                for(const BuildingType src : producersOf(needed))
                {
                    const char* srcName = BUILDING_NAMES[src];
                    if(!srcName || !*srcName)
                        continue;
                    const std::string srcWord = translate ? _(srcName) : srcName;
                    BOOST_TEST_CONTEXT(pass << " | " << name << " bekommt " << word << " vom Gebaeude " << srcWord
                                            << " | " << prose)
                    {
                        BOOST_TEST(!namesWord(prose, srcWord, buildings));
                    }
                    ++count.suppliers;
                }
                waresSeen.insert(needed);
                ++count.consumed;
            }

            // R3 - jedes Gebaeude, das der Quelltext beim Namen nennt, traegt im Katalog den
            // Namen des BAUMENUES. Nur im Katalogdurchgang: der erste ist der Quelltext selbst.
            if(!translate)
                continue;
            for(const BuildingType other : helpers::enumRange<BuildingType>())
            {
                const char* otherName = BUILDING_NAMES[other];
                if(!otherName || !*otherName)
                    continue;
                if(!namesWord(sourceProse, otherName, sourceBuildings))
                    continue;
                BOOST_TEST_CONTEXT(pass << " | " << name << " nennt " << otherName << " = " << _(otherName) << " | "
                                        << prose)
                {
                    BOOST_TEST(namesWord(prose, std::string(_(otherName)), buildings));
                }
                ++count.names;
            }
        }
        return count;
    };

    // --- Durchgang 1: der englische Quelltext. Er braucht ueberhaupt keinen Katalog.
    const PassCount source = runPass("Quelltext", false);
    BOOST_TEST(source.buildings == 39u);
    BOOST_TEST(source.produced == 28u);
    BOOST_TEST(source.consumed == 49u);
    BOOST_TEST(source.suppliers == 59u);
    BOOST_TEST(source.names == 0u); // R3 ist eine Aussage ueber Uebersetzungen

    // --- Durchgang 2..n: jeder ausgelieferte Katalog, ausdruecklich eingestellt.
    const std::vector<std::string> catalogs = shippedCatalogs();
    BOOST_TEST_REQUIRE(!catalogs.empty());
    std::vector<std::string> withTranslations;
    PassCount german;
    for(const std::string& code : catalogs)
    {
        const rttr::test::LocaleResetter useCatalog(code.c_str());
        if(code == "de")
        {
            // DASS der deutsche Katalog wirklich geladen ist - sonst waere dieser Durchgang
            // eine Kopie des ersten und der Ausloeser unsichtbar. Genau hier stand vorher die
            // Zusicherung, die auf jeder nicht-deutschen Maschine abbrach; sie steht jetzt
            // hinter einer SELBST eingestellten Sprache und gilt deshalb ueberall.
            BOOST_TEST_REQUIRE(std::string(_("Woodcutter")) != "Woodcutter");
            // ... und die Ware, an der der Ausloeser haengt, heisst im Katalog wirklich anders
            // als im Quelltext.
            BOOST_TEST_REQUIRE(std::string(_(WARE_NAMES[GoodType::Ham])) != WARE_NAMES[GoodType::Ham]);
        }
        const PassCount pass = runPass("Katalog " + code, true);
        if(pass.buildings == 0u)
            continue;
        withTranslations.push_back(code);
        if(code == "de")
            german = pass;
    }
    BOOST_TEST_MESSAGE("Kataloge: " << catalogs.size()
                                    << ", davon mit uebersetzten Zwecksaetzen: " << withTranslations.size());

    // ABDECKUNG. Der deutsche Durchgang ist HART gefordert - faellt er aus, faellt der Fall.
    BOOST_TEST(std::count(withTranslations.begin(), withTranslations.end(), std::string("de")) == 1);
    BOOST_TEST(german.buildings == 39u);
    BOOST_TEST(german.produced == 28u);
    BOOST_TEST(german.consumed == 49u);
    BOOST_TEST(german.suppliers == 59u);
    BOOST_TEST(german.names == 25u);
    // ALLE Waren, die ein Block ueberhaupt nennen kann. Die uebrigen 16 Eintraege von WARE_NAMES
    // (die Werkzeuge ausser der Zange, die vier Schilde, der leere Wassereimer) werden von
    // keinem Gebaeude hergestellt oder verbraucht - ueber sie kann ein Block der Nachschubzeile
    // also gar nicht widersprechen.
    BOOST_TEST(waresSeen.size() == 24u);

    // Die Querverbindung, die der erste Pruefer eigens genannt hat, noch einmal ausgeschrieben:
    // die Bergwerke listen "Fisch oder X oder Brot", der Satz der Fleischerei spricht von "einer
    // der drei Speisen deiner Bergleute". X und die Speise MUESSEN dasselbe Wort sein. Aus R1
    // und R2 folgt das bereits; hier steht es noch einmal an der Stelle, an der es gemessen
    // wurde - der Nachschubzeile eines Bergwerks gegen den Zwecksatz der Fleischerei. Der
    // Katalog wird auch dafuer ausdruecklich eingestellt.
    const rttr::test::LocaleResetter useGerman("de");
    const std::string meat = _(WARE_NAMES[GoodType::Meat]);
    const std::string ham = _(WARE_NAMES[GoodType::Ham]);
    BOOST_TEST_REQUIRE(meat != ham);
    const std::vector<std::string> wares = wareVocabulary(true);
    unsigned dropped = 0;
    const std::string minePanel = joinLines(proseLines(brief::ForBuilding(BuildingType::CoalMine), dropped));
    const std::string mineSupply = brief::ForBuilding(BuildingType::CoalMine).lines[2];
    BOOST_TEST(mineSupply.rfind(_("Supplies needed: "), 0) == 0u);
    BOOST_TEST(namesWord(mineSupply, meat, wares));
    BOOST_TEST(!namesWord(minePanel, ham, wares));
    BOOST_TEST(
      namesWord(joinLines(proseLines(brief::ForBuilding(BuildingType::Slaughterhouse), dropped)), meat, wares));
}

/// KLEINERES 5 der Pruefung: die Huettenliste liess Spaehturm, Abdecker und Wachstube aus - drei
/// von zehn. Ein Doppelpunkt verspricht eine Liste, und der Anfaenger trifft danach seine Wahl;
/// eine Liste, die drei Antworten verschweigt, ist schlechter als keine.
///
/// Geprueft wird gegen BUILDING_SIZE und nicht gegen eine zweite Aufzaehlung im Test: sonst
/// stuenden zwei Listen nebeneinander, die beide veralten koennen.
///
/// Der Katalog wird ausdruecklich eingestellt: hier steht ein uebersetzter SATZ gegen
/// uebersetzte NAMEN, und das ergibt nur innerhalb eines Katalogs einen Sinn (siehe
/// GermanCatalog).
BOOST_AUTO_TEST_CASE(TheHutListNamesEveryHutAndNothingElse)
{
    const GermanCatalog catalog;
    const std::string sentence = brief::ForNode(brief::NodeVerdict::Hut).joined();
    unsigned huts = 0;
    for(const BuildingType bld : helpers::enumRange<BuildingType>())
    {
        const char* name = BUILDING_NAMES[bld];
        if(!name || !*name)
            continue;
        const bool isHut = BUILDING_SIZE[bld] == BuildingQuality::Hut;
        if(isHut)
            ++huts;
        BOOST_TEST_CONTEXT(name << " -> " << sentence)
        {
            // Jede Huette steht drin ...
            // ... und nichts, was keine ist. Der zweite Teil ist der wichtigere: er faengt den
            // Fehler, bei dem jemand ein Gebaeude nennt, das hier gar nicht hinpasst.
            BOOST_TEST(namesBuilding(sentence, _(name)) == isHut);
        }
    }
    // Abdeckungskontrolle: dass ueberhaupt zehn Huetten gefunden wurden.
    BOOST_TEST(huts == 10u);
}

/// Dieselbe Regel fuer die zweite Aufzaehlung im Klartext: der Satz zum Niemandsland nennt die
/// Wachposten, mit denen die Grenze waechst. Auch das ist eine Liste, die vollstaendig sein muss
/// - und auch hier muessen die Namen die des BAUMENUES sein.
///
/// Gemessen und behoben: der deutsche Satz nannte ein "Wachhaus", das Baumenue eine
/// "Wachstube". Ein Anfaenger, der genau das sucht, findet es dort nicht. Derselbe Fehler stand
/// in den Bergwerkssaetzen ("Waffenschmiede" statt "Schmiede").
BOOST_AUTO_TEST_CASE(TheGuardPostListNamesEveryMilitaryBuilding)
{
    const GermanCatalog catalog;
    const std::string sentence = brief::ForNode(brief::NodeVerdict::NoMansLand).joined();
    for(const BuildingType bld : BuildingProperties::militaryBldTypes)
    {
        BOOST_TEST_CONTEXT(BUILDING_NAMES[bld] << " -> " << sentence)
        {
            BOOST_TEST(namesBuilding(sentence, _(BUILDING_NAMES[bld])));
        }
    }
    // Und die beiden Saetze, in denen die Schmiede als ABNEHMER vorkommt, nennen sie ebenso.
    for(const BuildingType bld : {BuildingType::CoalMine, BuildingType::Ironsmelter})
    {
        const std::string text = brief::ForBuilding(bld).joined();
        BOOST_TEST_CONTEXT(BUILDING_NAMES[bld] << " -> " << text)
        {
            BOOST_TEST(namesBuilding(text, _(BUILDING_NAMES[BuildingType::Armory])));
        }
    }
}

/// KLEINERES 4 der Pruefung. Der alte Satz lautete woertlich "Every building bigger than a hut
/// is made of it" und war als Faustregel brauchbar, woertlich aber falsch: die Wachstube ist
/// eine Huette und kostet 3 Steine, die vier Bergwerke sind groesser als eine Huette und kosten
/// keinen einzigen.
///
/// Der neue Satz behauptet zwei ueberpruefbare Dinge, und beide werden hier gegen
/// BUILDING_COSTS nachgezaehlt: die MEISTEN Gebaeude brauchen Steine, und wer Steine braucht,
/// braucht auch Bretter.
BOOST_AUTO_TEST_CASE(TheQuarrySentenceSaysWhatTheCostTableSays)
{
    unsigned buildable = 0, withStones = 0;
    bool hutCostsStones = false, mineCostsNoStones = false;
    for(const BuildingType bld : helpers::enumRange<BuildingType>())
    {
        if(bld == BuildingType::Nothing9 || bld == BuildingType::Headquarters)
            continue; // nicht baubar
        const BuildingCost cost = BUILDING_COSTS[bld];
        ++buildable;
        if(cost.stones > 0)
        {
            ++withStones;
            // "on top of boards" - es gibt kein Gebaeude, das Steine, aber keine Bretter kostet.
            BOOST_TEST_CONTEXT(BUILDING_NAMES[bld])
            {
                BOOST_TEST(cost.boards > 0);
            }
            if(BUILDING_SIZE[bld] == BuildingQuality::Hut)
                hutCostsStones = true;
        } else if(BUILDING_SIZE[bld] == BuildingQuality::Mine)
            mineCostsNoStones = true;
    }
    BOOST_TEST(buildable == 38u);
    // "Most buildings": echte Mehrheit, nicht die Haelfte.
    BOOST_TEST(withStones * 2u > buildable);
    // Und die beiden Gegenbeispiele, an denen der ALTE Satz zerbrach, gibt es wirklich - sonst
    // waere die Korrektur eine Behauptung ueber einen Fehler, den es gar nicht gab.
    BOOST_TEST(hutCostsStones);
    BOOST_TEST(mineCostsNoStones);
    BOOST_TEST(brief::ForBuilding(BuildingType::Quarry).joined().find("bigger than a hut") == std::string::npos);
}

// ============================================================================================
// 7. Das Aktionsfenster gegen den Kasten - eine Messung, keine Schaetzung
// ============================================================================================

namespace {

/// So gross ist das Aktionsfenster wirklich: iwAction.cpp:49 uebergibt Extent(200, 254) an
/// IngameWindow, und IngameWindow zieht davon nichts ab (es vergroessert nur, falls Rahmen und
/// Titelleiste mehr braeuchten). Die Zahl steht hier als Konstante und nicht als Aufruf, weil
/// dieser Test ohne geladene Rahmengrafiken auskommen soll.
constexpr Extent kActionWindowSize(200, 254);

/// Wo IngameWindow::SetPos ein Fenster dieser Groesse hinstellt, wenn es an `wanted` soll.
/// Wortgleich zu IngameWindow.cpp:138-176, auf die beiden Klemmen zusammengezogen.
Position clampWindow(const Position& wanted, const Extent& size, const Extent& renderSize)
{
    const Rect bounds = tv::WindowBoundsRect(renderSize, size);
    const Position maxPos = bounds.getEndPt() - size;
    Position pos = wanted;
    pos.x = std::min(std::max(pos.x, bounds.left), maxPos.x);
    pos.y = std::min(std::max(pos.y, bounds.top), maxPos.y);
    return pos;
}

long overlapOf(const Rect& a, const Rect& b)
{
    const long w = std::min(a.right, b.right) - std::max(a.left, b.left);
    const long h = std::min(a.bottom, b.bottom) - std::max(a.top, b.top);
    return (w > 0 && h > 0) ? w * h : 0;
}

/// Anteil der Zeigerstellungen einer Ansicht, in denen das Aktionsfenster den Kasten verdeckt -
/// und wieviel es im schlimmsten Fall verdeckt. Gerastert in 10er-Schritten, das sind je
/// Ansicht einige tausend Stellungen.
struct Coverage
{
    unsigned positions = 0, covered = 0;
    double worstFraction = 0.0;
    double coveredFraction() const { return positions ? double(covered) / positions : 0.0; }
};

Coverage measureCoverage(const Extent& renderSize, unsigned numViews, unsigned viewIdx, bool dodge)
{
    const Rect safeArea = tv::ActiveSafeAreaRect(renderSize);
    const Viewport vp = CalcViewports(renderSize, numViews)[viewIdx];
    const Rect viewport(vp.origin, vp.size);
    constexpr unsigned numLines = 6, lineHeight = 12; // ein voller Gebaeudeblock, umgebrochen

    Coverage out;
    for(int y = viewport.top; y < viewport.bottom; y += 10)
    {
        for(int x = viewport.left; x < viewport.right; x += 10)
        {
            const Position pos = clampWindow(Position(x, y), kActionWindowSize, renderSize);
            const Rect window(pos, kActionWindowSize);
            const Rect panel = dodge ? brief::PanelRect(viewport, safeArea, numLines, lineHeight, window) :
                                       brief::PanelRect(viewport, safeArea, numLines, lineHeight);
            ++out.positions;
            const long ov = overlapOf(panel, window);
            if(ov > 0)
            {
                ++out.covered;
                const auto area = static_cast<long>(panel.getSize().x) * static_cast<long>(panel.getSize().y);
                out.worstFraction = std::max(out.worstFraction, area ? double(ov) / double(area) : 1.0);
            }
        }
    }
    return out;
}

/// Fernsehmodus an, danach wieder wie vorgefunden. Ohne ihn ist die Safe Area die volle Flaeche
/// und die Messung saehe die Zahlen des Auftraggebers gar nicht.
struct TvModeGuard
{
    bool oldMode = SETTINGS.video.tvMode;
    unsigned oldPercent = SETTINGS.video.tvSafeAreaPercent;
    TvModeGuard()
    {
        SETTINGS.video.tvMode = true;
        SETTINGS.video.tvSafeAreaPercent = tv::SAFE_AREA_PERCENT_DEFAULT;
    }
    ~TvModeGuard()
    {
        SETTINGS.video.tvMode = oldMode;
        SETTINGS.video.tvSafeAreaPercent = oldPercent;
    }
};

} // namespace

/// BEFUND 3 der ERSTEN Pruefung, nachgemessen statt geglaubt: "in etwa 70 Prozent der
/// Zeigerstellungen verdeckt das Aktionsfenster ein Fuenftel des Infokastens".
///
/// BEFUND 3 DER ZWEITEN PRUEFUNG, und der Grund, warum dieser Fall jetzt anders aussieht: die
/// Fassung davor hat NUR 1920x1080 und 3840x2160 gerechnet und daraus "gar keine Ueberdeckung
/// mehr, in keiner Stellung" gemacht. Bei 1280x720 mit vier Ansichten stimmt das nicht - dort
/// ist der Kasten in JEDER Zeigerstellung angeschnitten. Die Begruendung im Kopf von
/// PlayerBrief.h ("dafuer muesste das Fenster ueber 470 Punkte hoch sein") war doppelt falsch:
/// die Zahl ist nicht 470, sondern der senkrechte Abstand der beiden Plaetze, und der haengt an
/// der Ansichtshoehe.
///
/// WAS WIRKLICH GILT, hier gerechnet und nicht behauptet: das Aktionsfenster kann beide Plaetze
/// nur dann zugleich treffen, wenn es hoeher ist als der Abstand zwischen ihnen. Der Abstand ist
///
///     Ansichtshoehe - 12 (zwei Raender) - 2 x Kastenhoehe - Anteil der Safe Area
///
/// und die Kastenhoehe des vollen Gebaeudeblocks ist 6 x 12 + 8 = 80. Auf 1080p mit vier
/// Ansichten sind das 540 - 12 - 160 - 54 = 314 Punkte gegen ein 254 Punkte hohes Fenster: es
/// passt nicht auf beide. Auf 720p mit vier Ansichten sind es 360 - 12 - 160 - 36 = 152 gegen
/// dieselben 254: es passt.
///
/// Deshalb behauptet dieser Fall zwei GETRENNTE Dinge, und die Grenze zwischen ihnen wird
/// mitgemessen:
///
///   - Ab der Bildhoehe, ab der die beiden Plaetze auseinanderliegen (gemessen: 952 Zeilen, siehe
///     TheTwoPlacesOnlyExistAboveThisImageHeight), gibt es KEINE Ueberdeckung mehr - in keiner
///     Ansichtszahl und in keiner Zeigerstellung.
///   - Darunter gibt es sie, und dann gilt nur noch die schwaechere Zusicherung: der Kasten
///     nimmt den Platz mit der kleineren Ueberdeckung, und das Fenster ist 200 Punkte breit,
///     waehrend der Kasten die ganze Ansichtsbreite einnimmt. Gemessen bleibt im schlimmsten
///     Fall - 1280x720, vier Ansichten, untere Reihe, jede Zeigerstellung - noch 78 % des
///     Kastens stehen. Angeschnitten ja, verdeckt nein.
///
/// Warum das NICHT geloest, sondern nur ehrlich benannt wird: bei 1280x720 mit vier Ansichten
/// ist ein Viewport 640 x 360, und das Aktionsfenster allein ist 254 dieser 360 Zeilen. Ein
/// dritter Platz muesste in die restlichen 106 Zeilen passen, die sich das Fenster ausserdem
/// beliebig auf beide Seiten aufteilen kann - fuer einen 80 Zeilen hohen Kasten gibt es
/// Zeigerstellungen, in denen es rechnerisch keinen freien Platz gibt. Die einzigen echten
/// Auswege waeren, den Kasten zu verkleinern oder das Fenster vom Zeiger wegzunehmen; das
/// Fenster steht am Zeiger, weil der Spieler dorthin sieht. Vier Spieler auf 720p sind ohnehin
/// 640 x 360 pro Kopf - das ist die Groesse, bei der der Fernsehmodus nicht mehr hilft.
BOOST_AUTO_TEST_CASE(TheActionWindowNoLongerCoversTheTextPanel)
{
    const TvModeGuard tvMode;
    constexpr unsigned numLines = 6, lineHeight = 12;
    constexpr int panelHeight = int(numLines * lineHeight) + 8;

    bool sawABadOldCase = false, sawATightCase = false, sawARoomyCase = false;
    for(const Extent renderSize :
        {Extent(1280, 720), Extent(1600, 900), Extent(1920, 1080), Extent(2560, 1440), Extent(3840, 2160)})
    {
        const Rect safeArea = tv::ActiveSafeAreaRect(renderSize);
        for(unsigned numViews = 1; numViews <= MAX_VIEWPORTS; ++numViews)
        {
            for(unsigned idx = 0; idx < numViews; ++idx)
            {
                const Viewport vp = CalcViewports(renderSize, numViews)[idx];
                const Rect viewport(vp.origin, vp.size);
                // Der Abstand der beiden Plaetze - dieselbe Rechnung wie in PlayerBrief.cpp, hier
                // aus den Rechtecken abgelesen statt aus einer Formel abgeschrieben.
                const int bottomY = std::min(viewport.bottom, safeArea.bottom) - 6;
                const int topY = std::max(viewport.top, safeArea.top) + 6;
                const int gap = (bottomY - panelHeight) - (topY + panelHeight);
                const bool roomForBoth = gap >= int(kActionWindowSize.y);

                BOOST_TEST_CONTEXT("Bild " << renderSize.x << "x" << renderSize.y << ", " << numViews
                                           << " Ansichten, Ansicht " << idx << ", Abstand der Plaetze " << gap)
                {
                    const Coverage before = measureCoverage(renderSize, numViews, idx, /*dodge*/ false);
                    const Coverage after = measureCoverage(renderSize, numViews, idx, /*dodge*/ true);
                    BOOST_TEST_MESSAGE("  " << renderSize.x << "x" << renderSize.y << ", " << numViews
                                            << " Ansichten, Ansicht " << idx << ": Abstand " << gap << ", vorher "
                                            << int(before.coveredFraction() * 100) << " % ("
                                            << int(before.worstFraction * 100) << " % des Kastens), nachher "
                                            << int(after.coveredFraction() * 100) << " % ("
                                            << int(after.worstFraction * 100) << " % des Kastens)");
                    if(roomForBoth)
                    {
                        // DIE STARKE ZUSICHERUNG - und sie gilt genau dort, wo der Abstand reicht.
                        BOOST_TEST(after.covered == 0u);
                        sawARoomyCase = true;
                    } else
                    {
                        // DIE SCHWACHE, und mehr steht auch im Kommentar von PlayerBrief.h nicht:
                        // der Kasten wird angeschnitten, nicht verdeckt. Gemessen bleiben im
                        // schlimmsten Fall (1280x720, vier Ansichten, untere Reihe) 78 % des
                        // Kastens stehen; die Grenze hier ist bewusst enger als "die Haelfte",
                        // damit ein Rueckschritt auffaellt.
                        BOOST_TEST(after.worstFraction < 0.25);
                        sawATightCase = true;
                    }
                    // Und in JEDEM Fall ist es nicht schlimmer als vorher.
                    BOOST_TEST(after.coveredFraction() <= before.coveredFraction());
                    BOOST_TEST(after.worstFraction <= before.worstFraction);
                    if(before.coveredFraction() > 0.5)
                        sawABadOldCase = true;
                }
            }
        }
    }
    // Der Befund der ERSTEN Pruefung war echt: es gibt Ansichten, in denen der alte Kasten in
    // ueber der Haelfte aller Zeigerstellungen verdeckt war.
    BOOST_TEST(sawABadOldCase);
    // Der Befund der ZWEITEN ebenso: es gibt wirklich Bildgroessen, in denen die beiden Plaetze
    // nicht auseinanderliegen. Ohne diese Zeile koennte der Fall gruen bleiben, indem die
    // engen Groessen aus der Liste verschwinden - genau so ist die alte Zusicherung entstanden.
    BOOST_TEST(sawATightCase);
    BOOST_TEST(sawARoomyCase);
}

/// Die Grenze selbst, ausgeschrieben: bei welcher Bildhoehe kippt die starke Zusicherung?
///
/// Sie steht hier als eigener Fall, weil die Zahl in den Kommentar von PlayerBrief.h gehoert und
/// ein Kommentar mit einer Zahl darin sonst niemandem auffaellt, wenn sich die Zahl aendert.
BOOST_AUTO_TEST_CASE(TheTwoPlacesOnlyExistAboveThisImageHeight)
{
    const TvModeGuard tvMode;
    constexpr unsigned numLines = 6, lineHeight = 12;
    constexpr int panelHeight = int(numLines * lineHeight) + 8;

    // Kleinste 16:9-Bildhoehe, bei der die beiden Plaetze in JEDER Ansicht jeder Ansichtszahl
    // auseinanderliegen. Gesucht in 8er-Schritten, das ist die Genauigkeit, auf die sich eine
    // Aussage im Kommentar stuetzen darf.
    unsigned threshold = 0;
    for(unsigned height = 360; height <= 2160 && threshold == 0; height += 8)
    {
        const Extent renderSize(height * 16 / 9, height);
        const Rect safeArea = tv::ActiveSafeAreaRect(renderSize);
        bool allRoomy = true;
        for(unsigned numViews = 1; numViews <= MAX_VIEWPORTS && allRoomy; ++numViews)
        {
            for(const Viewport& vp : CalcViewports(renderSize, numViews))
            {
                const Rect viewport(vp.origin, vp.size);
                const int bottomY = std::min(viewport.bottom, safeArea.bottom) - 6;
                const int topY = std::max(viewport.top, safeArea.top) + 6;
                if((bottomY - panelHeight) - (topY + panelHeight) < int(kActionWindowSize.y))
                {
                    allRoomy = false;
                    break;
                }
            }
        }
        if(allRoomy)
            threshold = height;
    }
    BOOST_TEST_MESSAGE("Ab dieser Bildhoehe liegen beide Plaetze frei: " << threshold);
    // Die Zahl, die im Kommentar von PlayerBrief.h steht. Aendert sich die Kastenhoehe, die
    // Fenstergroesse oder die Safe Area, faellt dieser Fall - und der Kommentar wird mit ihm
    // korrigiert, statt still falsch zu werden.
    BOOST_TEST(threshold == 952u);
    // Und die beiden Groessen, ueber die geredet wurde: 720p ist darunter, 1080p darueber.
    BOOST_TEST(720u < threshold);
    BOOST_TEST(1080u >= threshold);
}

/// ... und der Kasten weicht nur aus, wenn wirklich etwas im Weg ist. Ohne Hindernis liefert
/// PanelRect Zahl fuer Zahl das, was es vorher lieferte - unten in der Ansicht.
BOOST_AUTO_TEST_CASE(WithoutAnObstacleThePanelStaysWhereItWas)
{
    // Rect ist (links, oben, BREITE, HOEHE) - dieselben Zahlen wie im Test darueber: Safe Area
    // 5 % auf 1920x1080, und der Viewport unten links eines 2x2-Bildes.
    const Rect safe(96, 54, 1728, 972);
    const Rect viewport(0, 540, 960, 540);
    const Rect panel = brief::PanelRect(viewport, safe, 6, 12);
    BOOST_TEST(panel.bottom == 1020);
    BOOST_TEST(panel.top == 1020 - (6 * 12 + 8));
    // Ein Hindernis, das den Kasten nicht beruehrt (oben in der Ansicht), aendert nichts.
    const Rect farAway(Position(10, 545), Extent(200, 100));
    const Rect same = brief::PanelRect(viewport, safe, 6, 12, farAway);
    BOOST_TEST(same.top == panel.top);
    BOOST_TEST(same.bottom == panel.bottom);
    BOOST_TEST(same.left == panel.left);
    BOOST_TEST(same.right == panel.right);
}

// ============================================================================================
// 8. Die Verdrahtung selbst
// ============================================================================================

/// PUNKT 7 der Pruefung: "Die Verdrahtung des Zeichenwegs ist unbewacht. Pruefe, ob sich das
/// ohne Umbau schliessen laesst."
///
/// Der Befund stimmte, und er laesst sich zur HAELFTE schliessen. Nachgemessen:
///
///  - Msg_PaintAFTER laeuft in der Testumgebung vollstaendig durch, und damit auch sein letzter
///    Ausdruck, die Schleife ueber alle Ansichten nach DrawBrief. Genau das tut dieser Fall.
///    Voraussetzung war nur, dass die Fixture eine LEERE statt gar keiner NWF-Auskunft haelt -
///    die Schneckenanzeige der laggenden Spieler liest sie ungeprueft (emptyNwfInfo()).
///
///  - Msg_PaintBEFORE laeuft NICHT und ist ohne Umbau auch nicht zu retten. Es ruft Run(), Run()
///    ruft GameWorldView::Draw, und das ruft TerrainRenderer::Draw. Dort wird ueber
///    `sorted_textures[t]` indiziert, und diese Liste wird ausschliesslich in
///    TerrainRenderer::GenerateOpenGL gefuellt - das ist der Schritt, den der Konstruktor mit
///    initOGL == false gerade auslaesst, weil der MockupVideoDriver keinen GL-Kontext hat.
///    Ausprobiert: der Aufruf endet in einer Speicherschutzverletzung. Ihn gruen zu bekommen
///    hiesse, entweder einen GL-Kontext in die Suite zu holen oder Run() aufzuteilen - beides
///    ist der Umbau, der ausdruecklich nicht gefragt war.
///    Die Kette Msg_PaintBefore -> Run -> UpdateInput bleibt damit ungeprueft; UpdateInput
///    selbst ist es nicht, jeder Fall dieser Datei geht durch step().
///
/// WAS DIESER FALL ALSO BEWEIST: dass der ECHTE Zeichenschwanz mit einem vollen Klartextblock,
/// mit einem leeren und ueber mehrere Ansichten hinweg durchlaeuft.
/// WAS ER NICHT BEWEIST: dass am Fernseher etwas zu sehen ist - der DummyRenderer verwirft jeden
/// Zeichenaufruf, und weil DrawBrief nichts veraendert, kann kein Nachweis hier feststellen, DASS
/// es gerufen wurde. Mehr zu behaupten waere unehrlich.
BOOST_FIXTURE_TEST_CASE(TheRealPaintTailRunsThroughTheTextPanelOfEveryView, PadViewFixture<2>)
{
    const MapPoint spot = findBuildSpot(worldFixture.world, view(1).GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(spot.isValid());
    takePad(*this, 11, 1, spot);

    // Der Klartext entsteht auf dem produktiven Weg - step() ruft UpdateInput, und das ist die
    // einzige Schreibstelle von SetBrief IM PRODUKTIVCODE. Weiter unten setzt dieser Fall selbst
    // einen Block; das ist dann ausdruecklich kein produktiver Weg mehr, sondern die Vorgabe
    // eines bestimmten Inhalts fuer den Zeichenschwanz.
    step(16u);
    // Welcher Knotenblock es genau ist, sagen die Faelle weiter oben; findBuildSpot liefert
    // "mindestens eine Huette", also je nach Karte auch ein Haus.
    BOOST_TEST_REQUIRE(!view(1).GetBrief().empty());
    // Die Ansicht OHNE Pad bleibt leer - die harte Randbedingung, an der Stelle gemessen, an der
    // sie wirkt: der Mausspieler bekommt keinen Kasten.
    BOOST_TEST_REQUIRE(view(0).GetBrief().empty());

    // Und jetzt der ECHTE Zeichenschwanz, nicht der stillgelegte.
    dsk->paintForReal = true;
    BOOST_TEST_CHECKPOINT("Msg_PaintAfter mit Knotenblock");
    dsk->Msg_PaintAfter();

    // Derselbe Weg mit dem laengsten Block, den es gibt (Zweck, Standort, drei Nachschubwaren,
    // Kosten) - der Fall, in dem der Umbruch am meisten Zeilen erzeugt.
    view(1).SetBrief(brief::ForBuilding(BuildingType::GraniteMine));
    BOOST_TEST_REQUIRE(view(1).GetBrief().lines.size() == 4u);
    BOOST_TEST_CHECKPOINT("Msg_PaintAfter mit vollem Gebaeudeblock");
    dsk->Msg_PaintAfter();

    // ... und mit offenem Aktionsfenster, also in der Lage, in der der Kasten ausweichen muss.
    press(11, PadButton::A);
    BOOST_TEST_REQUIRE(view(1).actionwindow != static_cast<iwAction*>(nullptr));
    BOOST_TEST_CHECKPOINT("Msg_PaintAfter mit offenem Aktionsfenster");
    dsk->Msg_PaintAfter();
    dsk->paintForReal = false;

    view(1).actionwindow->Close();
    WINDOWMANAGER.Draw();
}

BOOST_AUTO_TEST_SUITE_END()
