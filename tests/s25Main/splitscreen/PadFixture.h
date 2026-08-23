// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "GamePlayer.h"
#include "Loader.h"
#include "buildings/nobBaseWarehouse.h"
#include "desktops/PlayerView.h"
#include "desktops/dskGameInterface.h"
#include "driver/PadEvent.h"
#include "drivers/VideoDriverWrapper.h"
#include "network/GameClient.h"
#include "nodeObjs/noFlag.h"
#include "input/PadRouter.h"
#include "pathfinding/FindPathForRoad.h"
#include "world/GameWorld.h"
#include "world/GameWorldViewer.h"
#include "mockupDrivers/MockupVideoDriver.h"
#include "uiHelper/uiHelpers.hpp"
#include "worldFixtures/CreateEmptyWorld.h"
#include "worldFixtures/WorldFixture.h"
#include "gameTypes/BuildingQuality.h"
#include "gameTypes/Direction.h"
#include "gameTypes/RoadBuildState.h"
#include "gameData/MapConsts.h"
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace rttr::test {

/// Die Knopfbelegung des Strassenbaus, an GENAU EINER Stelle. Jeder Nachweis unten nennt diese
/// Konstanten statt eines PadButton-Literals: eine Umbelegung kostet damit eine Zeile und nicht
/// eine Durchsicht aller Faelle. Die Belegung selbst wird in dskGameInterface::OnPadButton
/// begruendet.
namespace padRoad {
    /// A ausserhalb des Baumodus: auf der eigenen Flagge unter dem Zeiger anfangen.
    constexpr PadButton Begin = PadButton::A;
    /// A im Baumodus: bis zum Zeiger verlaengern. Erzeugt NIE ein GameCommand.
    constexpr PadButton Extend = PadButton::A;
    /// Schulter links: Wasserweg anfangen (nur an einer eigenen Wasserflagge).
    constexpr PadButton BeginWater = PadButton::LeftShoulder;
    /// X im Baumodus: festschreiben. Der einzige Knopf, der hier ein Kommando erzeugt.
    constexpr PadButton Commit = PadButton::X;
    /// B im Baumodus: ein Stueck zurueck; auf leerer Strecke Abbruch.
    constexpr PadButton StepBack = PadButton::B;
} // namespace padRoad

/// Die Knotenfolge einer Route, Startpunkt eingeschlossen.
inline std::vector<MapPoint> roadPoints(const GameWorldBase& world, MapPoint start,
                                        const std::vector<Direction>& route)
{
    std::vector<MapPoint> pts;
    pts.push_back(start);
    MapPoint cur = start;
    for(const Direction dir : route)
    {
        cur = world.GetNeighbour(cur, dir);
        pts.push_back(cur);
    }
    return pts;
}

/// Steht diese Strasse WIRKLICH im Spielzustand? Gemessen an World::GetPointRoad, also an der
/// Simulation - und nicht am Viewer, der auch die blosse Vorschau fuehrt.
inline bool worldHasRoad(const GameWorldBase& world, MapPoint start, const std::vector<Direction>& route)
{
    MapPoint cur = start;
    for(const Direction dir : route)
    {
        if(world.GetPointRoad(cur, dir) == PointRoad::None)
            return false;
        cur = world.GetNeighbour(cur, dir);
        // Gegenrichtung: eine Strasse ist in beiden Richtungen begehbar.
        if(world.GetPointRoad(cur, dir + 3u) == PointRoad::None)
            return false;
    }
    return true;
}

/// Zeichnet dieser Viewer JEDE Kante dieser Route?
///
/// Gemessen wird an GameWorldViewer::GetVisiblePointRoad - und das ist bewusst die Sicht des
/// Spielers und nicht die der Simulation: der Viewer legt die visuelle Vorschau UEBER die
/// wirkliche Strasse (GameWorldViewer::GetVisibleRoad, erst visualNodes, dann die Welt). Wer
/// wissen will, ob eine Strasse WIRKLICH steht, fragt worldHasRoad.
///
/// Bewusst kantenweise und nicht ueber IsOnRoad: jedes Gebaeude ist mit seiner Flagge durch ein
/// Wegstueck verbunden, eine HQ-Flagge liegt also immer "auf einer Strasse". Eine Pruefung ueber
/// IsOnRoad waere an jedem Startpunkt, der eine Gebaeudeflagge ist, blind wahr.
inline bool viewerDrawsRoad(const GameWorldViewer& viewer, MapPoint start,
                            const std::vector<Direction>& route)
{
    if(route.empty())
        return false;
    MapPoint cur = start;
    for(const Direction dir : route)
    {
        if(viewer.GetVisiblePointRoad(cur, dir) == PointRoad::None)
            return false;
        cur = viewer.GetWorld().GetNeighbour(cur, dir);
    }
    return true;
}

/// Zeichnet dieser Viewer auch nur EINE Kante dieser Route?
///
/// Der Geisterstrassen-Detektor - anwendbar ueberall dort, wo die Strasse in der WELT nicht
/// steht (Abbruch, Rueckbau, abgezogenes Pad, zerstoerte Flagge, abgelehntes Kommando). Dann
/// und nur dann kann das, was der Viewer noch zeichnet, ausschliesslich die stehengebliebene
/// Vorschau sein: GameWorldViewer::RoadConstructionEnded raeumt sie nur ab, wenn die RoadNote
/// den Spieler DIESES Viewers nennt.
inline bool viewerDrawsAnyOf(const GameWorldViewer& viewer, MapPoint start,
                             const std::vector<Direction>& route)
{
    MapPoint cur = start;
    for(const Direction dir : route)
    {
        if(viewer.GetVisiblePointRoad(cur, dir) != PointRoad::None)
            return true;
        cur = viewer.GetWorld().GetNeighbour(cur, dir);
    }
    return false;
}

/// Eine eigene Startflagge und ein Zielknoten, zwischen denen ein Landweg von mindestens zwei
/// Kanten moeglich ist.
///
/// Warum mindestens zwei: GameWorld::BuildRoad lehnt kuerzere Routen hart ab
/// (world/GameWorld.cpp:189-195, im Debugbau ein RTTR_Assert). Ein Nachweis, der eine
/// Ein-Kanten-Route baute, pruefte also nur den Ablehnungszweig.
struct RoadSpot
{
    MapPoint start = MapPoint::Invalid();
    MapPoint end = MapPoint::Invalid();
    std::vector<Direction> route;

    bool isValid() const { return start.isValid() && end.isValid() && route.size() >= 2; }
};

/// Sucht ein solches Paar, ausgehend von der HQ-Flagge DIESES Spielers.
///
/// Die HQ-Flagge ist der einzige Punkt, von dem sicher feststeht, dass er eine Flagge IST und
/// diesem Spieler gehoert - genau die beiden Bedingungen, die dskGameInterface::PadStartRoad
/// prueft. Der Weg wird mit DEMSELBEN Produktivaufruf berechnet, den BuildRoadPart benutzt
/// (FindPathForRoad auf dem Viewer dieses Spielers), also mit derselben Wegbedingung
/// einschliesslich Gebietsgrenze.
inline RoadSpot findRoadSpotFromHQ(const GameWorldViewer& viewer, const unsigned minLen = 2,
                                   const unsigned maxLen = 5)
{
    const GameWorldBase& world = viewer.GetWorld();
    const auto player = static_cast<unsigned char>(viewer.GetPlayerId());
    const MapPoint hqPos = world.GetPlayer(player).GetHQPos();
    if(!hqPos.isValid())
        return {};
    const auto* hq = world.GetSpecObj<nobBaseWarehouse>(hqPos);
    if(!hq)
        return {};
    RoadSpot spot;
    spot.start = hq->GetFlagPos();
    for(const MapPoint& pt : world.GetPointsInRadiusWithCenter(spot.start, maxLen + 2))
    {
        if(pt == spot.start)
            continue;
        if(world.GetNode(pt).obj)
            continue; // dort steht schon etwas
        if(world.IsFlagAround(pt))
            continue; // dort kann am Ende keine Flagge entstehen
        if(world.GetBQ(pt, player) == BuildingQuality::Nothing)
            continue;
        std::vector<Direction> route = FindPathForRoad(viewer, spot.start, pt, false, 100);
        if(route.size() < minLen || route.size() > maxLen)
            continue;
        spot.end = pt;
        spot.route = std::move(route);
        return spot;
    }
    return {};
}

/// Speist Gamepad-Ereignisse ein, ohne SDL und ohne Hardware.
///
/// Geschrieben wird in die Warteschlange des MockupVideoDriver; der Produktivcode holt sie mit
/// GENAU DEM AUFRUF ab, mit dem er sie auch vom SDL2-Treiber holt
/// (dskGameInterface::UpdateInput -> IVideoDriver::FetchPadEvents). Die Naht liegt damit so tief
/// wie moeglich: alles oberhalb von ihr ist gepruefter Produktivcode, nicht Testattrappe.
struct PadFeeder
{
    MockupVideoDriver& video;

    explicit PadFeeder(MockupVideoDriver& v) : video(v) { video.padEvents_.clear(); }

    void connect(PadDeviceId dev) { video.padEvents_.push_back(PadEvent::Connected(dev)); }
    void disconnect(PadDeviceId dev) { video.padEvents_.push_back(PadEvent::Disconnected(dev)); }
    void axis(PadDeviceId dev, PadAxis a, float v) { video.padEvents_.push_back(PadEvent::Axis(dev, a, v)); }
    void button(PadDeviceId dev, PadButton b, bool down)
    {
        video.padEvents_.push_back(PadEvent::Button(dev, b, down));
    }
    /// Kompletter Tastendruck: Flanke runter und wieder hoch.
    void tap(PadDeviceId dev, PadButton b)
    {
        button(dev, b, true);
        button(dev, b, false);
    }
    /// "Der Spieler nimmt das Pad in die Hand." Seit der Uebernahme durch Benutzung
    /// (input/PadRouter.h) reicht das blosse Anstecken nicht mehr, um eine Ansicht zu
    /// bekommen. Start ist dafuer bewusst gewaehlt: dskGameInterface::OnPadButton laesst den
    /// Knopf wirkungslos, die Uebernahme ist also der EINZIGE beobachtbare Effekt.
    void pickUp(PadDeviceId dev)
    {
        connect(dev);
        tap(dev, PadButton::Start);
    }
};

/// Wo liegt dieser Kartenknoten GERADE auf dem Bildschirm, in View-Koordinaten?
///
/// Dieselbe Umrechnung, die GameWorldView::UpdateSelection rueckwaerts benutzt
/// (world.GetNodePos(pt) - offset, danach die Zoomkorrektur ueber MapPosToView). Der
/// Kartenumbruch wird beruecksichtigt: die Darstellung wiederholt sich alle GetWidth()*TR_W
/// Pixel, genommen wird die Wiederholung, die dem Viewport am naechsten liegt - genau die
/// zeichnet Draw() auch.
inline Position padNodeViewPos(const GameWorldBase& world, const GameWorldView& v, const MapPoint pt)
{
    Position d = Position(world.GetNodePos(pt)) - Position(v.GetOffset());
    const Position span(static_cast<int>(world.GetWidth()) * TR_W, static_cast<int>(world.GetHeight()) * TR_H);
    const Position center(static_cast<int>(v.GetSize().x) / 2, static_cast<int>(v.GetSize().y) / 2);
    const auto nearest = [](int value, const int period, const int target) {
        while(value - target > period / 2)
            value -= period;
        while(target - value > period / 2)
            value += period;
        return value;
    };
    d.x = nearest(d.x, span.x, center.x);
    d.y = nearest(d.y, span.y, center.y);
    return v.MapPosToView(d);
}

/// Das Pad, das GERADE diese Ansicht steuert - gefragt wird der Router selbst und nicht eine
/// Buchhaltung des Tests. InvalidPadDevice, wenn keins zugeordnet ist.
inline PadDeviceId padOfView(const dskGameInterface& dsk, const unsigned viewIdx)
{
    const PadRouter& router = dsk.GetPadRouter();
    for(const PadDeviceId dev : router.GetDevices())
    {
        if(router.GetSlot(dev) == viewIdx)
            return dev;
    }
    return InvalidPadDevice;
}

/// Steuert die Ansicht `viewIdx` NUR MIT PADEREIGNISSEN auf den Knoten `pt`.
///
/// Erst faehrt der RECHTE Stick die Kamera, bis der Knoten bequem im eigenen Viewport liegt,
/// dann faehrt der LINKE Stick den Zeiger auf ihn - genau das, was ein Spieler mit dem Pad in
/// der Hand tut. Kein MoveToMapPt, kein MoveBy, kein SetCursorPos: jeder Pixel entsteht in
/// PadRouter::UpdateMotion aus einem PadEvent, das der Produktivcode selbst beim Treiber
/// abgeholt hat.
///
/// GENAU DARAN sind die Padtests der letzten Runde gescheitert: ihre Hilfsfunktion verschob die
/// Ansicht mit GameWorldView::MoveToMapPt - einem Aufruf, den kein Padknopf ausloest. Die Tests
/// waren gruen, obwohl der Spieler den Zielpunkt nie haette erreichen koennen.
///
/// Dosiert wird ueber die ZEIT und nicht ueber den Ausschlag: der Stick steht immer auf
/// Vollausschlag in Zielrichtung, und der Frame ist so lang, wie die Reststrecke es verlangt.
/// Sonst schluckte die radiale Totzone (PadRouter::Deadzone) die kleinen Schlussschritte.
///
/// `step(ms)` ist der Frame des jeweiligen Fixtures - der Aufruf von
/// dskGameInterface::UpdateInput.
template<class T_Step>
void padSteerTo(PadFeeder& pads, dskGameInterface& dsk, const GameWorldBase& world, const PadDeviceId dev,
                const unsigned viewIdx, const MapPoint pt, T_Step step)
{
    PlayerView& view = dsk.GetPlayerView(viewIdx);
    GameWorldView& gameView = view.GetView();
    BOOST_TEST_REQUIRE(view.HasPadCursor());
    // Steht dieser Spieler in einem Fenster, gehoert der LINKE Stick dem Fokus
    // (dskGameInterface::OnPadMove -> FocusPath::OnPadMove) und der Weltzeiger bewegt sich
    // ueberhaupt nicht mehr. Zielen ist dann kein Nachweis, sondern eine Endlosschleife -
    // deshalb sagt es die Zusicherung hier, wo sie erklaerbar ist, und nicht die
    // Konvergenzpruefung unten.
    BOOST_TEST_REQUIRE(!view.GetFocus().IsActive());
    const auto lenOf = [](const Position& p) {
        return std::sqrt(static_cast<float>(p.x) * p.x + static_cast<float>(p.y) * p.y);
    };
    for(unsigned i = 0; i < 400u; ++i)
    {
        if(gameView.GetSelectedPt() == pt)
            break;
        const Position target = padNodeViewPos(world, gameView, pt);
        const Position origin = gameView.GetPos();
        const Extent size = gameView.GetSize();
        // 30 Prozent Rand - bewusst MEHR als die 20 Prozent, ab denen der Randschub einsetzt
        // (dskGameInterface::PushCameraAtEdge). Der Zeigerweg von der Viewportmitte zu einem
        // Punkt innerhalb dieses Kastens bleibt damit vollstaendig im Innenrahmen, und die
        // Feinsteuerung faehrt nicht gegen eine gleichzeitig mitlaufende Kamera an.
        const Position margin(static_cast<int>(size.x) * 30 / 100, static_cast<int>(size.y) * 30 / 100);
        const bool inside = target.x >= origin.x + margin.x && target.y >= origin.y + margin.y
                            && target.x < origin.x + static_cast<int>(size.x) - margin.x
                            && target.y < origin.y + static_cast<int>(size.y) - margin.y;
        const Position err = inside ? target - view.GetPadCursor() : target - view.GetViewCenter();
        const float len = lenOf(err);
        if(len < 1.f)
        {
            step(16u);
            continue;
        }
        const float speed = inside ? PadRouter::PixelsPerSecond : PadRouter::CameraPixelsPerSecond;
        const PointF dir(err.x / len, err.y / len);
        // Alle vier Achsen jeden Schritt neu setzen: der Router fuehrt den Achsenzustand, ein
        // stehengelassener Vollausschlag fuehre sonst weiter.
        pads.axis(dev, PadAxis::LeftX, inside ? dir.x : 0.f);
        pads.axis(dev, PadAxis::LeftY, inside ? dir.y : 0.f);
        pads.axis(dev, PadAxis::RightX, inside ? 0.f : dir.x);
        pads.axis(dev, PadAxis::RightY, inside ? 0.f : dir.y);
        step(std::clamp(static_cast<unsigned>(std::lround(len / speed * 1000.f)), 1u, 32u));
    }
    // Sticks in die Ruhelage, sonst faehrt der naechste Frame weiter.
    for(const PadAxis a : {PadAxis::LeftX, PadAxis::LeftY, PadAxis::RightX, PadAxis::RightY})
        pads.axis(dev, a, 0.f);
    step(16u);
    BOOST_TEST_REQUIRE((gameView.GetSelectedPt() == pt));
}

/// Legt die im Produktivcode geschuetzten Eingabepfade offen.
///
/// Msg_PaintBefore/-After sind bewusst leer: sie rufen Run(), und Run() zeichnet ueber
/// GameWorldView::Draw -> TerrainRenderer::Draw, das ohne geladene S2-Texturen nicht arbeitet.
/// Genau deshalb liegt die gesamte Eingabelogik in dskGameInterface::UpdateInput, das oeffentlich
/// und GL-frei ist (das ist die Erledigung von P1).
struct TestableGameInterface : dskGameInterface
{
    using dskGameInterface::dskGameInterface;

    void Msg_PaintBefore() override {}
    void Msg_PaintAfter() override {}

    using dskGameInterface::ContextClick;
    using dskGameInterface::Msg_KeyDown;
    using dskGameInterface::Msg_LeftDown;
    using dskGameInterface::Msg_LeftUp;
    using dskGameInterface::Msg_MouseMove;
    using dskGameInterface::Msg_RightDown;
    using dskGameInterface::Msg_RightUp;
    using dskGameInterface::Msg_WheelDown;
    using dskGameInterface::Msg_WheelUp;
    using dskGameInterface::Msg_WindowClosed;
    using dskGameInterface::PadPlaceFlag;
};

/// Ein echtes dskGameInterface mit N Ansichten auf einer Welt OHNE laufende Partie.
///
/// Bewusst nicht LocalGameFixture: fuer die Zeigerzuordnung braucht es weder Server noch Client
/// noch NWF. Was hier geprueft wird - Slotvergabe, Achsenintegration, Klemme, Trennung der
/// Ansichten - ist damit in Millisekunden und ohne Netz nachweisbar. Der Nachweis, dass eine
/// Padaktion einen GameCommand fuer den RICHTIGEN Spieler erzeugt, braucht die echte Partie und
/// steht deshalb in testPadCommands.cpp.
///
/// Die Karte ist voreingestellt eine leere Landkarte; T_WorldCreator und die beiden Masse sind
/// nur dort zu setzen, wo eine andere gebraucht wird - der Wasserweg braucht Wasser
/// (CreateWaterWorld).
template<unsigned T_numViews, unsigned T_numPlayers = T_numViews, class T_WorldCreator = CreateEmptyWorld,
         unsigned T_width = 60, unsigned T_height = 30>
struct PadViewFixture : uiHelper::Fixture
{
    static_assert(T_numViews >= 1 && T_numViews <= 4, "MAX_VIEWPORTS");
    static_assert(T_numPlayers >= T_numViews, "Jede Ansicht braucht einen Spieler");

    WorldFixture<T_WorldCreator, T_numPlayers, T_width, T_height> worldFixture;
    std::vector<uint8_t> oldAdditional_;
    std::unique_ptr<TestableGameInterface> dsk;
    PadFeeder pads;

    PadViewFixture() : pads(*uiHelper::GetVideoDriver())
    {
        // Ohne die Dummy-Kartengrafiken ist Loader::map_gfx ein Nullzeiger, und JEDES
        // Gebaeudefenster stuerzt schon im Konstruktor ab (Loader::GetMapTexture). Genau diese
        // Fenster sind das Ziel des Padpfades, also muessen sie hier baubar sein.
        // Idempotent: LoadDummyMapFiles kehrt sofort zurueck, wenn das Archiv schon steht.
        LOADER.LoadDummyMapFiles();
        // dskGameInterface::CreateViews liest genau diese Liste (dskGameInterface.cpp:133). Ein
        // laufender Client ist dafuer nicht noetig - SetAdditionalLocalPlayers ist ein reiner
        // Setter (network/GameClient.cpp:1858-1861).
        oldAdditional_ = GAMECLIENT.GetAdditionalLocalPlayers();
        std::vector<uint8_t> additional;
        for(uint8_t i = 1; i < T_numViews; ++i)
            additional.push_back(i);
        GAMECLIENT.SetAdditionalLocalPlayers(additional);

        dsk = std::make_unique<TestableGameInterface>(worldFixture.game, std::shared_ptr<const NWFInfo>(), 0u,
                                                      /*initOGL*/ false);
        BOOST_TEST_REQUIRE(dsk->GetNumViews() == T_numViews);
    }

    ~PadViewFixture()
    {
        dsk.reset();
        // Der Desktop hat sich in der Welt eingetragen (dskGameInterface.cpp:184), der Destruktor
        // traegt sich nicht aus.
        worldFixture.world.SetGameInterface(nullptr);
        GAMECLIENT.SetAdditionalLocalPlayers(oldAdditional_);
        uiHelper::GetVideoDriver()->padEvents_.clear();
    }

    /// Wirft den Desktop weg und baut ihn neu auf - der Moment, in dem eine Partie beginnt.
    ///
    /// Gebraucht fuer alles, was VOR der Partie passiert: Ereignisse, die im Hauptmenue, in der
    /// Lobby oder im Ladebildschirm in der Warteschlange des Treibers aufgelaufen sind, liegen
    /// dort schon, bevor es ein dskGameInterface gibt. Der Aufruf ist bewusst der EINZIGE Weg
    /// dorthin: die Warteschlange wird dabei nicht angefasst.
    void restartDesktop()
    {
        dsk.reset();
        worldFixture.world.SetGameInterface(nullptr);
        dsk = std::make_unique<TestableGameInterface>(worldFixture.game, std::shared_ptr<const NWFInfo>(), 0u,
                                                      /*initOGL*/ false);
        BOOST_TEST_REQUIRE(dsk->GetNumViews() == T_numViews);
    }

    PlayerView& view(unsigned idx) { return dsk->GetPlayerView(idx); }
    GameWorldView& gwv(unsigned idx) { return dsk->GetPlayerView(idx).GetView(); }

    /// Ein Frame Eingabe. Die Maus liegt bewusst ausserhalb jeder Ansicht, wo sie nichts
    /// beitragen kann, solange nichts anderes gesagt wird.
    void step(unsigned elapsedMs, const Position& mousePos = Position(-10000, -10000))
    {
        dsk->UpdateInput(elapsedMs, mousePos);
    }

    /// Nimmt Pad `dev` in die Hand und steuert seinen Zeiger ueber den PADPFAD auf `pt`.
    void aimPadAt(const PadDeviceId dev, const unsigned viewIdx, const MapPoint pt)
    {
        pads.pickUp(dev);
        step(0);
        BOOST_TEST_REQUIRE(view(viewIdx).HasPadCursor());
        padSteerTo(dev, viewIdx, pt);
    }

    /// Richtet einen BEREITS zugeordneten Zeiger auf einen anderen Knoten aus - ebenfalls
    /// ausschliesslich mit Padereignissen. Welches Pad diese Ansicht steuert, sagt der Router.
    void aimAt(const unsigned viewIdx, const MapPoint pt)
    {
        const PadDeviceId dev = padOfView(*dsk, viewIdx);
        BOOST_TEST_REQUIRE(dev != InvalidPadDevice);
        padSteerTo(dev, viewIdx, pt);
    }

    void padSteerTo(const PadDeviceId dev, const unsigned viewIdx, const MapPoint pt)
    {
        rttr::test::padSteerTo(pads, *dsk, worldFixture.world, dev, viewIdx, pt,
                               [this](unsigned ms) { step(ms); });
    }

    Position nodeViewPos(const unsigned viewIdx, const MapPoint pt)
    {
        return padNodeViewPos(worldFixture.world, gwv(viewIdx), pt);
    }

    void releaseSticks(const PadDeviceId dev)
    {
        for(const PadAxis a : {PadAxis::LeftX, PadAxis::LeftY, PadAxis::RightX, PadAxis::RightY})
            pads.axis(dev, a, 0.f);
        step(16);
    }

    /// Ein vollstaendiger Knopfdruck (Flanke runter und hoch) plus der Frame, in dem er
    /// ausgeliefert wird. Genau der Weg, den auch der SDL2-Treiber nimmt.
    void press(const PadDeviceId dev, const PadButton button)
    {
        pads.tap(dev, button);
        step(16);
    }

};

} // namespace rttr::test
