// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "PadGameFixture.h"
#include "PointOutput.h"
#include "Settings.h"
#include "TvDisplay.h"
#include "WindowManager.h"
#include "drivers/VideoDriverWrapper.h"
#include "controls/ctrlButton.h"
#include "helpers/containerUtils.h"
#include "ingameWindows/IngameWindow.h"
#include "input/PadRouter.h"
#include "mockupDrivers/MockupVideoDriver.h"
#include "uiHelper/uiHelpers.hpp"
#include "world/ViewportLayout.h"
#include "gameData/GuiConsts.h"
#include "gameData/const_gui_ids.h"
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <limits>
#include <vector>

namespace {

/// Das BREITESTE Ingamefenster, in View-Einheiten: iwSave, "Spiel speichern"
/// (ingameWindows/iwSave.cpp:46 mit addHeight = 30 aus Zeile 129). Erreichbar aus dem
/// Spielmenue, aus iwEndgame und direkt aus dskGameInterface (Zeile 1191).
/// Am laufenden Spiel nachgemessen: 700x430.
constexpr Extent WIDEST_INGAME_WINDOW(700, 430);

/// Das HOECHSTE Ingamefenster: iwDiplomacy (ingameWindows/iwDiplomacy.cpp:45). Seine Hoehe
/// waechst mit der Spielerzahl:
///     FIRST_LINE_Y + numPlayers * (CELL_HEIGHT + SPACE_HEIGHT) + 20 = 75 + numPlayers * 70.
/// Bei MAX_PLAYERS = 8 (gameData/MaxPlayers.h) sind das 635. An einer laufenden Partie mit drei
/// Spielern nachgemessen: 500x285 = 75 + 3*70 - die Formel ist also bestaetigt und nicht bloss
/// abgelesen.
constexpr Extent TALLEST_INGAME_WINDOW(500, 635);

/// Die HUELLE beider Anschlaege. Es gibt kein einzelnes "groesstes" Fenster: das breiteste ist
/// nicht das hoechste. Fuer jede Platzrechnung zaehlt aber genau diese Huelle, denn ein Kasten,
/// in den sie passt, nimmt jedes Ingamefenster auf.
///
/// WIE GEMESSEN: alle 55 Ableitungen von IngameWindow durchgegangen, nicht nur die mit einer
/// konstanten Groesse im Konstruktor. Die rechnenden Fenster wurden an einer echten Partie
/// konstruiert und ihre GetSize() abgelesen (u.a. iwDiplomacy 500x285 bei drei Spielern,
/// iwSave 700x430, iwAddons 700x500, iwStatistics 252x336, iwPlayReplay 600x330,
/// iwMusicPlayer 440x330); fuer die vom Spielstand abhaengigen wurde die Formel im Quelltext
/// auf ihr Maximum ausgewertet (iwDiplomacy 8 Spieler -> 635, iwBuildings 37 Gebaeude ->
/// 190x568, iwBuildingProductivities 30 Symbole -> 400x590, iwEconomicProgress 8 Teams und
/// 7 Waren -> 615x275, iwTools mit Addon -> 212x432).
///
/// NICHT enthalten und bewusst nicht: die Fenster, die sich an ihren TEXT anpassen
/// (iwTextfile, iwMissionStatement, iwMsgbox, iwHelp, iwVictory). Ihre Groesse haengt an
/// uebersetzten Zeichenketten und an Dateien, hat also gar keine feste Obergrenze. Genau fuer
/// sie gibt es tv::WindowBoundsRect: der Rand gibt nach, statt sie aus dem Bild zu schieben.
constexpr Extent LARGEST_INGAME_WINDOW(WIDEST_INGAME_WINDOW.x, TALLEST_INGAME_WINDOW.y);

/// Das kleinste Fenster, das ein Padspieler oeffnen kann: iwAction (ingameWindows/iwAction.cpp:49).
constexpr Extent ACTION_WINDOW(200, 254);

/// Anteil der BILDSCHIRMHOEHE, den ein Fenster dieser View-Groesse physisch belegt.
///
/// Das ist das Lesbarkeitsmass dieser Phase. View-Einheiten allein sagen nichts: sie sind
/// bereits durch die GUI-Skalierung geteilt. Erst der Rueckweg nach Geraetepixeln
/// (GuiScale::viewToScreen - dieselbe Umrechnung, die GameWorldView::GetScissorRect produktiv
/// macht) geteilt durch die Panelhoehe ergibt eine Zahl, die man ueber verschiedene
/// Aufloesungen hinweg vergleichen darf.
double physicalHeightShare(const Extent& viewSize)
{
    const auto physical = VIDEODRIVER.getGuiScale().viewToScreen<int>(static_cast<float>(viewSize.y));
    return static_cast<double>(physical) / VIDEODRIVER.GetWindowSize().height;
}

/// Sichert und stellt JEDEN globalen Zustand wieder her, den diese Tests anfassen.
///
/// Notwendig, weil der Bildschirm nur EINMAL je Prozess angelegt wird
/// (uiHelper/uiHelpers.cpp:27-32): Aufloesung, GUI-Skalierung und Referenzhoehe ueberleben
/// sonst den Testfall und faerben spaetere Tests zufaellig ein. Bewusst im DESTRUKTOR und nicht
/// am Ende des Testkoerpers - bei einem BOOST_TEST_REQUIRE wird die letzte Zeile nie erreicht.
struct TvFixture
{
    VideoMode oldWindowSize;
    DisplayMode oldDisplayMode;
    unsigned oldGuiScalePercent;
    unsigned oldReferenceHeight;
    bool oldTvMode;
    unsigned oldSafeAreaPercent;
    unsigned oldGuiScaleSetting;
    VideoMode oldWindowedSize;
    VideoMode oldFullscreenSize;
    Position oldMousePos;

    TvFixture()
    {
        uiHelper::initGUITests();
        oldWindowSize = VIDEODRIVER.GetWindowSize();
        oldDisplayMode = VIDEODRIVER.GetDisplayMode();
        oldGuiScalePercent = VIDEODRIVER.getGuiScale().percent();
        oldReferenceHeight = VIDEODRIVER.getUiReferenceHeight();
        oldTvMode = SETTINGS.video.tvMode;
        oldSafeAreaPercent = SETTINGS.video.tvSafeAreaPercent;
        oldGuiScaleSetting = SETTINGS.video.guiScale;
        oldWindowedSize = SETTINGS.video.windowedSize;
        oldFullscreenSize = SETTINGS.video.fullscreenSize;
        oldMousePos = VIDEODRIVER.GetMousePos();
    }

    ~TvFixture()
    {
        SETTINGS.video.tvMode = oldTvMode;
        SETTINGS.video.tvSafeAreaPercent = oldSafeAreaPercent;
        SETTINGS.video.guiScale = oldGuiScaleSetting;
        VIDEODRIVER.setUiReferenceHeight(oldReferenceHeight);
        VIDEODRIVER.setGuiScalePercent(oldGuiScalePercent);
        VIDEODRIVER.ResizeScreen(oldWindowSize, oldDisplayMode);
        SETTINGS.video.windowedSize = oldWindowedSize;
        SETTINGS.video.fullscreenSize = oldFullscreenSize;
        // setGuiScalePercent loest einen Mausbewegungsevent aus (VideoDriver.cpp): die
        // Mausposition muss zurueck, sonst kippt ein spaeterer Besitznachweis aus einem Grund
        // um, der mit Skalierung nichts zu tun hat.
        uiHelper::GetVideoDriver()->SetMousePos(oldMousePos);
    }

    /// Aufloesung setzen und die Skalierungsautomatik neu greifen lassen - genau der Weg, den
    /// das Spiel nimmt (GameManager: setUiReferenceHeight, dann setGuiScalePercent).
    static void useScreen(unsigned width, unsigned height, bool tvMode)
    {
        SETTINGS.video.tvMode = tvMode;
        SETTINGS.video.guiScale = 0; // "automatisch"
        VIDEODRIVER.ResizeScreen(VideoMode(width, height), DisplayMode::Windowed);
        VIDEODRIVER.setUiReferenceHeight(tvMode ? tv::UI_REFERENCE_HEIGHT : 0u);
        VIDEODRIVER.setGuiScalePercent(0);
    }
};

} // namespace

