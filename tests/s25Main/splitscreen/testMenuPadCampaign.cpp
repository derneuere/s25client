// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

// DAS ABNAHMEKRITERIUM DIESER PHASE:
// Eine KAMPAGNENMISSION laesst sich ausschliesslich mit Padeingaben auswaehlen und starten -
// keine Maus, keine Tastatur.
//
// Vorher war das unmoeglich, und zwar aus einem Grund, der nicht in der Fokusnavigation lag:
// dskCampaignSelection und dskCampaignMissionSelection erben von Desktop und ueberschrieben
// WantsPadInput() nicht. WindowManager::PumpPadInput steigt genau darauf sofort aus, holt also
// fuer diese beiden Bildschirme gar keine Padereignisse ab. Man kam mit dem Pad HINEIN
// (dskSinglePlayer ist freigeschaltet) und dann ging nichts mehr - kein Fokusrahmen, kein A,
// kein B, kein Zurueck.
//
// DIE EINZIGE NAHT liegt wie in allen Nachweisen dieser Reihe beim Treiber: der Test schreibt
// PadEvents in MockupVideoDriver::padEvents_ und laesst danach nur Frames laufen. Nicht benutzt
// und nicht erlaubt: FocusPath, Window::Activate(), Desktop::Msg_ButtonClick(id),
// Msg_Group_ButtonClick, ctrlTable::SetSelection, ctrlMapSelection::setSelection.
//
// WARUM EINE EIGENE KAMPAGNE STATT DER AUSGELIEFERTEN: die beiden mitgelieferten Kampagnen
// (roman, world) zeigen mit ihrem mapFolder in die ORIGINALEN S2-Daten (<RTTR_GAME>/DATA/MAPS
// bzw. MAPS2, siehe data/RTTR/campaigns/*/campaign.lua). Die liegen nicht im Repository. Ein
// Nachweis, der daran haengt, waere auf jedem Rechner ohne S2-Installation wertlos - entweder
// rot oder still uebersprungen. Benutzerkampagnen unter <RTTR_USERDATA>/campaigns sind eine
// PRODUKTIVFUNKTION: dskCampaignSelection::loadCampaigns liest beide Ordner in derselben
// Schleife. Der Test legt dort eine eigene Kampagne mit zwei Missionen an und geht danach
// exakt denselben Weg wie ein Spieler.
//
// WAS DIESER FALL NICHT ABDECKT und was deshalb weiter unten in testMapSelectionNavigation.cpp
// steht: die zweite Bauform von dskCampaignMissionSelection, die Auswahl auf einer Weltkarte
// (ctrlMapSelection). Sie braucht SETUP990.LBM/WORLD.LBM/WORLDMSK.LBM aus den Originaldaten und
// ist hier nicht aufbaubar.

#include "LocalGameFixture.h"
#include "MenuPadFixture.h"
#include "RttrConfig.h"
#include "Settings.h"
#include "WindowManager.h"
#include "controls/ctrlGroup.h"
#include "controls/ctrlTable.h"
#include "desktops/dskCampaignMissionSelection.h"
#include "desktops/dskCampaignSelection.h"
#include "desktops/dskGameLobby.h"
#include "desktops/dskMainMenu.h"
#include "desktops/dskSinglePlayer.h"
#include "driver/KeyEvent.h"
#include "driver/MouseCoords.h"
#include "files.h"
#include "ingameWindows/IngameWindow.h"
#include "network/GameClient.h"
#include "test/testConfig.h"
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

namespace bfs = boost::filesystem;

