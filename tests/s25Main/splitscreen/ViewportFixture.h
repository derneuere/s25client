// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Cheats.h"
#include "GameInterface.h"
#include "GamePlayer.h"
#include "factories/GameCommandFactory.h"
#include "world/GameWorld.h"
#include "world/GameWorldViewer.h"
#include "uiHelper/uiHelpers.hpp"
#include "worldFixtures/CreateEmptyWorld.h"
#include "worldFixtures/WorldFixture.h"
#include <boost/test/unit_test.hpp>
#include <memory>

namespace rttr::test {

/// GameInterface-Attrappe. Notwendig, weil GameWorldView::Draw world.GetGameInterface()
/// ungeprueft dereferenziert (world/GameWorldView.cpp:168) und weil GamePlayer::IsBuildingEnabled
/// ueber GI_GetCheats() in der Simulation danach greift (GamePlayer.cpp:2337).
struct StubGameInterface : GameInterface
{
    struct StubGCFactory : GameCommandFactory
    {
        bool AddGC(gc::GameCommandPtr) override { return true; }
    };

    StubGCFactory factory;
    std::unique_ptr<Cheats> cheats;

    explicit StubGameInterface(GameWorldBase& world) : cheats(std::make_unique<Cheats>(world, factory)) {}

    void GI_PlayerDefeated(unsigned) override {}
    void GI_UpdateMinimap(MapPoint) override {}
    void GI_FlagDestroyed(MapPoint) override {}
    void GI_TreatyOfAllianceChanged(unsigned) override {}
    void GI_UpdateMapVisibility() override {}
    void GI_Winner(unsigned) override {}
    void GI_TeamWinner(unsigned) override {}
    void GI_StartRoadBuilding(MapPoint, bool) override {}
    void GI_CancelRoadBuilding() override {}
    void GI_BuildRoad() override {}
    Cheats& GI_GetCheats() override { return *cheats; }
};

/// Zwei Spieler mit weit auseinanderliegenden Hauptquartieren auf einer schmalen Karte. Weit
/// auseinander ist wichtig: nur dann sind die Sichtbarkeitsbereiche der beiden Spieler
/// disjunkt und der Fog-of-War-Nachweis bedeutet etwas.
///
/// Bewusst OHNE InitTerrainRenderer(): das ruft TerrainRenderer::GenerateOpenGL -> LoadTextures
/// und wirft im Test ("Invalid texture '<RTTR_GAME>/GFX/TEXTURES/TEX5.LBM'"), weil
/// LOADER.LoadDummyGUIFiles keine Terraintexturen anlegt. Gebraucht wird hier nur die
/// CPU-Geometrie, und genau die legt InitTerrainGeometry() an.
struct ViewportFixture : WorldFixture<CreateEmptyWorld, 2, 60, 30>
{
    StubGameInterface gi{world};
    GameWorldViewer viewer0{0, world};
    GameWorldViewer viewer1{1, world};

    ViewportFixture()
    {
        uiHelper::initGUITests();
        world.SetGameInterface(&gi);
        viewer0.InitTerrainGeometry();
        viewer1.InitTerrainGeometry();
        // Vorbedingung, ohne die der FoW-Nachweis trivial gruen waere: waere die ganze Karte
        // sichtbar, haetten alle Spieler dieselbe Sicht. WorldFixtureBase setzt dafuer
        // Exploration::Classic (worldFixtures/WorldFixture.h:105).
        BOOST_TEST_REQUIRE(!viewer0.IsAllVisible());
        BOOST_TEST_REQUIRE(!viewer1.IsAllVisible());
    }

    ~ViewportFixture() { world.SetGameInterface(nullptr); }
};

} // namespace rttr::test