BOOST_AUTO_TEST_SUITE(TvDisplayTests)

// --------------------------------------------------------------------------------------------
// 1. Der Ausloeser, als Zahl - und die Regressionsklammer fuer den Einzelspieler
// --------------------------------------------------------------------------------------------

/// DER BEFUND DES AUFTRAGGEBERS, gemessen: ohne Fernsehmodus belegt dasselbe Fenster auf einem
/// 4K-Bildschirm nur noch die HAELFTE des Bildanteils, den es auf 1080p belegt. Physisch ist es
/// damit halb so gross - genau das heisst "zu klein".
///
/// Ursache, im Code nachgeschlagen: die empfohlene GUI-Skalierung kommt aus getDpiScale()
/// (libs/driver/src/VideoDriver.cpp), und der ist konstant 1.0, weil SDL_WINDOW_ALLOW_HIGHDPI im
/// SDL2-Treiber auskommentiert ist (extras/videoDrivers/SDL2/VideoSDL2.cpp:353-356). Die
/// Automatik bietet am 4K-Fernseher also denselben Wert an wie auf einem 1366x768-Laptop.
///
/// Dieser Testfall ist zugleich die HARTE RANDBEDINGUNG: er beschreibt den Zustand, den ein
/// heutiger Maus-und-Tastatur-Spieler sieht. Er muss gruen bleiben.
BOOST_FIXTURE_TEST_CASE(WithoutTvModeTheUiHalvesPhysicallyOnA4kScreen, TvFixture)
{
    TvFixture::useScreen(1920, 1080, false);
    BOOST_TEST(VIDEODRIVER.getGuiScaleRange().recommendedPercent == 100u);
    const double share1080p = physicalHeightShare(LARGEST_INGAME_WINDOW);

    TvFixture::useScreen(3840, 2160, false);
    // Die Empfehlung aendert sich NICHT - das ist der Fehler, und er ist eine Zahl.
    BOOST_TEST(VIDEODRIVER.getGuiScaleRange().recommendedPercent == 100u);
    const double share2160p = physicalHeightShare(LARGEST_INGAME_WINDOW);

    BOOST_TEST(share1080p == 635.0 / 1080.0, boost::test_tools::tolerance(0.001));
    BOOST_TEST(share2160p == 635.0 / 2160.0, boost::test_tools::tolerance(0.001));
    // Halbiert. Der Faktor ist exakt 2, nicht ungefaehr.
    BOOST_TEST(share1080p / share2160p == 2.0, boost::test_tools::tolerance(0.01));
}

/// Die Gegenrichtung, und der Nachweis, dass der Hebel wirkt: MIT Fernsehmodus ist der
/// Bildanteil auf 4K derselbe wie auf 1080p. Ohne die Aenderung in
/// VideoDriver::getGuiScaleRange faellt diese Zusicherung um.
BOOST_FIXTURE_TEST_CASE(TvModeRestoresThePhysicalSizeOfTheUi, TvFixture)
{
    TvFixture::useScreen(1920, 1080, true);
    const double share1080p = physicalHeightShare(LARGEST_INGAME_WINDOW);
    // Auf 1080p ist die Referenzhoehe genau die Bildhoehe: der Faktor ist 1, hier aendert der
    // Fernsehmodus also NICHTS. Das ist Absicht.
    BOOST_TEST(VIDEODRIVER.getGuiScale().percent() == 100u);

    for(const auto& mode : {VideoMode(2560, 1440), VideoMode(3840, 2160), VideoMode(7680, 4320)})
    {
        BOOST_TEST_CONTEXT(mode.width << "x" << mode.height)
        {
            TvFixture::useScreen(mode.width, mode.height, true);
            BOOST_TEST(physicalHeightShare(LARGEST_INGAME_WINDOW) == share1080p, boost::test_tools::tolerance(0.01));
            BOOST_TEST(physicalHeightShare(ACTION_WINDOW) == 254.0 / 1080.0, boost::test_tools::tolerance(0.01));
        }
    }

    // Konkret fuer das Zielgeraet: 4K bekommt 200 %.
    TvFixture::useScreen(3840, 2160, true);
    BOOST_TEST(VIDEODRIVER.getGuiScale().percent() == 200u);
    BOOST_TEST(VIDEODRIVER.getGuiScaleRange().recommendedPercent == 200u);
    // ... und der Motor laesst noch Luft nach oben, falls der Fernseher groesser oder der Sessel
    // weiter weg steht: die Obergrenze bleibt unangetastet bei renderSize/800x600.
    BOOST_TEST(VIDEODRIVER.getGuiScaleRange().maxPercent >= 360u);
}

/// NEGATIVKONTROLLE zum vorigen Test: nimmt man die Referenzhoehe weg, faellt die Empfehlung
/// sofort auf 100 % zurueck. Waere der Test oben aus einem anderen Grund gruen, muesste er das
/// hier auch sein.
BOOST_FIXTURE_TEST_CASE(ClearingTheReferenceHeightRestoresTheOldRecommendation, TvFixture)
{
    TvFixture::useScreen(3840, 2160, true);
    BOOST_TEST_REQUIRE(VIDEODRIVER.getGuiScaleRange().recommendedPercent == 200u);

    VIDEODRIVER.setUiReferenceHeight(0);
    BOOST_TEST(VIDEODRIVER.getGuiScaleRange().recommendedPercent == 100u);
    // Und die Automatik zieht das sofort nach, ohne dass die Fenstergroesse sich aendert.
    BOOST_TEST(VIDEODRIVER.getGuiScale().percent() == 100u);
}

/// BEFUND D: derselbe Sachverhalt von der anderen Seite - und warum der Optionsschalter etwas
/// tun MUSS, was der Treiber allein nicht tut.
///
/// Der Fernsehmodus wirkt ausschliesslich ueber die EMPFEHLUNG. Steht die Skalierung auf einem
/// festen Wert (und das tut sie, sobald jemand den Regler je angefasst hat), ist
/// setUiReferenceHeight allein FOLGENLOS - der Schalter waere fuer diese Spieler tot. Genau das
/// wird hier zuerst nachgewiesen, damit die Loesung darunter nicht ins Leere zeigt.
BOOST_FIXTURE_TEST_CASE(TheReferenceHeightAloneIsUselessWithAFixedGuiScale, TvFixture)
{
    TvFixture::useScreen(3840, 2160, false);
    SETTINGS.video.guiScale = 100; // der Spieler hat irgendwann einmal "100%" gewaehlt
    VIDEODRIVER.setGuiScalePercent(100);
    BOOST_TEST_REQUIRE(VIDEODRIVER.getGuiScale().percent() == 100u);

    // Der Fernsehmodus wird eingeschaltet - aber NUR die Referenzhoehe gesetzt.
    SETTINGS.video.tvMode = true;
    VIDEODRIVER.setUiReferenceHeight(tv::UI_REFERENCE_HEIGHT);
    // Die Empfehlung steigt, die tatsaechliche Skalierung nicht. Der Schalter bleibt wirkungslos.
    BOOST_TEST(VIDEODRIVER.getGuiScaleRange().recommendedPercent == 200u);
    BOOST_TEST(VIDEODRIVER.getGuiScale().percent() == 100u);

    // DIE LOESUNG, genau die beiden Zeilen, die dskOptions beim Einschalten zusaetzlich
    // ausfuehrt (desktops/dskOptions.cpp, case ID_grpTvMode): die Auswahl zurueck auf
    // "automatisch" - sichtbar in der Auswahlliste als "Auto (200%)".
    SETTINGS.video.guiScale = 0;
    VIDEODRIVER.setGuiScalePercent(0);
    BOOST_TEST(VIDEODRIVER.getGuiScale().percent() == 200u);
}

/// Eine AUSDRUECKLICH gesetzte Skalierung schlaegt den Fernsehmodus - der Spieler behaelt das
/// letzte Wort. Kein Widerspruch zum Test darueber: dort wird der Modus EINGESCHALTET und stellt
/// dabei einmalig auf "automatisch"; hier waehlt der Spieler DANACH einen festen Wert, und der
/// bleibt.
BOOST_FIXTURE_TEST_CASE(AnExplicitGuiScaleWinsOverTvMode, TvFixture)
{
    TvFixture::useScreen(3840, 2160, true);
    BOOST_TEST_REQUIRE(VIDEODRIVER.getGuiScale().percent() == 200u);

    SETTINGS.video.guiScale = 150;
    VIDEODRIVER.setGuiScalePercent(150);
    BOOST_TEST(VIDEODRIVER.getGuiScale().percent() == 150u);
    // Die Empfehlung bleibt sichtbar (dskOptions zeigt sie als "Auto (200%)"), wirkt aber nicht.
    BOOST_TEST(VIDEODRIVER.getGuiScaleRange().recommendedPercent == 200u);
}

