// SPDX-License-Identifier: GPL-2.0-or-later
//
// Messwerkzeug fuer Phase 2: Was kosten vier GameWorldViewer mit je eigenem TerrainRenderer?
// Es wird nichts geschaetzt: Puffergroessen werden aus den echten Datentypen berechnet und
// gegen den tatsaechlichen Prozessspeicher gegengeprueft, Laufzeiten werden gemessen.

#include "Game.h"
#include "EventManager.h"
#include "GamePlayer.h"
#include "Replay.h"
#include "Savegame.h"
#include "notifications/NotificationManager.h"
#include "notifications/NodeNote.h"
#include "notifications/PlayerNodeNote.h"
#include "network/PlayerGameCommands.h"
#include "random/Random.h"
#include "variant.h"
#include "gameTypes/MapInfo.h"
#include "s25util/tmpFile.h"
#include "Loader.h"
#include "PlayerInfo.h"
#include "Point.h"
#include "RttrForeachPt.h"
#include "Settings.h"
#include "TerrainRenderer.h"
#include "helpers/containerUtils.h"
#include "ogl/glAllocator.h"
#include "world/GameWorld.h"
#include "world/GameWorldView.h"
#include "world/GameWorldViewer.h"
#include "world/MapLoader.h"
#include "gameData/DescIdx.h"
#include "gameData/EdgeDesc.h"
#include "gameData/LandscapeDesc.h"
#include "gameData/MapConsts.h"
#include "gameData/TerrainDesc.h"
#include "libsiedler2/Archiv.h"
#include "libsiedler2/ArchivItem_Map.h"
#include "libsiedler2/ArchivItem_Map_Header.h"
#include "libsiedler2/libsiedler2.h"
#include "libsiedler2/prototypen.h"
#include <boost/test/unit_test.hpp>
#include <uiHelper/uiHelpers.hpp>
#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <test/testConfig.h>
#include <vector>

#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

namespace {
// Exakte Nachbildung der privaten Layouts aus TerrainRenderer.h (Zeilen 88-133),
// uebersetzt mit demselben Compiler und denselben Flags wie das Original.
struct Vertex_
{
    PointF pos;
    float color;
    std::array<PointF, 2> borderPos;
    std::array<float, 2> borderColor;
};
struct Color_
{
    float r, g, b;
};
using Triangle_ = std::array<PointF, 3>;
using ColorTriangle_ = std::array<Color_, 3>;
struct Borders_
{
    std::array<DescIdx<EdgeDesc>, 2> left_right;
    std::array<DescIdx<EdgeDesc>, 2> right_left;
    std::array<DescIdx<EdgeDesc>, 2> top_down;
    std::array<unsigned, 2> left_right_offset;
    std::array<unsigned, 2> right_left_offset;
    std::array<unsigned, 2> top_down_offset;
};

size_t privateBytes()
{
    PROCESS_MEMORY_COUNTERS_EX pmc;
    ZeroMemory(&pmc, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));
    return pmc.PrivateUsage;
}

DescIdx<EdgeDesc> getEdgeType(const TerrainDesc& t1, const TerrainDesc& t2)
{
    if(!t1.edgeType || t1.edgePriority <= t2.edgePriority)
        return DescIdx<EdgeDesc>();
    return t1.edgeType;
}

