// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

// DAS KREISMENUE - Phase 13.
//
// Der Auftraggeber hat es ZWEIMAL bestellt, und beim zweiten Mal mit der Begruendung, auf die
// es ankommt: "Einfach weil es Benutzerfreundlicher ist und dann haben wir nur ein Paradigma."
// Dazu die zweite Bitte: "Ich verstehe noch nicht ganz wie ich die ganzen Symbole an und
// ausschalten kann die dann auf der Karte sind wenn ich einfach nur ein wenig zuschauen will."
//
// Diese Datei bewacht die drei Teile des Abnahmekriteriums, und zwar AUSSCHLIESSLICH ueber den
// produktiven Weg: Padereignis in die Warteschlange des Treibers (PadFeeder), dann
// dskGameInterface::UpdateInput. Kein Fall hier ruft OpenRing, RingTurnSector oder
// EnterWatchOnly selbst auf, um zu erzeugen, was der Spieler angeblich erlebt.
//
//  (a) Ein Padspieler waehlt ein Gebaeude ueber den RING und SETZT es - und der Klartextkasten
//      aus Phase 9 zeigt dabei die Beschreibung des gerade gewaehlten Gebaeudes.
//  (b) Das Back-Systemmenue ist DERSELBE Ring, mit allen seinen Punkten - die Symbolschalter
//      eingeschlossen.
//  (c) Im Splitscreen gehoert jeder Ring dem Sitzplatz, der ihn geoeffnet hat. Vier Spieler
//      koennen gleichzeitig je ihren eigenen offen haben.
//
// Dazu die drei Fragen, die kein anderer Fall stellt:
//  - Wird der Ring WIRKLICH GEZEICHNET? Gemessen am OpenGL-Aufruf selbst, mit ausgelesener
//    Geometrie (Sektorzahl, Winkel, Radius, Farbe).
//  - Zeigt der STICK wirklich, und faellt der Fokus auf den getroffenen Sektor?
//  - Bleibt die Bauhilfe AUS, wenn ein Mensch sie ausgeschaltet hat? (Das war der wahre Grund
//    fuer "ich kriege die Symbole nicht aus": PadOpenActionWindow erzwang sie bei jedem A.)

#include "GamePlayer.h"
#include "Loader.h"
#include "PointOutput.h"
#include "RttrForeachPt.h"
#include "WindowManager.h"
#include "buildings/nobBaseWarehouse.h"
#include "buildings/noBuildingSite.h"
#include "controls/ctrlBuildingIcon.h"
#include "controls/ctrlButton.h"
#include "controls/ctrlGroup.h"
#include "controls/ctrlTab.h"
#include "desktops/PlayerView.h"
#include "desktops/dskGameInterface.h"
#include "driver/PadEvent.h"
#include "drivers/VideoDriverWrapper.h"
#include "ingameWindows/IngameWindow.h"
#include "ingameWindows/iwAction.h"
#include "ingameWindows/iwPadSystemMenu.h"
#include "input/FocusPath.h"
#include "input/PadRing.h"
#include "input/PlayerBrief.h"
#include "network/GameClient.h"
#include "nodeObjs/noFlag.h"
#include "world/GameWorld.h"
#include "world/GameWorldViewer.h"
#include "PadFixture.h"
#include "PadGameFixture.h"
#include "NodalObjectTypes.h"
#include "gameTypes/BuildingQuality.h"
#include "gameTypes/BuildingType.h"
#include "gameTypes/RoadBuildMode.h"
#include "gameData/const_gui_ids.h"
#include "Settings.h"
#include "RttrConfig.h"
#include "files.h"
#include <boost/algorithm/string/join.hpp>
#include <boost/filesystem.hpp>
#include "driver/MouseCoords.h"
#include "helpers/EnumRange.h"
#include "TvDisplay.h"
#include "ogl/glFont.h"
#include <rttr/test/LocaleResetter.hpp>
#include <rttr/test/stubFunction.hpp>
#include <s25util/colors.h>
#include <s25util/warningSuppression.h>
#include <glad/glad.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

using namespace rttr::test;

namespace {

/// Ein Punkt, auf dem GENAU DIESER Spieler die gewuenschte Groesse bauen kann.
MapPoint findBuildSpotFor(const GameWorldBase& world, const GameWorldViewer& viewer, const BuildingQuality minBQ)
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

/// "Der Spieler nimmt das Pad in die Hand und setzt sich auf DIESEN Platz."
///
/// Anstecken, ein Frame, Slot zuweisen, noch ein Frame - genau die Folge, die auch
/// testPadKeyHints benutzt. Ohne das Anstecken kennt der Router das Geraet nicht und
/// AssignSlot schlaegt fehl.
template<class T_Fixture>
void seatPad(T_Fixture& f, const PadDeviceId dev, const unsigned viewIdx)
{
    f.pads.connect(dev);
    f.step(16);
    BOOST_TEST_REQUIRE(f.dsk->GetPadRouter().AssignSlot(dev, viewIdx));
    f.step(16);
}

MapPoint hqFlagOf(const GameWorldBase& world, const unsigned char player)
{
    const MapPoint hqPos = world.GetPlayer(player).GetHQPos();
    const auto* hq = world.GetSpecObj<nobBaseWarehouse>(hqPos);
    BOOST_TEST_REQUIRE(hq != nullptr);
    return hq->GetFlagPos();
}

/// DER MITSCHRIEB DES RINGZEICHNERS.
///
/// dskGameInterface::DrawRing gibt jeden Sektor als eigenen Dreiecksstreifen aus, und zwar in
/// genau der Folge glVertexPointer -> glTexCoordPointer -> BindTexture -> glColor4ub ->
/// glDrawArrays (dieselbe Folge wie glArchivItem_Bitmap::Draw). Ein Tap auf glVertexPointer
/// liest dabei nicht nur MIT, DASS gezeichnet wurde, sondern WAS: der Zeiger ist bei
/// glDrawArrays noch gueltig, die Eckpunkte lassen sich also zurueckrechnen.
///
/// Freie Funktionen mit globalem Zustand, weil ein OpenGL-Funktionszeiger keine Fangliste
/// tragen kann. RTTR_STUB_FUNCTION schreibt den alten Zeiger beim Verlassen des Blocks zurueck.
namespace ringTap {
    RTTR_IGNORE_DIAGNOSTIC("-Wmissing-declarations")

    struct Batch
    {
        GLenum mode;
        unsigned color;
        std::vector<PointF> verts;
    };
    std::vector<Batch> batches;
    const GLfloat* curVerts = nullptr;
    unsigned curColor = 0;

    void reset()
    {
        batches.clear();
        curVerts = nullptr;
        curColor = 0;
    }

    void APIENTRY glVertexPointer(GLint, GLenum, GLsizei, const void* ptr)
    {
        curVerts = static_cast<const GLfloat*>(ptr);
    }

    void APIENTRY glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) { curColor = MakeColor(a, r, g, b); }

    void APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count)
    {
        Batch b;
        b.mode = mode;
        b.color = curColor;
        if(curVerts)
        {
            for(GLsizei i = 0; i < count; ++i)
                b.verts.push_back(PointF(curVerts[2 * (first + i)], curVerts[2 * (first + i) + 1]));
        }
        batches.push_back(std::move(b));
    }

    RTTR_POP_DIAGNOSTIC

    /// Nur die SEKTOREN - die Dreiecksstreifen. Die Icons und die Schrift kommen als GL_QUADS
    /// heraus (glArchivItem_Bitmap bzw. glFont) und werden hier nicht mitgezaehlt.
    std::vector<Batch> sectors()
    {
        std::vector<Batch> out;
        for(const Batch& b : batches)
        {
            if(b.mode == GL_TRIANGLE_STRIP)
                out.push_back(b);
        }
        return out;
    }
} // namespace ringTap

/// Der Winkel eines Punktes um `center`, in Bildschirmgrad (0 = rechts, wachsend nach unten).
float angleOf(const PointF center, const PointF p)
{
    return std::atan2(p.y - center.y, p.x - center.x) * 180.f / 3.14159265358979323846f;
}

float radiusOf(const PointF center, const PointF p)
{
    const float dx = p.x - center.x;
    const float dy = p.y - center.y;
    return std::sqrt(dx * dx + dy * dy);
}

/// Auf (-180, 180] normieren - fuer Winkeldifferenzen.
float wrapDiff(float deg)
{
    while(deg > 180.f)
        deg -= 360.f;
    while(deg <= -180.f)
        deg += 360.f;
    return deg;
}

/// Die AUSGELIEFERTEN Kataloge, aus den DATEINAMEN und nicht aus einer Liste im Testcode -
/// dieselbe Rechnung wie in testPadBrief.cpp. Eine Liste hier veraltete beim ersten neuen
/// Katalog, ohne dass es jemandem auffiele.
std::vector<std::string> shippedRingCatalogs()
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

/// Die Leiste als Protokollzeile - fuer BOOST_TEST_MESSAGE, nicht fuer Zusicherungen.
std::string dumpRingKeys(const brief::Brief& b)
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
    return out.empty() ? std::string("(leer)") : out;
}

/// Was verspricht die Leiste fuer DIESEN KNOPF? `input == Button` gehoert dazu, seit sie auch
/// den Stick nennen kann - bei einem Stickhinweis traegt `button` keine Bedeutung.
std::optional<brief::KeyAction> ringActionFor(const brief::Brief& b, const PadButton button)
{
    const auto it = std::find_if(b.keys.begin(), b.keys.end(), [button](const brief::KeyHint& h) {
        return h.input == brief::KeyInput::Button && h.button == button;
    });
    return it == b.keys.end() ? std::nullopt : std::optional<brief::KeyAction>(it->action);
}

bool ringNamesButton(const brief::Brief& b, const PadButton button)
{
    return ringActionFor(b, button).has_value();
}

bool ringHasHint(const brief::Brief& b, const PadButton button, const brief::KeyAction action)
{
    const auto a = ringActionFor(b, button);
    return a.has_value() && *a == action;
}

/// Schneiden sich zwei Kaesten? Halboffen gerechnet - Kante an Kante ist keine Ueberlappung.
bool boxesOverlap(const Rect& a, const Rect& b)
{
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

bool boxInside(const Rect& inner, const Rect& outer)
{
    return inner.left >= outer.left && inner.right <= outer.right && inner.top >= outer.top
           && inner.bottom <= outer.bottom;
}

/// Aufloesung, Fernsehmodus und GUI-Skalierung setzen und HINTERHER zurueckgeben - woertlich
/// dieselbe Folge wie in testTvDisplay.cpp (GameManager: setUiReferenceHeight, dann
/// setGuiScalePercent). Ohne das Zuruecklegen zoege dieser Fall jeden spaeteren Fall mit.
struct ScreenSetting
{
    VideoMode oldWindowSize;
    DisplayMode oldDisplayMode;
    unsigned oldGuiScalePercent;
    unsigned oldReferenceHeight;
    bool oldTvMode;
    unsigned oldSafeAreaPercent;
    unsigned oldGuiScaleSetting;
    Position oldMousePos;

    ScreenSetting()
        : oldWindowSize(VIDEODRIVER.GetWindowSize()),
          oldDisplayMode(VIDEODRIVER.GetDisplayMode()),
          oldGuiScalePercent(VIDEODRIVER.getGuiScale().percent()),
          oldReferenceHeight(VIDEODRIVER.getUiReferenceHeight()), oldTvMode(SETTINGS.video.tvMode),
          oldSafeAreaPercent(SETTINGS.video.tvSafeAreaPercent), oldGuiScaleSetting(SETTINGS.video.guiScale),
          oldMousePos(VIDEODRIVER.GetMousePos())
    {}

    ~ScreenSetting()
    {
        SETTINGS.video.tvMode = oldTvMode;
        SETTINGS.video.tvSafeAreaPercent = oldSafeAreaPercent;
        SETTINGS.video.guiScale = oldGuiScaleSetting;
        VIDEODRIVER.setUiReferenceHeight(oldReferenceHeight);
        VIDEODRIVER.setGuiScalePercent(oldGuiScalePercent);
        VIDEODRIVER.ResizeScreen(oldWindowSize, oldDisplayMode);
        uiHelper::GetVideoDriver()->SetMousePos(oldMousePos);
    }

    static void use(unsigned width, unsigned height, bool tvMode)
    {
        SETTINGS.video.tvMode = tvMode;
        SETTINGS.video.guiScale = 0; // "automatisch"
        VIDEODRIVER.ResizeScreen(VideoMode(width, height), DisplayMode::Windowed);
        VIDEODRIVER.setUiReferenceHeight(tvMode ? tv::UI_REFERENCE_HEIGHT : 0u);
        VIDEODRIVER.setGuiScalePercent(0);
    }
};

} // namespace