// --------------------------------------------------------------------------------------------
// 2. Die Skalierung und die Viewportaufteilung
// --------------------------------------------------------------------------------------------

/// DIE AUSSAGE, WEGEN DER DER TEURE FENSTERUMBAU ENTFAELLT - PRAEZISIERT (Befund C).
///
/// Weil die Skalierung gegen eine feste Leinwandhoehe rechnet, ist die Renderflaeche in
/// View-Koordinaten IMMER tv::UI_REFERENCE_HEIGHT hoch - unabhaengig von der Aufloesung. Ein
/// Viertelbild ist damit immer 540 View-Einheiten hoch und auf 16:9 960 breit, also genau so
/// gross wie heute ein 1080p-VOLLBILD bei 100 %.
///
/// WAS DIESER TEST NICHT BEHAUPTET: dass daraus "jedes Ingamefenster passt in einen Viewport"
/// folgt. Diese Aussage stand hier einmal und war schlicht falsch - sie stuetzte sich auf eine
/// ungeprueft uebernommene Fenstergroesse (300x525). Nachgemessen ist das hoechste
/// Ingamefenster 635 View-Einheiten hoch, ein Viertelbild aber nur 540: es passt dort auch OHNE
/// jeden Rand nicht hinein. Der naechste Test rechnet das nach und benennt, was stattdessen
/// gilt.
///
/// Was hier steht, ist die Zusicherung, die den Fensterumbau wirklich erspart, und sie ist eine
/// Aussage ueber die LEINWAND, nicht ueber den Viertelausschnitt: die Renderflaeche schrumpft in
/// View-Einheiten NIE unter das, was heute ein 1080p-Vollbild hat. Ein Fenster, das heute in den
/// Bildschirm passt, passt deshalb bei jeder Aufloesung und jeder Ansichtszahl in die
/// Renderflaeche - die Ansichtszahl kommt in der Skalierung ueberhaupt nicht vor.
BOOST_FIXTURE_TEST_CASE(TvScaleKeepsEveryViewportAsLargeAsA1080pScreen, TvFixture)
{
    for(const auto& mode : {VideoMode(1920, 1080), VideoMode(2560, 1440), VideoMode(3840, 2160), VideoMode(7680, 4320)})
    {
        BOOST_TEST_CONTEXT(mode.width << "x" << mode.height)
        {
            TvFixture::useScreen(mode.width, mode.height, true);
            const Extent renderSize = VIDEODRIVER.GetRenderSize();
            // Die Renderflaeche in View-Koordinaten ist immer die logische Leinwand - auf ein
            // Prozent genau. Exakt kann sie nicht sein: die Skalierung ist eine GANZE
            // Prozentzahl (GuiScale haelt `unsigned percent`), 1440/1080 = 133,33 % wird also
            // zu 133 % und die Leinwand damit 1083 statt 1080 Zeilen hoch.
            BOOST_TEST(renderSize.y >= tv::UI_REFERENCE_HEIGHT - tv::UI_REFERENCE_HEIGHT / 100u);
            BOOST_TEST(renderSize.y <= tv::UI_REFERENCE_HEIGHT + tv::UI_REFERENCE_HEIGHT / 100u);

            for(unsigned numViews = 1; numViews <= MAX_VIEWPORTS; ++numViews)
            {
                BOOST_TEST_CONTEXT("numViews " << numViews)
                {
                    for(const Viewport& vp : CalcViewports(renderSize, numViews))
                    {
                        // Mindestens ein Viertel der logischen Leinwand. Das ist die Aussage
                        // ueber die WELTANSICHT je Spieler: sie ist so gross wie heute ein
                        // 1080p-Vollbild. Ueber Fenster sagt sie nichts - siehe naechster Test.
                        BOOST_TEST(vp.size.y >= tv::UI_REFERENCE_HEIGHT / 2u);
                        BOOST_TEST(vp.size.x >= 960u);
                    }
                }
            }
        }
    }
}

/// Gegenprobe: OHNE Fernsehmodus gilt die Aussage nicht. Auf 4K bei 100 % ist ein Viertelbild
/// zwar riesig in View-Einheiten, aber physisch schrumpft alles - und genau darum ging es.
/// Ohne diese Gegenprobe waere der Test oben auch dann gruen, wenn die Skalierung gar nichts
/// taete.
BOOST_FIXTURE_TEST_CASE(WithoutTvModeTheViewportsAreNotOnTheLogicalCanvas, TvFixture)
{
    TvFixture::useScreen(3840, 2160, false);
    const Extent renderSize = VIDEODRIVER.GetRenderSize();
    BOOST_TEST(renderSize.y == 2160u); // ungeskaliert - eben NICHT die logische Leinwand
    const std::vector<Viewport> vps = CalcViewports(renderSize, 4);
    BOOST_TEST_REQUIRE(vps.size() == 4u);
    BOOST_TEST(vps[0].size.y == 1080u);
    // Ein Fenster von 635 View-Einheiten ist hier physisch 635 Geraetepixel hoch statt 1270 -
    // knapp 30 % der Bildhoehe statt der knapp 59 %, die es auf einem 1080p-Geraet hat.
    BOOST_TEST(physicalHeightShare(LARGEST_INGAME_WINDOW) == 635.0 / 2160.0,
               boost::test_tools::tolerance(0.001));
}

// --------------------------------------------------------------------------------------------
// 3. Safe Area
// --------------------------------------------------------------------------------------------

/// Die reine Rechnung, ohne Treiber. percentPerSide == 0 ist der Auslieferungszustand und muss
/// EXAKT die volle Flaeche liefern - daran haengt, dass sich fuer heutige Spieler nichts
/// aendert.
BOOST_AUTO_TEST_CASE(SafeAreaRectIsTheFullSurfaceWhenTurnedOff)
{
    for(const Extent size : {Extent(800, 600), Extent(1920, 1080), Extent(3840, 2160)})
    {
        BOOST_TEST_CONTEXT(size)
        {
            const Rect full = tv::SafeAreaRect(size, 0);
            BOOST_TEST(full.left == 0);
            BOOST_TEST(full.top == 0);
            BOOST_TEST(full.right == static_cast<int>(size.x));
            BOOST_TEST(full.bottom == static_cast<int>(size.y));
        }
    }
}

/// 5 % je Seite: der Konsens aus EBU R95 (Graphics Safe), Microsofts "Designing for TV" und den
/// Android-TV-Layoutrichtlinien. Uebrig bleiben 90 % x 90 % der Flaeche.
BOOST_AUTO_TEST_CASE(SafeAreaRectLeavesFivePercentOnEachSide)
{
    const Rect r = tv::SafeAreaRect(Extent(3840, 2160), tv::SAFE_AREA_PERCENT_DEFAULT);
    BOOST_TEST(r.left == 192);
    BOOST_TEST(r.top == 108);
    BOOST_TEST(r.right == 3648);
    BOOST_TEST(r.bottom == 2052);
    BOOST_TEST(r.getSize().x == 3456u); // 90 % der Breite
    BOOST_TEST(r.getSize().y == 1944u); // 90 % der Hoehe

    // Der Rand ist gedeckelt: mehr als SAFE_AREA_PERCENT_MAX je Seite verschenkt mehr Bild, als
    // je ein Fernseher abschneidet.
    BOOST_TEST(
      (tv::SafeAreaRect(Extent(3840, 2160), 50) == tv::SafeAreaRect(Extent(3840, 2160), tv::SAFE_AREA_PERCENT_MAX)));
}

