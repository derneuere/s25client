// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ViewportFixture.h"
#include "drivers/VideoDriverWrapper.h"
#include "world/GameWorldView.h"
#include "world/ViewportLayout.h"
#include "helpers/EnumRange.h"
#include "gameData/MapConsts.h"
#include "mockupDrivers/MockupVideoDriver.h"
#include "uiHelper/uiHelpers.hpp"
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <optional>
#include <vector>

using rttr::test::ViewportFixture;

namespace {
bool overlaps(const Viewport& a, const Viewport& b)
{
    const int aRight = a.origin.x + static_cast<int>(a.size.x);
    const int aBottom = a.origin.y + static_cast<int>(a.size.y);
    const int bRight = b.origin.x + static_cast<int>(b.size.x);
    const int bBottom = b.origin.y + static_cast<int>(b.size.y);
    return a.origin.x < bRight && b.origin.x < aRight && a.origin.y < bBottom && b.origin.y < aBottom;
}

bool overlaps(const Rect& a, const Rect& b)
{
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

unsigned long long area(const Viewport& v)
{
    return static_cast<unsigned long long>(v.size.x) * v.size.y;
}

/// Liegt der Kartenpunkt im gezeichneten Ausschnitt dieser Ansicht? Genau ueber firstPt/lastPt
/// laufen sowohl TerrainRenderer::Draw als auch die Objektschleife in GameWorldView::Draw.
bool covers(const GameWorldView& view, const MapPoint& pt)
{
    const Position first = view.GetFirstPt();
    const Position last = view.GetLastPt();
    for(int y = first.y; y <= last.y; ++y)
    {
        for(int x = first.x; x <= last.x; ++x)
        {
            if(view.GetViewer().GetTerrainRenderer().ConvertCoords(Position(x, y)) == pt)
                return true;
        }
    }
    return false;
}
} // namespace

BOOST_AUTO_TEST_SUITE(ViewportTests)

// --------------------------------------------------------------------------------------------
// 1. Die reine Aufteilungsfunktion
// --------------------------------------------------------------------------------------------

/// N-Invarianten des Layouts. Kein Videotreiber, kein Spiel, keine Grafik.
BOOST_AUTO_TEST_CASE(ViewportLayoutTilesTheScreenWithoutOverlap)
{
    // Ungerade Kantenlaengen bewusst dabei, damit die Rundung mitgeprueft wird
    for(const Extent renderSize : {Extent(1920, 1080), Extent(800, 600), Extent(1281, 801), Extent(600, 1000)})
    {
        BOOST_TEST_CONTEXT("renderSize " << renderSize.x << "x" << renderSize.y)
        {
            BOOST_TEST(CalcViewports(renderSize, 0).empty());

            for(const unsigned numViews : {1u, 2u, 3u, 4u})
            {
                BOOST_TEST_CONTEXT("numViews " << numViews)
                {
                    const std::vector<Viewport> vps = CalcViewports(renderSize, numViews);
                    BOOST_TEST_REQUIRE(vps.size() == numViews);

                    unsigned long long coveredArea = 0;
                    for(unsigned i = 0; i < numViews; ++i)
                    {
                        // Innerhalb der Renderflaeche
                        BOOST_TEST(vps[i].origin.x >= 0);
                        BOOST_TEST(vps[i].origin.y >= 0);
                        BOOST_TEST(vps[i].origin.x + static_cast<int>(vps[i].size.x) <= static_cast<int>(renderSize.x));
                        BOOST_TEST(vps[i].origin.y + static_cast<int>(vps[i].size.y) <= static_cast<int>(renderSize.y));
                        // Nicht entartet
                        BOOST_TEST(vps[i].size.x > 0u);
                        BOOST_TEST(vps[i].size.y > 0u);
                        coveredArea += area(vps[i]);
                        // Keine Ueberlappung
                        for(unsigned j = i + 1; j < numViews; ++j)
                        {
                            BOOST_TEST_INFO("views " << i << " and " << j);
                            BOOST_TEST(!overlaps(vps[i], vps[j]));
                        }
                    }

                    // Kein Pixel geht verloren - bei JEDER Ansichtszahl.
                    //
                    // Bis zu dieser Runde stand hier fuer numViews == 3 das Gegenteil ("die
                    // vierte Zelle bleibt frei"). Die freie Zelle lag mitten auf dem Bildschirm,
                    // und der Eingabepfad beruft sich ausdruecklich darauf, dass jeder Punkt der
                    // Renderflaeche zu einer Ansicht gehoert (dskGameInterface::UpdateInput).
                    const auto fullArea = static_cast<unsigned long long>(renderSize.x) * renderSize.y;
                    BOOST_TEST(coveredArea == fullArea);
                }
            }
        }
    }
}

/// Die harte Randbedingung "Einzelspieler darf nicht regressieren", als Zahl: bei einer Ansicht
/// ist das Ergebnis exakt das, was dskGameInterface heute im Konstruktor baut
/// (Position(0,0) + VIDEODRIVER.GetRenderSize(), desktops/dskGameInterface.cpp:128).
BOOST_AUTO_TEST_CASE(SingleViewportIsExactlyFullscreen)
{
    const Extent renderSize(1920, 1080);
    const std::vector<Viewport> vps = CalcViewports(renderSize, 1);
    BOOST_TEST_REQUIRE(vps.size() == 1u);
    BOOST_TEST((vps[0].origin == Position(0, 0)));
    BOOST_TEST((vps[0].size == renderSize));
}

/// Zwei Ansichten teilen entlang der laengeren Achse: auf 16:9 links|rechts, auf einem hochkanten
/// Fenster oben|unten.
BOOST_AUTO_TEST_CASE(TwoViewportsSplitAlongTheLongerAxis)
{
    const std::vector<Viewport> wide = CalcViewports(Extent(1920, 1080), 2);
    BOOST_TEST_REQUIRE(wide.size() == 2u);
    BOOST_TEST((wide[0] == Viewport{Position(0, 0), Extent(960, 1080)}));
    BOOST_TEST((wide[1] == Viewport{Position(960, 0), Extent(960, 1080)}));

    const std::vector<Viewport> tall = CalcViewports(Extent(600, 1000), 2);
    BOOST_TEST_REQUIRE(tall.size() == 2u);
    BOOST_TEST((tall[0] == Viewport{Position(0, 0), Extent(600, 500)}));
    BOOST_TEST((tall[1] == Viewport{Position(0, 500), Extent(600, 500)}));
}

/// Vier Ansichten: 2x2-Raster in Leserichtung.
///
/// Drei Ansichten benutzten frueher DASSELBE Raster und liessen die vierte Zelle frei. Das ist
/// diese Runde weggefallen: die freie Zelle lag mitten auf dem Bildschirm, und dort stand die
/// Maus ueber gar keiner Ansicht - eine Luecke, in der ein Klick auf eine Ansicht wirkte, ueber
/// der die Maus nicht stand. Die dritte Ansicht nimmt jetzt die volle Breite der unteren Zeile.
/// Die beiden oberen behalten exakt die Geometrie des Vierer-Layouts.
BOOST_AUTO_TEST_CASE(ThreeViewportsFillTheBottomRowFourUseTheQuadrantGrid)
{
    const Extent renderSize(800, 600);
    const std::vector<Viewport> three = CalcViewports(renderSize, 3);
    const std::vector<Viewport> four = CalcViewports(renderSize, 4);
    BOOST_TEST_REQUIRE(three.size() == 3u);
    BOOST_TEST_REQUIRE(four.size() == 4u);

    BOOST_TEST((four[0] == Viewport{Position(0, 0), Extent(400, 300)}));
    BOOST_TEST((four[1] == Viewport{Position(400, 0), Extent(400, 300)}));
    BOOST_TEST((four[2] == Viewport{Position(0, 300), Extent(400, 300)}));
    BOOST_TEST((four[3] == Viewport{Position(400, 300), Extent(400, 300)}));

    BOOST_TEST((three[0] == four[0]));
    BOOST_TEST((three[1] == four[1]));
    BOOST_TEST((three[2] == Viewport{Position(0, 300), Extent(800, 300)}));
}

// --------------------------------------------------------------------------------------------
// 2. Mehrere echte GameWorldViews nebeneinander
// --------------------------------------------------------------------------------------------

/// N1: zwei Ansichten mit verschiedenem origin_/size_ zeigen verschiedene Kartenausschnitte.
/// Geprueft wird an firstPt/lastPt - genau den Werten, ueber die TerrainRenderer::Draw
/// (TerrainRenderer.cpp:693-698) und die Objektschleife (GameWorldView.cpp:172-175) laufen. Das
/// ist kein Ersatzmass, sondern dieselbe Zahl, die das Zeichnen benutzt.
BOOST_FIXTURE_TEST_CASE(TwoViewsShowDifferentMapSections, ViewportFixture)
{
    const std::vector<Viewport> vps = CalcViewports(Extent(800, 600), 2);
    BOOST_TEST_REQUIRE(vps.size() == 2u);

    GameWorldView left(viewer0, vps[0].origin, vps[0].size);
    GameWorldView right(viewer1, vps[1].origin, vps[1].size);

    const MapPoint hq0 = world.GetPlayer(0).GetHQPos();
    const MapPoint hq1 = world.GetPlayer(1).GetHQPos();
    BOOST_TEST_REQUIRE(hq0.isValid());
    BOOST_TEST_REQUIRE(hq1.isValid());
    BOOST_TEST_REQUIRE((hq0 != hq1));

    left.MoveToMapPt(hq0);
    right.MoveToMapPt(hq1);

    // Verschiedene Scrollposition ...
    BOOST_TEST((left.GetOffset() != right.GetOffset()));
    // ... und daraus verschiedene Ausschnitte
    BOOST_TEST((left.GetFirstPt() != right.GetFirstPt()));
    BOOST_TEST((left.GetLastPt() != right.GetLastPt()));

    // Jede Ansicht zeigt ihr eigenes HQ und nicht das des anderen
    BOOST_TEST(covers(left, hq0));
    BOOST_TEST(!covers(left, hq1));
    BOOST_TEST(covers(right, hq1));
    BOOST_TEST(!covers(right, hq0));
}

/// N2: derselbe Bildschirmpunkt bedeutet in zwei Ansichten zwei verschiedene Kartenpositionen.
/// Rot, sobald der origin_-Abzug in GameWorldView::ViewPosToMap (GameWorldView.cpp:113)
/// verschwindet.
BOOST_FIXTURE_TEST_CASE(ScreenPointMapsToDifferentMapPointPerView, ViewportFixture)
{
    const std::vector<Viewport> vps = CalcViewports(Extent(800, 600), 2);
    GameWorldView left(viewer0, vps[0].origin, vps[0].size);
    GameWorldView right(viewer1, vps[1].origin, vps[1].size);

    const Position screenPt(500, 50);
    BOOST_TEST((left.ViewPosToMap(screenPt) != right.ViewPosToMap(screenPt)));
    // Konkret: der rechte Viewport beginnt bei x=400, zieht also genau 400 ab.
    BOOST_TEST((left.ViewPosToMap(screenPt) == Position(500, 50)));
    BOOST_TEST((right.ViewPosToMap(screenPt) == Position(100, 50)));
}

/// N6/N7: jede Ansicht haengt an ihrem eigenen Viewer, und der Fog of War unterscheidet sich
/// messbar. Rot, sobald sich jemand aus Sparsamkeit einen Viewer fuer alle Ansichten teilt.
BOOST_FIXTURE_TEST_CASE(EachViewSeesItsOwnPlayersFogOfWar, ViewportFixture)
{
    GameWorldView left(viewer0, Position(0, 0), Extent(400, 600));
    GameWorldView right(viewer1, Position(400, 0), Extent(400, 600));

    BOOST_TEST((&left.GetViewer() != &right.GetViewer()));
    BOOST_TEST(left.GetViewer().GetPlayerId() == 0u);
    BOOST_TEST(right.GetViewer().GetPlayerId() == 1u);

    const MapPoint hq0 = world.GetPlayer(0).GetHQPos();
    const MapPoint hq1 = world.GetPlayer(1).GetHQPos();

    BOOST_TEST((viewer0.GetVisibility(hq0) == Visibility::Visible));
    BOOST_TEST((viewer0.GetVisibility(hq1) == Visibility::Invisible));
    BOOST_TEST((viewer1.GetVisibility(hq1) == Visibility::Visible));
    BOOST_TEST((viewer1.GetVisibility(hq0) == Visibility::Invisible));
}

// --------------------------------------------------------------------------------------------
// 3. Der Zeiger gehoert der Ansicht, nicht dem Videotreiber
// --------------------------------------------------------------------------------------------

/// N3/N4 und das Orakel: der selektierte Punkt kommt aus dem Zeiger DIESER Ansicht.
///
/// Das Orakel rechnet vorwaerts ueber GameWorldBase::GetNodePos, der Produktivcode rueckwaerts -
/// die beiden Rechenwege sind unabhaengig.
///
/// Warum das funktional zaehlt und nicht Kosmetik ist: dskGameInterface::ContextClick benutzt
/// gwv.GetSelectedPt() als den geklickten Punkt (dskGameInterface.cpp:498 und :560). Ohne diese
/// Naht wuerde Spieler 2 auf den Punkt klicken, auf den Spieler 1 zeigt.
BOOST_FIXTURE_TEST_CASE(CursorBelongsToTheViewNotTheVideoDriver, ViewportFixture)
{
    const std::vector<Viewport> vps = CalcViewports(Extent(800, 600), 2);
    GameWorldView left(viewer0, vps[0].origin, vps[0].size);
    GameWorldView right(viewer1, vps[1].origin, vps[1].size);

    const MapPoint hq0 = world.GetPlayer(0).GetHQPos();
    const MapPoint hq1 = world.GetPlayer(1).GetHQPos();
    left.MoveToMapPt(hq0);
    right.MoveToMapPt(hq1);

    // Orakel: der Bildschirmpunkt, an dem das eigene HQ liegt
    const Position screenPtOfHq1 = world.GetNodePos(hq1) - right.GetOffset() + right.GetPos();
    right.SetCursorPos(screenPtOfHq1);
    right.UpdateSelection();
    BOOST_TEST((right.GetSelectedPt() == hq1));

    const Position screenPtOfHq0 = world.GetNodePos(hq0) - left.GetOffset() + left.GetPos();
    left.SetCursorPos(screenPtOfHq0);
    left.UpdateSelection();
    BOOST_TEST((left.GetSelectedPt() == hq0));

    // N3: die globale Maus darf daran nichts mehr aendern.
    const MapPoint selectedBefore = right.GetSelectedPt();
    for(const Position p : {Position(0, 0), Position(1, 1), Position(399, 299), Position(799, 599)})
    {
        // Bewusst der Mockup-Treiber direkt: der Wrapper schluckt SetMousePos, wenn
        // enableMouseWarping aus ist (drivers/VideoDriverWrapper.cpp:425).
        uiHelper::GetVideoDriver()->SetMousePos(p);
        right.UpdateSelection();
        BOOST_TEST_INFO("global mouse at " << p.x << "," << p.y);
        BOOST_TEST((right.GetSelectedPt() == selectedBefore));
    }

    // Derselbe Bildschirmpunkt, zwei Ansichten, zwei verschiedene Kartenpunkte.
    left.SetCursorPos(Position(410, 150));
    right.SetCursorPos(Position(410, 150));
    left.UpdateSelection();
    right.UpdateSelection();
    BOOST_TEST((left.GetSelectedPt() != right.GetSelectedPt()));

    // N4: ohne Zeiger gibt es keinen selektierten Punkt - kein stiller Rueckfall auf die Maus.
    right.SetCursorPos(std::nullopt);
    right.UpdateSelection();
    BOOST_TEST((right.GetSelectedPt() == MapPoint::Invalid()));
}

/// N9, die Regressionsklammer: bei genau EINEM Vollbild-Viewport liefert die Naht exakt das,
/// was die alte, globale Abfrage geliefert haette.
BOOST_FIXTURE_TEST_CASE(SingleViewportBehavesExactlyAsBefore, ViewportFixture)
{
    const std::vector<Viewport> vps = CalcViewports(VIDEODRIVER.GetRenderSize(), 1);
    BOOST_TEST_REQUIRE(vps.size() == 1u);
    GameWorldView view(viewer0, vps[0].origin, vps[0].size);
    view.MoveToMapPt(world.GetPlayer(0).GetHQPos());

    for(const Position mousePt : {Position(10, 10), Position(400, 300), Position(700, 500)})
    {
        uiHelper::GetVideoDriver()->SetMousePos(mousePt);
        // Genau das macht dskGameInterface::Run vor jedem gwv.Draw
        view.SetCursorPos(VIDEODRIVER.GetMousePos());
        view.UpdateSelection();
        const MapPoint selected = view.GetSelectedPt();
        BOOST_TEST_REQUIRE(selected.isValid());
        // Gegenprobe ueber den unabhaengigen Vorwaertsweg: der ausgewaehlte Knoten liegt naeher
        // am Zeiger als jeder seiner sechs Nachbarn.
        const Position selPos = Position(world.GetNodePos(selected)) - view.GetOffset();
        const auto dist2 = [&](const Position& p) {
            const Position d = mousePt - p;
            return static_cast<long long>(d.x) * d.x + static_cast<long long>(d.y) * d.y;
        };
        for(const auto dir : helpers::EnumRange<Direction>{})
        {
            const MapPoint neighbour = world.GetNeighbour(selected, dir);
            const Position nbPos = Position(world.GetNodePos(neighbour)) - view.GetOffset();
            // Nur Nachbarn vergleichen, die nicht ueber den Kartenrand gewrappt sind
            if(std::abs(nbPos.x - selPos.x) > 4 * TR_W || std::abs(nbPos.y - selPos.y) > 4 * TR_H)
                continue;
            BOOST_TEST_INFO("neighbour dir " << static_cast<unsigned>(dir));
            BOOST_TEST(dist2(selPos) <= dist2(nbPos));
        }
    }
}

// --------------------------------------------------------------------------------------------
// 4. Die Begrenzung jeder Ansicht
// --------------------------------------------------------------------------------------------

/// N5: die glScissor-Rechtecke der Ansichten kacheln die Renderflaeche ueberlappungsfrei.
/// Geprueft wird das Rechteck, nicht seine Wirkung: im Testprozess ist glScissor ein Nullzeiger
/// (ogl/DummyRenderer.cpp:36-51 mockt es nicht), das tatsaechliche Klippen sieht man nur am
/// Bildschirm.
BOOST_FIXTURE_TEST_CASE(ScissorRectsTileTheScreenWithoutOverlap, ViewportFixture)
{
    for(const unsigned guiScalePercent : {100u, 150u})
    {
        BOOST_TEST_CONTEXT("guiScale " << guiScalePercent)
        {
            VIDEODRIVER.setGuiScalePercent(guiScalePercent);
            const Extent renderSize = VIDEODRIVER.GetRenderSize();
            const auto windowSize = VIDEODRIVER.GetWindowSize();
            const std::vector<Viewport> vps = CalcViewports(renderSize, 4);
            BOOST_TEST_REQUIRE(vps.size() == 4u);

            std::vector<Rect> rects;
            for(const Viewport& vp : vps)
            {
                GameWorldView view(viewer0, vp.origin, vp.size);
                rects.push_back(view.GetScissorRect());
            }

            long long coveredArea = 0;
            for(unsigned i = 0; i < rects.size(); ++i)
            {
                BOOST_TEST(rects[i].left >= 0);
                BOOST_TEST(rects[i].top >= 0);
                BOOST_TEST(rects[i].right <= static_cast<int>(windowSize.width));
                BOOST_TEST(rects[i].bottom <= static_cast<int>(windowSize.height));
                coveredArea += static_cast<long long>(rects[i].getSize().x) * rects[i].getSize().y;
                for(unsigned j = i + 1; j < rects.size(); ++j)
                {
                    BOOST_TEST_INFO("rects " << i << " and " << j);
                    BOOST_TEST(!overlaps(rects[i], rects[j]));
                }
            }
            // Zusammen decken sie die Fensterflaeche ab (bis auf Rundung an den Teilungskanten)
            const auto fullArea = static_cast<long long>(windowSize.width) * windowSize.height;
            BOOST_TEST(coveredArea > fullArea * 9 / 10);
            BOOST_TEST(coveredArea <= fullArea);
        }
    }
    VIDEODRIVER.setGuiScalePercent(100);
}

/// P2: der updateEffectiveZoomFactor()-Aufruf in GameWorldView::Resize (GameWorldView.cpp:787)
/// wirkt nachweisbar - bisher konnte kein Test sein Fehlen von seinem Vorhandensein
/// unterscheiden.
///
/// Der Trick ist ein Resize auf die GLEICHE Groesse: dann aendert sich ausschliesslich die
/// GuiScale, an der effectiveZoomFactor_ haengt (updateEffectiveZoomFactor,
/// GameWorldView.cpp:799-803). Ohne den Aufruf bliebe der Faktor auf dem alten Wert stehen, und
/// beide Zusicherungen unten fielen um.
///
/// ENTSCHEIDUNG zu P2: der Aufruf bleibt. Er ist nicht nur begruendet (Kommentar
/// GameWorldView.cpp:782-786), sondern messbar wirksam. Der Zustand "nicht unterscheidbar" war
/// eine Luecke im Test, nicht im Code.
BOOST_FIXTURE_TEST_CASE(ResizePicksUpAChangedGuiScale, ViewportFixture)
{
    VIDEODRIVER.setGuiScalePercent(100);
    GameWorldView view(viewer0, Position(0, 0), Extent(800, 600));
    view.MoveToMapPt(world.GetPlayer(0).GetHQPos());
    // Bewusst NICHT die Bildmitte: die ist unter jeder Zoomstufe ihr eigener Fixpunkt
    // (GameWorldView.cpp:111-120), dort wuerde sich nie etwas aendern.
    const Position mapPtBefore = view.ViewPosToMap(Position(100, 100));
    const Position lastBefore = view.GetLastPt();

    VIDEODRIVER.setGuiScalePercent(150);
    view.Resize(Extent(800, 600)); // GLEICHE Groesse - nur die GuiScale hat sich geaendert

    // ViewPosToMap rechnet ueber effectiveZoomFactor_ (GameWorldView.cpp:111-120)
    BOOST_TEST((view.ViewPosToMap(Position(100, 100)) != mapPtBefore));
    // ... und CalcFxLx ebenfalls (GameWorldView.cpp:762-773)
    BOOST_TEST((view.GetLastPt() != lastBefore));

    // Gegenprobe: zurueck auf 100% liefert wieder exakt die Ausgangswerte. Ohne den Aufruf
    // waere auch das nicht der Fall - der Faktor haenge dann irgendwo fest.
    VIDEODRIVER.setGuiScalePercent(100);
    view.Resize(Extent(800, 600));
    BOOST_TEST((view.ViewPosToMap(Position(100, 100)) == mapPtBefore));
    BOOST_TEST((view.GetLastPt() == lastBefore));
}

/// Der Vollbild-Viewport klippt die ganze Renderflaeche - Einzelspieler unveraendert.
BOOST_FIXTURE_TEST_CASE(FullscreenViewScissorsTheWholeWindow, ViewportFixture)
{
    GameWorldView view(viewer0, Position(0, 0), VIDEODRIVER.GetRenderSize());
    const Rect scissor = view.GetScissorRect();
    const auto windowSize = VIDEODRIVER.GetWindowSize();
    BOOST_TEST(scissor.left == 0);
    BOOST_TEST(scissor.top == 0);
    BOOST_TEST(scissor.getSize().x == windowSize.width);
    BOOST_TEST(scissor.getSize().y == windowSize.height);
}

BOOST_AUTO_TEST_SUITE_END()