namespace {

constexpr PadButton Activate = PadButton::A;
constexpr PadButton Back = PadButton::B;
constexpr PadButton NextCtrl = PadButton::RightShoulder;
constexpr PadButton Down = PadButton::DpadDown;
constexpr PadButton Up = PadButton::DpadUp;

/// dskSinglePlayer legt die Knoepfe mit den Ids 3,7,5,6,4,8 an; die Fokusnavigation geht in
/// ID-Reihenfolge, also 3,4,5,... "Campaigns" ist die 5 (dskSinglePlayer.cpp:43). Vom ersten
/// fokussierbaren Knopf (3) sind das ZWEI Schritte.
constexpr unsigned STEPS_TO_CAMPAIGNS = 2;

/// Der Name der Testkampagne. Bewusst NICHT uebersetzt und bewusst keine feste Zeilennummer:
/// neben ihr koennen die ausgelieferten Kampagnen in der Tabelle stehen, und deren Namen
/// haengen an der Sprache. Genau daran ist der Nachweis der Kartenauswahl schon einmal unter
/// pl_PL gescheitert (testMenuPadAcceptance.cpp).
const std::string kCampaignName = "PadTestCampaign";

/// Ein Tastendruck ueber den Eintrittspunkt des WindowManagers - genau die Methode, die der
/// echte SDL-Treiber ueber VideoDriverLoaderInterface ruft.
void pressKey(const KeyType kt)
{
    WINDOWMANAGER.Msg_KeyDown(KeyEvent{kt});
}

/// Mausklick ueber dieselben Eintrittspunkte.
void mouseClick(const Position& pos)
{
    MouseCoords mc(pos);
    WINDOWMANAGER.Msg_MouseMove(mc);
    mc.ldown = true;
    WINDOWMANAGER.Msg_LeftDown(mc);
    mc.ldown = false;
    WINDOWMANAGER.Msg_LeftUp(mc);
}

/// Ein leerer Desktop, der KEINE Padereignisse will - der Platzhalter fuer den Ladebildschirm.
struct EmptyDesktop : Desktop
{
    EmptyDesktop() : Desktop(nullptr) {}
};

struct CampaignPadFixture : rttr::test::LocalGameFixture, rttr::test::MenuPadFixture
{
    bfs::path campaignDir;

    CampaignPadFixture()
    {
        // <RTTR_USERDATA> zeigt dank LocalGameFixture bereits auf einen Wegwerfordner.
        campaignDir = RTTRCONFIG.ExpandPath(s25::folders::campaignsUser) / "padtest";
        bfs::create_directories(campaignDir);

        // Zwei Missionen aus derselben Testkarte. Unterscheidbar sind sie ueber ihren
        // DATEINAMEN - und genau den prueft der Abnahmefall am Ende ab
        // (GAMECLIENT.GetMapPath()). Ohne diese Zeile waere auch eine Umsetzung gruen, die
        // immer Mission 0 startet.
        const bfs::path srcMap = rttr::test::rttrBaseDir / "tests" / "testData" / "maps" / "LuaFunctions.SWD";
        for(const std::string& mission : {"PadMissionOne", "PadMissionTwo"})
        {
            bfs::copy_file(srcMap, campaignDir / (mission + ".SWD"));
            // Eine Kampagnenmission MUSS ein Lua-Skript neben der Karte haben:
            // loadCampaigns wirft die ganze Kampagne weg, wenn eines fehlt
            // (dskCampaignSelection.cpp:241-245).
            std::ofstream missionLua((campaignDir / (mission + ".lua")).string());
            missionLua << "function getRequiredLuaVersion()\n    return 1\nend\n";
        }

        std::ofstream campaignLua((campaignDir / "campaign.lua").string());
        campaignLua << "function getRequiredLuaVersion()\n    return 1\nend\n"
                    << "campaign = {\n"
                    << "    version = 1,\n"
                    << "    author = \"Test\",\n"
                    << "    name = \"" << kCampaignName << "\",\n"
                    << "    shortDescription = \"Pad\",\n"
                    << "    longDescription = \"Pad only\",\n"
                    << "    maxHumanPlayers = 1,\n"
                    << "    difficulty = \"easy\",\n"
                    << "    mapFolder = \"\",\n"
                    << "    luaFolder = \"\",\n"
                    << "    maps = { \"PadMissionOne.SWD\", \"PadMissionTwo.SWD\" }\n"
                    << "}\n";
    }

    /// Ein Frame in derselben Reihenfolge wie GameManager::Run: erst Client und Server, dann
    /// zeichnen.
    void frame() override
    {
        video.tickCount_ += frameMs;
        GAMECLIENT.Run();
        GAMESERVER.Run();
        WINDOWMANAGER.Draw();
    }