BOOST_AUTO_TEST_SUITE(PadRingTests)

// ============================================================================================
// 0. DIE REINE RINGRECHNUNG - ohne Grafik, ohne Partie
// ============================================================================================

/// padring::MakeSectors und SectorAt sind die Rechnung, auf der alles andere sitzt. Sie ist
/// bewusst frei von OpenGL, Window und Viewer (input/PadRing.h) und deshalb hier direkt
/// pruefbar. Der Rest dieser Datei misst danach den PRODUKTIVEN WEG; dieser Fall verankert nur,
/// dass die Rechnung selbst stimmt - namentlich, dass Sektor 0 IMMER oben sitzt.
BOOST_AUTO_TEST_CASE(SectorZeroSitsAtTwelveOClockNoMatterHowManyThereAre)
{
    // MUSKELGEDAECHTNIS ist der ganze Vorteil eines Rings, und es entsteht nur aus fester Lage
    // (CONTROLLER-UX.md 5.4). Der erste Eintrag muss deshalb bei JEDER Sektorzahl oben stehen.
    for(const unsigned count : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u})
    {
        BOOST_TEST_CONTEXT("Sektorzahl " << count)
        {
            const auto sectors = padring::MakeSectors(count);
            BOOST_TEST_REQUIRE(sectors.size() == count);
            // -90 Grad ist oben, weil y auf dem Bildschirm nach unten waechst.
            BOOST_TEST(sectors[0].midAngle() == -90.f, boost::test_tools::tolerance(0.01f));
            // Die Sektoren decken den Kreis vollstaendig und ueberlappungsfrei ab.
            for(unsigned i = 0; i + 1 < count; ++i)
                BOOST_TEST(sectors[i].endAngle == sectors[i + 1].startAngle, boost::test_tools::tolerance(0.01f));
            // Und jeder Sektormittelpunkt wird von SectorAt auch wirklich getroffen.
            for(unsigned i = 0; i < count; ++i)
            {
                const PointF aim = padring::PointOnRing(PointF(0.f, 0.f), 100.f, sectors[i].midAngle());
                BOOST_TEST(padring::SectorAt(count, aim) == static_cast<int>(i));
            }
        }
    }
    // Die MITTE waehlt nichts - Steam Inputs "nevermind"-Bereich.
    BOOST_TEST(padring::SectorAt(8u, PointF(0.f, 0.f)) == -1);
}

/// Der Zeiger laeuft AUF, wie ein Mauszeiger, und bleibt stehen, wenn der Stick losgelassen
/// wird. Das ist keine Notloesung, sondern die Barrierefreiheitsvorgabe, die Anno 1800 auf der
/// Konsole als eigene Option fuehrt - und die einzige Lesart, die ohne Stickzustand auskommt:
/// IPadTarget::OnPadMove liefert eine Verschiebung je Frame und NIE (0,0).
BOOST_AUTO_TEST_CASE(TheAimAccumulatesStaysPutAndIsClampedOutward)
{
    padring::Ring ring;
    ring.Open();
    BOOST_TEST(ring.IsOpen());
    // In der Mitte waehlt nichts.
    BOOST_TEST((ring.GetAim() == PointF(0.f, 0.f)));
    // Kleine Schritte laufen auf, bis die Totzone verlassen ist.
    for(int i = 0; i < 5; ++i)
        ring.Aim(Position(10, 0));
    BOOST_TEST(ring.GetAim().x > 0.f);
    // OHNE weiteren Ausschlag bleibt der Zeiger stehen - kein Ereignis heisst "kein Wechsel".
    const PointF held = ring.GetAim();
    BOOST_TEST((ring.GetAim() == held));
    // Und er laeuft nicht ins Unendliche, sonst muesste ein Spieler ihn erst zurueckfahren,
    // bevor die Gegenrichtung wirkt.
    for(int i = 0; i < 200; ++i)
        ring.Aim(Position(10, 0));
    BOOST_TEST(radiusOf(PointF(0.f, 0.f), ring.GetAimRaw())
                 <= padring::Ring::AimMaxRadius + 0.01f);
    // Ein Seitenwechsel setzt den Zeiger zurueck: die Sektoren bedeuten danach etwas anderes.
    ring.SetPage(1);
    BOOST_TEST((ring.GetAim() == PointF(0.f, 0.f)));
}

// ============================================================================================
// 1. (a) EIN PADSPIELER SETZT EIN GEBAEUDE UEBER DAS KREISMENUE
// ============================================================================================

