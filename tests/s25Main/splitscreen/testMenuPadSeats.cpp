// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "LocalGameFixture.h"
#include "MenuPadFixture.h"
#include "GameLobby.h"
#include "ILobbyClient.hpp"
#include "JoinPlayerInfo.h"
#include "WindowManager.h"
#include "controls/ctrlButton.h"
#include "controls/ctrlGroup.h"
#include "controls/ctrlTextButton.h"
#include "desktops/dskGameLobby.h"
#include "drivers/VideoDriverWrapper.h"
#include "ingameWindows/iwMsgbox.h"
#include "input/MenuPadInput.h"
#include "input/PadRouter.h"
#include "network/GameClient.h"
#include "network/GameServer.h"
#include "gameData/MaxPlayers.h"
#include "helpers/format.hpp"
#include <boost/test/unit_test.hpp>
#include <mygettext/mygettext.h>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr PadButton Activate = PadButton::A;
constexpr PadButton Back = PadButton::B;
constexpr PadButton NextCtrl = PadButton::RightShoulder;
constexpr PadButton PrevCtrl = PadButton::LeftShoulder;

/// Zwei Control-Ids aus dskGameLobby.cpp, das sie in einem anonymen namespace fuehrt.
///
/// Sie stehen hier, weil der Host seine Slots MIT DER MAUS einstellt und der Test genau diesen
/// Knopf treffen muss. Verrutscht die Aufzaehlung, findet der Test das Control nicht und
/// scheitert sichtbar (BOOST_TEST_REQUIRE unten) - er wird nicht still gruen.
constexpr unsigned ID_grpPlayerStart = 48;
constexpr unsigned ID_btPlayerState = 27;
/// Die Tauschknoepfe und die Sitzkarten liegen HINTER den Spielergruppen, mit denselben
/// Basen wie in dskGameLobby.cpp.
constexpr unsigned ID_btSwap = ID_grpPlayerStart + MAX_PLAYERS;
constexpr unsigned ID_btSeat = ID_btSwap + MAX_PLAYERS;
constexpr unsigned ID_txtSeats = ID_btSeat + MAX_VIEWPORTS;
/// Die beiden Knoepfe einer iwMsgbox (iwMsgbox.cpp:17-22): ID_BT_0 ist "Ja", ID_BT_0 + 1 "Nein".
constexpr unsigned ID_mbYes = 2;
constexpr unsigned ID_mbNo = 3;

/// Der Zuordnungsbildschirm der Lobby, mit echtem Server und echtem Client.
///
/// dskGameLobby wird GENAU SO konstruiert wie in Produktion (iwConnecting.cpp:85-89): derselbe
/// Konstruktor, dieselben vier Argumente. Der Weg dorthin - Kartenauswahl und HostGame ueber
/// Padeingaben - ist Gegenstand des Abnahmenachweises (testMenuPadAcceptance.cpp) und wird hier
/// abgekuerzt, damit diese Faelle schnell und ohne Kartenverzeichnis laufen.
struct LobbySeatFixture : rttr::test::LocalGameFixture, rttr::test::MenuPadFixture
{
    void frame() override
    {
        video.tickCount_ += frameMs;
        GAMECLIENT.Run();
        GAMESERVER.Run();
        WINDOWMANAGER.Draw();
    }

    void enterLobby()
    {
        hostAndEnterLobby();
        BOOST_TEST_REQUIRE(GAMECLIENT.GetAdditionalLocalPlayers().empty());
        enterLobbyKeepingLocalPlayers();
    }

    /// Dasselbe, aber ohne die Zusicherung "nichts vorbereitet" - fuer den Fall, in dem die
    /// KOMMANDOZEILE schon Sitze benannt hat.
    void enterLobbyKeepingLocalPlayers()
    {
        WINDOWMANAGER.Switch(std::make_unique<dskGameLobby>(ServerType::Local, GAMECLIENT.GetGameLobby(),
                                                            GAMECLIENT.GetPlayerId(), nullptr));
        frame();
        BOOST_TEST_REQUIRE(desktopAs<dskGameLobby>() != nullptr);
    }

    /// Zahl der Sitzkarten - der Router bekommt sie von dskGameLobby::GetNumPadSlots().
    static unsigned numSeats() { return router().GetNumSlots(); }