    /// Frames laufen lassen, bis die Bedingung wahr ist.
    ///
    /// Mit ECHTER Wartezeit je Frame, und das ist kein Schoenheitsfehler: dskCampaignSelection
    /// laedt seine Kampagnen verzoegert ueber AddTimer(..., 1ms), und ctrlTimer misst mit einem
    /// echten Timer und nicht mit MockupVideoDriver::tickCount_. Eine enge Schleife ohne
    /// Wanduhrzeit sieht die Tabelle nie gefuellt.
    template<class T_Pred>
    bool frameUntil(const T_Pred& isDone, const unsigned maxFrames = 2000)
    {
        for(unsigned i = 0; i < maxFrames && !isDone(); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            frame();
        }
        return isDone();
    }

    static ctrlTable* campaignTable()
    {
        auto* dsk = desktopAs<dskCampaignSelection>();
        BOOST_TEST_REQUIRE(dsk != nullptr);
        const auto tables = dsk->GetCtrls<ctrlTable>();
        BOOST_TEST_REQUIRE(tables.size() == 1u);
        return tables.front();
    }

    /// Die Zeile der Testkampagne, gesucht ueber ihren Namen in Spalte 0.
    static std::optional<unsigned> rowOfTestCampaign(const ctrlTable& table)
    {
        for(unsigned short row = 0; row < table.GetNumRows(); ++row)
        {
            if(table.GetItemText(row, 0) == kCampaignName)
                return static_cast<unsigned>(row);
        }
        return std::nullopt;
    }

    /// Fehlen die originalen S2-Daten, sind die AUSGELIEFERTEN Kampagnen kaputt und
    /// loadCampaigns stellt eine Fehlerbox ueber den Bildschirm. Sie mit dem Pad wegzuraeumen
    /// ist Teil des Nachweises: vorher war auch DAS unmoeglich, weil der Bildschirm gar keine
    /// Padereignisse bekam.
    void dismissMessageBoxes(const PadDeviceId pad)
    {
        for(unsigned i = 0; i < 5 && WINDOWMANAGER.GetTopMostWindow(); ++i)
            press(pad, Back);
        BOOST_TEST_REQUIRE(WINDOWMANAGER.GetTopMostWindow() == nullptr);
    }

    /// Hauptmenue -> Einzelspieler -> Kampagnen -> gefuellte Tabelle, rein mit dem Pad.
    /// Liefert die Tabelle, auf der der Fokus dann steht.
    ctrlTable* enterCampaignTable(const PadDeviceId pad)
    {
        toCampaignSelection(pad);
        auto* table = campaignTable();
        BOOST_TEST_REQUIRE(frameUntil([table] { return table->GetNumRows() > 0; }),
                           "the campaign table to be filled");
        dismissMessageBoxes(pad);
        // GENAU EIN Frame mehr. Die Tabelle entsteht in Msg_PaintBefore (ctrlTimer), der
        // Einstiegspunkt wird in MenuPadInput::Pump ausgewertet - und das laeuft im selben
        // Frame VORHER. Der Fokus zieht also erst im naechsten Frame nach.
        frame();
        // Der Fokus liegt auf der TABELLE und nicht auf "Zurueck" - obwohl es die Tabelle im
        // Moment des Bildschirmwechsels noch gar nicht gab
        // (dskCampaignSelection::GetPadEntryCtrl, MenuPadInput::focusUntouched_).
        BOOST_TEST_REQUIRE(focused(0) == static_cast<Window*>(table));
        return table;
    }

    /// Mit dem Steuerkreuz auf die gewuenschte Zeile fahren. Das Control verbraucht hoch/runter
    /// selbst als Wertaenderung (ctrlTable::StepValue), es wandert also kein Fokus.
    void selectRow(const PadDeviceId pad, ctrlTable& table, const unsigned targetRow)
    {
        // ctrlTable belegt selection_ im Konstruktor mit (unsigned)-1. Als std::optional ist
        // das BELEGT, has_value() ist also von Anfang an true, und der Wert liegt ausserhalb
        // jeder Zeilenzahl. Bestehender Fehler im Baum, bewusst nicht angefasst (er wuerde das
        // Verhalten des Tastaturspielers verschieben). RUNTER laeuft darauf ueber und landet
        // auf Zeile 0, HOCH prallt an SetSelection ab - der Spieler faengt hier also immer mit
        // Runter an, und genau das tut der Test auch.
        const auto initial = table.GetSelection();
        if(!initial || *initial >= table.GetNumRows())
            press(pad, Down);
        for(unsigned i = 0; i < table.GetNumRows() + 2u && table.GetSelection() != targetRow; ++i)
        {
            const auto sel = table.GetSelection();
            press(pad, (!sel || *sel < targetRow) ? Down : Up);
        }
        BOOST_TEST_REQUIRE((table.GetSelection() == targetRow));
    }