/// DAS ABNAHMEKRITERIUM (a), vollstaendig und nur mit dem Gamepad.
///
/// Zielen, A (der Ring geht auf UND ist betreten - in EINEM Druck), mit dem Steuerkreuz auf den
/// Sektor des Holzfaellers drehen, A. Danach steht die Baustelle in der WELT und im Replay
/// steht genau EIN GameCommand, und zwar fuer IHN.
///
/// Und die zweite Haelfte des Kriteriums, die genauso wichtig ist: waehrend gedreht wird, zeigt
/// der Klartextkasten aus Phase 9 die Beschreibung des GERADE GEWAEHLTEN Gebaeudes. Der Ring
/// ersetzt den Kasten nicht - er wird von ihm beschriftet, und zwar ohne eine einzige neue
/// Textzeile: RefreshBrief folgt dem FOKUS, und der Ring setzt den Fokus.
BOOST_FIXTURE_TEST_CASE(APadPlayerPicksABuildingFromTheRingAndPlacesIt, PadGameFixture)
{
    setUpTwoLocalPlayers();

    PlayerView& padView = dsk->GetPlayerView(1);
    const MapPoint spot = findBuildSpotFor(world(), padView.GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(spot.isValid());

    aimPadAt(10, 0, hqFlagOf(world(), 0));
    aimPadAt(11, 1, spot);
    const unsigned startGF = GAMECLIENT.GetGFNumber();

    // --- A: DER RING geht auf, und der Fokus steht sofort darin ---
    press(11, PadButton::A);
    iwAction* const wnd = padView.actionwindow;
    BOOST_TEST_REQUIRE(wnd != static_cast<iwAction*>(nullptr));
    BOOST_TEST_REQUIRE(padView.GetRing().IsOpen());
    BOOST_TEST_REQUIRE(padView.GetFocus().IsActive());
    BOOST_TEST(padView.GetFocus().GetRoot() == static_cast<Window*>(wnd));
    // Das Fenster wird nicht mehr gezeichnet - der Ring zeichnet an seiner Stelle.
    BOOST_TEST(!wnd->IsVisible());
    // Der NACHBAR hat keinen Ring. Sein Sitzplatz ist von alldem unberuehrt.
    BOOST_TEST(!dsk->GetPlayerView(0).GetRing().IsOpen());

    // Der Fokus steht auf einem GEBAEUDE und nicht auf einem Reiterkopf - die Reiterkoepfe sind
    // die Blaetterachse und gar keine Sektoren.
    const auto focusedIcon = [&] { return dynamic_cast<const ctrlBuildingIcon*>(padView.GetFocus().GetFocused()); };
    BOOST_TEST_REQUIRE(focusedIcon() != static_cast<const ctrlBuildingIcon*>(nullptr));

    // --- Steuerkreuz: den Ring drehen, bis der Holzfaeller gewaehlt ist ---
    //
    // Und bei JEDEM Schritt: sagt der Kasten, was der Ring zeigt? Der Vergleich laeuft gegen
    // brief::ForBuilding, also gegen die Phase-9-Rechnung selbst - nicht gegen eine im Test
    // abgeschriebene Zeichenkette.
    // Geprueft wird der Kasten AUF JEDEM Sektor, den der Ring dabei durchlaeuft - der erste
    // eingeschlossen. Und ausdruecklich SO HERUM, dass auch ein Ring, dessen erster Sektor
    // schon der Holzfaeller ist, wirklich gemessen wird.
    unsigned checked = 0;
    std::vector<std::string> seenTexts;
    for(unsigned i = 0; i < 13u; ++i)
    {
        const BuildingType shown = focusedIcon()->GetType();
        const brief::Brief expected = brief::ForBuilding(shown);
        BOOST_TEST_CONTEXT("Sektor mit Gebaeudeart " << unsigned(rttr::enum_cast(shown)))
        {
            BOOST_TEST(padView.GetBrief().title == expected.title);
            BOOST_TEST(padView.GetBrief().lines == expected.lines, boost::test_tools::per_element());
        }
        ++checked;
        seenTexts.push_back(padView.GetBrief().joined());
        if(shown == BuildingType::Woodcutter)
            break;
        press(11, PadButton::DpadRight);
        BOOST_TEST_REQUIRE(focusedIcon() != static_cast<const ctrlBuildingIcon*>(nullptr));
    }
    BOOST_TEST_REQUIRE((focusedIcon()->GetType() == BuildingType::Woodcutter));
    BOOST_TEST_MESSAGE("AUDIT: Sektoren mit gepruefter Beschriftung bis zum Holzfaeller = " << checked);
    BOOST_TEST(checked > 0u);

    // UND DER KASTEN AENDERT SICH WIRKLICH MIT DEM RING. Ohne diesen Schritt koennte er
    // ueberall dasselbe sagen und trotzdem gruen sein: einen Sektor weiter, andere Auskunft,
    // wieder zurueck, wieder die alte.
    const std::string atWoodcutter = padView.GetBrief().joined();
    press(11, PadButton::DpadRight);
    BOOST_TEST_REQUIRE(focusedIcon() != static_cast<const ctrlBuildingIcon*>(nullptr));
    BOOST_TEST_MESSAGE("AUDIT: ein Sektor weiter = " << padView.GetBrief().joined());
    BOOST_TEST(padView.GetBrief().joined() != atWoodcutter);
    BOOST_TEST(padView.GetBrief().joined() == brief::ForBuilding(focusedIcon()->GetType()).joined());
    press(11, PadButton::DpadLeft);
    BOOST_TEST_REQUIRE((focusedIcon()->GetType() == BuildingType::Woodcutter));
    BOOST_TEST(padView.GetBrief().joined() == atWoodcutter);

    // DER KASTEN BESCHRIFTET DEN RING: die Beschreibung des Holzfaellers steht da, und sie ist
    // eine ANDERE als die des Sektors davor. Ohne diesen zweiten Teil koennte der Kasten
    // ueberall dasselbe sagen und trotzdem gruen sein.
    const brief::Brief wood = brief::ForBuilding(BuildingType::Woodcutter);
    BOOST_TEST_MESSAGE("AUDIT: Klartext unter dem Ring = " << padView.GetBrief().joined());
    BOOST_TEST(padView.GetBrief().title == wood.title);
    BOOST_TEST(padView.GetBrief().joined() == wood.joined());
    BOOST_TEST(wood.joined() != brief::ForBuilding(BuildingType::Quarry).joined());

    // Und die LEISTE sagt, was IM RING gilt - nicht, was im Fenster galt.
    const auto hasHint = [](const brief::Brief& b, const PadButton button, const brief::KeyAction action) {
        return std::any_of(b.keys.begin(), b.keys.end(),
                           [&](const brief::KeyHint& h) {
                               return h.input == brief::KeyInput::Button && h.button == button
                                      && h.action == action;
                           });
    };
    BOOST_TEST(hasHint(padView.GetBrief(), PadButton::A, brief::KeyAction::Choose));
    BOOST_TEST(hasHint(padView.GetBrief(), PadButton::DpadRight, brief::KeyAction::TurnRing));
    BOOST_TEST(hasHint(padView.GetBrief(), PadButton::B, brief::KeyAction::CloseRing));

    // --- A: bauen. Die Ringauswahl war ANZEIGE; erst dieses A ist ein GameCommand. ---
    press(11, PadButton::A);
    pumpUntilGF(startGF + 40);

    BOOST_TEST_REQUIRE((world().GetNO(spot)->GetType() == NodalObjectType::Buildingsite));
    const auto* site = world().GetSpecObj<noBuildingSite>(spot);
    BOOST_TEST_REQUIRE(site != static_cast<const noBuildingSite*>(nullptr));
    BOOST_TEST(unsigned(site->GetPlayer()) == 1u);
    BOOST_TEST((site->GetBuildingType() == BuildingType::Woodcutter));
    BOOST_TEST(ci().numErrors == 0u);
    BOOST_TEST(ci().numAsync == 0u);

    // Das Fenster hat sich beim Bauen selbst geschlossen; der verwaiste Ring darf nicht
    // stehenbleiben, sonst verschluckte er jede weitere Weltflanke dieses Sitzplatzes.
    step(16);
    BOOST_TEST(!padView.GetRing().IsOpen());

    WINDOWMANAGER.Draw();
    tearDownDesktop();

    // Buchhaltung: genau EIN Kommando, und zwar fuer Spieler 1.
    const auto replayPath = stopAndGetReplay();
    BOOST_TEST(numGCsForPlayer(replayPath, 0) == 0u);
    BOOST_TEST(numGCsForPlayer(replayPath, 1) == 1u);
    BOOST_TEST(numGCsForPlayer(replayPath, 2) == 0u);
}

// ============================================================================================
// 2. WIRD DER RING WIRKLICH GEZEICHNET? - gemessen am OpenGL-Aufruf
// ============================================================================================

/// Ein Mechanismus, den ein Test nur ueber seine Rechnung prueft, kann aus dem ZEICHENWEG
/// fallen, ohne dass ein Fall rot wird - genau das ist diesem Projekt in Phase 9 passiert
/// (Befund N7: die letzte Zeile aus DrawBrief entfernt, 316 Faelle blieben gruen).
///
/// Dieser Fall schliesst die Luecke am tiefstmoeglichen Punkt: er faengt glVertexPointer,
/// glColor4ub und glDrawArrays ab und liest die GEOMETRIE zurueck, die DrawRing hinausschickt.
/// Gemessen wird also nicht "es wurde irgendetwas gezeichnet", sondern: wie viele Sektoren, an
/// welchem Winkel, mit welchem Radius, in welcher Farbe.
BOOST_FIXTURE_TEST_CASE(TheRingSectorsReallyReachOpenGLWithTheRightGeometry, PadViewFixture<2>)
{
    const MapPoint flagPt = [&] {
        const MapPoint hqPos = worldFixture.world.GetPlayer(1).GetHQPos();
        const auto* hq = worldFixture.world.GetSpecObj<nobBaseWarehouse>(hqPos);
        BOOST_TEST_REQUIRE(hq != nullptr);
        return hq->GetFlagPos();
    }();
    seatPad(*this, 11, 1);
    aimPadAt(11, 1, flagPt);

    // Back oeffnet den System-Ring: fuenf bis acht Textsektoren, keine Nationsgrafiken noetig.
    press(11, PadButton::Back);
    BOOST_TEST_REQUIRE(view(1).GetRing().IsOpen());

    const auto layout = dsk->LayoutRing(view(1));
    BOOST_TEST_REQUIRE(!layout.empty());
    BOOST_TEST_MESSAGE("AUDIT: Ring mit " << layout.entries.size() << " Sektoren, Mitte " << layout.center
                                          << ", Radien " << layout.rInner << ".." << layout.rOuter);

    // Einmal warmzeichnen: der erste Aufruf legt die Schrifttextur an, das soll die Messung
    // nicht mitzaehlen.
    dsk->DrawRing(view(1));

    ringTap::reset();
    {
        RTTR_STUB_FUNCTION(glVertexPointer, ringTap::glVertexPointer);
        RTTR_STUB_FUNCTION(glColor4ub, ringTap::glColor4ub);
        RTTR_STUB_FUNCTION(glDrawArrays, ringTap::glDrawArrays);
        dsk->DrawRing(view(1));
    }

    const auto sectors = ringTap::sectors();
    // GENAU SO VIELE Dreiecksstreifen wie die Rechnung Eintraege hat - kein Sektor faellt aus
    // dem Zeichenweg, und keiner kommt dazu.
    BOOST_TEST_REQUIRE(sectors.size() == layout.entries.size());

    const PointF center(layout.center);
    unsigned numSelected = 0;
    for(std::size_t i = 0; i < sectors.size(); ++i)
    {
        const auto& b = sectors[i];
        const auto& e = layout.entries[i];
        BOOST_TEST_CONTEXT("Sektor " << i)
        {
            BOOST_TEST_REQUIRE(b.verts.size() >= 4u);
            // Ein Dreiecksstreifen aus abwechselnd Aussen- und Innenradius.
            BOOST_TEST(radiusOf(center, b.verts[0]) == layout.rOuter, boost::test_tools::tolerance(0.5f));
            BOOST_TEST(radiusOf(center, b.verts[1]) == layout.rInner, boost::test_tools::tolerance(0.5f));
            // Der erste Eckpunkt liegt am Sektoranfang, der letzte am Sektorende.
            BOOST_TEST(wrapDiff(angleOf(center, b.verts[0]) - e.sector.startAngle) == 0.f,
                       boost::test_tools::tolerance(0.5f));
            BOOST_TEST(wrapDiff(angleOf(center, b.verts.back()) - e.sector.endAngle) == 0.f,
                       boost::test_tools::tolerance(0.5f));
            // Die FARBE unterscheidet gewaehlt / frei / gesperrt. Aus drei Metern vor einem
            // 55-Zoll-Fernseher ist der Helligkeitsunterschied das, was traegt.
            const unsigned expected = e.selected ? dskGameInterface::ringSelectedColor :
                                      (e.enabled ? dskGameInterface::ringSectorColor :
                                                   dskGameInterface::ringLockedColor);
            BOOST_TEST(b.color == expected);
            if(e.selected)
                ++numSelected;
        }
    }
    // GENAU EIN Sektor ist hervorgehoben - der, den A ausloest.
    BOOST_TEST(numSelected == 1u);
    // Der hervorgehobene ist deutlich HELLER als die anderen: das ist der Unterschied, der aus
    // drei Metern noch ankommt.
    BOOST_TEST(GetAlpha(dskGameInterface::ringSelectedColor) > GetAlpha(dskGameInterface::ringSectorColor));
    BOOST_TEST(GetAlpha(dskGameInterface::ringSectorColor) > GetAlpha(dskGameInterface::ringLockedColor));

    // UND JEDER SEKTOR TRAEGT EINE BESCHRIFTUNG - sonst waere der Ring sieben gleich aussehende
    // Farbflaechen, und der Spieler koennte sie nicht unterscheiden.
    //
    // Die Beschriftung kommt vom CONTROL SELBST (Window::GetRingLabel liefert bei einem
    // ctrlTextButton woertlich seinen Knopftext), nicht aus einer Tabelle im Ring - es gibt
    // also keine zweite Zeichenkette, die neben der ersten veralten koennte. glFont::Draw gibt
    // sie als GL_QUADS aus, vier Eckpunkte je Zeichen.
    const auto labelBatches = [] {
        std::vector<ringTap::Batch> out;
        for(const ringTap::Batch& b : ringTap::batches)
        {
            if(b.mode == GL_QUADS)
                out.push_back(b);
        }
        return out;
    }();
    // GEZAEHLT WIRD JE ZEICHENZEILE, nicht je Sektor: seit Befund K1 steht die Beschriftung
    // NEBEN dem Ring und darf umbrechen, wenn der Platz eng wird (RingEntry::labelLines). Ein
    // Sektor kann also mehrere Zeilen ausgeben. Die Zahl bleibt trotzdem exakt - sie kommt aus
    // demselben Layout, das der Zeichner benutzt, und ein ausgelassener Sektor faellt weiterhin
    // sofort auf.
    std::size_t expectedLines = 0;
    for(const auto& e : layout.entries)
        expectedLines += e.icon ? 1u : e.labelLines.size();
    BOOST_TEST_MESSAGE("AUDIT: gezeichnete Beschriftungszeilen im Ring = " << labelBatches.size() << " bei "
                                                                          << layout.entries.size() << " Sektoren und "
                                                                          << expectedLines << " erwarteten Zeilen");
    BOOST_TEST(labelBatches.size() == expectedLines);
    BOOST_TEST(labelBatches.size() >= layout.entries.size());
    for(std::size_t i = 0; i < labelBatches.size(); ++i)
    {
        // Mehr als ein Zeichen - eine leere oder einbuchstabige Beschriftung waere keine.
        BOOST_TEST_CONTEXT("Beschriftung " << i) BOOST_TEST(labelBatches[i].verts.size() >= 8u);
    }

    // GEGENPROBE OHNE EINGRIFF: ein GESCHLOSSENER Ring zeichnet NICHTS. Ohne diesen Teil waere
    // die Messung oben blind gegen einen Zeichner, der immer zeichnet.
    press(11, PadButton::B);
    BOOST_TEST_REQUIRE(!view(1).GetRing().IsOpen());
    ringTap::reset();
    {
        RTTR_STUB_FUNCTION(glVertexPointer, ringTap::glVertexPointer);
        RTTR_STUB_FUNCTION(glColor4ub, ringTap::glColor4ub);
        RTTR_STUB_FUNCTION(glDrawArrays, ringTap::glDrawArrays);
        dsk->DrawRing(view(1));
    }
    BOOST_TEST(ringTap::sectors().empty());
}

/// DER RING LIEGT DORT, WO SEINE BESCHRIFTUNG NICHT IST.
///
/// Nachgerechnet fuer den Zielfall (4K, Fernsehmodus, vier Spieler): ein Viewport ist 960x540,
/// der Klartextkasten steht unten. Ein Ring in der VIEWPORTMITTE liefe bis zu 72 Punkte tief in
/// seine eigene Beschriftung hinein - genau der Fehler, den diese Phase vermeiden soll.
BOOST_FIXTURE_TEST_CASE(TheRingSitsAboveItsOwnCaptionAndInsideTheSafeArea, PadViewFixture<2>)
{
    const MapPoint flagPt = [&] {
        const MapPoint hqPos = worldFixture.world.GetPlayer(1).GetHQPos();
        const auto* hq = worldFixture.world.GetSpecObj<nobBaseWarehouse>(hqPos);
        BOOST_TEST_REQUIRE(hq != nullptr);
        return hq->GetFlagPos();
    }();
    seatPad(*this, 11, 1);
    aimPadAt(11, 1, flagPt);
    press(11, PadButton::Back);
    BOOST_TEST_REQUIRE(view(1).GetRing().IsOpen());

    const auto ring = dsk->LayoutRing(view(1));
    const auto brief = dsk->LayoutBrief(view(1));
    BOOST_TEST_REQUIRE(!ring.empty());
    BOOST_TEST_REQUIRE(!brief.lines.empty());
    BOOST_TEST_MESSAGE("AUDIT: Ringunterkante = " << (ring.center.y + int(ring.rOuter))
                                                  << ", Kastenoberkante = " << brief.panel.top);
    // DER RING ENDET UEBER DEM KASTEN. Nicht "meistens" - immer.
    BOOST_TEST(ring.center.y + static_cast<int>(ring.rOuter) <= brief.panel.top);
    // Und er liegt in SEINEM Viewport, nicht im Bild des Nachbarn - das ist der Grund, warum er
    // KEIN IngameWindow ist (die klemmen gegen die ganze Renderflaeche).
    const Rect viewport(view(1).GetView().GetPos(), view(1).GetView().GetSize());
    BOOST_TEST(ring.center.x - static_cast<int>(ring.rOuter) >= static_cast<int>(viewport.left));
    BOOST_TEST(ring.center.x + static_cast<int>(ring.rOuter) <= static_cast<int>(viewport.right));
    BOOST_TEST(ring.center.y - static_cast<int>(ring.rOuter) >= static_cast<int>(viewport.top));
    // Der Kasten weicht dem unsichtbaren Fenster NICHT aus - sonst spraenge er unter dem Ring
    // weg, vor einem Fenster, das niemand sieht.
    BOOST_TEST(view(1).GetFocus().GetRoot() != static_cast<Window*>(nullptr));

    press(11, PadButton::B);
}

// ============================================================================================
// 3. DER STICK ZEIGT - und der Fokus faellt auf den getroffenen Sektor
// ============================================================================================

/// Die Sektorwahl ueber den linken Stick, ausschliesslich ueber den produktiven Weg
/// (PadFeeder -> UpdateInput -> dskGameInterface::OnPadMove).
///
/// Der Ring muss den Stick VOR dem Fokus bekommen. Sonst wanderte der Fokus in ID-Reihenfolge
/// (FocusPath::OnPadMove) statt zum getroffenen Sektor, und der Ring waere ein Rasterknopfwerk
/// in Kreisform.
BOOST_FIXTURE_TEST_CASE(TheLeftStickAimsAtSectorsAndTheFocusFollowsTheAim, PadViewFixture<2>)
{
    const MapPoint flagPt = [&] {
        const MapPoint hqPos = worldFixture.world.GetPlayer(1).GetHQPos();
        const auto* hq = worldFixture.world.GetSpecObj<nobBaseWarehouse>(hqPos);
        BOOST_TEST_REQUIRE(hq != nullptr);
        return hq->GetFlagPos();
    }();
    seatPad(*this, 11, 1);
    aimPadAt(11, 1, flagPt);
    press(11, PadButton::Back);
    BOOST_TEST_REQUIRE(view(1).GetRing().IsOpen());

    unsigned numPages = 1;
    const std::vector<Window*> ctrls = dskGameInterface::RingPageCtrls(view(1), numPages);
    BOOST_TEST_REQUIRE(ctrls.size() >= 4u);
    const auto sectors = padring::MakeSectors(static_cast<unsigned>(ctrls.size()));

    // JEDEN Sektor einmal anzielen - mit dem echten Stick, ueber die Ereigniswarteschlange.
    // Vor jedem Ziel wird der Zeiger zurueckgeholt, indem in die Gegenrichtung gefahren wird;
    // der Zeiger laeuft auf, wie ein Mauszeiger, und kennt keinen Sprung.
    unsigned hit = 0;
    for(unsigned i = 0; i < ctrls.size(); ++i)
    {
        // Zurueck in die Mitte und dann in die Zielrichtung - reichlich Weg, der Zeiger klemmt
        // ohnehin bei AimMaxRadius.
        const PointF dir = padring::PointOnRing(PointF(0.f, 0.f), 1.f, sectors[i].midAngle());
        for(int frame = 0; frame < 60; ++frame)
        {
            // Vollausschlag in die Sektorrichtung - der linke Stick, ueber die
            // Ereigniswarteschlange des Treibers, genau wie beim Zielen in der Welt.
            pads.axis(11, PadAxis::LeftX, dir.x);
            pads.axis(11, PadAxis::LeftY, dir.y);
            dsk->UpdateInput(16, Position(-10000, -10000));
        }
        // Stick loslassen: die Auswahl muss STEHENBLEIBEN. Das ist die
        // Barrierefreiheitsvorgabe, und sie wird hier mitgemessen - die Zusicherung unten
        // laeuft nach dem Loslassen.
        pads.axis(11, PadAxis::LeftX, 0.f);
        pads.axis(11, PadAxis::LeftY, 0.f);
        dsk->UpdateInput(16, Position(-10000, -10000));
        dsk->UpdateInput(16, Position(-10000, -10000));
        BOOST_TEST_CONTEXT("Sektor " << i)
        {
            const bool ok = view(1).GetFocus().GetFocused() == ctrls[i];
            BOOST_TEST(ok);
            if(ok)
                ++hit;
        }
    }
    BOOST_TEST_MESSAGE("AUDIT: mit dem Stick getroffene Sektoren = " << hit << " von " << ctrls.size());
    BOOST_TEST(hit == ctrls.size());

    press(11, PadButton::B);
}

// ============================================================================================
// 4. (b) DAS BACK-SYSTEMMENUE IST DERSELBE RING
// ============================================================================================

/// DAS ABNAHMEKRITERIUM (b): "nur ein Paradigma".
///
/// Back oeffnet den Ring, nicht eine Liste von Textknoepfen. ALLE seine Punkte sind darin
/// erreichbar - die Symbolschalter eingeschlossen, nach denen der Auftraggeber gefragt hat.
/// Gemessen ueber KENNUNGEN (iwPadSystemMenu::ButtonId) und nie ueber Text: drei Phasen dieses
/// Projekts sind an uebersetzten Zeichenketten zerbrochen.
BOOST_FIXTURE_TEST_CASE(TheBackMenuIsTheSameRingAndCarriesEverySwitch, PadViewFixture<2>)
{
    const MapPoint flagPt = [&] {
        const MapPoint hqPos = worldFixture.world.GetPlayer(1).GetHQPos();
        const auto* hq = worldFixture.world.GetSpecObj<nobBaseWarehouse>(hqPos);
        BOOST_TEST_REQUIRE(hq != nullptr);
        return hq->GetFlagPos();
    }();
    seatPad(*this, 11, 1);
    aimPadAt(11, 1, flagPt);

    press(11, PadButton::Back);
    IngameWindow* const menu = WINDOWMANAGER.FindNonModalWindow(CGI_PADMENU, 1);
    BOOST_TEST_REQUIRE(menu != static_cast<IngameWindow*>(nullptr));
    // ES IST EIN RING - und zwar derselbe Mechanismus wie beim Baumenue.
    BOOST_TEST_REQUIRE(view(1).GetRing().IsOpen());
    BOOST_TEST_REQUIRE(view(1).GetFocus().IsActive());
    BOOST_TEST(view(1).GetFocus().GetRoot() == static_cast<Window*>(menu));
    BOOST_TEST(!menu->IsVisible());

    // Alle sieben Punkte liegen auf EINER Seite - das Systemmenue braucht kein Blaettern.
    unsigned numPages = 1;
    const std::vector<Window*> ctrls = dskGameInterface::RingPageCtrls(view(1), numPages);
    BOOST_TEST_MESSAGE("AUDIT: Sektoren des System-Rings = " << ctrls.size() << ", Seiten = " << numPages);
    BOOST_TEST(numPages == 1u);

    // JEDER Punkt ist ein Sektor. Ueber die Kennung gesucht, nicht ueber die Reihenfolge.
    const auto sectorWithId = [&](const unsigned id) {
        return std::any_of(ctrls.begin(), ctrls.end(), [&](const Window* w) { return w->GetID() == id; });
    };
    for(const unsigned id : {iwPadSystemMenu::ID_MINIMAP, iwPadSystemMenu::ID_POST,
                             iwPadSystemMenu::ID_CONSTRUCTION_AID, iwPadSystemMenu::ID_NAMES,
                             iwPadSystemMenu::ID_PRODUCTIVITY, iwPadSystemMenu::ID_WATCH_ONLY,
                             iwPadSystemMenu::ID_MAIN_SELECTION})
    {
        BOOST_TEST_CONTEXT("Kennung " << id) BOOST_TEST(sectorWithId(id));
    }

    // UND SIE SIND ALLE DURCH DREHEN ERREICHBAR - der Ring laeuft um, eine Richtung genuegt.
    std::vector<unsigned> reached;
    for(unsigned i = 0; i < ctrls.size() + 2u; ++i)
    {
        const Window* const focused = view(1).GetFocus().GetFocused();
        BOOST_TEST_REQUIRE(focused != static_cast<const Window*>(nullptr));
        if(std::find(reached.begin(), reached.end(), focused->GetID()) == reached.end())
            reached.push_back(focused->GetID());
        press(11, PadButton::DpadRight);
    }
    BOOST_TEST_MESSAGE("AUDIT: durch Drehen erreichte Punkte = " << reached.size());
    BOOST_TEST(reached.size() == ctrls.size());

    // DIE SYMBOLSCHALTER WIRKEN WIRKLICH - jeder fuer sich, und nur fuer DIESEN Sitzplatz.
    const auto turnTo = [&](const unsigned id) {
        for(unsigned i = 0; i < 16u; ++i)
        {
            const Window* const focused = view(1).GetFocus().GetFocused();
            BOOST_TEST_REQUIRE(focused != static_cast<const Window*>(nullptr));
            if(focused->GetID() == id)
                return;
            press(11, PadButton::DpadRight);
        }
        BOOST_FAIL("Sektor per Pad nicht erreichbar");
    };

    const bool names0 = view(0).GetView().IsShowingNames();
    const bool prod1 = view(1).GetView().IsShowingProductivity();
    const bool names1 = view(1).GetView().IsShowingNames();
    turnTo(iwPadSystemMenu::ID_NAMES);
    press(11, PadButton::A);
    BOOST_TEST(view(1).GetView().IsShowingNames() == !names1);
    // NICHT mitgezogen: die Auslastung bleibt, wo sie war. Das ist der ganze Punkt der Trennung -
    // Namen sind Lernstoff, Auslastung ist eine Expertenzahl.
    BOOST_TEST(view(1).GetView().IsShowingProductivity() == prod1);
    // Und der Nachbar bleibt unberuehrt.
    BOOST_TEST(view(0).GetView().IsShowingNames() == names0);

    turnTo(iwPadSystemMenu::ID_PRODUCTIVITY);
    press(11, PadButton::A);
    BOOST_TEST(view(1).GetView().IsShowingProductivity() == !prod1);
    BOOST_TEST(view(1).GetView().IsShowingNames() == !names1);

    const bool bq1 = view(1).GetView().IsShowingBQ();
    turnTo(iwPadSystemMenu::ID_CONSTRUCTION_AID);
    press(11, PadButton::A);
    BOOST_TEST(view(1).GetView().IsShowingBQ() == !bq1);

    // Der Ring BLEIBT dabei offen: ein Umschalter, der das Menue zumacht, zwaenge den Spieler,
    // fuer jeden zweiten Schalter neu hineinzugehen.
    BOOST_TEST(view(1).GetRing().IsOpen());

    press(11, PadButton::B);
    BOOST_TEST(!view(1).GetRing().IsOpen());
    if(!menu->ShouldBeClosed())
        menu->Close();
    WINDOWMANAGER.Draw();
}

// ============================================================================================
// 5. DIE ZWEITE BITTE: "wenn ich einfach nur ein wenig zuschauen will"
// ============================================================================================

/// DER SAMMELSCHALTER - woertlich das, was der Auftraggeber beschrieben hat.
///
/// Gemessen war dieser Zustand vorher UNERREICHBAR: von den Weltsymbolen und Bildschirmelementen
/// sind ganze zwei geschaltet, und der Klartextkasten hatte gar keinen Schalter.
///
/// Und die haerteste Zusicherung daran: der Zustand hat GENAU EINEN Ausgang, und er ist
/// SICHTBAR. Ein Zustand, der alles ausblendet und seinen eigenen Ausgang verschweigt, ist die
/// Falle, die Phase 11 (Back verschluckt) und Phase 12 (luegende Leiste) je einmal gebaut haben.
BOOST_FIXTURE_TEST_CASE(JustWatchHidesEverythingAndSaysHowToComeBack, PadViewFixture<2>)
{
    const MapPoint flagPt = [&] {
        const MapPoint hqPos = worldFixture.world.GetPlayer(1).GetHQPos();
        const auto* hq = worldFixture.world.GetSpecObj<nobBaseWarehouse>(hqPos);
        BOOST_TEST_REQUIRE(hq != nullptr);
        return hq->GetFlagPos();
    }();
    seatPad(*this, 11, 1);
    aimPadAt(11, 1, flagPt);

    // Erst einmal ALLES ANSCHALTEN - sonst maesse der Fall an einem Zustand, in dem ohnehin
    // nichts an war (die drei Schalter stehen im Auslieferungszustand auf AUS).
    press(11, PadButton::Back);
    BOOST_TEST_REQUIRE(view(1).GetRing().IsOpen());
    const auto turnTo = [&](const unsigned id) {
        for(unsigned i = 0; i < 16u; ++i)
        {
            const Window* const focused = view(1).GetFocus().GetFocused();
            BOOST_TEST_REQUIRE(focused != static_cast<const Window*>(nullptr));
            if(focused->GetID() == id)
                return;
            press(11, PadButton::DpadRight);
        }
        BOOST_FAIL("Sektor per Pad nicht erreichbar");
    };
    for(const unsigned id : {iwPadSystemMenu::ID_CONSTRUCTION_AID, iwPadSystemMenu::ID_NAMES,
                             iwPadSystemMenu::ID_PRODUCTIVITY})
    {
        turnTo(id);
        if(!(id == iwPadSystemMenu::ID_CONSTRUCTION_AID ? view(1).GetView().IsShowingBQ() :
             id == iwPadSystemMenu::ID_NAMES            ? view(1).GetView().IsShowingNames() :
                                                          view(1).GetView().IsShowingProductivity()))
            press(11, PadButton::A);
    }
    BOOST_TEST_REQUIRE(view(1).GetView().IsShowingBQ());
    BOOST_TEST_REQUIRE(view(1).GetView().IsShowingNames());
    BOOST_TEST_REQUIRE(view(1).GetView().IsShowingProductivity());
    // Auch der NACHBAR schaltet alles an - damit gleich messbar ist, dass "nur zuschauen" bei
    // ihm nichts anfasst.
    if(!view(0).GetView().IsShowingNames())
        view(0).GetView().ToggleShowNames();
    BOOST_TEST_REQUIRE(view(0).GetView().IsShowingNames());

    // --- DER SEKTOR "NUR ZUSCHAUEN" ---
    turnTo(iwPadSystemMenu::ID_WATCH_ONLY);
    press(11, PadButton::A);

    BOOST_TEST_REQUIRE(view(1).IsWatchOnly());
    // ALLES AUS - und zwar nur bei ihm.
    BOOST_TEST(!view(1).GetView().IsShowingBQ());
    BOOST_TEST(!view(1).GetView().IsShowingNames());
    BOOST_TEST(!view(1).GetView().IsShowingProductivity());
    BOOST_TEST(view(0).GetView().IsShowingNames());
    BOOST_TEST(!view(0).IsWatchOnly());
    // Der Ring und das Menue sind mitgegangen: was er jetzt sehen will, ist die WELT.
    BOOST_TEST(!view(1).GetRing().IsOpen());
    BOOST_TEST(WINDOWMANAGER.FindNonModalWindow(CGI_PADMENU, 1) == static_cast<IngameWindow*>(nullptr));

    // DER KASTEN IST NICHT WEG, SONDERN AUF EINE ZEILE GESCHRUMPFT - und diese eine Zeile ist
    // der Ausgang. Das ist die Zusicherung, an der alles haengt.
    const brief::Brief& b = view(1).GetBrief();
    BOOST_TEST_MESSAGE("AUDIT: Kasten beim Zuschauen = '" << b.joined() << "' ["
                                                          << brief::KeyLine(b.keys) << "]");
    BOOST_TEST(b.lines.empty());
    BOOST_TEST_REQUIRE(b.keys.size() == 1u);
    BOOST_TEST((b.keys.front().button == PadButton::B));
    BOOST_TEST((b.keys.front().action == brief::KeyAction::LeaveWatchOnly));
    // Die Zeile ist nicht leer - ein unsichtbarer Ausgang waere keiner.
    BOOST_TEST(!brief::KeyLine(b.keys).empty());

    // KEIN ANDERER KNOPF WIRKT. A oeffnet kein Fenster, X legt keine Flagge, Back kein Menue.
    press(11, PadButton::A);
    BOOST_TEST(view(1).actionwindow == static_cast<iwAction*>(nullptr));
    press(11, PadButton::Back);
    BOOST_TEST(WINDOWMANAGER.FindNonModalWindow(CGI_PADMENU, 1) == static_cast<IngameWindow*>(nullptr));
    BOOST_TEST(view(1).IsWatchOnly());

    // --- B: und ALLES kommt zurueck, GENAU wie es war. Der Kasten verspricht das; ein Hinweis,
    // der luegt, ist schlimmer als keiner. ---
    press(11, PadButton::B);
    BOOST_TEST_REQUIRE(!view(1).IsWatchOnly());
    BOOST_TEST(view(1).GetView().IsShowingBQ());
    BOOST_TEST(view(1).GetView().IsShowingNames());
    BOOST_TEST(view(1).GetView().IsShowingProductivity());
    // Und die Welt ist wieder bedienbar.
    BOOST_TEST(!view(1).GetBrief().keys.empty());
    BOOST_TEST(view(1).GetBrief().keys.size() > 1u);
}

/// DER WAHRE GRUND FUER "ich verstehe nicht, wie ich die Symbole ausschalte".
///
/// GEMESSEN am Quelltext und hier am Verhalten: dskGameInterface::PadOpenActionWindow ruft
/// GameWorldView::ForceShowBQ() bei JEDEM A auf Bauland, und forcedShowBQ_ faellt ausschliesslich
/// in ToggleShowBQ. Wer die Bauhilfe also ausschaltete und danach einmal A drueckte, hatte sie
/// wieder an - jedes Mal aufs Neue. Der Schalter im Menue war damit nur die halbe Antwort.
///
/// Eine Bequemlichkeitsvorgabe darf einen ausgesprochenen Willen nicht ueberstimmen. Sie darf
/// ihn auch nicht festschreiben: schaltet der Mensch die Bauhilfe spaeter wieder ein, wirkt die
/// Vorgabe wieder. Beide Haelften werden hier gemessen.
BOOST_FIXTURE_TEST_CASE(SwitchingTheConstructionAidOffSurvivesThenextPressOfA, PadViewFixture<2>)
{
    const MapPoint spot = findBuildSpotFor(worldFixture.world, view(1).GetViewer(), BuildingQuality::Hut);
    BOOST_TEST_REQUIRE(spot.isValid());
    seatPad(*this, 11, 1);
    aimPadAt(11, 1, spot);

    // Der Spieler schaltet die Bauhilfe AUSDRUECKLICH aus - ueber den Ring, ueber den Padweg.
    press(11, PadButton::Back);
    BOOST_TEST_REQUIRE(view(1).GetRing().IsOpen());
    for(unsigned i = 0; i < 16u; ++i)
    {
        const Window* const focused = view(1).GetFocus().GetFocused();
        BOOST_TEST_REQUIRE(focused != static_cast<const Window*>(nullptr));
        if(focused->GetID() == iwPadSystemMenu::ID_CONSTRUCTION_AID)
            break;
        press(11, PadButton::DpadRight);
    }
    if(view(1).GetView().IsShowingBQ())
        press(11, PadButton::A);
    BOOST_TEST_REQUIRE(!view(1).GetView().IsShowingBQ());
    press(11, PadButton::B);
    BOOST_TEST_REQUIRE(!view(1).GetRing().IsOpen());

    // ... und drueckt danach A auf Bauland - die haeufigste Handlung des Spiels.
    aimPadAt(11, 1, spot);
    press(11, PadButton::A);
    BOOST_TEST_REQUIRE(view(1).GetRing().IsOpen());
    // DER BEFUND: sie bleibt aus. Vorher stand hier "an".
    BOOST_TEST(!view(1).GetView().IsShowingBQ());
    press(11, PadButton::B);

    // DIE ANDERE HAELFTE: schaltet er sie wieder ein, wirkt die Vorgabe wieder - der Wille wird
    // nicht festgeschrieben, sondern nur respektiert, solange er gilt.
    press(11, PadButton::Back);
    for(unsigned i = 0; i < 16u; ++i)
    {
        const Window* const focused = view(1).GetFocus().GetFocused();
        BOOST_TEST_REQUIRE(focused != static_cast<const Window*>(nullptr));
        if(focused->GetID() == iwPadSystemMenu::ID_CONSTRUCTION_AID)
            break;
        press(11, PadButton::DpadRight);
    }
    press(11, PadButton::A);
    BOOST_TEST_REQUIRE(view(1).GetView().IsShowingBQ());
    press(11, PadButton::B);
    // Jetzt wieder ausschalten waere ein neuer ausdruecklicher Wille; stattdessen wird geprueft,
    // dass ForceShowBQ nach dem Wiedereinschalten NICHT mehr gesperrt ist. Dafuer reicht es,
    // dass die Bauhilfe an ist und A sie anlaesst.
    aimPadAt(11, 1, spot);
    press(11, PadButton::A);
    BOOST_TEST(view(1).GetView().IsShowingBQ());
    press(11, PadButton::B);
}

// ============================================================================================
// 6. (c) VIER SITZPLAETZE, VIER RINGE - und jeder gehoert dem, der ihn geoeffnet hat
// ============================================================================================

/// DAS ABNAHMEKRITERIUM (c), und der Fall, der ROT werden MUSS, wenn die Besitzzuordnung faellt.
///
/// Vier Spieler oeffnen NACHEINANDER je ihren eigenen Ring, und danach sind alle vier
/// gleichzeitig offen. Jeder dreht auf einen ANDEREN Sektor, und keiner aendert etwas am
/// anderen. Der Ringzustand lebt auf PlayerView (padring::Ring ring_), genau wie der Fokus -
/// daran haengt die ganze Zusicherung.
///
/// GEMESSEN WIRD ELEMENTWEISE: Ringzustand, Fokuswurzel, fokussierter Sektor und die Leiste,
/// je Sitzplatz. Ein Ring, der einem globalen Zustand gehoerte, koennte hoechstens EINEN dieser
/// vier Bloecke gleichzeitig richtig fuellen.
BOOST_FIXTURE_TEST_CASE(FourSeatsHoldFourIndependentRingsAtTheSameTime, PadViewFixture<4>)
{
    // Jeder Sitzplatz bekommt sein eigenes Pad und zielt auf SEINE eigene HQ-Flagge.
    for(unsigned v = 0; v < 4u; ++v)
    {
        const PadDeviceId dev = static_cast<PadDeviceId>(20 + v);
        seatPad(*this, dev, v);
        const MapPoint hqPos = worldFixture.world.GetPlayer(v).GetHQPos();
        const auto* hq = worldFixture.world.GetSpecObj<nobBaseWarehouse>(hqPos);
        BOOST_TEST_REQUIRE(hq != nullptr);
        aimPadAt(dev, v, hq->GetFlagPos());
    }

    // --- Alle vier oeffnen ihren System-Ring, einer nach dem anderen ---
    std::vector<IngameWindow*> menus(4, nullptr);
    for(unsigned v = 0; v < 4u; ++v)
    {
        press(static_cast<PadDeviceId>(20 + v), PadButton::Back);
        menus[v] = WINDOWMANAGER.FindNonModalWindow(CGI_PADMENU, v);
        BOOST_TEST_CONTEXT("Sitzplatz " << v)
        {
            BOOST_TEST_REQUIRE(menus[v] != static_cast<IngameWindow*>(nullptr));
            BOOST_TEST_REQUIRE(view(v).GetRing().IsOpen());
        }
    }

    // ALLE VIER SIND GLEICHZEITIG OFFEN - und jeder zeigt SEIN eigenes Fenster.
    for(unsigned v = 0; v < 4u; ++v)
    {
        BOOST_TEST_CONTEXT("Sitzplatz " << v)
        {
            BOOST_TEST(view(v).GetRing().IsOpen());
            BOOST_TEST(view(v).GetFocus().GetRoot() == static_cast<Window*>(menus[v]));
            BOOST_TEST(menus[v]->GetOwner() == v);
        }
    }
    // Und es sind wirklich VIER verschiedene Fenster, nicht viermal dasselbe.
    for(unsigned a = 0; a < 4u; ++a)
    {
        for(unsigned b2 = a + 1; b2 < 4u; ++b2)
            BOOST_TEST(menus[a] != menus[b2]);
    }

    // --- Jeder dreht auf eine ANDERE Zahl von Sektoren weiter ---
    //
    // Sitzplatz v dreht v-mal. Danach muessen vier VERSCHIEDENE Sektoren gewaehlt sein - und
    // zwar jeder im eigenen Fenster. Das ist der Kern von (c): Spieler 1 aendert mit seiner
    // Auswahl nichts an Spieler 0.
    std::vector<const Window*> before(4, nullptr);
    for(unsigned v = 0; v < 4u; ++v)
        before[v] = view(v).GetFocus().GetFocused();

    for(unsigned v = 0; v < 4u; ++v)
    {
        for(unsigned i = 0; i < v; ++i)
            press(static_cast<PadDeviceId>(20 + v), PadButton::DpadRight);
    }

    std::vector<unsigned> chosenIds;
    for(unsigned v = 0; v < 4u; ++v)
    {
        const Window* const focused = view(v).GetFocus().GetFocused();
        BOOST_TEST_CONTEXT("Sitzplatz " << v)
        {
            BOOST_TEST_REQUIRE(focused != static_cast<const Window*>(nullptr));
            // Der Fokus steht im EIGENEN Fenster - nie im des Nachbarn.
            BOOST_TEST(view(v).GetFocus().GetRoot() == static_cast<Window*>(menus[v]));
            // Sitzplatz 0 hat NICHT gedreht und steht deshalb noch auf seinem ersten Sektor.
            if(v == 0)
                BOOST_TEST(focused == before[0]);
            else
                BOOST_TEST(focused != before[v]);
        }
        chosenIds.push_back(focused->GetID());
    }
    BOOST_TEST_MESSAGE("AUDIT: gewaehlte Sektorkennungen der vier Sitzplaetze = "
                       << chosenIds[0] << " " << chosenIds[1] << " " << chosenIds[2] << " " << chosenIds[3]);
    // VIER VERSCHIEDENE Kennungen. Waere der Ringzustand geteilt, staenden hier vier gleiche.
    std::vector<unsigned> unique_ = chosenIds;
    std::sort(unique_.begin(), unique_.end());
    unique_.erase(std::unique(unique_.begin(), unique_.end()), unique_.end());
    BOOST_TEST(unique_.size() == 4u);

    // Und die LEISTE jedes Sitzplatzes nennt die Ringbelegung - viermal, unabhaengig.
    const auto hasHint = [](const brief::Brief& b, const PadButton button, const brief::KeyAction action) {
        return std::any_of(b.keys.begin(), b.keys.end(),
                           [&](const brief::KeyHint& h) {
                               return h.input == brief::KeyInput::Button && h.button == button
                                      && h.action == action;
                           });
    };
    for(unsigned v = 0; v < 4u; ++v)
        BOOST_TEST_CONTEXT("Sitzplatz " << v)
    BOOST_TEST(hasHint(view(v).GetBrief(), PadButton::B, brief::KeyAction::CloseRing));

    // --- EINER schliesst. Die anderen drei bleiben, wo sie sind. ---
    press(22, PadButton::B);
    BOOST_TEST(!view(2).GetRing().IsOpen());
    for(const unsigned v : {0u, 1u, 3u})
    {
        BOOST_TEST_CONTEXT("Sitzplatz " << v)
        {
            BOOST_TEST(view(v).GetRing().IsOpen());
            BOOST_TEST(view(v).GetFocus().GetRoot() == static_cast<Window*>(menus[v]));
            BOOST_TEST(view(v).GetFocus().GetFocused()->GetID() == chosenIds[v]);
        }
    }

    for(const unsigned v : {0u, 1u, 3u})
        press(static_cast<PadDeviceId>(20 + v), PadButton::B);
    for(unsigned v = 0; v < 4u; ++v)
    {
        if(menus[v] && !menus[v]->ShouldBeClosed())
            menus[v]->Close();
    }
    WINDOWMANAGER.Draw();
}

/// DIE HAERTERE HAELFTE VON (c): eine Auswahl des einen darf am anderen NICHTS aendern - auch
/// nicht an dem, was der andere gleich AUSLOEST.
///
/// Zwei Sitzplaetze stehen im System-Ring auf demselben Punkt. Sitzplatz 1 dreht weiter und
/// loest aus; danach hat GENAU EIN Sitzplatz gehandelt, und der andere steht unveraendert da,
/// wo er stand - mit demselben Sektor, demselben Fenster und demselben Anzeigezustand.
BOOST_FIXTURE_TEST_CASE(OneSeatsRingChoiceChangesNothingForTheOther, PadViewFixture<2>)
{
    for(unsigned v = 0; v < 2u; ++v)
    {
        const PadDeviceId dev = static_cast<PadDeviceId>(20 + v);
        seatPad(*this, dev, v);
        const MapPoint hqPos = worldFixture.world.GetPlayer(v).GetHQPos();
        const auto* hq = worldFixture.world.GetSpecObj<nobBaseWarehouse>(hqPos);
        BOOST_TEST_REQUIRE(hq != nullptr);
        aimPadAt(dev, v, hq->GetFlagPos());
        press(dev, PadButton::Back);
        BOOST_TEST_REQUIRE(view(v).GetRing().IsOpen());
    }

    // Beide stehen auf demselben Sektor - auf dem ersten.
    const unsigned startId0 = view(0).GetFocus().GetFocused()->GetID();
    BOOST_TEST_REQUIRE(view(1).GetFocus().GetFocused()->GetID() == startId0);

    const bool names0 = view(0).GetView().IsShowingNames();
    const bool prod0 = view(0).GetView().IsShowingProductivity();

    // Sitzplatz 1 dreht auf "Namen" und loest aus.
    for(unsigned i = 0; i < 16u; ++i)
    {
        if(view(1).GetFocus().GetFocused()->GetID() == iwPadSystemMenu::ID_NAMES)
            break;
        press(21, PadButton::DpadRight);
    }
    BOOST_TEST_REQUIRE(view(1).GetFocus().GetFocused()->GetID() == unsigned(iwPadSystemMenu::ID_NAMES));
    const bool names1 = view(1).GetView().IsShowingNames();
    press(21, PadButton::A);

    // ER hat geschaltet.
    BOOST_TEST(view(1).GetView().IsShowingNames() == !names1);
    // DER ANDERE NICHT - weder in der Anzeige, noch im Ring, noch im Fokus.
    BOOST_TEST(view(0).GetView().IsShowingNames() == names0);
    BOOST_TEST(view(0).GetView().IsShowingProductivity() == prod0);
    BOOST_TEST(view(0).GetRing().IsOpen());
    BOOST_TEST(view(0).GetFocus().GetFocused()->GetID() == startId0);

    press(20, PadButton::B);
    press(21, PadButton::B);
    for(unsigned v = 0; v < 2u; ++v)
    {
        if(IngameWindow* const menu = WINDOWMANAGER.FindNonModalWindow(CGI_PADMENU, v))
        {
            if(!menu->ShouldBeClosed())
                menu->Close();
        }
    }
    WINDOWMANAGER.Draw();
}

// ============================================================================================
// 7. BEFUND K1 - DIE BESCHRIFTUNGEN MUESSEN IN IHREN PLATZ PASSEN
// ============================================================================================

/// DER SICHTBARSTE FEHLER DER PHASE, und er trifft genau den Fernseher, um den es geht.
///
/// Der Umsetzer hatte behauptet, Nachbarbeschriftungen ueberlappten "rechnerisch nicht". Das war
/// die EINZIGE Stelle, an der er gerechnet statt gemessen hat, und genau dort war das Ergebnis
/// verkehrt: sechs von sieben deutschen Beschriftungen waren zu breit fuer ihren Sektor, im
/// Franzoesischen war eine breiter als der ganze Ring, im Polnischen alle sieben zu breit.
///
/// Dieser Fall RECHNET NICHT, er MISST - mit den ausgelieferten Schriften, in drei Sprachen und
/// bei BEIDEN Ringdurchmessern (200 bei der Testaufloesung, 300 als Deckel auf dem Zielgeraet).
/// Gemessen wird der KASTEN, IN DEN GEZEICHNET WIRD (RingEntry::labelBox) - dieselbe Zahl, die
/// dskGameInterface::DrawRing benutzt. Eine zweite Rechnung im Nachweis koennte neben der
/// ersten veralten; genau das war der Fehler, den dieser Fall bewacht.
///
/// Drei Zusicherungen, und keine davon ist "es sieht gut aus":
///   (1) JEDER Eintrag ist lesbar: entweder Bild oder Text. Ein stummer Sektor waere eine Tuer
///       ohne Schild, und der Ring ist die EINZIGE Tuer zu den Symbolschaltern.
///   (2) KEIN Kasten verlaesst die freie Flaeche (Viewport geschnitten mit Safe Area, oberhalb
///       des Klartextkastens). Vier Beschriftungen ragten vorher ueber den Ring hinaus.
///   (3) KEINE ZWEI Kaesten ueberlappen einander. Zwei Paare taten es vorher.
BOOST_FIXTURE_TEST_CASE(EveryRingLabelFitsItsPlaceInEveryMeasuredLanguage, PadViewFixture<2>)
{
    const ScreenSetting restoreScreen;

    struct Screen
    {
        const char* what;
        unsigned w, h;
        bool tv;
    };
    // Der Ringdurchmesser ist NICHT fest: er ist das Minimum aus 300 (dem Deckel), 0,85 der
    // freien Hoehe und 0,5 der freien Breite. Beide Pruefer hatten recht, jeder fuer seinen
    // Bildschirm - deshalb wird hier auf beiden gemessen.
    const Screen screens[] = {{"Testaufloesung 800x600", 800, 600, false},
                              {"Zielgeraet 4K im Fernsehmodus", 3840, 2160, true}};
    // Deutsch, Franzoesisch, Polnisch - genau die drei, in denen der Befund gemessen wurde.
    const char* const langs[] = {"de", "fr", "pl"};

    unsigned passes = 0;
    unsigned entriesSeen = 0;
    for(const Screen& sc : screens)
    {
        ScreenSetting::use(sc.w, sc.h, sc.tv);
        // Der Desktop rechnet seine Viewports beim Aufbau aus - nach einer Aufloesungsaenderung
        // muss er also neu entstehen, sonst maesse dieser Fall gegen alte Rechtecke.
        restartDesktop();
        for(const char* const lang : langs)
        {
            const rttr::test::LocaleResetter useLang(lang);
            // Das Menue wird UNTER DIESER SPRACHE gebaut: iwPadSystemMenu setzt seine
            // Beschriftungen im Konstruktor und in UpdateToggleLabels ueber _(). Deshalb wird
            // der Ring je Sprache frisch geoeffnet und wieder geschlossen.
            seatPad(*this, 11, 1);
            press(11, PadButton::Back);
            BOOST_TEST_REQUIRE(view(1).GetRing().IsOpen());
            const auto layout = dsk->LayoutRing(view(1));
            BOOST_TEST_REQUIRE(!layout.empty());

            const float rMid = (layout.rInner + layout.rOuter) / 2.f;
            const float arc = 2.f * 3.14159265358979323846f * rMid / static_cast<float>(layout.entries.size());
            BOOST_TEST_MESSAGE("AUDIT: " << sc.what << ", Sprache " << lang << ": Durchmesser "
                                         << static_cast<int>(2.f * layout.rOuter) << ", " << layout.entries.size()
                                         << " Sektoren, Sektorbogen " << static_cast<int>(arc) << ", freie Flaeche "
                                         << layout.freeArea.getSize() << " bei " << layout.freeArea.getOrigin());

            for(unsigned i = 0; i < layout.entries.size(); ++i)
            {
                const auto& e = layout.entries[i];
                ++entriesSeen;
                std::string text;
                for(const std::string& line : e.labelLines)
                    text += (text.empty() ? "" : "|") + line;
                BOOST_TEST_MESSAGE("  Sektor " << i << " " << (e.icon ? "[Bild] " : "") << "\"" << text
                                               << "\" Kasten " << e.labelBox.getSize() << " bei "
                                               << e.labelBox.getOrigin());
                BOOST_TEST_CONTEXT(sc.what << " / " << lang << " / Sektor " << i << " \"" << text << "\"")
                {
                    // (1) Nichts ist stumm.
                    BOOST_TEST((e.icon != nullptr || !e.labelLines.empty()));
                    // (2) Nichts ragt aus der freien Flaeche.
                    BOOST_TEST(boxInside(e.labelBox, layout.freeArea));
                    if(!e.icon)
                    {
                        // (4) TEXT STEHT NEBEN DEM RING, nicht darin - das ist die Loesung des
                        // Befundes, und sie wird hier gemessen und nicht geglaubt: JEDE Ecke
                        // des Kastens liegt ausserhalb des Aussenradius. Ein Bild bleibt
                        // dagegen im Sektor; deshalb steht diese Frage nur fuer Text.
                        const PointF c(layout.center);
                        const int xs[] = {e.labelBox.left, e.labelBox.right};
                        const int ys[] = {e.labelBox.top, e.labelBox.bottom};
                        float minR = 1e9f;
                        for(const int x : xs)
                            for(const int y : ys)
                                minR = std::min(minR, radiusOf(c, PointF(static_cast<float>(x), static_cast<float>(y))));
                        BOOST_TEST(minR >= layout.rOuter);
                        // (5) AUF DEM ZIELGERAET wird kein Wort umgebrochen. Der Auftraggeber
                        // sitzt drei Meter vor einem 55-Zoll-Fernseher; dort muss jedes der
                        // sieben Tuerschilder in einer Zeile stehen. Auf einem 400 Punkte
                        // breiten Testviewport bricht der Umbruch bewusst um, statt die
                        // Schrift zu verkleinern - das ist die Entartung, nicht der Normalfall.
                        if(sc.tv)
                            BOOST_TEST(e.labelLines.size() == 1u);
                    }
                }
            }
            // (3) Nichts ueberlappt.
            for(unsigned i = 0; i < layout.entries.size(); ++i)
            {
                for(unsigned j = i + 1; j < layout.entries.size(); ++j)
                {
                    BOOST_TEST_CONTEXT(sc.what << " / " << lang << " / Sektoren " << i << " und " << j)
                    BOOST_TEST(!boxesOverlap(layout.entries[i].labelBox, layout.entries[j].labelBox));
                }
            }
            ++passes;
            press(11, PadButton::B);
            BOOST_TEST_REQUIRE(!view(1).GetRing().IsOpen());
        }
    }
    // Waere hier 0, waere der ganze Fall wertlos - genau die Sorte stiller Schrumpfung, an der
    // ein Vorgaengernachweis dieses Projekts schon einmal gestorben ist.
    BOOST_TEST_MESSAGE("AUDIT: " << passes << " Durchgaenge, " << entriesSeen << " Beschriftungen gemessen");
    BOOST_TEST(passes == 6u);
    BOOST_TEST(entriesSeen >= 42u);
}

// ============================================================================================
// 8. BEFUND K2 - IM RING WIRKEN TASTEN, DIE NIEMAND NENNT
// ============================================================================================

/// DER MASSSTAB AUS PHASE 12, in drei Korrekturrunden erkaempft: die Tastenhinweisleiste darf
/// NIRGENDS luegen, und sie soll keine Taste verschweigen, die wirkt. Im Ring hielt sie ihn
/// nicht - beide Pruefer haben es unabhaengig gemessen:
///
///   4A  LB und RB wirkten im EINSEITIGEN Ring, ohne genannt zu werden, und warfen die Auswahl
///       WORTLOS auf Sektor 0 zurueck. Der Quelltext behauptete daneben ausdruecklich das
///       Gegenteil.
///   4B  Back schloss den Ring, ungenannt.
///   4C  DpadUp drehte den Ring, ungenannt.
///   4D  DpadDown ebenso.
///   4E  Der linke Stick - das HAUPTZEIGEMITTEL - konnte von der Leiste konstruktiv gar nicht
///       genannt werden, weil ein Stickausschlag kein PadButton ist.
///
/// Dieser Fall geht ALLE Knoepfe durch, einen nach dem anderen, ueber den produktiven Weg
/// (Padereignis in die Warteschlange des Treibers, dann dskGameInterface::UpdateInput). Fuer
/// jeden wird die Leiste VOR dem Druck gelesen und der Zustand des Sitzplatzes VOR und NACH dem
/// Druck verglichen. Die Zusicherung ist eine GLEICHHEIT in beide Richtungen - was wirkt, steht
/// da, und was dasteht, wirkt. Dazu der Stick, der kein Knopf ist und trotzdem genannt gehoert.
BOOST_FIXTURE_TEST_CASE(InTheRingTheBarNamesEveryInputThatDoesSomething, PadViewFixture<2>)
{
    const MapPoint flagPt = hqFlagOf(worldFixture.world, 1);
    seatPad(*this, 11, 1);
    aimPadAt(11, 1, flagPt);

    /// ALLES, woran man merken kann, dass ein Druck etwas getan hat. Bewusst breit: waere hier
    /// nur der Fokus, hielte der Fall einen Umschalter (Bauhilfe an/aus) faelschlich fuer
    /// wirkungslos, und die Gleichheit unten waere eine Luege in die andere Richtung.
    struct Snapshot
    {
        bool ringOpen;
        unsigned page;
        const Window* focused;
        const Window* root;
        const IngameWindow* top;
        bool showBQ, showNames, showProductivity, watchOnly;

        bool operator==(const Snapshot& o) const
        {
            return ringOpen == o.ringOpen && page == o.page && focused == o.focused && root == o.root
                   && top == o.top && showBQ == o.showBQ && showNames == o.showNames
                   && showProductivity == o.showProductivity && watchOnly == o.watchOnly;
        }
    };
    const auto snap = [&]() {
        PlayerView& v = view(1);
        return Snapshot{v.GetRing().IsOpen(),
                        v.GetRing().GetPage(),
                        v.GetFocus().GetFocused(),
                        v.GetFocus().GetRoot(),
                        WINDOWMANAGER.GetTopMostWindow(1u),
                        v.GetView().IsShowingBQ(),
                        v.GetView().IsShowingNames(),
                        v.GetView().IsShowingProductivity(),
                        v.IsWatchOnly()};
    };
    const auto closeRing = [&]() {
        for(int i = 0; i < 3 && view(1).GetRing().IsOpen(); ++i)
            press(11, PadButton::B);
        for(unsigned v = 0; v < 2u; ++v)
        {
            if(IngameWindow* const w = WINDOWMANAGER.GetTopMostWindow(v))
            {
                if(!w->ShouldBeClosed() && w->getCloseBehavior() == CloseBehavior::Regular)
                    w->Close();
            }
        }
        step(16);
        if(view(1).IsWatchOnly())
            press(11, PadButton::B);
        step(16);
    };
    /// Den Ring frisch oeffnen und den Fokus VOM ERSTEN SEKTOR WEGSTELLEN. Das zweite ist der
    /// Kern von 4A: der stille Ruecksprung auf Sektor 0 ist nur zu sehen, wenn man vorher
    /// woanders steht.
    const auto openRingAwayFromSectorZero = [&]() {
        if(!view(1).GetRing().IsOpen())
            press(11, PadButton::Back);
        BOOST_TEST_REQUIRE(view(1).GetRing().IsOpen());
        press(11, PadButton::DpadRight);
        press(11, PadButton::DpadRight);
    };

    // --- (I) DER EINSEITIGE RING, Knopf fuer Knopf ----------------------------------------
    unsigned named = 0, acted = 0, checked = 0;
    for(const PadButton button : helpers::enumRange<PadButton>())
    {
        closeRing();
        openRingAwayFromSectorZero();
        BOOST_TEST_REQUIRE(!dskGameInterface::RingHasPages(view(1))); // das Systemmenue ist EINSEITIG
        const std::string bar = dumpRingKeys(view(1).GetBrief());
        const auto claimed = ringActionFor(view(1).GetBrief(), button);
        const Snapshot before = snap();
        press(11, button);
        const bool reallyActed = !(snap() == before);
        ++checked;
        if(claimed)
            ++named;
        if(reallyActed)
            ++acted;
        BOOST_TEST_CONTEXT("Knopf " << brief::PadButtonLabel(button) << "  Leiste=" << bar)
        BOOST_TEST(reallyActed == claimed.has_value());
    }
    BOOST_TEST_MESSAGE("AUDIT: " << checked << " Knoepfe im einseitigen Ring, genannt = " << named
                                 << ", gewirkt = " << acted);
    BOOST_TEST(acted > 0u);
    BOOST_TEST(named == acted);

    // --- (II) DIE FUENF BEFUNDE, jeder EINZELN benannt -------------------------------------
    closeRing();
    openRingAwayFromSectorZero();
    BOOST_TEST_MESSAGE("AUDIT: Leiste im Ring = " << brief::KeyLine(view(1).GetBrief().keys));

    // 4A: LB und RB stehen NICHT da - und sie tun jetzt auch wirklich nichts mehr. Vorher warfen
    // sie die Auswahl wortlos auf Sektor 0.
    BOOST_TEST(!ringNamesButton(view(1).GetBrief(), PadButton::LeftShoulder));
    BOOST_TEST(!ringNamesButton(view(1).GetBrief(), PadButton::RightShoulder));
    const Window* const focusedBefore = view(1).GetFocus().GetFocused();
    BOOST_TEST_REQUIRE(focusedBefore != static_cast<const Window*>(nullptr));
    press(11, PadButton::LeftShoulder);
    BOOST_TEST(view(1).GetFocus().GetFocused() == focusedBefore);
    press(11, PadButton::RightShoulder);
    BOOST_TEST(view(1).GetFocus().GetFocused() == focusedBefore);
    BOOST_TEST(view(1).GetRing().GetPage() == 0u);

    // 4C und 4D: das GANZE Steuerkreuz steht da - und jede Richtung dreht wirklich.
    for(const PadButton dpad :
        {PadButton::DpadLeft, PadButton::DpadRight, PadButton::DpadUp, PadButton::DpadDown})
    {
        BOOST_TEST_CONTEXT("Steuerkreuz " << brief::PadButtonLabel(dpad))
        {
            BOOST_TEST(ringHasHint(view(1).GetBrief(), dpad, brief::KeyAction::TurnRing));
            const Window* const was = view(1).GetFocus().GetFocused();
            press(11, dpad);
            BOOST_TEST(view(1).GetFocus().GetFocused() != was);
        }
    }

    // 4E: DER LINKE STICK steht in der Leiste - als STICK und nicht als geliehener Knopf. Und
    // er zeigt wirklich: ein Vollausschlag nach oben stellt den Fokus auf Sektor 0.
    {
        const brief::Brief& b = view(1).GetBrief();
        const auto stickHint =
          std::find_if(b.keys.begin(), b.keys.end(),
                       [](const brief::KeyHint& h) { return h.input == brief::KeyInput::LeftStickAxis; });
        BOOST_TEST_REQUIRE((stickHint != b.keys.end()));
        BOOST_TEST((stickHint->action == brief::KeyAction::AimRing));
        // ... und der Stick ist NICHT als PadButton::LeftStick (der Stickklick, L3) ausgegeben -
        // sonst verspraeche die Leiste einen Klick, der nichts tut.
        BOOST_TEST(!ringNamesButton(b, PadButton::LeftStick));
        // ... und er steht wirklich in der Zeile, die der Spieler liest.
        BOOST_TEST(brief::KeyLine(b.keys).find(brief::KeyInputLabel(*stickHint)) != std::string::npos);
    }
    {
        unsigned numPages = 1;
        const std::vector<Window*> ctrls = dskGameInterface::RingPageCtrls(view(1), numPages);
        BOOST_TEST_REQUIRE(!ctrls.empty());
        for(int frame = 0; frame < 60; ++frame)
        {
            pads.axis(11, PadAxis::LeftX, 0.f);
            pads.axis(11, PadAxis::LeftY, -1.f); // Vollausschlag nach OBEN = Sektor 0
            step(16);
        }
        pads.axis(11, PadAxis::LeftY, 0.f);
        step(16);
        BOOST_TEST(view(1).GetFocus().GetFocused() == ctrls.front());
    }

    // 4B: BACK steht da - und schliesst den Ring wirklich. Das ist die letzte Zusicherung,
    // weil sie den Ring zumacht.
    BOOST_TEST(ringHasHint(view(1).GetBrief(), PadButton::Back, brief::KeyAction::CloseRing));
    BOOST_TEST(ringHasHint(view(1).GetBrief(), PadButton::B, brief::KeyAction::CloseRing));
    press(11, PadButton::Back);
    BOOST_TEST(!view(1).GetRing().IsOpen());

    closeRing();
}

/// DIE GEGENPROBE ZU 4A: wo es WIRKLICH etwas zu blaettern gibt, stehen LB und RB da UND wirken.
/// Ohne diesen Fall waere die Korrektur oben nur "die Schultern tun nie etwas" - und das waere
/// eine andere Luege.
BOOST_FIXTURE_TEST_CASE(WhereThereReallyArePagesTheBarNamesTheShouldersAndTheyWork, PadGameFixture)
{
    setUpTwoLocalPlayers();
    PlayerView& padView = dsk->GetPlayerView(1);
    const MapPoint spot = findBuildSpotFor(world(), padView.GetViewer(), BuildingQuality::Castle);
    BOOST_TEST_REQUIRE(spot.isValid());
    // Erst Platz 0, dann Platz 1 - der Router vergibt die Sitzplaetze in der Reihenfolge, in
    // der die Pads in die Hand genommen werden.
    aimPadAt(10, 0, hqFlagOf(world(), 0));
    aimPadAt(11, 1, spot);
    // Das AKTIONSFENSTER hat Reiter (Bauen/Flagge/Anzeige) - dort gibt es wirklich Seiten.
    press(11, PadButton::A);
    BOOST_TEST_REQUIRE(padView.GetRing().IsOpen());
    BOOST_TEST_REQUIRE(dskGameInterface::RingHasPages(padView));

    BOOST_TEST_MESSAGE("AUDIT: Leiste im mehrseitigen Ring = " << brief::KeyLine(padView.GetBrief().keys));
    BOOST_TEST(ringHasHint(padView.GetBrief(), PadButton::RightShoulder, brief::KeyAction::RingNextPage));
    BOOST_TEST(ringHasHint(padView.GetBrief(), PadButton::LeftShoulder, brief::KeyAction::RingPrevPage));

    // ... UND GEDRUECKT: es aendert sich wirklich etwas.
    unsigned numPages = 1;
    const std::vector<Window*> before = dskGameInterface::RingPageCtrls(padView, numPages);
    const unsigned pageBefore = padView.GetRing().GetPage();
    press(11, PadButton::RightShoulder);
    const std::vector<Window*> after = dskGameInterface::RingPageCtrls(padView, numPages);
    BOOST_TEST_MESSAGE("AUDIT: Seite " << pageBefore << " -> " << padView.GetRing().GetPage() << ", "
                                       << before.size() << " -> " << after.size() << " Eintraege");
    BOOST_TEST((padView.GetRing().GetPage() != pageBefore || after != before));

    press(11, PadButton::B);
}

// ============================================================================================
// 9. BEFUND K3 - DAS UNSICHTBARE RINGFENSTER FING DIE MAUS
// ============================================================================================

/// DIE EINZIGE REGRESSION DIESER PHASE, und sie trifft den Alltag dieses Projekts: EIN Mensch
/// an der Maus, drei am Pad.
///
/// GEMESSEN: der Ring setzt sein Traegerfenster auf SetVisible(false), laesst es aber mit
/// unveraendertem Rechteck im Stapel des WindowManagers stehen. WindowManager::FindWindowAtPos
/// fragte die Sichtbarkeit NICHT. Der Mensch an der Maus sieht an dieser Stelle die Karte; sein
/// Rechtsklick landete im Fenster, schloss es, kam auf der Karte nie an - und riss dem
/// Padspieler nebenbei den Ring weg. WindowManager::Msg_RightDown schliesst das gefundene
/// Fenster naemlich VOR jedem Weiterreichen; IngameWindow::IsMessageRelayAllowed konnte das
/// nicht abfangen, weil bis dorthin gar nichts weitergereicht wird.
///
/// Gemessen wird ueber den PRODUKTIVEN MAUSWEG (WindowManager::Msg_RightDown /
/// ::Msg_LeftDown mit einem echten Punkt), und zwar in BEIDE Richtungen. Teil (A) zuerst, und
/// zwar mit Absicht: er haelt fest, dass ein SICHTBARES Fenster die Maus weiterhin faengt und
/// vom Rechtsklick zugeht - das ist der Mausspieler von heute, und ohne diese Haelfte hiesse
/// die Korrektur nur "die Maus findet nie ein Fenster".
BOOST_FIXTURE_TEST_CASE(TheInvisibleRingWindowNoLongerSwallowsTheNeighboursMouse, PadViewFixture<2>)
{
    // --- (A) DER MAUSSPIELER VON HEUTE: sichtbares Fenster, Klick trifft, Fenster geht zu ---
    {
        IngameWindow* plain = nullptr;
        {
            const dskGameInterface::ViewScope scope(1);
            plain = &WINDOWMANAGER.Show(std::make_unique<iwPadSystemMenu>(*dsk, view(1), DrawPoint(40, 40)));
        }
        step(16);
        BOOST_TEST_REQUIRE(plain->IsVisible());
        const Rect r = plain->GetDrawRect();
        const Position p(static_cast<int>((r.left + r.right) / 2), static_cast<int>((r.top + r.bottom) / 2));
        BOOST_TEST_REQUIRE(WINDOWMANAGER.FindWindowAtPos(p) == plain);
        WINDOWMANAGER.Msg_RightDown(MouseCoords(p));
        BOOST_TEST_MESSAGE("AUDIT: sichtbares Fenster nach Rechtsklick geschlossen = " << plain->ShouldBeClosed());
        BOOST_TEST(plain->ShouldBeClosed());
        WINDOWMANAGER.Draw(); // raeumt das geschlossene Fenster wirklich aus dem Stapel
    }

    // --- (B) DER RING: unsichtbares Fenster, dieselben Klicks, und nichts passiert ---------
    const MapPoint flagPt = hqFlagOf(worldFixture.world, 1);
    seatPad(*this, 11, 1);
    aimPadAt(11, 1, flagPt);
    press(11, PadButton::Back);
    BOOST_TEST_REQUIRE(view(1).GetRing().IsOpen());
    auto* const menu = dynamic_cast<IngameWindow*>(view(1).GetFocus().GetRoot());
    BOOST_TEST_REQUIRE(menu != static_cast<IngameWindow*>(nullptr));
    // Die Voraussetzung des Befundes: das Fenster ist UNSICHTBAR und steht trotzdem im Stapel,
    // mit seinem vollen Rechteck.
    BOOST_TEST_REQUIRE(!menu->IsVisible());
    const Rect rect = menu->GetDrawRect();
    const Position hit(static_cast<int>((rect.left + rect.right) / 2),
                       static_cast<int>((rect.top + rect.bottom) / 2));
    BOOST_TEST_MESSAGE("AUDIT: Rechteck des unsichtbaren Ringfensters = "
                       << rect.getOrigin() << " bis "
                       << (rect.getOrigin() + Position(static_cast<int>(rect.getSize().x),
                                                       static_cast<int>(rect.getSize().y)))
                       << ", geprueft wird bei " << hit);

    // (1) DIE FRAGE SELBST: findet die Maus dort ein Fenster?
    BOOST_TEST_MESSAGE("AUDIT: FindWindowAtPos trifft das unsichtbare Ringfenster = "
                       << (WINDOWMANAGER.FindWindowAtPos(hit) == menu));
    BOOST_TEST(WINDOWMANAGER.FindWindowAtPos(hit) != menu);

    // (2) DER RECHTSKLICK des Nachbarn - ueber den produktiven Weg.
    WINDOWMANAGER.Msg_RightDown(MouseCoords(hit));
    BOOST_TEST_MESSAGE("AUDIT: Rechtsklick des Nachbarn schliesst den Ring = " << menu->ShouldBeClosed());
    BOOST_TEST(!menu->ShouldBeClosed());
    BOOST_TEST(view(1).GetRing().IsOpen());
    BOOST_TEST(view(1).GetFocus().GetRoot() == static_cast<Window*>(menu));

    // (3) UND DER LINKSKLICK, nach derselben Kette.
    WINDOWMANAGER.Msg_LeftDown(MouseCoords(hit));
    BOOST_TEST(!menu->ShouldBeClosed());
    BOOST_TEST(view(1).GetRing().IsOpen());

    press(11, PadButton::B);
    WINDOWMANAGER.Draw();
}

// ============================================================================================
// 10. BEFUND K4 - DIE TUERSCHILDER DES RINGS UND IHRE SPRACHEN
// ============================================================================================

/// DER BEFUND: die neuen Phase-13-Zeichenketten ("Names: off", "Output: off", "Just watch")
/// standen unuebersetzt in allen 27 Katalogen. Der Ring ist die EINZIGE Tuer zu den
/// Symbolschaltern - drei von sieben Tuerschildern waren ausserhalb des Deutschen englisch.
///
/// DIE ENTSCHEIDUNG, und sie folgt dem Praezedenzfall, den dieses Projekt sich in Phase 12
/// selbst gesetzt hat (CMakeLists.txt, --no-fuzzy-matching): eine unuebersetzte msgid faellt
/// auf das englische Original zurueck, "was ehrlich ist und sichtbar bleibt" - GERATEN wird
/// nicht. 26 Kataloge mit Uebersetzungen zu fuellen, die niemand hier nachpruefen kann, waere
/// genau das Raten, das dort mit Gruenden verboten wurde; eine falsche Uebersetzung ist eine
/// Luege, die der Spieler nicht bemerken kann. Die msgids stehen in rttr.pot und damit in jedem
/// Katalog - ein Uebersetzer findet sie, und mehr kann diese Runde ehrlich leisten.
///
/// WAS DIESE RUNDE ABER SEHR WOHL SCHULDET: die Sprache des Auftraggebers. Er spielt auf
/// Deutsch, und in SEINER Sprache darf kein Tuerschild des Rings englisch sein. Dieser Fall
/// misst genau das - ueber den produktiven Weg, und ohne eine einzige im Test abgeschriebene
/// Uebersetzung: er oeffnet denselben Ring einmal ohne Katalog und einmal auf Deutsch und
/// vergleicht Schild fuer Schild. Fuer die uebrigen Kataloge zaehlt er und BERICHTET.
BOOST_FIXTURE_TEST_CASE(EveryRingDoorSignIsTranslatedInTheClientsOwnLanguage, PadViewFixture<2>)
{
    const MapPoint flagPt = hqFlagOf(worldFixture.world, 1);

    /// Die Tuerschilder des Rings, in Sektorreihenfolge - gelesen aus DEM Layout, das auch
    /// gezeichnet wird.
    const auto doorSigns = [&](const char* const lang) {
        const rttr::test::LocaleResetter useLang(lang);
        seatPad(*this, 11, 1);
        aimPadAt(11, 1, flagPt);
        press(11, PadButton::Back);
        BOOST_TEST_REQUIRE(view(1).GetRing().IsOpen());
        const auto layout = dsk->LayoutRing(view(1));
        BOOST_TEST_REQUIRE(!layout.empty());
        std::vector<std::string> out;
        for(const auto& e : layout.entries)
        {
            std::string joined;
            for(const std::string& line : e.labelLines)
                joined += (joined.empty() ? "" : " ") + line;
            out.push_back(joined);
        }
        press(11, PadButton::B);
        BOOST_TEST_REQUIRE(!view(1).GetRing().IsOpen());
        return out;
    };

    // "C" stellt keinen Katalog ein - was hier herauskommt, ist der englische Quelltext.
    const std::vector<std::string> source = doorSigns("C");
    BOOST_TEST_REQUIRE(source.size() == 7u);
    BOOST_TEST_MESSAGE("AUDIT: Quelltext = " << boost::algorithm::join(source, " / "));

    const std::vector<std::string> german = doorSigns("de");
    BOOST_TEST_REQUIRE(german.size() == source.size());
    BOOST_TEST_MESSAGE("AUDIT: Deutsch   = " << boost::algorithm::join(german, " / "));
    unsigned translated = 0;
    for(unsigned i = 0; i < source.size(); ++i)
    {
        BOOST_TEST_CONTEXT("Tuerschild " << i << " \"" << source[i] << "\"")
        BOOST_TEST(german[i] != source[i]);
        if(german[i] != source[i])
            ++translated;
    }
    BOOST_TEST_MESSAGE("AUDIT: uebersetzte Tuerschilder auf Deutsch = " << translated << " von " << source.size());
    BOOST_TEST(translated == source.size());

    // --- UND DER BERICHT ueber alle ausgelieferten Kataloge. Bewusst OHNE Zusicherung: das
    //     englische Original ist der abgesprochene Rueckfall, nicht ein Fehler. Die Zahl steht
    //     hier, damit sie beim naechsten Uebersetzungslauf jemand sieht - ein Befund, den
    //     niemand mehr misst, verschwindet.
    unsigned catalogs = 0;
    unsigned complete = 0;
    for(const std::string& code : shippedRingCatalogs())
    {
        const std::vector<std::string> signs = doorSigns(code.c_str());
        BOOST_TEST_REQUIRE(signs.size() == source.size());
        unsigned n = 0;
        for(unsigned i = 0; i < source.size(); ++i)
        {
            if(signs[i] != source[i])
                ++n;
        }
        ++catalogs;
        if(n == source.size())
            ++complete;
        BOOST_TEST_MESSAGE("AUDIT: Katalog " << code << ": " << n << " von " << source.size()
                                             << " Tuerschildern uebersetzt");
    }
    BOOST_TEST_MESSAGE("AUDIT: " << complete << " von " << catalogs
                                 << " Katalogen tragen alle sieben Tuerschilder");
    BOOST_TEST(catalogs > 0u);
}

BOOST_AUTO_TEST_SUITE_END()