    static std::vector<uint8_t> localPlayers() { return GAMECLIENT.GetAdditionalLocalPlayers(); }

    static const JoinPlayerInfo& player(unsigned id) { return GAMECLIENT.GetGameLobby()->getPlayer(id); }

    /// Ein Mausklick auf ein beliebiges Control - genau der Weg, den der Host nimmt.
    void clickWithMouse(Window& ctrl)
    {
        MouseCoords mc(ctrl.GetDrawPos() + Position(ctrl.GetSize() / 2u));
        WINDOWMANAGER.Msg_MouseMove(mc);
        mc.ldown = true;
        WINDOWMANAGER.Msg_LeftDown(mc);
        mc.ldown = false;
        WINDOWMANAGER.Msg_LeftUp(mc);
        for(int i = 0; i < 20; ++i)
            frame();
    }

    /// Frames laufen lassen, bis die Loopbackverbindung ihre Nachrichten zugestellt hat.
    void settle(unsigned frames = 40)
    {
        for(unsigned i = 0; i < frames; ++i)
            frame();
    }

    /// Der Host schaltet den Zustand eines Spielerslots einmal weiter - Mausklick auf den
    /// Zustandsknopf der Spielerreihe (dskGameLobby::Msg_Group_ButtonClick ->
    /// GameLobbyController::TogglePlayerState).
    void togglePlayerState(unsigned playerId)
    {
        auto* lobby = desktopAs<dskGameLobby>();
        BOOST_TEST_REQUIRE(lobby != nullptr);
        auto* group = lobby->GetCtrl<ctrlGroup>(ID_grpPlayerStart + playerId);
        BOOST_TEST_REQUIRE(group != nullptr);
        auto* bt = group->GetCtrl<Window>(ID_btPlayerState);
        BOOST_TEST_REQUIRE(bt != nullptr);
        clickWithMouse(*bt);
    }

    /// Klickt weiter, bis der Slot den gewuenschten Zustand hat.
    ///
    /// Bewusst nicht mit einer festen Klickzahl: die Kette
    /// Frei -> KI leicht -> mittel -> schwer -> Dummy -> geschlossen laeuft ueber einen echten
    /// Loopbacksocket, und der Ausgangspunkt haengt davon ab, welche Nachrichten schon
    /// angekommen sind. Gelesen wird also der SPIELZUSTAND - genau wie ein Mensch, der auf die
    /// Zeile schaut und noch einmal klickt.
    template<class T_Pred>
    void toggleUntil(unsigned playerId, const T_Pred& isDone, const char* what)
    {
        settle();
        for(unsigned n = 0; n < 8 && !isDone(); ++n)
            togglePlayerState(playerId);
        BOOST_TEST_REQUIRE(isDone(), "the host to be able to set a slot to " << what);
    }

    void closeSlotWithMouse(unsigned playerId)
    {
        toggleUntil(
          playerId, [playerId] { return player(playerId).ps == PlayerState::Locked; }, "closed");
    }

    void setHardAiWithMouse(unsigned playerId)
    {
        toggleUntil(
          playerId,
          [playerId] {
              const JoinPlayerInfo& p = player(playerId);
              return p.ps == PlayerState::AI && p.aiInfo.type == AI::Type::Default
                     && p.aiInfo.level == AI::Level::Hard;
          },
          "a hard AI");
    }

    /// Der Host tauscht SICH SELBST mit einem anderen Slot - der einzige Tausch, den die
    /// Oberflaeche anbietet (dskGameLobby::Msg_ButtonClick, ID_btSwap).
    void swapWithHostUsingMouse(unsigned playerId)
    {
        auto* lobby = desktopAs<dskGameLobby>();
        BOOST_TEST_REQUIRE(lobby != nullptr);
        auto* bt = lobby->GetCtrl<Window>(ID_btSwap + playerId);
        BOOST_TEST_REQUIRE(bt != nullptr);
        clickWithMouse(*bt);
    }

    /// Die Beschriftung einer Sitzkarte - genau das, was auf dem Bildschirm steht.
    static std::string seatText(unsigned seat)
    {
        auto* lobby = desktopAs<dskGameLobby>();
        BOOST_TEST_REQUIRE(lobby != nullptr);
        auto* bt = lobby->GetCtrl<ctrlTextButton>(ID_btSeat + seat);
        BOOST_TEST_REQUIRE(bt != nullptr);
        return bt->GetText();
    }