    /// Hauptmenue -> Einzelspieler -> Kampagnen, rein mit dem Pad.
    void toCampaignSelection(const PadDeviceId pad)
    {
        WINDOWMANAGER.Switch(std::make_unique<dskMainMenu>());
        frame();
        BOOST_TEST_REQUIRE(desktopAs<dskMainMenu>() != nullptr);

        pickUp(pad);
        BOOST_TEST_REQUIRE(router().GetSlot(pad) == 0u);
        press(pad, Activate);
        BOOST_TEST_REQUIRE(desktopAs<dskSinglePlayer>() != nullptr);

        frame();
        pressN(pad, NextCtrl, STEPS_TO_CAMPAIGNS);
        BOOST_TEST_REQUIRE(focusedId(0) == 5u); // "Campaigns"
        press(pad, Activate);
        BOOST_TEST_REQUIRE(desktopAs<dskCampaignSelection>() != nullptr);
    }
};

} // namespace

BOOST_AUTO_TEST_SUITE(MenuPadCampaignTests)

/// DER ABNAHMEFALL: Hauptmenue -> Einzelspieler -> Kampagnen -> Kampagne -> Mission -> Lobby ->
/// Spiel, ohne eine einzige Maus- oder Tastatureingabe.
///
/// DIE EINE LUECKE, benannt und nicht ueberbrueckt: dskGameLoader braucht die originalen
/// S2-Texturen und ist im Test unerreichbar. Ab dort uebernimmt der Test dieselben zwei
/// Aufrufe, die LocalGameFixture::startGame an derselben Stelle macht. Alles davor ist
/// Produktivcode.
BOOST_FIXTURE_TEST_CASE(ACampaignMissionIsStartedWithPadInputOnly, CampaignPadFixture)
{
    constexpr PadDeviceId pad = 41;

    // AUSGANGSLAGE: nichts ist vorbereitet.
    BOOST_TEST_REQUIRE(focused(0) == nullptr);
    BOOST_TEST_REQUIRE(router().GetSlot(pad) == PadRouter::NoSlot);

    // --- 1. bis zur Kampagnenauswahl ---------------------------------------------------------
    SETTINGS.server.localPort = static_cast<uint16_t>(rttr::test::randomValue(1024, 49151));
    toCampaignSelection(pad);

    // --- 2. Die Tabelle fuellt sich (verzoegert, siehe frameUntil) ----------------------------
    ctrlTable* table = enterCampaignTable(pad);
    const auto targetRow = rowOfTestCampaign(*table);
    BOOST_TEST_REQUIRE(!!targetRow, "the test campaign to show up in the table");

    // --- 3. Kampagne waehlen ------------------------------------------------------------------
    selectRow(pad, *table, *targetRow);

    // A auf der Tabelle ist "diese Kampagne" - derselbe Weg wie der Doppelklick
    // (ctrlTable::Activate -> Msg_TableChooseItem -> showCampaignMissionSelectionScreen).
    press(pad, Activate);
    auto* missionDsk = desktopAs<dskCampaignMissionSelection>();
    BOOST_TEST_REQUIRE(missionDsk != nullptr);
    frame();

    // --- 4. Mission waehlen -------------------------------------------------------------------
    // Der Fokus liegt auf dem ERSTEN MISSIONSKNOPF und nicht auf "Zurueck" (ID 0 waere sonst
    // der erste Kandidat gewesen).
    const auto groups = missionDsk->GetCtrls<ctrlGroup>();
    BOOST_TEST_REQUIRE(groups.size() == 1u);
    auto* firstMission = groups.front()->GetCtrl<Window>(0);
    auto* secondMission = groups.front()->GetCtrl<Window>(1);
    BOOST_TEST_REQUIRE(firstMission != nullptr);
    BOOST_TEST_REQUIRE(secondMission != nullptr);
    BOOST_TEST_REQUIRE(focused(0) == firstMission);

    // Ein Schritt nach unten steht auf der ZWEITEN Mission. Das ist die unbequeme Stelle: der
    // Test startet bewusst NICHT die erste.
    press(pad, Down);
    BOOST_TEST_REQUIRE(focused(0) == secondMission);
    press(pad, Activate);

    // --- 5. Ab hier laeuft eine echte Loopbackverbindung --------------------------------------
    BOOST_TEST_REQUIRE(frameUntil([this] { return desktopAs<dskGameLobby>() != nullptr; }),
                       "the lobby to appear after hosting the campaign mission");
    frame();

    // DIE UNBEQUEME ZEILE: der Client laedt die GEWAEHLTE Mission und nicht irgendeine.
    // Der ORDNER ist hier nicht der Kampagnenordner: der Client legt die vom Server empfangene
    // Karte unter <RTTR_USERDATA>/MAPS ab. Der DATEINAME bleibt und ist das, worauf es ankommt.
    BOOST_TEST(GAMECLIENT.GetMapPath().filename() == bfs::path("PadMissionTwo.SWD"));

    // --- 6. "Spiel starten", mit dem Pad ------------------------------------------------------
    BOOST_TEST_REQUIRE(focusedId(0) == 0u); // ID_btStartGame
    GAMECLIENT.SetInterface(&ci());
    press(pad, Activate);
    BOOST_TEST_REQUIRE(frameUntil([] { return GAMECLIENT.GetState() == ClientState::Loading; }, 20000),
                       "the countdown to finish and loading to start");

    // --- 7. DIE EINE LUECKE: dskGameLoader ----------------------------------------------------
    WINDOWMANAGER.Switch(std::make_unique<EmptyDesktop>());
    frame();
    GAMECLIENT.GameLoaded();
    pumpUntil([] { return GAMECLIENT.GetState() == ClientState::Game; }, "the client to enter the game state");
    BOOST_TEST_REQUIRE(!!ci().game);
    GAMECLIENT.OnGameStart();
    BOOST_TEST(ci().game->IsStarted());
}