/// Laedt genau die Terrain-, Rand- und Strassentexturen, die TerrainRenderer::LoadTextures
/// (TerrainRenderer.cpp:93-200) spaeter ueber LOADER.GetImageN erwartet.
/// Gleiche Auswahl wie GameLoader::initTextures (gameData/GameLoader.cpp:29-67).
bool loadTerrainTextures(const GameWorld& world)
{
    const WorldDescription& desc = world.GetDescription();
    std::set<DescIdx<TerrainDesc>> usedTerrains;
    RTTR_FOREACH_PT(MapPoint, world.GetSize())
    {
        usedTerrains.insert(world.GetNode(pt).t1);
        usedTerrains.insert(world.GetNode(pt).t2);
    }
    std::vector<std::string> textures;
    std::set<DescIdx<EdgeDesc>> usedEdges;
    std::set<DescIdx<LandscapeDesc>> usedLandscapes;
    for(DescIdx<TerrainDesc> tIdx : usedTerrains)
    {
        const TerrainDesc& t = desc.get(tIdx);
        if(!helpers::contains(textures, t.texturePath))
            textures.push_back(t.texturePath);
        usedEdges.insert(t.edgeType);
        usedLandscapes.insert(t.landscape);
    }
    for(DescIdx<EdgeDesc> eIdx : usedEdges)
    {
        if(!eIdx)
            continue;
        const std::string& p = desc.get(eIdx).texturePath;
        if(!helpers::contains(textures, p))
            textures.push_back(p);
    }
    for(DescIdx<LandscapeDesc> lIdx : usedLandscapes)
    {
        for(const RoadTextureDesc& r : desc.get(lIdx).roadTexDesc)
        {
            if(!helpers::contains(textures, r.texturePath))
                textures.push_back(r.texturePath);
        }
    }
    return LOADER.LoadFiles(textures);
}

/// Anzahl der Rand-Dreiecke exakt zaehlen - gleiche Logik wie GenerateOpenGL (TerrainRenderer.cpp:311-332)
unsigned countBorderTriangles(const GameWorld& world)
{
    const WorldDescription& desc = world.GetDescription();
    unsigned n = 0;
    RTTR_FOREACH_PT(MapPoint, world.GetSize())
    {
        const TerrainDesc& t1 = desc.get(world.GetNode(pt).t1);
        const TerrainDesc& t2 = desc.get(world.GetNode(pt).t2);
        const TerrainDesc& t3 = desc.get(world.GetNode(world.GetNeighbour(pt, Direction::East)).t1);
        const TerrainDesc& t4 = desc.get(world.GetNode(world.GetNeighbour(pt, Direction::SouthWest)).t2);
        if(getEdgeType(t2, t1))
            ++n;
        if(getEdgeType(t1, t2))
            ++n;
        if(getEdgeType(t3, t2))
            ++n;
        if(getEdgeType(t2, t3))
            ++n;
        if(getEdgeType(t4, t1))
            ++n;
        if(getEdgeType(t1, t4))
            ++n;
    }
    return n;
}

struct LoadedMap
{
    std::shared_ptr<Game> game;
    bool ok = false;
};

LoadedMap loadMap(const char* mapName)
{
    LoadedMap res;
    std::vector<PlayerInfo> players(4);
    for(auto& p : players)
        p.ps = PlayerState::Occupied;
    res.game = std::make_shared<Game>(GlobalGameSettings(), 0u, players);
    // Bewusst ohne MapLoader::PlaceHQs (MapLoader.cpp:86): Startpositionen sind fuer die
    // Terrain-Messung irrelevant, wuerden aber bei Karten mit mehr Spielern als hier fehlschlagen.
    libsiedler2::Archiv mapArchiv;
    const auto path = rttr::test::rttrBaseDir / "data/RTTR/MAPS/NEW" / mapName;
    if(libsiedler2::loader::LoadMAP(path, mapArchiv) != 0)
        return res;
    const auto& map = *static_cast<libsiedler2::ArchivItem_Map*>(mapArchiv[0]);
    MapLoader loader(res.game->world_);
    res.ok = loader.Load(map, res.game->world_.GetGGS().exploration);
    return res;
}
} // namespace

BOOST_AUTO_TEST_SUITE(TrMem)