    /// Frames laufen lassen, bis die Bedingung wahr ist. Liefert false bei Zeitueberschreitung.
    template<class T_Pred>
    bool frameUntil(const T_Pred& isDone, unsigned maxFrames = 600)
    {
        for(unsigned i = 0; i < maxFrames && !isDone(); ++i)
            frame();
        return isDone();
    }

    /// Ab dem Startknopf ersetzt der Test den Ladebildschirm - dieselbe eine Luecke wie im
    /// Abnahmenachweis: dskGameLoader braucht die originalen S2-Daten
    /// (testMenuPadAcceptance.cpp, Abschnitt 6).
    void takeOverTheClientInterface() { GAMECLIENT.SetInterface(&ci()); }

    static iwMsgbox* topMsgbox() { return dynamic_cast<iwMsgbox*>(WINDOWMANAGER.GetTopMostWindow()); }
};

} // namespace

BOOST_AUTO_TEST_SUITE(MenuPadSeatTests)

/// Der Bildschirm bietet ueberhaupt mehrere Padplaetze an - anderswo waere das ein Wettlauf um
/// den naechsten Desktopwechsel und deshalb ausdruecklich verboten (Desktop::GetNumPadSlots).
BOOST_FIXTURE_TEST_CASE(TheLobbyOffersOnePadSlotPerSeat, LobbySeatFixture)
{
    enterLobby();
    pickUp(10);
    BOOST_TEST_REQUIRE(numSeats() >= 2u);
    BOOST_TEST(numSeats() <= MAX_VIEWPORTS);
    BOOST_TEST(router().GetSlot(10) == 0u);
}

/// XR-115, "Press A to join": ein zweites Pad nimmt Platz, und daraus entsteht der Spielzustand -
/// nicht aus der Kommandozeile.
BOOST_FIXTURE_TEST_CASE(APadJoinsBySayingA, LobbySeatFixture)
{
    enterLobby();
    pickUp(10); // Host
    pickUp(11);
    BOOST_TEST_REQUIRE(router().GetSlot(11) == 1u);
    // Sein Fokus liegt auf der ersten freien Sitzkarte (dskGameLobby::GetPadEntryCtrl).
    BOOST_TEST_REQUIRE(focused(1) != nullptr);

    press(11, Activate);
    frame();
    const std::vector<uint8_t> expected{1};
    BOOST_TEST(localPlayers() == expected, boost::test_tools::per_element());
    BOOST_TEST((GAMECLIENT.GetGameLobby()->getPlayer(1).ps == PlayerState::AI));
}

/// DIE SCHARFE PROBE: die Zuordnung folgt dem DRUECKENDEN Geraet, nicht der Steckreihenfolge.
/// Mit nur einem Pad waere auch eine Umsetzung gruen, die schlicht "das erste freie Geraet"
/// nimmt - deshalb zwei Pads, und deshalb in umgedrehter Reihenfolge.
BOOST_FIXTURE_TEST_CASE(TheSeatFollowsThePressingDevice, LobbySeatFixture)
{
    enterLobby();
    pickUp(10); // Host, Slot 0
    pickUp(11); // Slot 1
    pickUp(12); // Slot 2
    BOOST_TEST_REQUIRE(numSeats() >= 3u);
    BOOST_TEST_REQUIRE(router().GetSlot(11) == 1u);
    BOOST_TEST_REQUIRE(router().GetSlot(12) == 2u);

    // Das SPAETER aufgenommene Pad drueckt ZUERST - es bekommt damit Sitz 2 (Ansicht 1).
    press(12, Activate);
    frame();
    BOOST_TEST(router().GetSlot(12) == 1u);

    // Das verdraengte Pad steht nicht ohne Slot da: es hat einen freien bekommen
    // (PadRouter::RebalanceUnassigned). Ohne diesen Ausgleich koennte es nie mehr beitreten.
    BOOST_TEST_REQUIRE(router().GetSlot(11) != PadRouter::NoSlot);
    // Das andere sieht die naechste freie Karte und nimmt sie.
    press(11, Activate);
    frame();

    const std::vector<uint8_t> expected{1, 2};
    BOOST_TEST(localPlayers() == expected, boost::test_tools::per_element());
    // Ansicht 1 gehoert dem Geraet, das zuerst gedrueckt hat.
    BOOST_TEST(router().GetSlot(12) == 1u);
    BOOST_TEST(router().GetSlot(11) == 2u);
}