/// Der Rueckweg. Ohne ihn waeren beide Bildschirme Sackgassen, aus denen nur die Maus
/// herausfuehrt - genau der Zustand vor dieser Phase.
BOOST_FIXTURE_TEST_CASE(BLeadsBackOutOfBothCampaignScreens, CampaignPadFixture)
{
    constexpr PadDeviceId pad = 42;

    ctrlTable* table = enterCampaignTable(pad);
    const auto targetRow = rowOfTestCampaign(*table);
    BOOST_TEST_REQUIRE(!!targetRow);
    selectRow(pad, *table, *targetRow);
    press(pad, Activate);
    BOOST_TEST_REQUIRE(desktopAs<dskCampaignMissionSelection>() != nullptr);

    // B auf dem Missionsbildschirm -> zurueck zur Kampagnenauswahl.
    press(pad, Back);
    BOOST_TEST_REQUIRE(desktopAs<dskCampaignSelection>() != nullptr);

    // B auf der Kampagnenauswahl -> zurueck zum Einzelspielermenue.
    press(pad, Back);
    BOOST_TEST(desktopAs<dskSinglePlayer>() != nullptr);
}

/// GEGENPROBE ZUR ZUSTELLUNG: nach dem Wechsel auf die Kampagnenauswahl bleibt KEIN Padereignis
/// beim Treiber liegen.
///
/// Genau das war vorher der Fall: WindowManager::PumpPadInput steigt fuer einen Desktop ohne
/// WantsPadInput aus, ohne FetchPadEvents zu rufen. Die Ereignisse blieben in der Warteschlange
/// des Treibers stehen und wurden dem NAECHSTEN Bildschirm zugestellt, der Padeingaben wollte -
/// ein Knopfdruck von Bildschirm A rutschte in Bildschirm B.
BOOST_FIXTURE_TEST_CASE(NoPadEventIsLeftBehindOnTheCampaignScreen, CampaignPadFixture)
{
    constexpr PadDeviceId pad = 43;

    toCampaignSelection(pad);
    BOOST_TEST_REQUIRE(video.padEvents_.empty());

    // Ein paar Eingaben, die der Bildschirm selbst beantworten muss.
    pressN(pad, NextCtrl, 3);
    BOOST_TEST(video.padEvents_.empty());
}