// ---------------------------------------------------------------------------------------------
// A) Datentypen: was kostet ein Kartenknoten in den einzelnen Puffern?
// ---------------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(Sizes)
{
    std::printf("\n=== A) Datentypen (sizeof, gleicher Compiler/Build) ===\n");
    std::printf("sizeof(PointF)                 = %u\n", unsigned(sizeof(PointF)));
    std::printf("vertices[i]      Vertex        = %u B  (spielerabhaengig: color+borderColor)\n",
                unsigned(sizeof(Vertex_)));
    std::printf("terrain[i]       2x DescIdx    = %u B  (rein Karte)\n",
                unsigned(sizeof(std::array<DescIdx<TerrainDesc>, 2>)));
    std::printf("borders[i]       Borders       = %u B  (rein Karte)\n", unsigned(sizeof(Borders_)));
    std::printf("gl_vertices      2x Triangle   = %u B  (rein Karte+Hoehe)\n", unsigned(2 * sizeof(Triangle_)));
    std::printf("gl_texcoords     2x Triangle   = %u B  (rein Karte)\n", unsigned(2 * sizeof(Triangle_)));
    std::printf("gl_colors        2x ColorTri   = %u B  (SPIELERABHAENGIG)\n", unsigned(2 * sizeof(ColorTriangle_)));
    const size_t perNode = sizeof(Vertex_) + sizeof(std::array<DescIdx<TerrainDesc>, 2>) + sizeof(Borders_)
                           + 2 * sizeof(Triangle_) + 2 * sizeof(Triangle_) + 2 * sizeof(ColorTriangle_);
    std::printf("=> Summe pro Knoten            = %u B (ohne Rand-Dreiecke)\n", unsigned(perNode));
    std::printf("   davon gl_colors             = %u B = %.1f%%\n", unsigned(2 * sizeof(ColorTriangle_)),
                100.0 * 2 * sizeof(ColorTriangle_) / perNode);
    std::printf("   pro Rand-Dreieck zusaetzlich= %u B (gl_vertices+gl_texcoords+gl_colors)\n",
                unsigned(2 * sizeof(Triangle_) + sizeof(ColorTriangle_)));

    // Hochrechnung mit 15% Rand-Dreiecken (gemessene Spanne auf echten Karten: 8,0% - 20,6%)
    const double perBorderTri = 2 * sizeof(Triangle_) + sizeof(ColorTriangle_);
    const double perNodeWithBorders = perNode + 0.15 * 2 * perBorderTri;
    std::printf("\nHochrechnung bei 15%% Rand-Dreiecken: %.0f B je Knoten\n", perNodeWithBorders);
    const std::array<unsigned, 5> edges = {256, 320, 512, 1024, 2048};
    for(unsigned e : edges)
    {
        const double n = double(e) * e;
        std::printf("  %4ux%-4u : 1 Renderer %8.1f MiB | 4 Renderer %8.1f MiB (RAM) + ebensoviel im VBO\n", e, e,
                    n * perNodeWithBorders / 1048576.0, 4 * n * perNodeWithBorders / 1048576.0);
    }
    std::printf("  (Kartengenerator laesst hoechstens 320x320 zu: iwMapGenerator.cpp:59;\n");
    std::printf("   MAX_MAP_SIZE=2048 in gameData/MapConsts.h:15 ist nur die theoretische Schranke)\n");
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------------------------
// B) Gemessener Speicher von TerrainRenderer::Init ueber verschiedene Kartengroessen
// ---------------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(MeasureInitMemory)
{
    const std::array<MapExtent, 5> sizes = {MapExtent(128, 128), MapExtent(256, 256), MapExtent(512, 512),
                                            MapExtent(1024, 1024), MapExtent(2048, 2048)};
    std::printf("\n=== B) Gemessener Speicher von TerrainRenderer::Init (Private Bytes, ohne Rand-Dreiecke) ===\n");
    const size_t perNode = sizeof(Vertex_) + sizeof(std::array<DescIdx<TerrainDesc>, 2>) + sizeof(Borders_)
                           + 4 * sizeof(Triangle_) + 2 * sizeof(ColorTriangle_);
    for(const MapExtent& s : sizes)
    {
        const size_t before = privateBytes();
        auto tr = std::make_unique<TerrainRenderer>();
        tr->Init(s);
        const size_t after = privateBytes();
        const size_t used = after - before;
        const double nodes = double(s.x) * double(s.y);
        std::printf("%4ux%-4u : gemessen %8.2f MiB (%6.1f B/Knoten) | gerechnet %8.2f MiB (%u B/Knoten)\n", s.x, s.y,
                    used / 1048576.0, used / nodes, nodes * perNode / 1048576.0, unsigned(perNode));
        std::fflush(stdout);
    }
}

// ---------------------------------------------------------------------------------------------
// C) Echte Karten: exakte Puffergroessen, volles GenerateOpenGL, Laufzeiten
// ---------------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(RealMap)
{
    uiHelper::initGUITests();
    libsiedler2::setAllocator(new GlAllocator);
    // Der MockupVideoDriver stellt nur die in DummyRenderer::initOpenGL (ogl/DummyRenderer.cpp:36-52)
    // gemockten GL-Funktionen bereit. glGenBuffers gehoert nicht dazu -> VBO-Pfad abschalten.
    SETTINGS.video.vbo = false;

    const std::array<const char*, 5> mapNames = {"GoldenCookies.SWD", "WAGE_0_3.SWD", "Hochebene.swd",
                                                    "TAL018.SWD", "pangea.wld"};
    for(const char* mapName : mapNames)
    {
        LoadedMap m = loadMap(mapName);
        if(!m.ok)
        {
            std::printf("\n### Karte %s konnte nicht geladen werden\n", mapName);
            std::fflush(stdout);
            continue;
        }
        GameWorld& world = m.game->world_;
        const MapExtent s = world.GetSize();
        const double nodes = double(s.x) * double(s.y);
        std::printf("\n=== C) Karte %s : %ux%u = %.0f Knoten, %u Spieler ===\n", mapName, s.x, s.y, nodes,
                    world.GetNumPlayers());
        std::fflush(stdout);

        // C1) Rand-Dreiecke exakt zaehlen
        const unsigned numBorderTris = countBorderTriangles(world);
        const unsigned numBaseTris = unsigned(nodes) * 2;
        std::printf("Basis-Dreiecke  = %u | Rand-Dreiecke = %u (+%.1f%%)\n", numBaseTris, numBorderTris,
                    100.0 * numBorderTris / numBaseTris);

        // C2) Exakte Puffergroessen aus den echten Datentypen
        const double bVertices = nodes * sizeof(Vertex_);
        const double bTerrain = nodes * sizeof(std::array<DescIdx<TerrainDesc>, 2>);
        const double bBorders = nodes * sizeof(Borders_);
        const unsigned totalTris = numBaseTris + numBorderTris;
        const double bGlVert = double(totalTris) * sizeof(Triangle_);
        const double bGlTex = double(totalTris) * sizeof(Triangle_);
        const double bGlClr = double(totalTris) * sizeof(ColorTriangle_);
        const double bTotal = bVertices + bTerrain + bBorders + bGlVert + bGlTex + bGlClr;
        // Spielerabhaengig sind: gl_colors komplett (TerrainRenderer.cpp:393-418 / 514-596) und
        // innerhalb von Vertex die Felder color (4 B) und borderColor (2x4 B) - gesetzt in
        // UpdateVertexColor (TerrainRenderer.cpp:222-241) aus gwv.GetVisibility.
        const double vertexPlayerBytes = sizeof(float) + 2 * sizeof(float);
        const double bPlayerDep = bGlClr + nodes * vertexPlayerBytes;
        std::printf("  vertices     = %7.2f MiB (davon %.2f MiB color/borderColor = spielerabhaengig)\n",
                    bVertices / 1048576.0, nodes * vertexPlayerBytes / 1048576.0);
        std::printf("  terrain      = %7.2f MiB\n", bTerrain / 1048576.0);
        std::printf("  borders      = %7.2f MiB\n", bBorders / 1048576.0);
        std::printf("  gl_vertices  = %7.2f MiB\n", bGlVert / 1048576.0);
        std::printf("  gl_texcoords = %7.2f MiB\n", bGlTex / 1048576.0);
        std::printf("  gl_colors    = %7.2f MiB   <-- vollstaendig spielerabhaengig\n", bGlClr / 1048576.0);
        std::printf("  SUMME 1 TerrainRenderer = %.2f MiB, davon spielerabhaengig %.2f MiB (%.1f%%)\n",
                    bTotal / 1048576.0, bPlayerDep / 1048576.0, 100.0 * bPlayerDep / bTotal);
        std::printf("  4 TerrainRenderer (RAM) = %.2f MiB | + gleich viel im VBO auf der GPU\n",
                    4 * bTotal / 1048576.0);
        std::fflush(stdout);

        // C3) Texturen laden, damit GenerateOpenGL vollstaendig laufen kann
        if(!loadTerrainTextures(world))
        {
            std::printf("### Terrain-Texturen fehlen, ueberspringe GenerateOpenGL fuer %s\n", mapName);
            std::fflush(stdout);
            continue;
        }

        // C4) Speicher + Zeit eines vollstaendigen GameWorldViewer inkl. GenerateOpenGL
        double genMs = 0;
        {
            const size_t before = privateBytes();
            auto gwv = std::make_unique<GameWorldViewer>(0, world);
            const size_t afterViewer = privateBytes();
            const auto t0 = std::chrono::steady_clock::now();
            gwv->InitTerrainRenderer(); // ruft TerrainRenderer::GenerateOpenGL
            const auto t1 = std::chrono::steady_clock::now();
            const size_t afterTr = privateBytes();
            genMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
            std::printf("GameWorldViewer ohne TR      = %7.2f MiB (%.1f B/Knoten)\n",
                        (afterViewer - before) / 1048576.0, (afterViewer - before) / nodes);
            std::printf("GenerateOpenGL Speicher      = %7.2f MiB (gerechnet %.2f MiB)\n",
                        (afterTr - afterViewer) / 1048576.0, bTotal / 1048576.0);
            std::printf("GenerateOpenGL Zeit          = %7.2f ms  -> 4 Spieler = %.2f ms (einmalig beim Laden)\n",
                        genMs, 4 * genMs);
            std::fflush(stdout);

            // C5) UpdateAllColors mit vollstaendig aufgebauten Raendern
            {
                gwv->RecalcAllColors(); // warmup
                const int reps = 5;
                const auto a = std::chrono::steady_clock::now();
                for(int i = 0; i < reps; ++i)
                    gwv->RecalcAllColors();
                const auto b = std::chrono::steady_clock::now();
                const double ms = std::chrono::duration<double, std::milli>(b - a).count() / reps;
                std::printf("UpdateAllColors              = %7.2f ms  -> 4 Spieler = %.2f ms\n", ms, 4 * ms);
            }

            // C6) VisibilityChanged: Kosten eines einzelnen Sichtbarkeitsereignisses
            {
                const int reps = 20000;
                const auto a = std::chrono::steady_clock::now();
                for(int i = 0; i < reps; ++i)
                {
                    const MapPoint pt(unsigned short(i % s.x), unsigned short((i / s.x) % s.y));
                    const_cast<TerrainRenderer&>(gwv->GetTerrainRenderer()).VisibilityChanged(pt, *gwv);
                }
                const auto b = std::chrono::steady_clock::now();
                const double us = std::chrono::duration<double, std::micro>(b - a).count() / reps;
                std::printf("VisibilityChanged (1 Knoten) = %7.3f us -> 4 Spieler = %.3f us\n", us, 4 * us);
            }

            // C7) Wieviele Knoten zeichnet ein Vollbild-View, wieviele vier Viertelbild-Views?
            {
                const Extent full(1920, 1080);
                GameWorldView vFull(*gwv, Position(0, 0), full);
                const Position f = vFull.GetFirstPt(), l = vFull.GetLastPt();
                const double nFull = double(l.x - f.x + 1) * double(l.y - f.y + 1);

                const Extent quarter(960, 540);
                GameWorldView vQ(*gwv, Position(0, 0), quarter);
                const Position qf = vQ.GetFirstPt(), ql = vQ.GetLastPt();
                const double nQ = double(ql.x - qf.x + 1) * double(ql.y - qf.y + 1);

                std::printf("maxNodeAltitude = %u\n", unsigned(gwv->getMaxNodeAltitude()));
                std::printf("Gezeichnete Knoten 1x1920x1080 = %.0f (%.0fx%.0f)\n", nFull, l.x - f.x + 1.0,
                            l.y - f.y + 1.0);
                std::printf("Gezeichnete Knoten 4x960x540   = %.0f (4 x %.0fx%.0f) = %+.1f%% gegenueber Vollbild\n",
                            4 * nQ, ql.x - qf.x + 1.0, ql.y - qf.y + 1.0, 100.0 * (4 * nQ / nFull - 1.0));
            }
            std::fflush(stdout);
        }

        // C8) Vier vollstaendige Viewer nebeneinander - gemessener Gesamtspeicher
        {
            const size_t before = privateBytes();
            std::vector<std::unique_ptr<GameWorldViewer>> viewers;
            const auto t0 = std::chrono::steady_clock::now();
            for(unsigned i = 0; i < 4; ++i)
            {
                viewers.push_back(std::make_unique<GameWorldViewer>(i % world.GetNumPlayers(), world));
                viewers.back()->InitTerrainRenderer();
            }
            const auto t1 = std::chrono::steady_clock::now();
            const size_t after = privateBytes();
            std::printf("4x (GameWorldViewer + GenerateOpenGL) = %.2f MiB, Aufbauzeit %.2f ms\n",
                        (after - before) / 1048576.0, std::chrono::duration<double, std::milli>(t1 - t0).count());
            std::fflush(stdout);
        }
    }
}


// ---------------------------------------------------------------------------------------------
// D) Wie oft feuern Sichtbarkeitsereignisse wirklich? Und was kostet es, wenn statt einem
//    TerrainRenderer vier daran haengen? Gemessen an einem echten Replay (8 Spieler, harte KI,
//    dieselbe Datei, die Test_autoplay abspielt).
// ---------------------------------------------------------------------------------------------
namespace {
struct ReplayMockGameState : ILocalGameState
{
    unsigned GetPlayerId() const override { return 0; }
    bool IsHost() const override { return true; }
    std::string FormatGFTime(unsigned) const override { return ""; }
    void SystemChat(const std::string&) override {}
};

/// Spielt das Replay bis maxGF ab. numViewers GameWorldViewer (mit vollem TerrainRenderer)
/// haengen dabei als Beobachter an der Welt - genau wie im Spiel.
/// Gibt die reine Wanduhrzeit zurueck.
unsigned gNumPlayers = 0;

double runReplay(unsigned maxGF, unsigned numViewers, unsigned* visNotesOut, unsigned* altNotesOut)
{
    const auto replayPath = rttr::test::rttrBaseDir / "tests" / "testData" / "200kGFs.rpl";
    Replay replay;
    BOOST_TEST_REQUIRE(replay.LoadHeader(replayPath));
    MapInfo mapInfo;
    BOOST_TEST_REQUIRE(replay.LoadGameData(mapInfo));
    std::vector<PlayerInfo> players;
    for(unsigned i = 0; i < replay.GetNumPlayers(); i++)
        players.emplace_back(replay.GetPlayer(i));
    gNumPlayers = replay.GetNumPlayers();
    Game game(replay.ggs, 0u, players);
    RANDOM.Init(replay.getSeed());
    GameWorld& world = game.world_;
    BOOST_TEST_REQUIRE(!!mapInfo.savegame);
    {
        ReplayMockGameState gs;
        mapInfo.savegame->sgd.ReadSnapshot(game, gs);
    }
    world.InitAfterLoad();

    unsigned visNotes = 0, altNotes = 0;
    Subscription subVis = world.GetNotifications().subscribe<PlayerNodeNote>([&](const PlayerNodeNote& n) {
        if(n.type == PlayerNodeNote::Visibility)
            ++visNotes;
    });
    Subscription subAlt = world.GetNotifications().subscribe<NodeNote>([&](const NodeNote& n) {
        if(n.type == NodeNote::Altitude)
            ++altNotes;
    });

    std::vector<std::unique_ptr<GameWorldViewer>> viewers;
    if(numViewers > 0)
        BOOST_TEST_REQUIRE(loadTerrainTextures(world));
    for(unsigned i = 0; i < numViewers; ++i)
    {
        viewers.push_back(std::make_unique<GameWorldViewer>(i, world));
        viewers.back()->InitTerrainRenderer();
    }

    auto nextGF = replay.ReadGF();
    BOOST_TEST_REQUIRE(nextGF.has_value());
    bool endOfReplay = false;
    const auto t0 = std::chrono::steady_clock::now();
    do
    {
        const unsigned curGF = game.em_->GetCurrentGF();
        if(curGF >= maxGF)
            break;
        while(nextGF && *nextGF == curGF)
        {
            const auto cmd = replay.ReadCommand();
            visit(composeVisitor([](const Replay::ChatCommand&) {},
                                 [&](const Replay::GameCommand& c) {
                                     for(const gc::GameCommandPtr& gc : c.cmds.gcs)
                                         gc->Execute(game.world_, c.player);
                                 }),
                  cmd);
            nextGF = replay.ReadGF();
            if(!nextGF)
            {
                endOfReplay = true;
                break;
            }
        }
        game.RunGF();
    } while(!endOfReplay);
    const auto t1 = std::chrono::steady_clock::now();
    if(visNotesOut)
        *visNotesOut = visNotes;
    if(altNotesOut)
        *altNotesOut = altNotes;
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}
} // namespace

BOOST_AUTO_TEST_CASE(ReplayViewerLoad)
{
    uiHelper::initGUITests();
    libsiedler2::setAllocator(new GlAllocator);
    SETTINGS.video.vbo = false;

    const unsigned maxGF = 100000;
    std::printf("\n=== D) Echtes Replay 200kGFs.rpl, %u GF (= %.1f Spielminuten bei 20 GF/s) ===\n", maxGF,
                maxGF / 20.0 / 60.0);
    std::fflush(stdout);

    unsigned vis0 = 0, alt0 = 0, vis1 = 0, alt1 = 0, vis4 = 0, alt4 = 0;
    const double t0 = runReplay(maxGF, 0, &vis0, &alt0);
    std::printf("0 Viewer : %8.1f ms | Visibility-Notes %8u | Altitude-Notes %6u\n", t0, vis0, alt0);
    std::fflush(stdout);
    const double t1 = runReplay(maxGF, 1, &vis1, &alt1);
    std::printf("1 Viewer : %8.1f ms (%+.1f%% gegenueber 0)\n", t1, 100.0 * (t1 / t0 - 1.0));
    std::fflush(stdout);
    const double t4 = runReplay(maxGF, 4, &vis4, &alt4);
    std::printf("4 Viewer : %8.1f ms (%+.1f%% gegenueber 0, %+.1f%% gegenueber 1)\n", t4,
                100.0 * (t4 / t0 - 1.0), 100.0 * (t4 / t1 - 1.0));
    std::printf("Visibility-Notes je GF (alle Spieler zusammen) = %.1f\n", double(vis0) / maxGF);
    std::printf("Visibility-Notes je GF und Spieler             = %.2f (%u Spieler im Replay)\n",
                double(vis0) / maxGF / gNumPlayers, gNumPlayers);
    std::printf("Mehrkosten durch den 2.-4. Viewer je GF = %.3f ms (bei 20 GF/s = %.2f ms je Sekunde)\n",
                (t4 - t1) / maxGF, (t4 - t1) / maxGF * 20.0);
    std::fflush(stdout);
    BOOST_TEST(vis0 == vis1);
    BOOST_TEST(vis0 == vis4);
}

BOOST_AUTO_TEST_SUITE_END()