/// XR-115: ein neu hinzukommender Controller nimmt einem sitzenden Spieler NIE den Platz weg.
BOOST_FIXTURE_TEST_CASE(ANewPadDoesNotTakeAnOccupiedSeat, LobbySeatFixture)
{
    enterLobby();
    pickUp(10);
    pickUp(11);
    press(11, Activate);
    frame();
    BOOST_TEST_REQUIRE(router().GetSlot(11) == 1u);

    pickUp(12);
    press(12, Activate);
    frame();
    // Der Erste sitzt unveraendert auf Ansicht 1.
    BOOST_TEST(router().GetSlot(11) == 1u);
    BOOST_TEST(router().GetSlot(12) != 1u);
}

/// Aufstehen mit B: der Slot faellt auf die gewoehnliche KI zurueck, NICHT auf den Dummy aus
/// ApplyAdditionalLocalPlayers - sonst hinterliesse jeder Aussteiger eine untaetige KI.
BOOST_FIXTURE_TEST_CASE(LeavingASeatRestoresTheOrdinaryAi, LobbySeatFixture)
{
    enterLobby();
    pickUp(10);
    pickUp(11);
    press(11, Activate);
    frame();
    BOOST_TEST_REQUIRE(localPlayers().size() == 1u);

    press(11, Back);
    for(int i = 0; i < 20; ++i)
        frame();
    BOOST_TEST(localPlayers().empty());
    const JoinPlayerInfo& player = GAMECLIENT.GetGameLobby()->getPlayer(1);
    BOOST_TEST((player.ps == PlayerState::AI));
    BOOST_TEST((player.aiInfo.type == AI::Type::Default));
}

/// Kabel raus: der Sitz wird ausdruecklich frei. Ohne diesen Schritt zoege der PadRouter von
/// sich aus ein danebenliegendes Pad in den frei gewordenen Slot nach - ein Unbeteiligter saesse
/// ungefragt auf Sitz 2 (XR-115, TV-RECHERCHE.md:167).
BOOST_FIXTURE_TEST_CASE(UnpluggingAPadFreesItsSeat, LobbySeatFixture)
{
    enterLobby();
    pickUp(10);
    pickUp(11);
    press(11, Activate);
    frame();
    BOOST_TEST_REQUIRE(localPlayers().size() == 1u);

    disconnect(11);
    frame();
    frame();
    BOOST_TEST(localPlayers().empty());
    BOOST_TEST(router().GetSlot(11) == PadRouter::NoSlot);
}

/// HARTE RANDBEDINGUNG: ein MAUSKLICK auf eine Sitzkarte nimmt keinen Platz ein. Ein Sitzplatz
/// ohne Pad waere ein Spieler ohne Eingabegeraet - und der Mausspieler soll durch den neuen
/// Bildschirm nichts verlieren und nichts ungewollt gewinnen.
BOOST_FIXTURE_TEST_CASE(AMouseClickOnASeatCardTakesNoSeat, LobbySeatFixture)
{
    enterLobby();
    pickUp(10);
    pickUp(11);
    // Die Karte, auf der Pad 11 steht - dieselbe, die es gleich per A nehmen wuerde.
    Window* card = focused(1);
    BOOST_TEST_REQUIRE(card != nullptr);

    MouseCoords mc(card->GetDrawPos() + Position(card->GetSize() / 2u));
    WINDOWMANAGER.Msg_MouseMove(mc);
    mc.ldown = true;
    WINDOWMANAGER.Msg_LeftDown(mc);
    mc.ldown = false;
    WINDOWMANAGER.Msg_LeftUp(mc);
    frame();
    BOOST_TEST(localPlayers().empty());

    // GEGENPROBE: derselbe Knopf, mit dem Pad gedrueckt, wirkt.
    press(11, Activate);
    frame();
    BOOST_TEST(localPlayers().size() == 1u);
}