/// BEFUND C, DIE ENTSCHEIDUNG - und die Rechnung, die sie erzwingt.
///
/// Die Zahl, die hier frueher stand, war falsch: als groesstes Ingamefenster war 300x525
/// (iwOptionsWindow) angenommen, gemessen ist die Huelle 700x635 (iwSave breit, iwDiplomacy bei
/// acht Spielern hoch). Die Entscheidung wird davon nicht umgestossen - sie wird STAERKER:
///
/// Ein Viertelbild ist 540 View-Einheiten hoch. Mit 525 waere ein Fenster dort mit fuenfzehn
/// Einheiten Luft gerade noch hineingegangen, und ein Safe-Rand von 5 % (54 Einheiten) haette
/// die Rechnung gekippt. Mit den gemessenen 635 geht sie schon OHNE Rand nicht auf: 540 < 635.
/// Ein Fenster in seinen VIEWPORT zu klemmen ist damit nicht bloss knapp, sondern von vornherein
/// unmoeglich - auch auf dem Weg "nur an den AEUSSEREN Kanten", bei dem fuer die obere Reihe
/// eines 2x2-Bildes 540 - 54 = 486 blieben.
///
/// DIE ENTSCHEIDUNG IST DESHALB: der Safe-Area-Rand ist eine Eigenschaft des BILDSCHIRMS und
/// wird nur an dessen vier Kanten abgezogen. Ein Fenster wird gegen die ganze Renderflaeche
/// geklemmt, NIE gegen einen Viewport - eine Kante zwischen zwei Ansichten ist kein Bildrand und
/// wird von keinem Fernseher abgeschnitten. Damit ist der Kasten bei 5 % 1080 - 2*54 = 972 hoch
/// und 1920 - 2*96 = 1728 breit; die 700x635 passen mit 337 Einheiten Luft in der Hoehe und
/// 1028 in der Breite. Beim GROESSTEN erlaubten Rand (10 %) bleiben 864 x 1536, also immer noch
/// 229 bzw. 836 Einheiten Luft.
///
/// Dieser Test rechnet BEIDES nach: dass der Viewport-Weg nicht traegt, und dass der gewaehlte
/// traegt - bei jedem erlaubten Randwert.
BOOST_FIXTURE_TEST_CASE(TheSafeAreaIsAScreenPropertyAndNeverHidesAWindow, TvFixture)
{
    // Die logische Leinwand im Fernsehmodus, unabhaengig von der Aufloesung (16:9).
    constexpr Extent canvas(1920, tv::UI_REFERENCE_HEIGHT);

    // (a) Warum NICHT je Viewport: die Zahl, an der der Viewport-Weg scheitert.
    {
        const unsigned marginY = canvas.y * tv::SAFE_AREA_PERCENT_DEFAULT / 100u;
        BOOST_TEST(marginY == 54u);
        constexpr unsigned quarterHeight = tv::UI_REFERENCE_HEIGHT / 2u;
        BOOST_TEST(quarterHeight == 540u);
        // Er scheitert schon OHNE jeden Rand - das ist der Unterschied zur frueheren, falschen
        // Zahl. Es gibt also gar keinen Randwert, bei dem er aufginge.
        BOOST_TEST(quarterHeight < LARGEST_INGAME_WINDOW.y);
        // Und erst recht mit dem guenstigsten Fall des Viewport-Wegs (nur die aeussere Kante).
        BOOST_TEST(quarterHeight - marginY < LARGEST_INGAME_WINDOW.y);
    }

    // (b) Der gewaehlte Weg: gegen den BILDSCHIRM geklemmt, bei jedem erlaubten Randwert.
    for(unsigned percent = 0; percent <= tv::SAFE_AREA_PERCENT_MAX; ++percent)
    {
        BOOST_TEST_CONTEXT("percent " << percent)
        {
            const Extent safeSize = tv::SafeAreaRect(canvas, percent).getSize();
            BOOST_TEST(safeSize.x >= LARGEST_INGAME_WINDOW.x);
            BOOST_TEST(safeSize.y >= LARGEST_INGAME_WINDOW.y);
            // Luft nach oben statt Millimeterarbeit, und zwar in BEIDEN Achsen. Die Schranke
            // ist so gewaehlt, dass sie beim GROESSTEN erlaubten Rand noch gilt - dort ist die
            // Hoehe mit 229 Einheiten Luft der engere Fall.
            BOOST_TEST(safeSize.y - LARGEST_INGAME_WINDOW.y >= tv::UI_REFERENCE_HEIGHT / 5u);
            BOOST_TEST(safeSize.x - LARGEST_INGAME_WINDOW.x >= tv::UI_REFERENCE_HEIGHT / 5u);
        }
    }
    // Die beiden Eckwerte ausgeschrieben, damit die Zahlen im Kopfkommentar nachpruefbar sind.
    {
        const Extent atDefault = tv::SafeAreaRect(canvas, tv::SAFE_AREA_PERCENT_DEFAULT).getSize();
        BOOST_TEST((atDefault == Extent(1728, 972)));
        BOOST_TEST(atDefault.y - LARGEST_INGAME_WINDOW.y == 337u);
        const Extent atMax = tv::SafeAreaRect(canvas, tv::SAFE_AREA_PERCENT_MAX).getSize();
        BOOST_TEST((atMax == Extent(1536, 864)));
        BOOST_TEST(atMax.y - LARGEST_INGAME_WINDOW.y == 229u);
    }

    // (c) Und die Klemme gibt nach, wenn ein Fenster doch einmal nicht hineinpasst. Die
    // Zusicherung "die Safe Area schiebt nie ein Fenster aus dem Bild" gilt damit ohne jede
    // Bedingung - auch auf einem 800x600-Fenster mit dem groessten Rand (600 - 120 = 480 < 635).
    //
    // Gemessen wird hier mit dem HOECHSTEN Fenster (500x635) und nicht mit der Huelle: es ist
    // zu hoch fuer den Kasten, aber nicht zu breit (640 >= 500). Genau daran zeigt sich, dass
    // je Achse GETRENNT entschieden wird - mit der 700 breiten Huelle gaeben beide Achsen nach
    // und der Nachweis waere blind fuer den Unterschied.
    {
        constexpr Extent tiny(800, 600);
        SETTINGS.video.tvMode = true;
        SETTINGS.video.tvSafeAreaPercent = tv::SAFE_AREA_PERCENT_MAX;
        const Extent tinySafe = tv::SafeAreaRect(tiny, tv::SAFE_AREA_PERCENT_MAX).getSize();
        BOOST_TEST_REQUIRE(tinySafe.y < TALLEST_INGAME_WINDOW.y);
        BOOST_TEST_REQUIRE(tinySafe.x >= TALLEST_INGAME_WINDOW.x);
        const Rect bounds = tv::WindowBoundsRect(tiny, TALLEST_INGAME_WINDOW);
        // Die HOEHE gibt nach, ganz, bis auf die volle Renderflaeche ...
        BOOST_TEST(bounds.top == 0);
        BOOST_TEST(bounds.bottom == static_cast<int>(tiny.y));
        // ... die BREITE nicht, dort passt das Fenster ja. Je Achse getrennt entschieden.
        BOOST_TEST(bounds.left == 80);
        BOOST_TEST(bounds.right == 720);
        // Und mit der vollen Huelle geben beide nach - dann ist der Kasten die ganze Flaeche.
        BOOST_TEST((tv::WindowBoundsRect(tiny, LARGEST_INGAME_WINDOW) == Rect(Position(0, 0), tiny)));
    }
}

/// Der Auslieferungszustand, an derselben Funktion: ohne Fernsehmodus ist der Kasten fuer JEDE
/// Fenstergroesse exakt die volle Renderflaeche. Damit rechnet IngameWindow::SetPos Zahl fuer
/// Zahl wie vor dieser Phase - unabhaengig davon, was in tvSafeAreaPercent steht.
BOOST_FIXTURE_TEST_CASE(WindowBoundsAreTheFullSurfaceWithoutTvMode, TvFixture)
{
    SETTINGS.video.tvMode = false;
    SETTINGS.video.tvSafeAreaPercent = tv::SAFE_AREA_PERCENT_MAX;
    for(const Extent screen : {Extent(800, 600), Extent(1920, 1080), Extent(3840, 2160)})
    {
        for(const Extent wnd : {Extent(1, 1), LARGEST_INGAME_WINDOW, Extent(9999, 9999)})
        {
            BOOST_TEST_CONTEXT(screen << " / " << wnd)
            BOOST_TEST((tv::WindowBoundsRect(screen, wnd) == Rect(Position(0, 0), screen)));
        }
    }
}