/// DIE SCHAERFSTE REGRESSIONSKONTROLLE DIESER PHASE.
///
/// ctrlTable::Msg_KeyDown prueft bewusst KEINEN Fokus (ctrlTable.h). Die Pfeiltasten muessen
/// fuer den Maus-und-Tastatur-Spieler unveraendert funktionieren - auch dann, wenn nebenan ein
/// Pad in Benutzung ist und sein Fokusrahmen ganz woanders steht. Eine Umsetzung, die
/// Msg_KeyDown "aufraeumt" und an den Fokus koppelt, wird HIER rot und nur hier; dskSelectMap,
/// dskLAN und dskLobby haengen an derselben Stelle.
///
/// Der Fall laeuft in drei Schichten: ohne Pad, mit gestecktem aber unbenutztem Pad, und mit
/// AKTIVEM Pad, dessen Fokus auf einem anderen Control steht.
BOOST_FIXTURE_TEST_CASE(ArrowKeysAndMouseKeepWorkingOnTheCampaignTable, CampaignPadFixture)
{
    constexpr PadDeviceId pad = 44;

    // --- Schicht 1: KEIN Pad ------------------------------------------------------------------
    WINDOWMANAGER.Switch(std::make_unique<dskCampaignSelection>(
      CreateServerInfo(ServerType::Local, SETTINGS.server.localPort, "PadTest")));
    frame();
    auto* table = campaignTable();
    BOOST_TEST_REQUIRE(frameUntil([table] { return table->GetNumRows() > 0; }), "the campaign table to be filled");
    for(unsigned i = 0; i < 5 && WINDOWMANAGER.GetTopMostWindow(); ++i)
    {
        WINDOWMANAGER.GetTopMostWindow()->Close();
        frame();
    }
    BOOST_TEST_REQUIRE(focused(0) == nullptr); // wirklich kein Pad im Spiel

    // Genau das dokumentierte Verhalten des Ist-Zustands: selection_ startet als (unsigned)-1,
    // das erste Runter laeuft darauf ueber und landet auf Zeile 0.
    pressKey(KeyType::Down);
    BOOST_TEST_REQUIRE(!!table->GetSelection());
    BOOST_TEST(*table->GetSelection() == 0u);
    if(table->GetNumRows() > 1)
    {
        pressKey(KeyType::Down);
        BOOST_TEST(*table->GetSelection() == 1u);
        pressKey(KeyType::Up);
        BOOST_TEST(*table->GetSelection() == 0u);
    }
    // Am oberen Rand passiert nichts mehr.
    pressKey(KeyType::Up);
    BOOST_TEST(*table->GetSelection() == 0u);

    // --- Schicht 2: Pad GESTECKT, aber nicht benutzt -------------------------------------------
    connect(pad);
    frame();
    BOOST_TEST_REQUIRE(focused(0) == nullptr); // kein Fokusrahmen
    pressKey(KeyType::Down);
    BOOST_TEST(*table->GetSelection() == (table->GetNumRows() > 1 ? 1u : 0u));

    // --- Schicht 3: Pad AKTIV, Fokus WOANDERS --------------------------------------------------
    tap(pad, PadButton::Start);
    frame();
    BOOST_TEST_REQUIRE(router().GetSlot(pad) == 0u);
    // Fokus vom Tisch weg auf ein anderes Control fahren.
    for(unsigned i = 0; i < 10 && focused(0) == static_cast<Window*>(table); ++i)
        press(pad, NextCtrl);
    BOOST_TEST_REQUIRE(focused(0) != nullptr);
    BOOST_TEST_REQUIRE(focused(0) != static_cast<Window*>(table));

    const auto selBefore = table->GetSelection();
    pressKey(KeyType::Up);
    // DIE ZEILE, um die es geht: die Tabelle hat sich bewegt, obwohl der Padfokus woanders steht.
    BOOST_TEST((table->GetSelection() != selBefore || table->GetNumRows() == 1u));
    // Und der Padfokus ist dabei stehen geblieben - die Tastatur bewegt ihn nicht.
    BOOST_TEST(focused(0) != static_cast<Window*>(table));

    // --- Und die Maus waehlt weiterhin eine Zeile ----------------------------------------------
    const auto selBeforeClick = table->GetSelection();
    mouseClick(table->GetDrawPos() + Position(20, 40));
    frame();
    BOOST_TEST((table->GetSelection() != selBeforeClick || table->GetNumRows() == 1u));
}

BOOST_AUTO_TEST_SUITE_END()