/// HARTE RANDBEDINGUNG: --local-players bleibt, was es war.
///
/// Der Zuordnungsbildschirm liest die Kommandozeilenvorgabe und ZEIGT sie, statt sie zu
/// ueberschreiben. Ohne diese Zusicherung haette der neue Bildschirm den einzigen bisherigen
/// Weg in den Splitscreen still abgeraeumt.
BOOST_FIXTURE_TEST_CASE(TheCommandLineSeatsSurviveTheSeatPanel, LobbySeatFixture)
{
    hostAndEnterLobby();
    // Das ist der Zustand, den s25client.cpp:452-487 herstellt.
    GAMECLIENT.SetAdditionalLocalPlayers({1});
    enterLobbyKeepingLocalPlayers();

    const std::vector<uint8_t> expected{1};
    BOOST_TEST(localPlayers() == expected, boost::test_tools::per_element());
    for(int i = 0; i < 20; ++i)
        frame();
    BOOST_TEST((GAMECLIENT.GetGameLobby()->getPlayer(1).ps == PlayerState::AI));

    // Und ein Pad kann sich auf genau diesen Sitz setzen, ohne dass sich die Belegung aendert.
    pickUp(10);
    pickUp(11);
    press(11, Activate);
    frame();
    BOOST_TEST(localPlayers() == expected, boost::test_tools::per_element());
    BOOST_TEST(router().GetSlot(11) == 1u);
}

// -----------------------------------------------------------------------------------------
// BEFUND 2: Der Padbeitritt setzt Spielerslots zurueck, die der Host eingestellt hat.
//
// Der Zuordnungsbildschirm bildet seine Sitze EINMAL beim Aufbau aus allen Slots, die damals
// belegbar waren. Was der Host danach mit der Maus daran aendert, sieht er nicht - und bei
// jedem Beitritt schrieb er alle nicht eingenommenen Sitze auf die Standard-KI zurueck. Das
// ist Datenverlust genau dort, wo der Nutzer gerade etwas eingestellt hat.
// -----------------------------------------------------------------------------------------

/// Ein vom Host GESCHLOSSENER Slot bleibt geschlossen, wenn sich ein Pad woanders hinsetzt.
BOOST_FIXTURE_TEST_CASE(AClosedSlotSurvivesAPadJoiningAnotherSeat, LobbySeatFixture)
{
    enterLobby();
    BOOST_TEST_REQUIRE(GAMECLIENT.GetGameLobby()->getNumPlayers() >= 3u);

    // Der Host schliesst Slot 3 mit der Maus.
    closeSlotWithMouse(2);

    pickUp(10); // Host
    pickUp(11);
    press(11, Activate); // Sitz 2 (Slot 1) einnehmen
    for(int i = 0; i < 20; ++i)
        frame();
    BOOST_TEST_REQUIRE(localPlayers().size() == 1u);

    // Der geschlossene Slot ist immer noch geschlossen.
    BOOST_TEST((player(2).ps == PlayerState::Locked));
}

/// Eine vom Host auf SCHWER gestellte KI bleibt schwer.
BOOST_FIXTURE_TEST_CASE(AHardAiSurvivesAPadJoiningAnotherSeat, LobbySeatFixture)
{
    enterLobby();
    BOOST_TEST_REQUIRE(GAMECLIENT.GetGameLobby()->getNumPlayers() >= 3u);

    setHardAiWithMouse(2);

    pickUp(10);
    pickUp(11);
    press(11, Activate);
    for(int i = 0; i < 20; ++i)
        frame();
    BOOST_TEST_REQUIRE(localPlayers().size() == 1u);

    BOOST_TEST((player(2).ps == PlayerState::AI));
    BOOST_TEST((player(2).aiInfo.level == AI::Level::Hard));
}

