// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "GamePlayer.h"
#include "Loader.h"
#include "ogl/FontStyle.h"
#include "controls/ctrlTextButton.h"
#include "RttrConfig.h"
#include "RttrForeachPt.h"
#include "Replay.h"
#include "WindowManager.h"
#include "controls/ctrlProgress.h"
#include "controls/ctrlTimer.h"
#include "desktops/PlayerView.h"
#include "factories/GameCommandFactory.h"
#include "files.h"
#include "ingameWindows/IngameWindow.h"
#include "input/FocusPath.h"
#include "network/GameClient.h"
#include "variant.h"
#include "world/GameWorld.h"
#include "LocalGameFixture.h"
#include "PadFixture.h"
#include "gameTypes/AIInfo.h"
#include "gameTypes/BuildingQuality.h"
#include "gameTypes/MapInfo.h"
#include "gameTypes/PlayerState.h"
#include "gameTypes/SettingsTypes.h"
#include "gameData/const_gui_ids.h"
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

/// Eine laufende Partie mit zwei lokalen Menschen plus Dummy-KI und einem echten
/// dskGameInterface mit zwei Ansichten.
///
/// Herausgezogen aus testPadCommands.cpp, damit auch die Nachweise zum Fensterbesitz
/// (testWindowDataOwnership.cpp) an derselben, unveraenderten Naht messen: gezaehlt wird im
/// Replay, also nur, was tatsaechlich vom Server zurueckkam.
namespace rttr::test {

/// Ein Punkt, auf dem GENAU DIESER Spieler eine Flagge setzen kann und kein anderer.
///
/// Die zweite Haelfte ist der Punkt: GameWorld::SetFlag prueft GetBQ(pt, player)
/// (world/GameWorld.cpp:73). Ausserhalb des eigenen Gebiets ist die BQ Nothing, das Kommando
/// verpufft. Genau deshalb ist ein vertauschtes Geraet hier ein ECHTER Fehlschlag - die Flagge
/// entsteht dann gar nicht - und nicht bloss eine kosmetische Abweichung.
inline MapPoint findExclusiveFlagSpot(const GameWorld& world, const unsigned char player, const unsigned char otherPlayer)
{
    RTTR_FOREACH_PT(MapPoint, world.GetSize())
    {
        if(world.GetBQ(pt, player) == BuildingQuality::Nothing)
            continue;
        if(world.GetBQ(pt, otherPlayer) != BuildingQuality::Nothing)
            continue; // waere kein Unterscheidungsmerkmal
        if(world.IsFlagAround(pt))
            continue;
        if(world.GetNO(pt)->GetType() == NodalObjectType::Flag)
            continue;
        return pt;
    }
    return MapPoint::Invalid();
}

/// Zaehlt die im Replay aufgezeichneten GameCommands eines Spielers. Aufgezeichnet wird
/// ausschliesslich aus nwfInfo (GameClientGF_Game.cpp:26-31) - also nur, was tatsaechlich vom
/// Server zurueckkam. Ein clientlokaler Kurzschluss taucht hier nicht auf.
inline unsigned numGCsForPlayer(const boost::filesystem::path& replayPath, const unsigned char player)
{
    unsigned count = 0;
    Replay replay;
    BOOST_TEST_REQUIRE(replay.LoadHeader(replayPath));
    MapInfo mapInfo;
    BOOST_TEST_REQUIRE(replay.LoadGameData(mapInfo));
    for(auto gf = replay.ReadGF(); gf.has_value(); gf = replay.ReadGF())
    {
        visit(composeVisitor([](const Replay::ChatCommand&) {},
                             [&](const Replay::GameCommand& cmd) {
                                 if(cmd.player == player)
                                     count += static_cast<unsigned>(cmd.cmds.gcs.size());
                             }),
              replay.ReadCommand());
    }
    return count;
}

/// Ein Fenster, wie es das Spiel baut: es bekommt eine GameCommandFactory herein und erzeugt
/// daraus auf Knopfdruck ein Kommando. dskGameInterface uebergibt an dieser Stelle GAMECLIENT
/// (dskGameInterface.cpp, Zeilen 708-726), und genau das tut dieser Test auch - er baut also
/// keinen Sonderfall nach, sondern den Normalfall.
struct FlagButtonWnd : IngameWindow
{
    FlagButtonWnd(GameCommandFactory& gcFactory, const MapPoint pt)
        : IngameWindow(CGI_HELP, DrawPoint(0, 0), Extent(200, 120), "", nullptr, false, CloseBehavior::Regular),
          gcFactory_(gcFactory), pt_(pt)
    {
        AddTextButton(1, DrawPoint(10, 10), Extent(80, 20), TextureColor::Green1, "Flagge", NormalFont);
    }
    void Msg_ButtonClick(unsigned) override { gcFactory_.SetFlag(pt_); }

private:
    GameCommandFactory& gcFactory_;
    MapPoint pt_;
};

/// Zwei lokale Menschen (Slot 0 und 1) plus eine Dummy-KI (Slot 2) in einer laufenden Partie,
/// dazu ein echtes dskGameInterface mit zwei Ansichten und zwei angesteckten Gamepads.
struct PadGameFixture : LocalGameFixture
{
    std::unique_ptr<TestableGameInterface> dsk;
    PadFeeder pads{*uiHelper::GetVideoDriver()};