/// Die Entscheidung aus (b) am ECHTEN Fenster und an der ECHTEN Splitscreen-Geometrie: ein
/// Fenster in der Groesse des groessten Ingamefensters bleibt bei jeder Ansichtszahl
/// vollstaendig im Bild. Dass es dabei ueber die Kante seines eigenen Viewports hinausragen
/// KANN, ist die bewusste Kehrseite und steht hier als Zahl, nicht als Hoffnung.
BOOST_FIXTURE_TEST_CASE(TheLargestWindowStaysFullyVisibleAtEveryViewCount, TvFixture)
{
    constexpr auto id = CGI_OBSERVATION;
    BOOST_TEST_REQUIRE((SETTINGS.windows.persistentSettings.find(id) == SETTINGS.windows.persistentSettings.end()));

    TvFixture::useScreen(3840, 2160, true);
    SETTINGS.video.tvSafeAreaPercent = tv::SAFE_AREA_PERCENT_DEFAULT;
    const Extent renderSize = VIDEODRIVER.GetRenderSize();
    const Rect safe = tv::ActiveSafeAreaRect(renderSize);

    IngameWindow wnd(id, IngameWindow::posCenter, LARGEST_INGAME_WINDOW, "TV", nullptr);
    for(const DrawPoint corner :
        {DrawPoint(-99999, -99999), DrawPoint(99999, 99999), DrawPoint(-99999, 99999), DrawPoint(99999, -99999)})
    {
        BOOST_TEST_CONTEXT(corner)
        {
            wnd.SetPos(corner);
            BOOST_TEST(wnd.GetPos().x >= safe.left);
            BOOST_TEST(wnd.GetPos().y >= safe.top);
            BOOST_TEST(wnd.GetPos().x + static_cast<int>(wnd.GetSize().x) <= safe.right);
            BOOST_TEST(wnd.GetPos().y + static_cast<int>(wnd.GetSize().y) <= safe.bottom);
        }
    }

    // Die Kehrseite, ausgesprochen: in der oberen Reihe eines 2x2-Bildes ist das Fenster hoeher
    // als der Platz vom oberen Safe-Rand bis zur Viewportkante. Wer es dort ganz unten oeffnet,
    // verdeckt einen Streifen der Ansicht darunter. Das ist der Preis dafuer, dass es ueberhaupt
    // vollstaendig sichtbar ist.
    const std::vector<Viewport> vps = CalcViewports(renderSize, 4);
    BOOST_TEST_REQUIRE(vps.size() == 4u);
    const int usableInTopRow = vps[0].origin.y + static_cast<int>(vps[0].size.y) - safe.top;
    BOOST_TEST(usableInTopRow == 486);
    BOOST_TEST(usableInTopRow < static_cast<int>(LARGEST_INGAME_WINDOW.y));
    // Und ohne jeden Rand ebenfalls: der Viertelausschnitt ist schlicht kleiner als das
    // hoechste Ingamefenster. Die Kehrseite ist also kein Preis der Safe Area, sondern eine
    // Eigenschaft des Splitscreens.
    BOOST_TEST(vps[0].size.y < LARGEST_INGAME_WINDOW.y);
}

/// Der Einstellungs-Adapter: der Rand wirkt NUR bei eingeschaltetem Fernsehmodus.
BOOST_FIXTURE_TEST_CASE(SafeAreaOnlyAppliesInTvMode, TvFixture)
{
    SETTINGS.video.tvSafeAreaPercent = tv::SAFE_AREA_PERCENT_DEFAULT;

    SETTINGS.video.tvMode = false;
    BOOST_TEST(tv::ActiveSafeAreaPercent() == 0u);
    BOOST_TEST((tv::ActiveSafeAreaRect(Extent(3840, 2160)) == Rect(Position(0, 0), Extent(3840, 2160))));

    SETTINGS.video.tvMode = true;
    BOOST_TEST(tv::ActiveSafeAreaPercent() == tv::SAFE_AREA_PERCENT_DEFAULT);
    BOOST_TEST((tv::ActiveSafeAreaRect(Extent(3840, 2160)) != Rect(Position(0, 0), Extent(3840, 2160))));
}

/// Am ECHTEN Fenster: ein Fenster kann sich nicht mehr in die Bildschirmecke druecken lassen,
/// sondern haelt den Rand ein. Mit der Gegenprobe bei ausgeschaltetem Modus - ohne sie
/// koennte der Test auch etwas ganz anderes messen.
BOOST_FIXTURE_TEST_CASE(WindowsStayInsideTheSafeArea, TvFixture)
{
    // Bewusst eine Fenster-ID OHNE persistente Einstellungen: IngameWindow::SetPos schriebe
    // sonst die gespeicherte Position fuer alle folgenden Tests um.
    constexpr auto id = CGI_OBSERVATION;
    BOOST_TEST_REQUIRE((SETTINGS.windows.persistentSettings.find(id) == SETTINGS.windows.persistentSettings.end()));

    TvFixture::useScreen(3840, 2160, true);
    SETTINGS.video.tvSafeAreaPercent = tv::SAFE_AREA_PERCENT_DEFAULT;
    const Extent renderSize = VIDEODRIVER.GetRenderSize();
    const Rect safe = tv::ActiveSafeAreaRect(renderSize);
    BOOST_TEST_REQUIRE(safe.left > 0);

    IngameWindow wnd(id, IngameWindow::posCenter, Extent(300, 400), "TV", nullptr);

    wnd.SetPos(DrawPoint(-1000, -1000));
    BOOST_TEST(wnd.GetPos().x == safe.left);
    BOOST_TEST(wnd.GetPos().y == safe.top);

    wnd.SetPos(DrawPoint(100000, 100000));
    BOOST_TEST(wnd.GetPos().x + static_cast<int>(wnd.GetSize().x) <= safe.right);
    BOOST_TEST(wnd.GetPos().y + static_cast<int>(wnd.GetSize().y) <= safe.bottom);

    // Zentriert wird auf die Mitte der SAFE AREA, nicht auf die der Renderflaeche. Gemessen am
    // Konstruktorweg posCenter, weil MoveToCenter protected ist: der Abstand zum linken
    // Safe-Rand muss gleich dem zum rechten sein.
    {
        IngameWindow centered(id, IngameWindow::posCenter, Extent(301, 400), "TV center", nullptr);
        const int leftGap = centered.GetPos().x - safe.left;
        const int rightGap = safe.right - (centered.GetPos().x + static_cast<int>(centered.GetSize().x));
        BOOST_TEST(std::abs(leftGap - rightGap) <= 1);
        BOOST_TEST(leftGap > 0);
    }

    // GEGENPROBE: Rand aus -> das Fenster darf wieder in die Ecke.
    SETTINGS.video.tvSafeAreaPercent = 0;
    wnd.SetPos(DrawPoint(-1000, -1000));
    BOOST_TEST(wnd.GetPos() == DrawPoint(0, 0));
    wnd.SetPos(DrawPoint(100000, 100000));
    BOOST_TEST(wnd.GetPos().x + static_cast<int>(wnd.GetSize().x) == static_cast<int>(renderSize.x));
    BOOST_TEST(wnd.GetPos().y + static_cast<int>(wnd.GetSize().y) == static_cast<int>(renderSize.y));
}

/// Die harte Randbedingung, am Fenster nachgemessen: ohne Fernsehmodus rechnet SetPos Zahl fuer
/// Zahl wie vorher - Ecke ist Ecke, Mitte ist Bildmitte.
BOOST_FIXTURE_TEST_CASE(WithoutTvModeWindowPlacementIsUnchanged, TvFixture)
{
    constexpr auto id = CGI_OBSERVATION;
    TvFixture::useScreen(3840, 2160, false);
    const Extent renderSize = VIDEODRIVER.GetRenderSize();

    IngameWindow wnd(id, IngameWindow::posCenter, Extent(300, 400), "Desktop", nullptr);
    BOOST_TEST(wnd.GetPos() == DrawPoint(renderSize - wnd.GetSize()) / 2);

    wnd.SetPos(DrawPoint(0, 0));
    BOOST_TEST(wnd.GetPos() == DrawPoint(0, 0));
    wnd.SetPos(DrawPoint(100000, 100000));
    BOOST_TEST(wnd.GetPos() == DrawPoint(renderSize - wnd.GetSize()));
}