/// Und beim AUFSTEHEN wird der Slot auf das zurueckgesetzt, was vor dem Beitritt dort stand -
/// nicht auf eine pauschale Vorgabe. Sonst verlaere der Host seine Einstellung eben eine
/// Bewegung spaeter.
BOOST_FIXTURE_TEST_CASE(LeavingASeatRestoresWhatWasThereBefore, LobbySeatFixture)
{
    enterLobby();
    BOOST_TEST_REQUIRE(GAMECLIENT.GetGameLobby()->getNumPlayers() >= 3u);

    // Der Host stellt AUSGERECHNET den Sitz, den gleich ein Pad nimmt, auf schwer.
    setHardAiWithMouse(1);

    pickUp(10);
    pickUp(11);
    press(11, Activate);
    for(int i = 0; i < 20; ++i)
        frame();
    BOOST_TEST_REQUIRE(localPlayers().size() == 1u);

    press(11, Back); // aufstehen
    for(int i = 0; i < 20; ++i)
        frame();
    BOOST_TEST_REQUIRE(localPlayers().empty());
    BOOST_TEST((player(1).ps == PlayerState::AI));
    BOOST_TEST((player(1).aiInfo.level == AI::Level::Hard));
}

/// Ein geschlossener Slot ist auch kein Sitzplatz mehr: wer sich dort hinsetzen koennte, haette
/// die Entscheidung des Hosts stillschweigend rueckgaengig gemacht.
BOOST_FIXTURE_TEST_CASE(APadCannotSitDownOnAClosedSlot, LobbySeatFixture)
{
    enterLobby();
    BOOST_TEST_REQUIRE(GAMECLIENT.GetGameLobby()->getNumPlayers() >= 3u);

    // ALLE Sitze ausser dem des Hosts schliessen.
    settle();
    const unsigned numSeatCards = numSeats();
    BOOST_TEST_REQUIRE(numSeatCards >= 2u);
    for(unsigned id = 1; id < numSeatCards; ++id)
        closeSlotWithMouse(id);

    pickUp(10);
    pickUp(11);
    press(11, Activate);
    for(int i = 0; i < 20; ++i)
        frame();

    BOOST_TEST(localPlayers().empty());
    for(unsigned id = 1; id < numSeatCards; ++id)
        BOOST_TEST((player(id).ps == PlayerState::Locked));
}

// -----------------------------------------------------------------------------------------
// BEFUND 3: B des Hostpads beendet die Lobby ohne Rueckfrage.
// -----------------------------------------------------------------------------------------

/// B raeumt die Partievorbereitung nicht mehr wortlos ab, sondern fragt.
BOOST_FIXTURE_TEST_CASE(BOnTheHostPadAsksBeforeLeavingTheLobby, LobbySeatFixture)
{
    enterLobby();
    pickUp(10);
    press(10, Back);
    for(int i = 0; i < 5; ++i)
        frame();

    BOOST_TEST_REQUIRE(desktopAs<dskGameLobby>() != nullptr);
    BOOST_TEST_REQUIRE(topMsgbox() != nullptr);
    // Der Fokus liegt auf der HARMLOSEN Antwort. Ein reflexhaftes B-dann-A bleibt damit in der
    // Lobby, statt sie zu verlassen.
    BOOST_TEST(focusedId(0) == ID_mbNo);

    press(10, Activate); // "Nein"
    for(int i = 0; i < 5; ++i)
        frame();
    BOOST_TEST(desktopAs<dskGameLobby>() != nullptr);
    BOOST_TEST(topMsgbox() == nullptr);
}

/// Und wer die Frage ausdruecklich bejaht, kommt auch heraus - der Weg nach draussen bleibt
/// also rein mit dem Pad begehbar.
BOOST_FIXTURE_TEST_CASE(ConfirmingTheQuestionLeavesTheLobby, LobbySeatFixture)
{
    enterLobby();
    pickUp(10);
    press(10, Back);
    for(int i = 0; i < 5; ++i)
        frame();
    BOOST_TEST_REQUIRE(topMsgbox() != nullptr);

    // Vom "Nein" zum "Ja" - eine bewusste Bewegung, kein Reflex. "Ja" steht davor, es geht
    // also zurueck; die Fokusnavigation laeuft ohne Umlauf (FocusPath::Move).
    for(int i = 0; i < 4 && focusedId(0) != ID_mbYes; ++i)
        press(10, PrevCtrl);
    BOOST_TEST_REQUIRE(focusedId(0) == ID_mbYes);

    press(10, Activate);
    for(int i = 0; i < 20; ++i)
        frame();
    BOOST_TEST(desktopAs<dskGameLobby>() == nullptr);
}