    void setUpTwoLocalPlayers()
    {
        hostAndEnterLobby();
        GAMECLIENT.SetAdditionalLocalPlayers({1});
        GameClient::ApplyAdditionalLocalPlayers(lobby(), GAMECLIENT.GetAdditionalLocalPlayers());
        lobby().SetPlayerState(2, PlayerState::AI, AI::Info(AI::Type::Dummy));
        pumpUntil(
          [] {
              const auto l = GAMECLIENT.GetGameLobby();
              return l->getPlayer(1).ps == PlayerState::AI && l->getPlayer(2).ps == PlayerState::AI;
          },
          "lobby to apply the local player configuration");
        startGame();

        BOOST_TEST_REQUIRE(GAMECLIENT.IsLocalHumanPlayer(0));
        BOOST_TEST_REQUIRE(GAMECLIENT.IsLocalHumanPlayer(1));

        dsk = std::make_unique<TestableGameInterface>(ci().game, GAMECLIENT.GetNWFInfo(), GAMECLIENT.GetPlayerId(),
                                                      /*initOGL*/ false);
        BOOST_TEST_REQUIRE(dsk->GetNumViews() == 2u);
        BOOST_TEST_REQUIRE(dsk->GetPlayerView(0).GetPlayerId() == 0u);
        BOOST_TEST_REQUIRE(dsk->GetPlayerView(1).GetPlayerId() == 1u);
    }

    /// Ein Frame Eingabe. Die Maus liegt bewusst ausserhalb jeder Ansicht.
    void step(const unsigned elapsedMs) { dsk->UpdateInput(elapsedMs, kMouseOffScreen); }

    /// Nimmt Pad `dev` in die Hand und steuert seinen Zeiger auf `pt` - ueber den PADPFAD.
    ///
    /// Angesteckt UND einmal benutzt (erst das gibt ihm seine Ansicht, siehe input/PadRouter.h),
    /// dann rechter Stick fuer die Kamera und linker fuer den Zeiger. Frueher stand hier
    /// GameWorldView::MoveToMapPt - ein Aufruf, den KEIN Padknopf ausloest; die Nachweise waren
    /// dadurch gruen, obwohl der Spieler den Zielpunkt gar nicht haette erreichen koennen.
    void aimPadAt(const PadDeviceId dev, const unsigned viewIdx, const MapPoint pt)
    {
        pads.pickUp(dev);
        step(0);
        BOOST_TEST_REQUIRE(dsk->GetPlayerView(viewIdx).HasPadCursor());
        // Ein frisch zugeordnetes Pad startet in der Mitte SEINER Ansicht (OnPadAssigned).
        BOOST_TEST_REQUIRE((dsk->GetPlayerView(viewIdx).GetPadCursor()
                            == dsk->GetPlayerView(viewIdx).GetViewCenter()));
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
        rttr::test::padSteerTo(pads, *dsk, world(), dev, viewIdx, pt, [this](unsigned ms) { step(ms); });
    }