/// BEFUND 3, DIE ENTSCHEIDUNG: Rahmen, Eckstatuen und Knopfleiste liegen in EINEM Kasten.
///
/// Vorher hob nur die Knopfleiste ab: CalcButtonBarOrigin zog sie um den Safe-Rand herein,
/// waehrend der gekachelte Rahmen und die vier Statuen an der Bildkante blieben. Im
/// Fernsehmodus klaffte dazwischen bei 5 % eine Luecke von 54 View-Einheiten, und die Leiste
/// sass nicht mehr auf ihrem Mittelstueck.
///
/// Entschieden wurde fuer "alles herein" und nicht fuer "Leiste zurueck an die Kante", weil die
/// Leiste das am haeufigsten gebrauchte Bedienelement ist und der Rahmen reine Zier - und weil
/// der Rahmen es KANN: CustomBorderBuilder::buildBorder setzt ihn aus Kacheln fuer jede Groesse
/// >= 640x480 zusammen. Diese Untergrenze ist zugleich die Bedingung, unter der der Rand
/// nachgibt.
BOOST_FIXTURE_TEST_CASE(FrameStatuesAndButtonBarShareOneBox, TvFixture)
{
    // Ohne Fernsehmodus ist der Kasten die ganze Flaeche - Zahl fuer Zahl das heutige Bild.
    SETTINGS.video.tvMode = false;
    SETTINGS.video.tvSafeAreaPercent = tv::SAFE_AREA_PERCENT_DEFAULT;
    for(const Extent screen : {Extent(800, 600), Extent(1920, 1080), Extent(3840, 2160)})
    {
        BOOST_TEST_CONTEXT(screen)
        BOOST_TEST((tv::ScreenChromeRect(screen) == Rect(Position(0, 0), screen)));
    }

    // Mit Fernsehmodus rueckt er um den Rand herein - und zwar genau so weit wie die Fenster.
    SETTINGS.video.tvMode = true;
    {
        constexpr Extent canvas(1920, tv::UI_REFERENCE_HEIGHT);
        const Rect chrome = tv::ScreenChromeRect(canvas);
        BOOST_TEST((chrome == tv::ActiveSafeAreaRect(canvas)));
        BOOST_TEST((chrome.getOrigin() == Position(96, 54)));
        BOOST_TEST((chrome.getSize() == Extent(1728, 972)));
        // Der Rahmenbauer kann diese Groesse bauen - das ist die Bedingung, an der alles haengt.
        BOOST_TEST(chrome.getSize().x >= tv::SCREEN_CHROME_MIN_SIZE.x);
        BOOST_TEST(chrome.getSize().y >= tv::SCREEN_CHROME_MIN_SIZE.y);
    }

    // Und er gibt nach, sobald der Rahmenbauer die Restflaeche nicht mehr bauen koennte. Ohne
    // dieses Nachgeben stuenden in dskGameInterface vier Nullzeiger.
    SETTINGS.video.tvSafeAreaPercent = tv::SAFE_AREA_PERCENT_MAX;
    for(const Extent screen : {Extent(640, 480), Extent(700, 520), Extent(800, 600), Extent(1024, 768),
                               Extent(1280, 720), Extent(1920, 1080), Extent(3840, 2160)})
    {
        BOOST_TEST_CONTEXT(screen)
        {
            const Extent chromeSize = tv::ScreenChromeRect(screen).getSize();
            // Entweder er passt - oder er ist die volle Flaeche, also genau so gross wie ohne
            // Fernsehmodus. Ein Zustand dazwischen kann nicht entstehen.
            BOOST_TEST((chromeSize == screen || (chromeSize.x >= tv::SCREEN_CHROME_MIN_SIZE.x
                                                 && chromeSize.y >= tv::SCREEN_CHROME_MIN_SIZE.y)));
            BOOST_TEST(chromeSize.x <= screen.x);
            BOOST_TEST(chromeSize.y <= screen.y);
        }
    }
    // Der konkrete Fall: 800x600 mit 10 % ergaebe 640x480 - genau die Untergrenze, also noch
    // baubar. 700x520 ergaebe 560x416 und ist es nicht mehr; dort bleibt der Rahmen an der Kante.
    BOOST_TEST((tv::ScreenChromeRect(Extent(800, 600)).getSize() == Extent(640, 480)));
    BOOST_TEST((tv::ScreenChromeRect(Extent(700, 520)) == Rect(Position(0, 0), Extent(700, 520))));
}

/// Und dieselbe Entscheidung am ECHTEN Spieldesktop: die Knoepfe der unteren Leiste liegen im
/// Kasten, aus dem auch der Rahmen gebaut wird (dskGameInterface: cbb.buildBorder und
/// CalcButtonBarOrigin bekommen beide tv::ScreenChromeRect). Ohne Fernsehmodus liegen sie an
/// derselben Stelle wie vorher - der untere Bildrand.
///
/// Was dieser Nachweis NICHT leistet: er sieht den gezeichneten Rahmen nicht, denn der entsteht
/// erst im Zeichenpfad. Er nagelt die Seite fest, die beobachtbar ist, und die zweite haengt im
/// Quelltext an derselben Funktion.
BOOST_FIXTURE_TEST_CASE(TheButtonBarSitsInTheSameBoxAsTheFrame, rttr::test::PadGameFixture)
{
    TvFixture guard;

    const auto buttonBounds = [&] {
        Rect bounds(DrawPoint(std::numeric_limits<int>::max(), std::numeric_limits<int>::max()),
                    Extent(0, 0));
        bounds.right = std::numeric_limits<int>::min();
        bounds.bottom = std::numeric_limits<int>::min();
        for(const auto* bt : dsk->GetCtrls<ctrlButton>())
        {
            bounds.left = std::min(bounds.left, bt->GetPos().x);
            bounds.top = std::min(bounds.top, bt->GetPos().y);
            bounds.right = std::max(bounds.right, bt->GetPos().x + static_cast<int>(bt->GetSize().x));
            bounds.bottom = std::max(bounds.bottom, bt->GetPos().y + static_cast<int>(bt->GetSize().y));
        }
        return bounds;
    };

    // Gemessen wird bei EINER Aufloesung mit und ohne Fernsehmodus, und verglichen wird der
    // ABSTAND der Leiste zur Unterkante ihres Kastens. Absolute Pixelwerte taugen hier nicht:
    // die Leistengrafik ist im Testaufbau eine Attrappe von 1x1 (Loader::LoadDummyGUIFiles), der
    // Abstand haengt also an einer Zahl, die im Spiel eine andere ist. Der VERGLEICH haengt an
    // keiner davon.
    //
    // 1920x1080 ist bewusst gewaehlt: dort ist der Skalierungsfaktor im Fernsehmodus genau 1,
    // die Renderflaeche also in beiden Durchgaengen dieselbe. Was sich unterscheidet, ist allein
    // der Safe-Rand.
    SETTINGS.video.tvMode = false;
    setUpTwoLocalPlayers();

    TvFixture::useScreen(1920, 1080, false);
    dsk.reset();
    dsk = std::make_unique<rttr::test::TestableGameInterface>(ci().game, GAMECLIENT.GetNWFInfo(),
                                                              GAMECLIENT.GetPlayerId(), /*initOGL*/ false);
    const Extent renderSize = VIDEODRIVER.GetRenderSize();
    BOOST_TEST_REQUIRE(renderSize == Extent(1920, 1080));
    BOOST_TEST((tv::ScreenChromeRect(renderSize) == Rect(Position(0, 0), renderSize)));
    const Rect btsOff = buttonBounds();
    BOOST_TEST_REQUIRE(btsOff.right > btsOff.left); // es gibt ueberhaupt Knoepfe

    dsk.reset();
    TvFixture::useScreen(1920, 1080, true);
    SETTINGS.video.tvSafeAreaPercent = tv::SAFE_AREA_PERCENT_DEFAULT;
    dsk = std::make_unique<rttr::test::TestableGameInterface>(ci().game, GAMECLIENT.GetNWFInfo(),
                                                              GAMECLIENT.GetPlayerId(), /*initOGL*/ false);
    BOOST_TEST_REQUIRE((VIDEODRIVER.GetRenderSize() == renderSize)); // Faktor 1 auf 1080p
    const Rect chrome = tv::ScreenChromeRect(renderSize);
    BOOST_TEST_REQUIRE((chrome == Rect(96, 54, 1728u, 972u)));
    const Rect btsOn = buttonBounds();
    BOOST_TEST_REQUIRE(btsOn.right > btsOn.left);

    // Die Kernaussage: der Abstand der Leiste zur Unterkante IHRES Kastens ist derselbe. Sie ist
    // also mit dem Rahmen mitgewandert und nicht von ihm abgerueckt.
    BOOST_TEST((static_cast<int>(renderSize.y) - btsOff.bottom) == (chrome.bottom - btsOn.bottom));
    // Und sie ist tatsaechlich gewandert, und zwar um genau den unteren Safe-Rand. Ohne diese
    // Zeile waere der Test auch dann gruen, wenn gar nichts passierte.
    BOOST_TEST(btsOff.bottom - btsOn.bottom == 54);
    // Waagerecht bleibt sie mittig - der Rand ist symmetrisch, die Mitte also dieselbe.
    BOOST_TEST(btsOff.left == btsOn.left);
    BOOST_TEST(btsOff.right == btsOn.right);
}