/// Ein SITZENDES Pad behaelt seine bisherige Bedeutung von B: aufstehen. Die Rueckfrage gilt
/// nur dem Host, der mit B die ganze Vorbereitung beenden wuerde.
BOOST_FIXTURE_TEST_CASE(BOnASeatedPadStillOnlyStandsUp, LobbySeatFixture)
{
    enterLobby();
    pickUp(10);
    pickUp(11);
    press(11, Activate);
    for(int i = 0; i < 20; ++i)
        frame();
    BOOST_TEST_REQUIRE(localPlayers().size() == 1u);

    press(11, Back);
    for(int i = 0; i < 20; ++i)
        frame();
    BOOST_TEST(localPlayers().empty());
    BOOST_TEST(desktopAs<dskGameLobby>() != nullptr);
    BOOST_TEST(topMsgbox() == nullptr);
}

/// HARTE RANDBEDINGUNG, andere Richtung: wer nur Maus und Tastatur benutzt, soll den
/// Zuordnungsbildschirm gar nicht erst sehen. Seine Knoepfe tun auf einen Mausklick
/// ABSICHTLICH nichts (AMouseClickOnASeatCardTakesNoSeat) - sichtbar und wirkungslos waere
/// davon die schlechtere Haelfte.
BOOST_FIXTURE_TEST_CASE(TheSeatPanelOnlyAppearsWhenAPadIsPluggedIn, LobbySeatFixture)
{
    enterLobby();
    settle(5);
    auto* lobby = desktopAs<dskGameLobby>();
    BOOST_TEST_REQUIRE(lobby != nullptr);
    // Die Karten GIBT es - der Bildschirm baut sie einmal auf -, sie sind nur nicht zu sehen.
    auto* card = lobby->GetCtrl<Window>(ID_btSeat + 1);
    BOOST_TEST_REQUIRE(card != nullptr);
    BOOST_TEST(!card->IsVisible());
    BOOST_TEST(!lobby->GetCtrl<Window>(ID_txtSeats)->IsVisible());

    // Ein Pad wird angesteckt - noch nicht einmal benutzt.
    connect(10);
    frame();
    BOOST_TEST(card->IsVisible());
    BOOST_TEST(lobby->GetCtrl<Window>(ID_txtSeats)->IsVisible());

    // Und wieder abgezogen: der Bildschirm gehoert wieder der Maus allein.
    disconnect(10);
    frame();
    BOOST_TEST(!card->IsVisible());
}

// -----------------------------------------------------------------------------------------
// BEFUND A: Der Host schliesst einen Sitz, auf dem ein Pad SITZT.
//
// Das ist die Gegenrichtung zu BEFUND 2 und von dessen Korrektur nicht erfasst: dort ging es
// darum, dass der Padbeitritt die Einstellungen des Hosts zurueckschrieb, hier darum, dass die
// Einstellung des Hosts den Sitzzustand nicht erreicht. Uebrig blieb ein Slot, der
// gleichzeitig geschlossen UND lokaler Zusatzspieler war - und genau daran scheitert
// GameClient::SetupLocalPlayers beim Spielstart (OnError -> Stop, die Partie ist weg).
// -----------------------------------------------------------------------------------------

/// Der Sitz wird geraeumt, und die Sitzkarte sagt es.
BOOST_FIXTURE_TEST_CASE(ClosingASeatAPadSitsOnVacatesThatSeat, LobbySeatFixture)
{
    enterLobby();
    BOOST_TEST_REQUIRE(GAMECLIENT.GetGameLobby()->getNumPlayers() >= 3u);
    pickUp(10);
    pickUp(11);
    press(11, Activate);
    settle();
    const std::vector<uint8_t> seated{1};
    BOOST_TEST_REQUIRE(localPlayers() == seated, boost::test_tools::per_element());
    BOOST_TEST_REQUIRE(seatText(1) == helpers::format(_("Seat %1%: pad %2%"), 2, 11));

    // Der Host schliesst AUSGERECHNET den Sitz, auf dem das Pad sitzt.
    closeSlotWithMouse(1);
    settle();

    // Der Host hat entschieden - der Slot bleibt geschlossen.
    BOOST_TEST((player(1).ps == PlayerState::Locked));
    // Und der Sitzzustand sagt dasselbe wie der Spielzustand.
    BOOST_TEST(localPlayers().empty());
    BOOST_TEST(seatText(1) == helpers::format(_("Seat %1%: closed"), 2));
}