    /// Ein vollstaendiger Knopfdruck (Flanke runter und hoch) plus der Frame, in dem er
    /// ausgeliefert wird.
    void press(const PadDeviceId dev, const PadButton button)
    {
        pads.tap(dev, button);
        dsk->UpdateInput(16, kMouseOffScreen);
    }

    /// Das Replay dieser Partie schliessen und den Pfad zurueckgeben. Danach ist die Partie zu
    /// Ende; alle Zaehlungen laufen ueber numGCsForPlayer.
    boost::filesystem::path stopAndGetReplay()
    {
        const auto replayPath = RTTRCONFIG.ExpandPath(s25::folders::replays) / GAMECLIENT.GetReplayFilename();
        GAMECLIENT.Stop(); // schliesst und komprimiert das Replay
        BOOST_TEST_REQUIRE(boost::filesystem::exists(replayPath));
        return replayPath;
    }

    /// Wandert mit dem ECHTEN Padpfad (Schulterknopf = FocusPath::Dir::Next) weiter, bis der
    /// Fokus dieses Spielers auf einem Fortschrittsbalken steht.
    ctrlProgress* focusFirstProgressBar(PlayerView& view, const PadDeviceId dev)
    {
        for(unsigned i = 0; i < 32u; ++i)
        {
            if(auto* prog = dynamic_cast<ctrlProgress*>(view.GetFocus().GetFocused()))
                return prog;
            pads.tap(dev, PadButton::RightShoulder);
            dsk->UpdateInput(16, kMouseOffScreen);
        }
        return nullptr;
    }

    /// Verstellt den fokussierten Balken um genau einen Schritt, ueber DpadRight - den echten
    /// Weg (FocusPath::Step -> ctrlProgress::StepValue -> Msg_ProgressChange). Steht der Balken
    /// schon am Anschlag, wird in die Gegenrichtung verstellt.
    bool nudge(ctrlProgress& prog, const PadDeviceId dev)
    {
        const auto before = prog.GetPosition();
        pads.tap(dev, PadButton::DpadRight);
        dsk->UpdateInput(16, kMouseOffScreen);
        if(prog.GetPosition() == before)
        {
            pads.tap(dev, PadButton::DpadLeft);
            dsk->UpdateInput(16, kMouseOffScreen);
        }
        return prog.GetPosition() != before;
    }

    /// Laesst den 2-Sekunden-Timer des Fensters JETZT feuern. Verkuerzt wird ausschliesslich die
    /// Wartezeit; der Aufruf danach ist GENAU der, den WindowManager::Draw je Fenster macht
    /// (WindowManager.cpp:96) - also der echte, klammerfreie Weg.
    static void fireTransmitTimer(IngameWindow& wnd)
    {
        const auto timers = wnd.GetCtrls<ctrlTimer>();
        BOOST_TEST_REQUIRE(timers.size() == 1u);
        timers.front()->Start(std::chrono::milliseconds(0));
        wnd.Msg_PaintBefore();
    }

    void tearDownDesktop()
    {
        dsk.reset();
        world().SetGameInterface(nullptr);
    }

    /// Die Maus liegt ausserhalb jeder Ansicht: sie kann in diesen Tests nachweislich nichts
    /// beitragen.
    inline static const Position kMouseOffScreen{-10000, -10000};
};

/// Formatiert Militaereinstellungen so, wie der Pruefbericht sie gemessen hat.
inline std::string formatMilitary(const MilitarySettings& settings)
{
    std::string out;
    for(const auto v : settings)
        out += " " + std::to_string(unsigned(v));
    return out;
}

/// Liest die WIRKLICHEN Militaereinstellungen eines Spielers aus dem Spielzustand.
inline MilitarySettings readMilitary(const GamePlayer& player)
{
    MilitarySettings settings;
    for(unsigned i = 0; i < settings.size(); ++i)
        settings[i] = player.GetMilitarySetting(i);
    return settings;
}

} // namespace rttr::test