// --------------------------------------------------------------------------------------------
// 4. Der Kartenzoom - der einzige Hebel, der auf die WELT wirkt
// --------------------------------------------------------------------------------------------

/// Die GUI-Skalierung wirkt auf die Karte nachweislich gar nicht
/// (GameWorldView::updateEffectiveZoomFactor rechnet sie wieder heraus). Der Startzoom ist
/// deshalb ein zweiter, unabhaengiger Hebel - und er rechnet gegen dieselbe Referenzhoehe,
/// damit beide Hebel dieselbe Geschichte erzaehlen.
BOOST_AUTO_TEST_CASE(RecommendedZoomMatchesTheReferenceHeight)
{
    // Unter und auf der Referenzhoehe: exakt der bisherige Standard. Einzelspieler auf 1080p
    // sieht also dieselbe Karte wie heute, selbst mit eingeschaltetem Fernsehmodus.
    BOOST_TEST(tv::RecommendedZoomFactor(600) == ZOOM_FACTORS[ZOOM_DEFAULT_INDEX]);
    BOOST_TEST(tv::RecommendedZoomFactor(1080) == ZOOM_FACTORS[ZOOM_DEFAULT_INDEX]);
    BOOST_TEST(ZOOM_FACTORS[ZOOM_DEFAULT_INDEX] == 1.f);

    BOOST_TEST(tv::RecommendedZoomFactor(1440) == 1.25f);
    BOOST_TEST(tv::RecommendedZoomFactor(2160) == 2.f);

    // Immer ein ANGEBOTENER Faktor: der Spieler laeuft mit Pad und Mausrad durch dieselbe
    // Liste, in der er gestartet ist.
    for(const unsigned h : {480u, 600u, 720u, 1080u, 1200u, 1440u, 1600u, 2160u, 4320u})
    {
        BOOST_TEST_CONTEXT("height " << h)
        {
            const float f = tv::RecommendedZoomFactor(h);
            BOOST_TEST(helpers::contains(ZOOM_FACTORS, f));
            BOOST_TEST(f >= ZOOM_FACTORS[ZOOM_DEFAULT_INDEX]); // nie weiter herausgezoomt als heute
            BOOST_TEST(f <= ZOOM_FACTORS.back());
        }
    }
}

/// Warum ausgerechnet 2.0 bei 4K: ein Kartenknoten ist dann wieder genauso gross wie auf einem
/// 1080p-Geraet bei Zoom 1. Gerechnet ueber TR_W/TR_H (gameData/MapConsts.h), den Knotenabstand
/// in physischen Pixeln bei Zoom 1 - die GUI-Skalierung kommt darin bewusst NICHT vor.
BOOST_AUTO_TEST_CASE(TvZoomReproducesThePhysicalNodeSizeOfA1080pScreen)
{
    const auto nodeShareOfScreenHeight = [](unsigned screenHeight) {
        return TR_H * tv::RecommendedZoomFactor(screenHeight) / static_cast<double>(screenHeight);
    };
    const double reference = nodeShareOfScreenHeight(1080);
    BOOST_TEST(nodeShareOfScreenHeight(2160) == reference, boost::test_tools::tolerance(0.001));
    // 1440p trifft es nicht exakt - 1440/1080 = 1.333 liegt zwischen den angebotenen Stufen
    // 1.25 und 1.5. Genommen wird die kleinere, der Knoten ist dort also etwas kleiner als das
    // Vorbild. Bewusst so: lieber etwas zu klein als ein Zoom, den die Liste nicht anbietet.
    BOOST_TEST(nodeShareOfScreenHeight(1440) < reference);
    BOOST_TEST(nodeShareOfScreenHeight(1440) > reference * 0.9);
}

/// DIE VERDRAHTUNG des Startzooms, am ECHTEN Desktop einer laufenden Splitscreen-Partie.
///
/// Die Rechnung allein beweist nichts - sie muss auch aufgerufen werden. Geprueft werden beide
/// Zweige an derselben Partie: erst der Kontrollfall ohne Fernsehmodus (Zoom 1.0, also exakt der
/// Wert, den der GameWorldView-Konstruktor ohnehin setzt), dann derselbe Desktop noch einmal
/// gebaut, diesmal auf 4K mit Fernsehmodus.
BOOST_FIXTURE_TEST_CASE(TvModeGivesEveryLocalViewATvSizedStartZoom, rttr::test::PadGameFixture)
{
    TvFixture guard; // sichert Aufloesung, Skalierung und Einstellungen fuer diesen Testfall

    SETTINGS.video.tvMode = false;
    setUpTwoLocalPlayers();
    BOOST_TEST_REQUIRE(dsk->GetNumViews() == 2u);
    for(unsigned i = 0; i < 2; ++i)
        BOOST_TEST(dsk->GetPlayerView(i).GetView().GetCurrentTargetZoomFactor() == ZOOM_FACTORS[ZOOM_DEFAULT_INDEX]);

    // Denselben Desktop noch einmal, diesmal am Fernseher.
    dsk.reset();
    TvFixture::useScreen(3840, 2160, true);
    BOOST_TEST_REQUIRE(VIDEODRIVER.getGuiScale().percent() == 200u);
    dsk = std::make_unique<rttr::test::TestableGameInterface>(ci().game, GAMECLIENT.GetNWFInfo(),
                                                              GAMECLIENT.GetPlayerId(), /*initOGL*/ false);
    BOOST_TEST_REQUIRE(dsk->GetNumViews() == 2u);
    for(unsigned i = 0; i < 2; ++i)
    {
        BOOST_TEST_CONTEXT("view " << i)
        {
            // Die Karte startet auf dem Zoom, der die physische Knotengroesse eines
            // 1080p-Bildschirms reproduziert ...
            BOOST_TEST(dsk->GetPlayerView(i).GetView().GetCurrentTargetZoomFactor() == 2.f);
            // ... und die Ansicht liegt vollstaendig in der Renderflaeche.
            //
            // Hier stand einmal "die Ansicht ist gross genug fuer jedes Ingamefenster". Das war
            // dieselbe falsche Zahl wie oben und ist zurueckgezogen: bei VIER Ansichten waere
            // ein Viertelbild 960x540 und damit niedriger als die gemessenen 635. Die Aussage,
            // die traegt, ist die ueber die RENDERFLAECHE - und die steht dort, wo sie
            // hingehoert (TvScaleKeepsEveryViewportAsLargeAsA1080pScreen).
            const auto& view = dsk->GetPlayerView(i).GetView();
            BOOST_TEST(view.GetPos().x + static_cast<int>(view.GetSize().x)
                       <= static_cast<int>(VIDEODRIVER.GetRenderSize().x));
            BOOST_TEST(view.GetPos().y + static_cast<int>(view.GetSize().y)
                       <= static_cast<int>(VIDEODRIVER.GetRenderSize().y));
        }
    }
}

// --------------------------------------------------------------------------------------------
// 5. Die kleineren Befunde des Pruefers, jeder mit seiner Zahl
// --------------------------------------------------------------------------------------------