/// Und die Partie laesst sich danach starten. DAS ist der eigentliche Schaden gewesen.
BOOST_FIXTURE_TEST_CASE(TheGameStillStartsAfterTheHostClosedAnOccupiedSeat, LobbySeatFixture)
{
    enterLobby();
    pickUp(10);
    pickUp(11);
    press(11, Activate);
    settle();
    BOOST_TEST_REQUIRE(localPlayers().size() == 1u);

    closeSlotWithMouse(1);
    settle();

    takeOverTheClientInterface();
    press(10, PadButton::Start); // der Host startet, mit dem Pad
    BOOST_TEST(frameUntil([] { return GAMECLIENT.GetState() == ClientState::Loading; }),
               "the game to start after the host closed an occupied seat");
    BOOST_TEST(ci().numErrors == 0u);
}

// -----------------------------------------------------------------------------------------
// BEFUND B: Jedes Pad kann die Partie fuer alle starten.
//
// Msg_PadCommand prueft den CLIENT (isHost), nicht den handelnden SLOT. Das ist die
// Asymmetrie zu B, wo ausdruecklich verhindert wird, dass ein sitzloses Zweitpad fuer alle
// hinausgeht - und Starten ist mindestens so folgenreich wie Verlassen.
// -----------------------------------------------------------------------------------------

BOOST_FIXTURE_TEST_CASE(AGuestPadCannotStartTheGameForEveryone, LobbySeatFixture)
{
    enterLobby();
    pickUp(10);
    pickUp(11);
    press(11, Activate);
    settle();
    BOOST_TEST_REQUIRE(localPlayers().size() == 1u);
    BOOST_TEST_REQUIRE(router().GetSlot(11) == 1u);

    takeOverTheClientInterface();
    press(11, PadButton::Start); // das GASTPAD auf Sitz 2
    for(unsigned i = 0; i < 200; ++i)
        frame();
    BOOST_TEST((GAMECLIENT.GetState() == ClientState::Config));
    BOOST_TEST(desktopAs<dskGameLobby>() != nullptr);

    // GEGENPROBE: das Pad des Hostplatzes startet sehr wohl - der Weg bleibt mit dem Pad
    // begehbar, er gehoert nur nicht mehr jedem.
    press(10, PadButton::Start);
    BOOST_TEST(frameUntil([] { return GAMECLIENT.GetState() == ClientState::Loading; }),
               "the host pad to still be able to start the game");
}

// -----------------------------------------------------------------------------------------
// BEFUND D: Spielertausch wird im Sitzzustand nicht nachgefuehrt.
// -----------------------------------------------------------------------------------------

/// Nach einem Tausch zeigt der Sitz auf den Slot, auf dem sein Spieler JETZT sitzt. Sonst
/// setzt das Aufstehen den falschen Slot zurueck und laesst auf dem verlassenen die Dummy-KI
/// stehen - genau das, was ApplyLocalSeats laut eigenem Kommentar vermeiden will.
BOOST_FIXTURE_TEST_CASE(SwappingPlayersMovesTheSeatWithThem, LobbySeatFixture)
{
    enterLobby();
    BOOST_TEST_REQUIRE(GAMECLIENT.GetGameLobby()->getNumPlayers() >= 3u);
    pickUp(10);
    pickUp(11);
    press(11, Activate);
    settle();
    const std::vector<uint8_t> before{1};
    BOOST_TEST_REQUIRE(localPlayers() == before, boost::test_tools::per_element());

    // Der Host tauscht sich mit genau dem Slot, auf dem das Pad sitzt.
    swapWithHostUsingMouse(1);
    settle();
    const std::vector<uint8_t> after{0};
    BOOST_TEST_REQUIRE(localPlayers() == after, boost::test_tools::per_element());

    // Aufstehen: der VERLASSENE Slot faellt auf die gewoehnliche KI zurueck.
    press(11, Back);
    settle();
    BOOST_TEST(localPlayers().empty());
    BOOST_TEST((player(0).ps == PlayerState::AI));
    BOOST_TEST((player(0).aiInfo.type == AI::Type::Default));
}

BOOST_AUTO_TEST_SUITE_END()