/// BEFUND E: ein negativer Wert in der ini landete nach der Wandlung nach unsigned auf dem
/// MAXIMUM - dem groessten Rand also, wo offensichtlich keiner gemeint war.
BOOST_AUTO_TEST_CASE(ANegativeSafeAreaFromTheConfigFileFallsBackToTheDefault)
{
    // Der Fehlerfall, mit der Zahl, die frueher herauskam.
    BOOST_TEST(static_cast<unsigned>(-1) > tv::SAFE_AREA_PERCENT_MAX); // ... deshalb der Deckel
    BOOST_TEST(tv::SanitizeSafeAreaPercent(-1) == tv::SAFE_AREA_PERCENT_DEFAULT);
    BOOST_TEST(tv::SanitizeSafeAreaPercent(-100) == tv::SAFE_AREA_PERCENT_DEFAULT);

    // Zu gross heisst weiterhin "so viel wie geht" - das ist eine deutbare Absicht.
    BOOST_TEST(tv::SanitizeSafeAreaPercent(50) == tv::SAFE_AREA_PERCENT_MAX);
    // Und alles Gueltige bleibt unangetastet, die ausdrueckliche Null eingeschlossen.
    for(int percent = 0; percent <= static_cast<int>(tv::SAFE_AREA_PERCENT_MAX); ++percent)
        BOOST_TEST(tv::SanitizeSafeAreaPercent(percent) == static_cast<unsigned>(percent));
}

/// BEFUND F: die Referenzhoehe kennt nur ZEILEN. Auf einem hochkanten Fenster verlangte sie
/// deshalb eine Skalierung, bei der die Renderflaeche SCHMALER wird als die 800 View-Einheiten,
/// auf die die Bedienoberflaeche ausgelegt ist.
///
/// Die erste Korrektur dazu war unfertig: sie kappte auf iround(maxScale * 100), also auf einen
/// GERUNDETEN Wert. Runden geht aber in beide Richtungen, und nach oben ist es genau der
/// Fehler, den die Kappung verhindern soll - bei 804x1920 kam 101 % heraus, davon blieben
/// 804/1.01 = 796 View-Einheiten Breite. Von den 601 Breiten 800..1400 fielen so 296 durch,
/// also fast jede zweite. Diese Zusicherung faehrt deshalb den ganzen Bereich ab und nicht nur
/// eine Handvoll Stichproben.
BOOST_FIXTURE_TEST_CASE(TvScaleNeverShrinksTheSurfaceBelowTheUiMinimum, TvFixture)
{
    // Der Fall, an dem es kippte: 1920 Zeilen wollen 178 %, davon blieben 1080/1.78 = 607
    // View-Einheiten Breite.
    TvFixture::useScreen(1080, 1920, true);
    BOOST_TEST(VIDEODRIVER.GetRenderSize().x >= 800u);
    BOOST_TEST(VIDEODRIVER.GetRenderSize().y >= 600u);
    // Gekappt auf das, was in BEIDE Achsen NACHWEISLICH passt: 1080/800 = 1.35, und die
    // Umrechnung liefert hier tatsaechlich glatte 800 View-Einheiten. Der Wert wird nicht
    // gerechnet und geglaubt, sondern mit derselben abschneidenden Umrechnung nachgeprueft, die
    // spaeter auch die Renderflaeche erzeugt - deshalb steht die gemessene Breite mit daneben.
    BOOST_TEST(VIDEODRIVER.getGuiScaleRange().recommendedPercent == 135u);
    BOOST_TEST(VIDEODRIVER.GetRenderSize().x == 800u);

    // Und quer durch alles, was ein Fenster oder ein Bildschirm sein kann - hochkant, quer,
    // winzig, riesig.
    for(const auto& mode : {VideoMode(800, 600), VideoMode(1024, 768), VideoMode(1080, 1920), VideoMode(600, 800),
                            VideoMode(1920, 1080), VideoMode(3840, 2160), VideoMode(2160, 3840), VideoMode(3840, 1080)})
    {
        BOOST_TEST_CONTEXT(mode.width << "x" << mode.height)
        {
            TvFixture::useScreen(mode.width, mode.height, true);
            const Extent render = VIDEODRIVER.GetRenderSize();
            // Nie kleiner als ohne Fernsehmodus, und nie unter das UI-Minimum - ausser das
            // Fenster selbst ist schon kleiner, dann kann keine Skalierung mehr helfen.
            BOOST_TEST(render.x >= std::min<unsigned>(800u, mode.width));
            BOOST_TEST(render.y >= std::min<unsigned>(600u, mode.height));
        }
    }
}

/// Derselbe Befund, aber LUECKENLOS: genau die 601 Breiten, an denen die unfertige Korrektur
/// gemessen wurde. Vorher lagen 296 davon unter 800; hier muss es keine einzige sein.
///
/// Bewusst ein eigener Testfall und kein weiterer Block im vorigen: er ist der teuerste
/// (601 Bildschirmwechsel), und wenn er faellt, soll der Name sagen, WAS gefallen ist.
BOOST_FIXTURE_TEST_CASE(NoWidthInTheCheckedRangeFallsBelowTheUiMinimum, TvFixture)
{
    unsigned numChecked = 0;
    unsigned numTooNarrow = 0;
    unsigned firstBad = 0;
    for(unsigned width = 800; width <= 1400; ++width)
    {
        TvFixture::useScreen(width, 1920, true);
        ++numChecked;
        if(VIDEODRIVER.GetRenderSize().x < 800u)
        {
            if(numTooNarrow == 0)
                firstBad = width;
            ++numTooNarrow;
        }
    }
    BOOST_TEST(numChecked == 601u);
    BOOST_TEST_CONTEXT("erste zu schmale Breite: " << firstBad)
    BOOST_TEST(numTooNarrow == 0u);
}

/// BEFUND G, DIE ENTSCHEIDUNG: die Zeigergeschwindigkeit des Pads wird in VIEW-EINHEITEN
/// gemessen, nicht in physischen Pixeln (PadRouter::PixelsPerSecond, dort ausfuehrlich
/// begruendet).
///
/// Die Folge ist die, die der Pruefer gemessen hat, und sie ist gewollt: im Fernsehmodus laeuft
/// der Zeiger auf einem 4K-Bildschirm bildschirmbezogen doppelt so schnell. Der Grund, warum das
/// die richtige Achse ist, steht hier als Zahl: AUFLOESUNG IST NICHT GROESSE. Ein 4K- und ein
/// 1080p-Fernseher derselben Diagonale sind gleich gross, und der Zeiger braucht auf beiden
/// gleich lang ueber das Glas. In physischen Pixeln gemessen waere er auf dem 4K-Geraet doppelt
/// so langsam - fuer den Spieler ohne erkennbare Ursache.
BOOST_FIXTURE_TEST_CASE(ThePadCursorCrossesTheScreenInTheSameTimeOnEveryTvResolution, TvFixture)
{
    // Sekunden fuer die volle BREITE der Renderflaeche bei Vollausschlag.
    const auto secondsAcrossScreen = [] { return VIDEODRIVER.GetRenderSize().x / PadRouter::PixelsPerSecond; };

    TvFixture::useScreen(1920, 1080, true);
    const float reference = secondsAcrossScreen();
    BOOST_TEST(reference == 1920.f / 900.f, boost::test_tools::tolerance(0.001f));

    for(const auto& mode : {VideoMode(2560, 1440), VideoMode(3840, 2160), VideoMode(7680, 4320)})
    {
        BOOST_TEST_CONTEXT(mode.width << "x" << mode.height)
        {
            TvFixture::useScreen(mode.width, mode.height, true);
            // Gleiche Zeit ueber denselben Fernseher - unabhaengig von der Aufloesung.
            BOOST_TEST(secondsAcrossScreen() == reference, boost::test_tools::tolerance(0.02f));
        }
    }

    // OHNE Fernsehmodus aendert sich fuer heutige Spieler nichts: View-Einheiten sind dort
    // Bildschirmpixel, und die Zeit ist dieselbe wie vor dieser Phase.
    TvFixture::useScreen(1920, 1080, false);
    BOOST_TEST(VIDEODRIVER.GetRenderSize().x == 1920u);
    BOOST_TEST(secondsAcrossScreen() == 1920.f / 900.f, boost::test_tools::tolerance(0.001f));
}

BOOST_AUTO_TEST_SUITE_END()
