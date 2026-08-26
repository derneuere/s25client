// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dskGameInterface.h"
#include "CollisionDetection.h"
#include "EventManager.h"
#include "Game.h"
#include "GamePlayer.h"
#include "Loader.h"
#include "NWFInfo.h"
#include "Settings.h"
#include "SoundManager.h"
#include "TvDisplay.h"
#include "WindowManager.h"
#include "input/MenuPadInput.h"
#include "addons/AddonMaxWaterwayLength.h"
#include "buildings/noBuildingSite.h"
#include "buildings/nobHQ.h"
#include "buildings/nobHarborBuilding.h"
#include "buildings/nobMilitary.h"
#include "buildings/nobStorehouse.h"
#include "buildings/nobTemple.h"
#include "buildings/nobUsual.h"
#include "controls/ctrlImageButton.h"
#include "controls/ctrlText.h"
#include "driver/MouseCoords.h"
#include "drivers/VideoDriverWrapper.h"
#include "factories/GameCommandFactory.h"
#include "helpers/containerUtils.h"
#include "helpers/format.hpp"
#include "helpers/strUtils.h"
#include "helpers/toString.h"
#include "ingameWindows/iwAIDebug.h"
#include "ingameWindows/iwAction.h"
#include "ingameWindows/iwBaseWarehouse.h"
#include "ingameWindows/iwBuildOrder.h"
#include "ingameWindows/iwBuilding.h"
#include "ingameWindows/iwBuildingProductivities.h"
#include "ingameWindows/iwBuildingSite.h"
#include "ingameWindows/iwBuildings.h"
#include "ingameWindows/iwDiplomacy.h"
#include "ingameWindows/iwDistribution.h"
#include "ingameWindows/iwEconomicProgress.h"
#include "ingameWindows/iwEndgame.h"
#include "ingameWindows/iwHQ.h"
#include "ingameWindows/iwHarborBuilding.h"
#include "ingameWindows/iwInventory.h"
#include "ingameWindows/iwMainMenu.h"
#include "ingameWindows/iwMapDebug.h"
#include "ingameWindows/iwMerchandiseStatistics.h"
#include "ingameWindows/iwMilitary.h"
#include "ingameWindows/iwMilitaryBuilding.h"
#include "ingameWindows/iwMinimap.h"
#include "ingameWindows/iwMusicPlayer.h"
#include "ingameWindows/iwOptionsWindow.h"
#include "ingameWindows/iwPadSystemMenu.h"
#include "ingameWindows/iwPostWindow.h"
#include "ingameWindows/iwRoadWindow.h"
#include "ingameWindows/iwSave.h"
#include "ingameWindows/iwSettings.h"
#include "ingameWindows/iwShip.h"
#include "ingameWindows/iwSkipGFs.h"
#include "ingameWindows/iwStatistics.h"
#include "ingameWindows/iwTempleBuilding.h"
#include "ingameWindows/iwTextfile.h"
#include "ingameWindows/iwTools.h"
#include "ingameWindows/iwTrade.h"
#include "ingameWindows/iwTransport.h"
#include "ingameWindows/iwVictory.h"
#include "lua/GameDataLoader.h"
#include "network/GameClient.h"
#include "notifications/BuildingNote.h"
#include "notifications/NotificationManager.h"
#include "ogl/FontStyle.h"
#include "ogl/SoundEffectItem.h"
#include "ogl/glArchivItem_Bitmap_Player.h"
#include "ogl/glFont.h"
#include "pathfinding/FindPathForRoad.h"
#include "postSystem/PostBox.h"
#include "postSystem/PostMsg.h"
#include "random/Random.h"
#include "world/GameWorldBase.h"
#include "world/GameWorldViewer.h"
#include "nodeObjs/noFlag.h"
#include "nodeObjs/noTree.h"
#include "gameData/BuildingProperties.h"
#include "gameData/GameConsts.h"
#include "gameData/GuiConsts.h"
#include "gameData/TerrainDesc.h"
#include "gameData/const_gui_ids.h"
#include "liblobby/LobbyClient.h"
#include "s25util/Log.h"
#include <algorithm>
#include <cstdio>
#include <utility>

namespace {
enum
{
    ID_btMap,
    ID_btOptions,
    ID_btConstructionAid,
    ID_btPost,
    ID_txtNumMsg
};

/// Size of buttons on lower bar
constexpr Extent btSize = Extent(37, 32);
/// Offsets of the buttons relative to the "border" graphics on the lower bar
constexpr DrawPoint btOffset(44, 4);

/// Linke obere Ecke der GRAFIK der unteren Knopfleiste.
///
/// Bei ausgeschaltetem Fernsehmodus liefert tv::ScreenChromeRect die volle Flaeche, dann liegt
/// die Leiste wie bisher mittig am unteren Bildschirmrand. Im Fernsehmodus wandert sie mit dem
/// Rahmen und den Statuen gemeinsam um den Safe-Rand herein - sonst schneidet ein Fernseher mit
/// Overscan genau das Bedienelement ab, das am haeufigsten gebraucht wird.
///
/// Gerechnet wird gegen den KASTEN und nicht gegen den Bildschirm, obwohl beide bei einem
/// symmetrischen Rand dieselbe Mitte haben: die Leiste sitzt auf dem Mittelstueck des unteren
/// Rahmens, das der Rahmenbauer bei size.x/2 SEINES Rahmens einsetzt. Steht die Rechnung am
/// Kasten, bleiben die beiden auch dann zusammen, wenn der Rand einmal nur auf einer Achse
/// nachgibt.
///
/// Bewusst EINE Funktion fuer alle vier Aufrufer (Konstruktor, Resize, Msg_PaintBefore,
/// Msg_LeftDown): die Grafik, die Knoepfe darauf und das Rechteck, das den Klick abfaengt,
/// muessen dieselbe Rechnung benutzen, sonst wandern sie auseinander.
///
/// EINE der vier Stellen rechnet dadurch NICHT mehr Zahl fuer Zahl wie vorher: Msg_LeftDown
/// bildete die Mitte frueher als `renderSize.x / 2 - barWidth / 2`, also mit zwei getrennten
/// Ganzzahldivisionen. Das weicht von `(renderSize.x - barWidth) / 2` - der Form, die
/// Konstruktor, Resize und Msg_PaintBefore schon immer benutzt haben - um genau einen Pixel ab,
/// sobald Bildbreite und Leistenbreite verschiedene Paritaet haben. Der Klickfaenger lag damit
/// um einen Pixel neben der gezeichneten Leiste; jetzt liegt er darauf. Das ist die einzige
/// Verhaltensaenderung ohne Fernsehmodus, und sie ist eine Korrektur.
DrawPoint CalcButtonBarOrigin(const Extent& screenSize, const Extent& barSize)
{
    const Rect chrome = tv::ScreenChromeRect(screenSize);
    const auto chromeSize = chrome.getSize();
    return DrawPoint(chrome.left + (static_cast<int>(chromeSize.x) - static_cast<int>(barSize.x)) / 2,
                     chrome.bottom - static_cast<int>(barSize.y));
}

float getNextZoomLevel(const float currentZoom)
{
    // Get first level bigger than current zoom
    // NOLINTNEXTLINE(readability-qualified-auto)
    auto it = std::upper_bound(ZOOM_FACTORS.begin(), ZOOM_FACTORS.end(), currentZoom);
    return (it == ZOOM_FACTORS.end()) ? ZOOM_FACTORS.front() : *it;
}

float getPreviousZoomLevel(const float currentZoom)
{
    // Get last level bigger or equal than current zoom
    // NOLINTNEXTLINE(readability-qualified-auto)
    auto it = std::lower_bound(ZOOM_FACTORS.begin(), ZOOM_FACTORS.end(), currentZoom);
    return (it == ZOOM_FACTORS.begin()) ? ZOOM_FACTORS.back() : *(--it);
}
} // namespace

std::vector<std::unique_ptr<PlayerView>> dskGameInterface::CreateViews(const unsigned mainPlayerIdx,
                                                                      GameWorldBase& world)
{
    // Der Hauptspieler steht immer vorn; danach die zusaetzlich lokal gesteuerten Slots aus
    // Phase 1 (network/LocalPlayerCommands, ueber GameClient::GetAdditionalLocalPlayers()).
    // Im Replay und in einer Netzwerkpartie ist diese Liste leer - dort entsteht also genau eine
    // Ansicht und alles bleibt exakt wie bisher.
    std::vector<unsigned> playerIds{mainPlayerIdx};
    for(const uint8_t id : GAMECLIENT.GetAdditionalLocalPlayers())
    {
        if(playerIds.size() >= MAX_VIEWPORTS)
        {
            // LETZTE Bremse, nicht die erste: geklemmt wird schon beim Auswerten von
            // --local-players (s25client.cpp) und in GameClient::ValidateAdditionalLocalPlayers.
            // Kaeme hier trotzdem noch etwas an, waere der Spieler lokal gesteuert, haette aber
            // weder Ansicht noch Eingabegeraet noch KI - ein stummer Geisterslot. Frueher brach
            // die Schleife dafuer wortlos ab.
            LOG.write(_("Only %1% local views are supported, ignoring the additional local "
                        "player(s) starting at slot %2%\n"))
              % MAX_VIEWPORTS % unsigned(id);
            break;
        }
        if(id < world.GetNumPlayers() && !helpers::contains(playerIds, unsigned(id)))
            playerIds.push_back(id);
    }

    const std::vector<Viewport> viewports =
      CalcViewports(VIDEODRIVER.GetRenderSize(), static_cast<unsigned>(playerIds.size()));
    RTTR_Assert(viewports.size() == playerIds.size());

    // Startzoom der KARTE. Zweiter Hebel neben der GUI-Skalierung, und fuer die Karte der
    // einzige: GameWorldView::updateEffectiveZoomFactor rechnet die GUI-Skalierung fuer die Welt
    // ausdruecklich wieder heraus (world/GameWorldView.cpp:829-833), ein Knoten bleibt also bei
    // jeder Skalierung TR_W = 56 physische Pixel breit.
    //
    // Gerechnet wird gegen die PHYSISCHE Hoehe (GetWindowSize), nicht gegen GetRenderSize() -
    // letztere ist bereits durch die GUI-Skalierung geteilt und wuerde sich selbst aufheben.
    //
    // Ohne Fernsehmodus ist das Ergebnis ZOOM_FACTORS[ZOOM_DEFAULT_INDEX] == 1.0, also exakt der
    // Wert, den der GameWorldView-Konstruktor ohnehin setzt: fuer Einzelspieler, Replay und
    // Netzwerkpartie aendert sich nichts.
    const float startZoom = tv::IsTvModeEnabled() ? tv::RecommendedZoomFactor(VIDEODRIVER.GetWindowSize().height) : 1.f;

    std::vector<std::unique_ptr<PlayerView>> result;
    result.reserve(playerIds.size());
    for(unsigned i = 0; i < playerIds.size(); ++i)
    {
        result.push_back(std::make_unique<PlayerView>(i, playerIds[i], world, viewports[i]));
        if(startZoom != 1.f) //-V550
            result.back()->GetView().SetZoomFactor(startZoom, false);
    }
    return result;
}

dskGameInterface::dskGameInterface(std::shared_ptr<Game> game, std::shared_ptr<const NWFInfo> nwfInfo,
                                   unsigned playerIdx, bool initOGL)
    : Desktop(nullptr), game_(std::move(game)), nwfInfo_(std::move(nwfInfo)),
      views_(CreateViews(playerIdx, const_cast<Game&>(*game_).world_)), worldViewer(primary().GetViewer()),
      gwv(primary().GetView()), minimap(primary().GetMinimap()), road(primary().GetRoad()),
      actionwindow(primary().actionwindow), roadwindow(primary().roadwindow),
      touchDuration(primary().touchDuration), isScrolling(primary().isScrolling),
      startScrollPt(primary().startScrollPt), cbb(*LOADER.GetPaletteN("pal5")),
      cheats_(const_cast<Game&>(*game_).world_, GAMECLIENT), cheatCommandTracker_(cheats_)
{
    SetScale(false);

    // Der WindowManager kennt nur Ansichtsnummern. Hier - und nur hier - ist bekannt, welcher
    // Spieler hinter einer Ansicht steht; deshalb haengt die Uebersetzung an diesem Objekt und
    // nicht am WindowManager. Abgemeldet wird im Destruktor.
    WINDOWMANAGER.SetWindowOwnerObserver(this);

    const glArchivItem_Bitmap& imgButtonBar = *LOADER.GetImageN("resource", 29);

    auto barPos = CalcButtonBarOrigin(GetSize(), imgButtonBar.GetSize()) + btOffset;

    AddImageButton(ID_btMap, barPos, btSize, TextureColor::Green1, LOADER.GetImageN("io", 50), _("Map"))
      ->SetBorder(false);
    barPos.x += btSize.x;
    AddImageButton(ID_btOptions, barPos, btSize, TextureColor::Green1, LOADER.GetImageN("io", 192), _("Main selection"))
      ->SetBorder(false);
    barPos.x += btSize.x;
    AddImageButton(ID_btConstructionAid, barPos, btSize, TextureColor::Green1, LOADER.GetImageN("io", 83),
                   _("Construction aid mode"))
      ->SetBorder(false);
    barPos.x += btSize.x;
    AddImageButton(ID_btPost, barPos, btSize, TextureColor::Green1, LOADER.GetImageN("io", 62), _("Post office"))
      ->SetBorder(false);
    barPos += DrawPoint(18, 24);

    AddText(ID_txtNumMsg, barPos, "", COLOR_YELLOW, FontStyle::CENTER | FontStyle::VCENTER, SmallFont);

    const_cast<Game&>(*game_).world_.SetGameInterface(this);

    std::fill(borders.begin(), borders.end(), (glArchivItem_Bitmap*)(nullptr));
    cbb.loadEdges(LOADER.GetArchive("resource"));
    // Der Rahmen wird fuer den KASTEN gebaut, nicht fuer die ganze Flaeche - er soll im
    // Fernsehmodus mit der Knopfleiste zusammen hereinruecken (tv::ScreenChromeRect). Ohne
    // Fernsehmodus ist das dieselbe Groesse wie bisher.
    cbb.buildBorder(tv::ScreenChromeRect(VIDEODRIVER.GetRenderSize()).getSize(), borders);

    // Bis zum ersten UpdateInput haelt die Hauptansicht die Maus: ein Pad kann vor dem ersten
    // Frame gar keinen Slot bekommen haben (PadRouter::SetNumSlots laeuft dort). Ohne diesen
    // Startwert waere ein Mausklick, der noch vor dem ersten UpdateInput eintrifft,
    // wirkungslos - eine Regression fuer den Einzelspieler.
    mouseView_ = views_.front().get();

    InitPlayer();
    // Die zusaetzlichen Ansichten haben keine Buttonleiste, kein Postfach und keine Fenster -
    // sie zeigen nur die Welt ihres Spielers. Eingaberouting kommt in Phase 3.
    for(unsigned i = 1; i < views_.size(); ++i)
        views_[i]->MoveToOwnHQ();

    if(initOGL)
    {
        // Je Ansicht ein eigener TerrainRenderer. Bewusst nichts geteilt: der Speicher ist
        // gemessen unkritisch (auf der groessten mitgelieferten Karte 15,7 MiB je Renderer).
        forEachView([](PlayerView& view) { view.GetViewer().InitTerrainRenderer(); });
    } else
    {
        // Ohne OpenGL bleibt die CPU-Geometrie noetig: UpdateInput rechnet ueber
        // GameWorldView::UpdateSelection -> TerrainRenderer::ConvertCoords, und das rechnet ohne
        // TerrainRenderer::Init mit size_ == (0,0). InitTerrainGeometry ruft genau dieses Init
        // und nichts weiter (world/GameWorldViewer.cpp:57-60) - kein einziger GL-Aufruf.
        forEachView([](PlayerView& view) { view.GetViewer().InitTerrainGeometry(); });
    }

    VIDEODRIVER.setTargetFramerate(SETTINGS.video.framerate); // Use requested setting for ingame

    // ... und zuletzt: alles wegwerfen, was vor dieser Partie am Pad passiert ist.
    DiscardStalePadEvents();
    // ... und dann das mitnehmen, was im Menue schon entschieden wurde.
    AdoptPadAssignmentFromMenu();
}

void dskGameInterface::DiscardStalePadEvents()
{
    IVideoDriver* const driver = VIDEODRIVER.GetDriver();
    if(!driver)
        return;
    // Der einzige Abnehmer von FetchPadEvents ist dieser Desktop. Im Hauptmenue, in der Lobby
    // und im Ladebildschirm gibt es ihn noch nicht, der Treiber sammelt aber weiter. Ohne diese
    // Stelle kaeme die gesamte Menuenavigation beim ersten UpdateInput als Flankengewitter an:
    // wer im Menue A gedrueckt hat, setzte beim Spielstart sofort eine Flagge.
    padEvents_.clear();
    driver->FetchPadEvents(padEvents_);
    for(const PadEvent& ev : padEvents_)
    {
        // Der GERAETEBESTAND muss ueberleben. Fuer ein schon vor dem Start gestecktes Pad ist
        // das Connected laengst durch (der SDL2-Treiber meldet es beim Hochfahren); wuerde es
        // hier mit weggeworfen, bliebe das Pad die ganze Partie ueber unbekannt.
        if(ev.type == PadEvent::Type::Connected || ev.type == PadEvent::Type::Disconnected)
            padRouter_.OnEvent(ev);
    }
    // ... und derselbe Bestand geht an den Menuerouter. Diese Warteschlange gehoert ab jetzt
    // dieser Partie; der WindowManager kaeme an ein hier abgeholtes Connected nie wieder heran.
    WINDOWMANAGER.NotifyPadDevices(padEvents_);
    // Achsen und Knoepfe fallen bewusst weg: ein im Menue gehaltener Stick soll den Zeiger beim
    // Spielstart nicht sofort wegschleudern, und ein im Menue gedrueckter Knopf ist keine
    // Spielhandlung. Beides heilt von selbst, sobald der Spieler das Pad wirklich benutzt.
    padEvents_.clear();
}

void dskGameInterface::AdoptPadAssignmentFromMenu()
{
    const PadRouter& menuRouter = WINDOWMANAGER.GetPadInput().GetRouter();
    const std::vector<PadDeviceId> devices = menuRouter.GetDevices();
    if(devices.empty())
        return; // niemand hat im Menue ein Pad benutzt - alles wie vor dieser Phase
    // Vor AssignSlot, sonst weist der Router jeden Slot als ausserhalb des Bereichs zurueck.
    // UpdateInput setzt denselben Wert im ersten Frame noch einmal.
    padRouter_.SetNumSlots(GetNumViews());
    for(const PadDeviceId dev : devices)
    {
        // Der Bestand kommt hier als kuenstliches Connected herein - genau die Form, in der ihn
        // sonst der Treiber liefert. Damit gibt es weiterhin nur EINEN Weg, auf dem ein Geraet
        // dem Router bekannt wird.
        padRouter_.OnEvent(PadEvent::Connected(dev));
        const unsigned slot = menuRouter.GetSlot(dev);
        if(slot < GetNumViews())
            padRouter_.AssignSlot(dev, slot);
    }
}

void dskGameInterface::LayoutViews(const Extent& renderSize)
{
    const std::vector<Viewport> viewports = CalcViewports(renderSize, GetNumViews());
    RTTR_Assert(viewports.size() == views_.size());
    for(unsigned i = 0; i < views_.size(); ++i)
        views_[i]->SetViewport(viewports[i]);
}

void dskGameInterface::InitPlayer()
{
    // Jump to players HQ if it exists
    if(worldViewer.GetPlayer().GetHQPos().isValid())
        gwv.MoveToMapPt(worldViewer.GetPlayer().GetHQPos());

    // P4: gefiltert auf JEDEN lokal dargestellten Spieler, nicht nur den Hauptspieler. Der
    // erste Vergleich ist bewusst der alte und steht bewusst zuerst: damit ist der bisherige
    // Fall (auch Replay und Zuschauer, wo es keine lokal gesteuerten Spieler gibt) bit-identisch
    // erhalten und die Erweiterung rein additiv.
    evBld = worldViewer.GetWorld().GetNotifications().subscribe<BuildingNote>([this](const auto& note) {
        if(note.player == worldViewer.GetPlayerId())
        {
            this->OnBuildingNote(note);
            return;
        }
        for(const auto& view : views_)
        {
            if(note.player == view->GetPlayerId())
            {
                this->OnBuildingNote(note);
                return;
            }
        }
    });
    // Ein Postfach fuer JEDE dargestellte Ansicht, nicht nur fuer die Hauptansicht. Ohne das
    // wird die Post der Spieler 2 bis 4 gar nicht erst aufgehoben (siehe GetPostBoxFor), und
    // das Padmenue oeffnete ein leeres Fenster, in dem nie etwas ankommen kann.
    forEachView([this](PlayerView& view) { GetPostBox(view); });
    // Beobachtet wird nur das Fach des HAUPTspielers: daran haengen das Taubensymbol, der
    // Zaehler und der Ton, und die sitzen alle drei an der EINEN Knopfleiste ueber die volle
    // Bildbreite. Eine Anzeige je Ansicht ist ein eigener Schritt (sie braucht einen Ort im
    // Viewport); bis dahin waere ein Zaehler, der die Nachrichten von vier Spielern addiert,
    // eine Luege.
    PostBox& postBox = GetPostBox();
    postBox.ObserveNewMsg([this](const auto& msg, auto msgCt) { this->NewPostMessage(msg, msgCt); });
    postBox.ObserveDeletedMsg([this](auto msgCt) { this->PostMessageDeleted(msgCt); });
    UpdatePostIcon(postBox.GetNumMsgs(), true);
}

void dskGameInterface::OnWindowOwnerChanged(const unsigned ownerIdx)
{
    // Der Besitzer eines Fensters bestimmt, fuer WEN seine Knoepfe Kommandos erzeugen.
    // SHARED_WINDOW_OWNER (kein Besitzer, also Nachrichtenbox, Chat, Systemfenster) und jede
    // Nummer ohne Ansicht bedeuten ausdruecklich "Hauptspieler" - genau das bisherige
    // Verhalten und damit der Einzelspielerfall.
    if(ownerIdx < views_.size())
        GAMECLIENT.SetWindowOwnerPlayer(static_cast<uint8_t>(views_[ownerIdx]->GetPlayerId()));
    else
        GAMECLIENT.SetWindowOwnerPlayer(std::nullopt);
}

GameCommandFactory& dskGameInterface::gcFactoryFor(const PlayerView& view)
{
    // Der Kommandopfad DIESES Spielers (network/LocalPlayerGCFactory). Gibt es ihn nicht -
    // Replay, und dort kann ohnehin kein Kommando entstehen -, ist GAMECLIENT selbst die
    // Fabrik, also exakt wie bisher.
    if(GameCommandFactory* factory = GAMECLIENT.GetGCFactory(static_cast<uint8_t>(view.GetPlayerId())))
        return *factory;
    return GAMECLIENT;
}

PostBox& dskGameInterface::GetPostBox()
{
    return GetPostBoxFor(worldViewer.GetPlayerId());
}

PostBox& dskGameInterface::GetPostBox(const PlayerView& view)
{
    return GetPostBoxFor(view.GetPlayerId());
}

PostBox& dskGameInterface::GetPostBoxFor(const unsigned playerId)
{
    // BEFUND DIESER PHASE, gemessen: es gab genau EIN Postfach, das des Hauptspielers.
    // PostManager haelt zwar MAX_PLAYERS Faecher, aber AddPostBox hatte im ganzen Baum genau
    // einen Aufrufer - diesen hier -, und der nahm worldViewer.GetPlayerId(). PostManager::
    // SendMsg gibt bei fehlendem Fach STILL auf (postSystem/PostManager.cpp). Jede Meldung an
    // die Spieler 2 bis 4 - "Wir werden angegriffen!", "Das Bergwerk ist erschoepft",
    // "Eisenerz gefunden" - wurde damit spurlos verworfen.
    //
    // Ein Fach anzulegen beruehrt den Determinismus NICHT: PostManager und PostBox stehen in
    // keiner Serialize-Funktion und in keiner Pruefsumme, und die Simulation liest sie nirgends
    // (die einzigen Zugriffe sind SendMsg/SetMissionGoal aus der Simulation heraus und die
    // Fenster darueber). Es aendert sich allein, WAS AUFGEHOBEN WIRD.
    PostBox* postBox = worldViewer.GetWorld().GetPostMgr().GetPostBox(playerId);
    if(!postBox)
        postBox = &worldViewer.GetWorldNonConst().GetPostMgr().AddPostBox(playerId);
    RTTR_Assert(postBox != nullptr);
    return *postBox;
}

dskGameInterface::~dskGameInterface()
{
    // Nur den EIGENEN Eintrag entfernen: legt ein Nachfolger sich schon angemeldet, darf der
    // sterbende Vorgaenger ihn nicht wieder abraeumen.
    if(WINDOWMANAGER.GetWindowOwnerObserver() == this)
        WINDOWMANAGER.SetWindowOwnerObserver(nullptr);
    // Und ebenso den Rohzeiger, den die WELT auf dieses Objekt haelt (oben, SetGameInterface).
    // Der Game-Zeiger gehoert nicht diesem Desktop allein - GameClient haelt ihn ebenfalls -,
    // die Welt ueberlebt den Desktop also. Blieb der Zeiger stehen, griff der naechste Leser
    // auf freigegebenen Speicher zu; GameWorldViewer::IsAllVisible tut genau das ungeprueft
    // ueber gi->GI_GetCheats() (world/GameWorldViewer.cpp:130-131), und schon der KONSTRUKTOR
    // eines neuen dskGameInterface laeuft dort hindurch, bevor er den Zeiger neu setzt.
    if(game_ && const_cast<Game&>(*game_).world_.GetGameInterface() == this)
        const_cast<Game&>(*game_).world_.SetGameInterface(nullptr);
    // Ein offener handelnder Spieler wuerde sonst ueber das Ende dieser Partie hinaus stehen
    // bleiben.
    GAMECLIENT.SetWindowOwnerPlayer(std::nullopt);
    // LEBENSDAUER: die Fokusrahmen zeigen VOM Fenster AUF den FocusPath dieser Ansicht
    // (IngameWindow::focusRings_). Die Ansichten sterben gleich mit diesem Objekt, die Fenster
    // gehoeren aber dem WindowManager und leben weiter - bis zum naechsten Desktopwechsel.
    // ~IngameWindow dereferenziert dort jeden noch angemeldeten Rahmen als Backstop und griffe
    // dann auf freigegebenen Speicher zu. Also hier abmelden, solange beide Seiten noch da sind.
    forEachView([this](PlayerView& view) { ReleaseFocus(view); });
    for(auto& border : borders)
        deletePtr(border);
    GAMECLIENT.RemoveInterface(this);
    LOBBYCLIENT.RemoveListener(this);
}

void dskGameInterface::SetActive(bool activate)
{
    if(activate == IsActive())
        return;
    if(!activate && isScrolling)
    {
        // Stay active if scrolling and no modal window is open
        const IngameWindow* wnd = WINDOWMANAGER.GetTopMostWindow();
        if(wnd && wnd->IsModal())
            StopScrolling();
        else
            return;
    }
    Desktop::SetActive(activate);
    // Do this here to allow previous screen to keep control
    if(activate)
    {
        GAMECLIENT.SetInterface(this);
        LOBBYCLIENT.AddListener(this);
        if(!game_->IsStarted())
        {
            GAMECLIENT.OnGameStart();

            ShowPersistentWindowsAfterSwitch();
        }
    }
}

void dskGameInterface::StopScrolling()
{
    isScrolling = false;
    // Der Zug ist vorbei - die Ansicht, die er verschoben hat, gehoert ihm nicht mehr.
    scrollView_ = nullptr;
    // Dieselbe Entscheidung wie ueberall sonst, an genau einer Stelle formuliert.
    UpdateRoadCursor(primary());
}

void dskGameInterface::StartScrolling(const Position& mousePos)
{
    startScrollPt = mousePos;
    isScrolling = true;
    WINDOWMANAGER.SetCursor(Cursor::Scroll);
}

void dskGameInterface::ToggleFoW()
{
    DisableFoW(!GAMECLIENT.IsReplayFOWDisabled());
}

void dskGameInterface::DisableFoW(const bool hideFOW)
{
    GAMECLIENT.SetReplayFOW(hideFOW);
    // Notify viewer and minimap to recalculate the visibility - fuer JEDE Ansicht, jede hat
    // ihren eigenen Fog of War.
    forEachView([](PlayerView& view) { view.RecalcAllColors(); });
}

void dskGameInterface::ShowPersistentWindowsAfterSwitch()
{
    // Wiederhergestellt wird nur fuer die HAUPTansicht. SETTINGS.windows.persistentSettings ist
    // allein nach GUI_ID geschluesselt (Settings.cpp) - es gibt also genau EINEN gemerkten Satz
    // "welche Fenster waren offen". Ihn auf alle Ansichten anzuwenden hiesse, jedem Spieler die
    // Fenster des Hauptspielers aufzudraengen und sie beim Schliessen durcheinander
    // zurueckzuschreiben. Getrennte Erinnerung je Ansicht ist ein eigener Schritt; bis dahin ist
    // "nur Ansicht 0" die ehrliche Form.
    PlayerView& view = primary();
    const ViewScope ownerScope(view.GetIndex());
    auto& windows = SETTINGS.windows.persistentSettings;

    if(windows[CGI_CHAT].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwChat>(this));
    if(windows[CGI_POSTOFFICE].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwPostWindow>(view.GetView(), GetPostBox()));
    if(windows[CGI_DISTRIBUTION].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwDistribution>(view.GetViewer(), gcFactoryFor(view)));
    if(windows[CGI_BUILDORDER].isOpen && view.GetViewer().GetWorld().GetGGS().isEnabled(AddonId::CUSTOM_BUILD_SEQUENCE))
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwBuildOrder>(view.GetViewer()));
    if(windows[CGI_TRANSPORT].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwTransport>(view.GetViewer(), gcFactoryFor(view)));
    if(windows[CGI_MILITARY].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwMilitary>(view.GetViewer(), gcFactoryFor(view)));
    if(windows[CGI_TOOLS].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwTools>(view.GetViewer(), gcFactoryFor(view)));
    if(windows[CGI_INVENTORY].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwInventory>(view.GetViewer().GetPlayer()));
    if(windows[CGI_MINIMAP].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwMinimap>(view.GetMinimap(), view.GetView()));
    if(windows[CGI_BUILDINGS].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwBuildings>(view.GetView(), gcFactoryFor(view)));
    if(windows[CGI_BUILDINGSPRODUCTIVITY].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwBuildingProductivities>(view.GetViewer().GetPlayer()));
    if(windows[CGI_MUSICPLAYER].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwMusicPlayer>());
    if(windows[CGI_STATISTICS].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwStatistics>(view.GetViewer()));
    if(windows[CGI_ECONOMICPROGRESS].isOpen && view.GetViewer().GetWorld().getEconHandler())
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwEconomicProgress>(view.GetViewer()));
    if(windows[CGI_DIPLOMACY].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwDiplomacy>(view.GetViewer(), gcFactoryFor(view)));
    if(windows[CGI_SHIP].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(
          std::make_unique<iwShip>(view.GetView(), gcFactoryFor(view), view.GetViewer().GetPlayer().GetShipByID(0)));
    if(windows[CGI_MERCHANDISE_STATISTICS].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwMerchandiseStatistics>(view.GetViewer().GetPlayer()));
}

void dskGameInterface::Resize(const Extent& newSize)
{
    Window::Resize(newSize);

    // recreate borders
    for(auto& border : borders)
        deletePtr(border);
    cbb.buildBorder(tv::ScreenChromeRect(newSize).getSize(), borders);

    // move buttons
    // Get real renderer size as newSize may get capped but we want to keep the manually drawn borders intact
    const Extent realNewSize = VIDEODRIVER.GetRenderSize();
    const glArchivItem_Bitmap& imgButtonBar = *LOADER.GetImageN("resource", 29);
    DrawPoint barPos = CalcButtonBarOrigin(realNewSize, imgButtonBar.GetSize()) + btOffset;

    auto* button = GetCtrl<ctrlButton>(ID_btMap);
    button->SetPos(barPos);

    barPos.x += button->GetSize().x;
    button = GetCtrl<ctrlButton>(ID_btOptions);
    button->SetPos(barPos);

    barPos.x += button->GetSize().x;
    button = GetCtrl<ctrlButton>(ID_btConstructionAid);
    button->SetPos(barPos);

    barPos.x += button->GetSize().x;
    button = GetCtrl<ctrlButton>(ID_btPost);
    button->SetPos(barPos);

    barPos += DrawPoint(18, 24);
    auto* text = GetCtrl<ctrlText>(ID_txtNumMsg);
    text->SetPos(barPos);

    // Einziger Ort, an dem das Viewport-Layout neu berechnet wird. Er deckt auch eine geaenderte
    // GuiScale ab: VideoDriver::setGuiScalePercent ruft direkt WindowResized
    // (libs/driver/src/VideoDriver.cpp:110-112) -> WindowManager::WindowResized ->
    // Desktop::Msg_ScreenResize -> Resize. Bei genau einer Ansicht liefert CalcViewports exakt
    // Position(0,0) + newSize, das ist bit-identisch zum frueheren gwv.Resize(newSize).
    LayoutViews(newSize);
}

void dskGameInterface::Msg_ButtonClick(const unsigned ctrl_id)
{
    // Es gibt genau EINE Knopfleiste, ueber die volle Bildschirmbreite. Sie gehoert deshalb der
    // Hauptansicht - ausdruecklich und nicht aus Versehen. Eine Leiste je Ansicht ist ein
    // eigener Schritt; bis dahin waere jede andere Wahl geraten.
    PlayerView& view = primary();
    const ViewScope ownerScope(view.GetIndex());
    switch(ctrl_id)
    {
        case ID_btMap: OpenMinimapFor(view); break;
        case ID_btOptions: OpenMainMenuFor(view); break;
        case ID_btConstructionAid:
            // Die Desktoppruefung bleibt AUSDRUECKLICH hier und wandert NICHT in
            // ToggleConstructionAidFor: sie ist eine Eigenart des MAUSknopfes (solange
            // irgendein Ingamefenster aktiv ist, ist der Desktop deaktiviert und der Knopf tut
            // nichts). Der Padspieler drueckt seinen Schalter IN einem Fenster - genau dann
            // waere die Bedingung immer falsch und der Schalter dauerhaft tot. Die Bedingung
            // gehoert also zum Aufrufer, nicht zur Handlung; so bleibt der Mauspfad Bit fuer
            // Bit der alte.
            if(WINDOWMANAGER.IsDesktopActive())
                ToggleConstructionAidFor(view);
            break;
        case ID_btPost: OpenPostOfficeFor(view); break;
    }
}

/// --- Die vier Handlungen der Knopfleiste, benannt und auf GENAU EINE Ansicht bezogen --------
///
/// Herausgezogen aus Msg_ButtonClick, weil sie seit dieser Phase ZWEI Aufrufer haben: den
/// Mausknopf der einen Leiste (immer primary()) und das Padmenue des jeweiligen Sitzplatzes
/// (iwPadSystemMenu). Dasselbe Muster wie Phase 4f mit OpenObjectWindow, und aus demselben
/// Grund: fuer dieselbe Handlung darf es nicht zwei Regelwerke geben, die beim naechsten Zusatz
/// auseinanderlaufen.
///
/// Die Besitzklammer setzt hier KEINE der vier - beide Aufrufer haben sie bereits offen
/// (Msg_ButtonClick fuer die Maus, OnPadButton fuer das Pad). Zwei Klammern uebereinander waeren
/// wirkungsgleich, verschleierten aber, woher der Besitzer kommt.

IngameWindow* dskGameInterface::OpenMinimapFor(PlayerView& view)
{
    return WINDOWMANAGER.ToggleWindow(std::make_unique<iwMinimap>(view.GetMinimap(), view.GetView()));
}

IngameWindow* dskGameInterface::OpenMainMenuFor(PlayerView& view)
{
    return WINDOWMANAGER.ToggleWindow(std::make_unique<iwMainMenu>(view.GetView(), gcFactoryFor(view)));
}

void dskGameInterface::ToggleConstructionAidFor(PlayerView& view)
{
    // REINE ANZEIGE, und das ist nachgeprueft und keine Annahme: GameWorldView::show_bq wird
    // ausschliesslich in GameWorldView::Draw gelesen (DrawConstructionAid) und sonst nirgends.
    // Es entsteht kein GameCommand, es wird nichts an den Server geschickt, und die Simulation
    // sieht den Wert nie - der Determinismus des Lockstep ist nicht beruehrt. Genau deshalb
    // darf dieser Schalter ueberhaupt am Fensterknopf haengen und braucht keinen Kommandopfad.
    view.GetView().ToggleShowBQ();
}

void dskGameInterface::ToggleNamesAndProductivityFor(PlayerView& view)
{
    // Ebenfalls reine Anzeige (show_names/show_productivity, gelesen nur in
    // GameWorldView::DrawNameProductivityOverlay). Derselbe Aufruf, den der Reiter
    // "Anzeigeoptionen" des Aktionsfensters schon macht (iwAction.cpp).
    view.GetView().ToggleShowNamesAndProductivity();
}

IngameWindow* dskGameInterface::OpenPostOfficeFor(PlayerView& view)
{
    // Das Postfach DIESER Ansicht, nicht das des Hauptspielers. Vorher stand hier GetPostBox()
    // ohne Argument - ein Padspieler in Ansicht 1 haette die Post von Spieler 0 gelesen und
    // dessen Nachrichten geloescht.
    PostBox& box = GetPostBox(view);
    IngameWindow* const wnd = WINDOWMANAGER.ToggleWindow(std::make_unique<iwPostWindow>(view.GetView(), box));
    // Das Taubensymbol und der Zaehler sitzen an der EINEN Knopfleiste und gehoeren damit der
    // Hauptansicht. Sie werden deshalb nur dann zurueckgesetzt, wenn auch wirklich der
    // Hauptspieler seine Post geoeffnet hat.
    if(&view == &primary())
        UpdatePostIcon(box.GetNumMsgs(), false);
    return wnd;
}

void dskGameInterface::PadMenuLeaveTo(PlayerView& view, IngameWindow* const opened)
{
    // Ein Menue verschwindet, wenn man einen Punkt daraus gewaehlt hat, und der Spieler steht
    // danach IN dem, was er gewaehlt hat. Alles andere waere die Sackgasse, an der der
    // Auftraggeber beim Tagebuch haengengeblieben ist: ein Fenster liegt sichtbar obenauf, und
    // der Knopf, der es betreten wuerde (Y), wird vom Fokus im Menue geschluckt.
    //
    // Die beiden ANZEIGESCHALTER rufen das bewusst NICHT - sie lassen das Menue stehen, damit
    // der Spieler die Beschriftung umspringen sieht und gleich noch den zweiten Schalter legen
    // kann.
    if(auto* menu = WINDOWMANAGER.FindNonModalWindow(CGI_PADMENU, view.GetIndex()))
    {
        if(view.GetFocus().GetRoot() == menu)
            ReleaseFocus(view);
        menu->Close();
    }
    // `opened` ist nullptr, wenn ToggleWindow ein bereits offenes Fenster ZUGEMACHT hat. Dann
    // gibt es nichts zu betreten, und der Spieler steht danach wieder in der Welt - das ist die
    // richtige Antwort auf "Postfenster" bei schon offenem Postfenster.
    EnterWindow(view, opened);
}

void dskGameInterface::Msg_PaintBefore()
{
    Desktop::Msg_PaintBefore();

    // Spiel ausführen
    Run();

    /// Padding of the figures
    const DrawPoint figPadding(12, 12);
    // Rahmen, Statuen und Knopfleiste liegen in EINEM Kasten - siehe tv::ScreenChromeRect.
    // Ohne Fernsehmodus ist das Rect(0, 0, GetRenderSize()), und dann rechnen alle Zeilen
    // unten Zahl fuer Zahl wie vorher.
    const Rect chrome = tv::ScreenChromeRect(VIDEODRIVER.GetRenderSize());
    const DrawPoint chromeOrigin = chrome.getOrigin();
    const DrawPoint chromeSize(chrome.getSize());
    // Rahmen zeichnen
    borders[0]->DrawFull(chromeOrigin);                                                       // oben (mit Ecken)
    borders[1]->DrawFull(chromeOrigin + DrawPoint(0, chromeSize.y - figPadding.y));            // unten (mit Ecken)
    borders[2]->DrawFull(chromeOrigin + DrawPoint(0, figPadding.y));                           // links
    borders[3]->DrawFull(chromeOrigin + DrawPoint(chromeSize.x - figPadding.x, figPadding.y)); // rechts

    // The figure/statues and the button bar
    glArchivItem_Bitmap& imgFigLeftTop = *LOADER.GetImageN("resource", 17);
    glArchivItem_Bitmap& imgFigRightTop = *LOADER.GetImageN("resource", 18);
    glArchivItem_Bitmap& imgFigLeftBot = *LOADER.GetImageN("resource", 19);
    glArchivItem_Bitmap& imgFigRightBot = *LOADER.GetImageN("resource", 20);
    imgFigLeftTop.DrawFull(chromeOrigin + figPadding);
    imgFigRightTop.DrawFull(chromeOrigin
                            + DrawPoint(chromeSize.x - figPadding.x - imgFigRightTop.getWidth(), figPadding.y));
    imgFigLeftBot.DrawFull(chromeOrigin
                           + DrawPoint(figPadding.x, chromeSize.y - figPadding.y - imgFigLeftBot.getHeight()));
    imgFigRightBot.DrawFull(chromeOrigin + chromeSize - figPadding - DrawPoint(imgFigRightBot.GetSize()));

    glArchivItem_Bitmap& imgButtonBar = *LOADER.GetImageN("resource", 29);
    imgButtonBar.DrawFull(CalcButtonBarOrigin(VIDEODRIVER.GetRenderSize(), imgButtonBar.GetSize()));
}

void dskGameInterface::Msg_PaintAfter()
{
    Desktop::Msg_PaintAfter();

    const GameWorldBase& world = worldViewer.GetWorld();

    if(SETTINGS.global.showGFInfo)
    {
        std::array<char, 256> nwf_string;
        if(GAMECLIENT.IsReplayModeOn())
        {
            snprintf(nwf_string.data(), nwf_string.size(),
                     _("(Replay-Mode) Current GF: %u (End at: %u) / GF length: %u ms / NWF length: %u gf (%u ms)"),
                     world.GetEvMgr().GetCurrentGF(), GAMECLIENT.GetLastReplayGF(),
                     GAMECLIENT.GetGFLength() / FramesInfo::milliseconds32_t(1), GAMECLIENT.GetNWFLength(),
                     GAMECLIENT.GetNWFLength() * GAMECLIENT.GetGFLength() / FramesInfo::milliseconds32_t(1));
        } else
            snprintf(nwf_string.data(), nwf_string.size(),
                     _("Current GF: %u / GF length: %u ms / NWF length: %u gf (%u ms) /  Ping: %u ms"),
                     world.GetEvMgr().GetCurrentGF(), GAMECLIENT.GetGFLength() / FramesInfo::milliseconds32_t(1),
                     GAMECLIENT.GetNWFLength(),
                     GAMECLIENT.GetNWFLength() * GAMECLIENT.GetGFLength() / FramesInfo::milliseconds32_t(1),
                     worldViewer.GetPlayer().ping);
        NormalFont->Draw(DrawPoint(30, 1), nwf_string.data(), FontStyle{}, COLOR_YELLOW);
    }

    // tournament mode?
    const unsigned tournamentDuration = GAMECLIENT.GetTournamentModeDuration();
    if(tournamentDuration)
    {
        unsigned curGF = world.GetEvMgr().GetCurrentGF();
        std::string tournamentNotice;
        if(curGF >= tournamentDuration)
            tournamentNotice = _("Tournament finished");
        else
        {
            tournamentNotice =
              helpers::format("Tournament mode: %1% remaining", GAMECLIENT.FormatGFTime(tournamentDuration - curGF));
        }
        NormalFont->Draw(DrawPoint(VIDEODRIVER.GetRenderSize().x - 30, 1), tournamentNotice, FontStyle::AlignH::RIGHT,
                         COLOR_YELLOW);
    }

    // Replaydateianzeige in der linken unteren Ecke
    if(GAMECLIENT.IsReplayModeOn())
    {
        NormalFont->Draw(DrawPoint(0, VIDEODRIVER.GetRenderSize().y), GAMECLIENT.GetReplayFilename().string(),
                         FontStyle::BOTTOM, COLOR_YELLOW);
    } else
    {
        // Laggende Spieler anzeigen in Form von Schnecken
        DrawPoint snailPos(VIDEODRIVER.GetRenderSize().x - 70, 35);
        for(const NWFPlayerInfo& player : nwfInfo_->getPlayerInfos())
        {
            if(player.isLagging)
            {
                LOADER.GetPlayerImage("rttr", 0)->DrawFull(Rect(snailPos, 30, 30), COLOR_WHITE,
                                                           game_->world_.GetPlayer(player.id).color);
                snailPos.x -= 40;
            }
        }
    }

    // Show icons in the upper right corner of the game interface
    DrawPoint iconPos(VIDEODRIVER.GetRenderSize().x - 56, 32);

    // Draw cheating indicator icon (WINTER)
    if(cheats_.isCheatModeOn())
    {
        glArchivItem_Bitmap* cheatingImg = LOADER.GetImageN("io", 75);
        cheatingImg->DrawFull(iconPos);
        iconPos -= DrawPoint(cheatingImg->getWidth() + 6, 0);
    }

    // Draw speed indicator icon
    const int speedStep = static_cast<int>(REFERENCE_SPEED / 10ms) - static_cast<int>(GAMECLIENT.GetGFLength() / 10ms);

    if(speedStep != 0)
    {
        glArchivItem_Bitmap* runnerImg = LOADER.GetImageN("io", 164);

        runnerImg->DrawFull(iconPos);

        if(speedStep != 1)
        {
            std::string multiplier = helpers::toString(std::abs(speedStep));
            NormalFont->Draw(iconPos - runnerImg->GetOrigin() + DrawPoint(19, 6), multiplier, FontStyle::LEFT,
                             speedStep > 0 ? COLOR_YELLOW : COLOR_RED);
        }
        iconPos -= DrawPoint(runnerImg->getWidth() + 4, 0);
    }

    // Draw zoom level indicator icon
    if(gwv.GetCurrentTargetZoomFactor() != 1.f) //-V550
    {
        glArchivItem_Bitmap* magnifierImg = LOADER.GetImageN("io", 36);

        magnifierImg->DrawFull(iconPos);

        std::string zoom_percent = helpers::toString((int)(gwv.GetCurrentTargetZoomFactor() * 100)) + "%";
        NormalFont->Draw(iconPos - magnifierImg->GetOrigin() + DrawPoint(9, 7), zoom_percent, FontStyle::CENTER,
                         COLOR_YELLOW);
        iconPos -= DrawPoint(magnifierImg->getWidth() + 4, 0);
    }

    // Der Klartext je Ansicht - die einzige Lesestelle von PlayerView::GetBrief().
    //
    // HIER und nicht in Run(): Msg_PaintBefore zeichnet nach Run() noch Rahmen, Statuen und
    // Knopfleiste, die dem Kasten sonst ueber die Unterkante liefen. Fensterinhalte liegen
    // weiterhin darueber - genau wie beim Postfach und der Chatzeile, und das ist richtig: ein
    // Fenster, das der Spieler gerade bedient, gehoert nach vorn.
    //
    // Ohne angestecktes Pad ist jeder Block leer (RefreshBrief), und diese Schleife zeichnet
    // nichts. Der Einzelspieler mit Maus bekommt also keinen einzigen zusaetzlichen Zeichenruf.
    forEachView([this](const PlayerView& view) { DrawBrief(view); });
}

bool dskGameInterface::OpenObjectWindow(PlayerView& view, const MapPoint cSel)
{
    // Der Kern von ContextClick, herausgezogen und auf EINE Ansicht bezogen: alles hier liest
    // den Viewer DIESER Ansicht, oeffnet mit IHRER GameWorldView und IHRER Kommandofabrik und
    // sucht Vorgaenger unter IHRER Besitznummer. Damit ist der Mauspfad des Hauptspielers
    // unveraendert (er ruft mit primary() herein) und der Padpfad braucht keine zweite,
    // parallel zu pflegende Fassung derselben Entscheidung.
    //
    // Die Besitzklammer wird hier NICHT gesetzt - beide Aufrufer haben sie bereits offen
    // (ContextClick und OnPadButton). Zwei Klammern uebereinander waeren wirkungsgleich, aber
    // sie verschleierten, wo der Besitzer wirklich herkommt.
    GameWorldViewer& viewer = view.GetViewer();
    const unsigned wndId = CGI_BUILDING + MapBase::CreateGUIID(cSel);

    // Vielleicht steht hier auch ein Schiff?
    if(const noShip* ship = viewer.GetShip(cSel))
    {
        WINDOWMANAGER.Show(std::make_unique<iwShip>(view.GetView(), gcFactoryFor(view), ship));
        return true;
    }

    // Evtl ists nen Haus? (unser Haus)
    const noBase& selObj = *viewer.GetWorld().GetNO(cSel);
    if(selObj.GetType() == NodalObjectType::Building && viewer.IsOwner(cSel))
    {
        if(auto* wnd = WINDOWMANAGER.FindNonModalWindow(wndId, view.GetIndex()))
        {
            WINDOWMANAGER.SetActiveWindow(*wnd);
            return true;
        }
        BuildingType bt = static_cast<const noBuilding&>(selObj).GetBuildingType();
        // HQ
        if(bt == BuildingType::Headquarters)
            WINDOWMANAGER.Show(std::make_unique<iwHQ>(view.GetView(), gcFactoryFor(view),
                                                      viewer.GetWorldNonConst().GetSpecObj<nobHQ>(cSel)));
        // Lagerhäuser
        else if(bt == BuildingType::Storehouse)
            WINDOWMANAGER.Show(std::make_unique<iwBaseWarehouse>(
              view.GetView(), gcFactoryFor(view), viewer.GetWorldNonConst().GetSpecObj<nobStorehouse>(cSel)));
        // Hafengebäude
        else if(bt == BuildingType::HarborBuilding)
            WINDOWMANAGER.Show(std::make_unique<iwHarborBuilding>(
              view.GetView(), gcFactoryFor(view), viewer.GetWorldNonConst().GetSpecObj<nobHarborBuilding>(cSel)));
        // Militärgebäude
        else if(BuildingProperties::IsMilitary(bt))
            WINDOWMANAGER.Show(std::make_unique<iwMilitaryBuilding>(
              view.GetView(), gcFactoryFor(view), viewer.GetWorldNonConst().GetSpecObj<nobMilitary>(cSel)));
        else if(bt == BuildingType::Temple)
            WINDOWMANAGER.Show(std::make_unique<iwTempleBuilding>(
              view.GetView(), gcFactoryFor(view), viewer.GetWorldNonConst().GetSpecObj<nobTemple>(cSel)));
        else
            WINDOWMANAGER.Show(std::make_unique<iwBuilding>(view.GetView(), gcFactoryFor(view),
                                                            viewer.GetWorldNonConst().GetSpecObj<nobUsual>(cSel)));
        return true;
    }
    // oder vielleicht eine Baustelle?
    if(selObj.GetType() == NodalObjectType::Buildingsite && viewer.IsOwner(cSel))
    {
        if(!WINDOWMANAGER.FindNonModalWindow(wndId, view.GetIndex()))
            WINDOWMANAGER.Show(std::make_unique<iwBuildingSite>(
              view.GetView(), viewer.GetWorld().GetSpecObj<noBuildingSite>(cSel)));
        return true;
    }
    return false;
}

bool dskGameInterface::PadOpenWindow(PlayerView& view)
{
    // Der A-Knopf. Die Besitzklammer ist hier bereits offen (OnPadButton) - das entstehende
    // Fenster gehoert damit DIESEM Sitzplatz, arbeitet mit SEINEM Viewer und liegt in SEINEM
    // Viewport.
    const MapPoint pt = view.GetView().GetSelectedPt();
    if(!pt.isValid())
        return false;
    return OpenObjectWindow(view, pt);
}

bool dskGameInterface::PadOpenActionWindow(PlayerView& view)
{
    // Die Besitzklammer ist hier bereits offen (OnPadButton): das Fenster gehoert DIESEM
    // Sitzplatz, und jeder Knopf darin bucht auf SEINEN Spieler.
    const MapPoint pt = view.GetView().GetSelectedPt();
    if(!pt.isValid())
        return false;

    // DIESELBE Entscheidung wie beim Mausklick - kein zweites Regelwerk (ComputeActionOptions).
    const ActionOptions opts = ComputeActionOptions(view, pt);
    if(opts.tradeWarehouse)
    {
        WINDOWMANAGER.Show(std::make_unique<iwTrade>(*opts.tradeWarehouse, view.GetViewer(), GAMECLIENT));
        view.ClearRejection();
        return true;
    }
    // Ein Fenster, in dem nur "Anzeigeoptionen" steht, ist fuer einen Padspieler keine Antwort.
    // Er bekommt stattdessen die Rueckmeldung des Aufrufers (PadReject).
    if(!opts.hasAction())
        return false;

    if(view.actionwindow)
        view.actionwindow->Close();
    // Das Fenster erscheint am ZEIGER DIESES SPIELERS - also in seinem Viewport und nicht dort,
    // wo die Maus eines anderen Menschen gerade liegt. Der Zeiger steht in denselben
    // Koordinaten wie MouseCoords::pos (PlayerView::GetPadCursor), die Rechnung im
    // iwAction-Konstruktor stimmt damit unveraendert.
    const DrawPoint wndPos = view.HasPadCursor() ? DrawPoint(view.GetPadCursor()) : DrawPoint(view.GetViewCenter());
    ShowActionWindow(view, opts.tabs, pt, wndPos, opts.enableMilitaryBuildings, iwAction::MousePointer::LeaveAlone);
    // BAUHILFE ERZWINGEN, sobald es hier ueberhaupt etwas zu bauen gibt.
    //
    // Der zweite Halbsatz des Auftraggebers war "als Anfaenger ist auch nicht klar, wann Flagge
    // und wann Gebaeude kommt". Die Auskunft steht laengst auf dem Bildschirm - die Bauhilfe
    // malt je Knoten ein Symbol (gelbe Flagge, Huette, Haus, Burg, Bergwerk) -, sie ist nur
    // voreingestellt AUS (Settings.cpp: ingame.showBQ = false) und laesst sich nur ueber eine
    // Taste einschalten, die ein Padspieler gar nicht hat.
    //
    // Sie einzuschalten ist deshalb richtig, aber allein NICHT genug: die Symbole sagen einem
    // Anfaenger nichts, solange ihm niemand sagt, was sie bedeuten. Das tut der Klartext
    // (RefreshBrief), und zwar in denselben Worten - "Platz fuer eine kleine Huette" steht dort,
    // wo im Bild die Huette liegt. Erst zusammen ergeben die beiden eine Lektion: der Spieler
    // liest den Satz und lernt dabei das Symbol.
    //
    // Nur diese eine Ansicht, und in einem Feld, das SaveIngameSettingsValues nicht anfasst
    // (ForceShowBQ) - siehe die Begruendung dort. Der Mausspieler merkt davon nichts, auch nicht
    // nach einem Neustart.
    if(opts.tabs.build)
        view.GetView().ForceShowBQ();
    view.ClearRejection();
    return true;
}

bool dskGameInterface::ContextClick(const MouseCoords& mc)
{
    // BEFUND 2: der Klick gehoert der Ansicht, die den MAUSZEIGER haelt - nicht mehr
    // unbedingt der Hauptansicht.
    //
    // Frueher stand hier primary(), und der ganze Pfad las danach gwv (= primary().GetView()).
    // UpdateInput gibt einer Ansicht mit zugeordnetem Pad aber IMMER den Padzeiger; hatte der
    // Hauptspieler ein Pad in der Hand, zeigte gwv.GetSelectedPt() auf den PADpunkt, und der
    // Mausklick wirkte dort. Solange davon nur ein Fenster aufging, war das laestig; mit dem
    // Strassenbau am Pad legte der Klick ein Wegstueck und der naechste schrieb die Strasse
    // ueber CommitRoad fest - ein echtes GameCommand auf einen Punkt, den niemand angeklickt
    // hatte. Die alte Begruendung ("ein bloss angestecktes Pad aendert am Mauspfad nichts")
    // deckte genau diesen Fall nicht ab: sie sprach vom ANGESTECKTEN Pad, der Fehler entsteht
    // beim BENUTZTEN.
    //
    // Warum GetMouseView() und nicht "letztes benutztes Geraet gewinnt" - siehe die
    // Begruendung an GetMouseView() im Kopf.
    PlayerView* const clicked = GetMouseView();
    if(!clicked)
        return false; // alle Ansichten haben ein Pad: es gibt keinen Mauspunkt auf der Karte
    PlayerView& view = *clicked;
    // Der Besitzer eines hier geoeffneten Fensters ist der Spieler, aus dessen Sicht der Punkt
    // ausgewaehlt wurde - und das ist jetzt zwingend derselbe, dessen Zeiger die Maus ist.
    const ViewScope ownerScope(view.GetIndex());
    GameWorldView& clickedView = view.GetView();
    // Ohne gueltigen selektierten Punkt gibt es nichts anzuklicken; unten wuerde
    // GetNO(selPt)/GetNode(selPt) sonst ausserhalb der Karte zugreifen.
    if(!clickedView.GetSelectedPt().isValid())
        return false;

    // Handle road building mode if active
    //
    // Ab hier durchgehend `view` statt der Uebergangsreferenzen road/worldViewer. Wertgleich zu
    // vorher, weil `view` hier primary() IST (siehe oben) - aber jetzt steht es da, statt aus
    // einer Uebergangsreferenz zu folgen. Der Strassenbau haengt damit an keiner Stelle mehr
    // still an der Hauptansicht; wo er es bewusst tut, steht primary() ausgeschrieben.
    RoadBuildState& rb = view.GetRoad();
    GameWorldViewer& viewer = view.GetViewer();
    if(rb.mode != RoadBuildMode::Disabled)
    {
        // in "richtige" Map-Koordinaten Konvertieren, den aktuellen selektierten Punkt
        const MapPoint selPt = clickedView.GetSelectedPt();

        if(selPt == rb.point)
        {
            // Selektierter Punkt ist der gleiche wie der Straßenpunkt --> Fenster mit Wegbau abbrechen
            ShowRoadWindow(view, mc.pos);
        } else
        {
            // altes Roadwindow schließen
            WINDOWMANAGER.Close((unsigned)CGI_ROADWINDOW, view.GetIndex());

            // BEFUND 4: AtLengthLimit fuehrt in ALLEN drei Zweigen darunter zu genau gar
            // nichts - kein Fenster, kein Kommando, kein Mauswarp. Das ist woertlich das
            // Verhalten von vor dem Umbau: BuildRoadPart meldete am Wasserweg-Anschlag Erfolg
            // und setzte cSel auf das unveraenderte Wegende, worauf jeder dieser Zweige
            // durchfiel. Nur wird es jetzt ausgesprochen statt aus zwei Zufaellen zu folgen.
            //
            // Warum nicht das Fenster? Weil der Anschlag KEIN Fehlgriff des Spielers ist,
            // sondern eine Regel, die schon in der Vorschau sichtbar ist
            // (GameWorldView::DrawGUI faerbt die Strecke ab maxWaterWayLen um). Ein Fenster,
            // das dafuer aufgeht UND dem Spieler den Mauszeiger auf seinen Vorgabeknopf zieht
            // (iwRoadWindow-Konstruktor: VIDEODRIVER.SetMousePos), waere fuer eine blosse
            // Weigerung eine unverhaeltnismaessig grosse Stoerung - und ein Bruch der harten
            // Randbedingung "der Mausspieler baut exakt wie vorher".

            // Ist das ein gültiger neuer Wegpunkt?
            if(viewer.IsRoadAvailable(rb.mode == RoadBuildMode::Boat, selPt) && viewer.IsPlayerTerritory(selPt))
            {
                MapPoint targetPt = selPt;
                if(BuildRoadPart(view, targetPt) == RoadPartResult::Rejected)
                    ShowRoadWindow(view, mc.pos);
            } else if(viewer.GetBQ(selPt) != BuildingQuality::Nothing)
            {
                // Wurde bereits auf das gebaute Stück geklickt?
                unsigned idOnRoad = GetIdInCurBuildRoad(view, selPt);
                if(idOnRoad)
                    DemolishRoad(view, idOnRoad);
                else
                {
                    MapPoint targetPt = selPt;
                    const RoadPartResult res = BuildRoadPart(view, targetPt);
                    if(res == RoadPartResult::Built)
                    {
                        // Ist der Zielpunkt der gleiche geblieben?
                        if(selPt == targetPt)
                            CommitRoad(view);
                    } else if(res == RoadPartResult::Rejected && selPt == targetPt)
                        ShowRoadWindow(view, mc.pos);
                }
            }
            // Wurde auf eine Flagge geklickt und ist diese Flagge nicht der Weganfangspunkt?
            else if(viewer.GetWorld().GetNO(selPt)->GetType() == NodalObjectType::Flag && selPt != rb.start)
            {
                MapPoint targetPt = selPt;
                const RoadPartResult res = BuildRoadPart(view, targetPt);
                if(res == RoadPartResult::Built)
                {
                    if(selPt == targetPt)
                        CommitRoad(view);
                } else if(res == RoadPartResult::Rejected && selPt == targetPt)
                    ShowRoadWindow(view, mc.pos);
            } else
            {
                unsigned tbr = GetIdInCurBuildRoad(view, selPt);
                // Wurde bereits auf das gebaute Stück geklickt?
                if(tbr)
                    DemolishRoad(view, tbr);
                else
                    ShowRoadWindow(view, mc.pos);
            }
        }
    } else // Not in road building mode
    {
        const MapPoint cSel = clickedView.GetSelectedPt();

        // Das Fenster des Objekts auf diesem Knoten - dieselbe Entscheidung, die auch der
        // Padspieler bekommt (OpenObjectWindow). Entsteht dabei nichts, gibt es hier ein
        // Aktionsfenster.
        if(OpenObjectWindow(view, cSel))
            return true;

        // Was hier moeglich ist, steht jetzt an EINER Stelle - und der Padpfad liest dieselbe
        // (PadOpenActionWindow). Der Mauspfad oeffnet das Fenster wie bisher IMMER, auch wenn
        // nur der Reiter "Anzeigeoptionen" darin steht: der Spieler sieht dann selbst, dass
        // hier nichts geht. Genau das ist der Punkt, an dem der Padpfad bewusst abweicht - er
        // hat keinen Blick auf ein leeres Fenster uebrig und antwortet stattdessen.
        const ActionOptions opts = ComputeActionOptions(view, cSel);
        if(opts.tradeWarehouse)
        {
            WINDOWMANAGER.Show(std::make_unique<iwTrade>(*opts.tradeWarehouse, view.GetViewer(), GAMECLIENT));
            return true;
        }

        // Bisheriges Actionfenster schließen, falls es eins gab
        // aktuelle Mausposition merken, da diese durch das Schließen verändert werden kann
        if(view.actionwindow)
            view.actionwindow->Close();
        VIDEODRIVER.SetMousePos(mc.pos);

        ShowActionWindow(view, opts.tabs, cSel, mc.pos, opts.enableMilitaryBuildings);
    }

    return true;
}

bool dskGameInterface::ActionOptions::hasAction() const
{
    // ContextClick setzt `watch` unbedingt - der Reiter mit Beobachtungsfenster, Haeusernamen,
    // "zum HQ" und "Verbuendete benachrichtigen" steht auf JEDEM Knoten. Er ist deshalb kein
    // Merkmal dafuer, dass hier etwas moeglich WAERE, und zaehlt hier bewusst nicht mit.
    return tabs.build || tabs.setflag || tabs.flag || tabs.cutroad || tabs.upgradeRoad || tabs.attack
           || tabs.sea_attack;
}

dskGameInterface::ActionOptions dskGameInterface::ComputeActionOptions(PlayerView& view, const MapPoint cSel)
{
    // WOERTLICH der Block, der frueher in ContextClick stand - nur ohne die Annahme, dass die
    // fragende Ansicht die des Mausspielers ist. Gelesen wird durchgehend der Viewer DIESER
    // Ansicht: eigenes Gebiet, eigene BQ, eigene Sichtbarkeit, eigene Buendnisse.
    GameWorldViewer& viewer = view.GetViewer();
    ActionOptions out;
    const noBase& selObj = *viewer.GetWorld().GetNO(cSel);
    out.tabs.watch = true;
    // Unser Land
    if(viewer.IsOwner(cSel))
    {
        const BuildingQuality bq = viewer.GetBQ(cSel);
        // Kann hier was gebaut werden?
        if(bq >= BuildingQuality::Mine)
        {
            out.tabs.build = true;

            // Welches Gebäude kann gebaut werden?
            switch(bq)
            {
                case BuildingQuality::Mine: out.tabs.build_tabs = iwAction::BuildTab::Mine; break;
                case BuildingQuality::Hut: out.tabs.build_tabs = iwAction::BuildTab::Hut; break;
                case BuildingQuality::House: out.tabs.build_tabs = iwAction::BuildTab::House; break;
                case BuildingQuality::Castle: out.tabs.build_tabs = iwAction::BuildTab::Castle; break;
                case BuildingQuality::Harbor: out.tabs.build_tabs = iwAction::BuildTab::Harbor; break;
                default: break;
            }

            if(!viewer.GetWorld().IsFlagAround(cSel))
                out.tabs.setflag = true;

            // Prüfen, ob sich Militärgebäude in der Nähe befinden, wenn nein, können auch eigene
            // Militärgebäude gebaut werden
            out.enableMilitaryBuildings = !viewer.GetWorld().IsMilitaryBuildingNearNode(cSel, viewer.GetPlayerId());
        } else if(bq == BuildingQuality::Flag)
            out.tabs.setflag = true;
        else if(selObj.GetType() == NodalObjectType::Flag)
            out.tabs.flag = true;

        if(selObj.GetType() != NodalObjectType::Flag && selObj.GetType() != NodalObjectType::Building)
        {
            // Check if there are roads
            for(const Direction dir : helpers::EnumRange<Direction>{})
            {
                const PointRoad curRoad = viewer.GetVisiblePointRoad(cSel, dir);
                if(curRoad != PointRoad::None)
                {
                    out.tabs.cutroad = true;
                    out.tabs.upgradeRoad |= (curRoad == PointRoad::Normal);
                }
            }
        }
    }
    // evtl ists ein feindliches Militärgebäude, welches NICHT im Nebel liegt?
    else if(viewer.GetVisibility(cSel) == Visibility::Visible)
    {
        if(selObj.GetType() == NodalObjectType::Building)
        {
            const auto* building = viewer.GetWorld().GetSpecObj<noBuilding>(cSel); //-V807
            BuildingType bt = building->GetBuildingType();

            // Only if trade is enabled
            if(viewer.GetWorld().GetGGS().isEnabled(AddonId::TRADE))
            {
                // Allied warehouse? -> Show trade window
                if(BuildingProperties::IsWareHouse(bt) && viewer.GetPlayer().IsAlly(building->GetPlayer()))
                {
                    // NUR gemerkt, nicht gezeigt: diese Funktion laeuft seit Phase 9 einmal je
                    // Frame und Ansicht (RefreshBrief). Ein Show() an dieser Stelle machte
                    // daraus sechzig Handelsfenster in der Sekunde, sobald ein Spieler mit dem
                    // Zeiger auf einem verbuendeten Lagerhaus stehenbleibt.
                    out.tradeWarehouse = static_cast<const nobBaseWarehouse*>(building);
                    return out;
                }
            }

            // Ist es ein gewöhnliches Militärgebäude?
            if(BuildingProperties::IsMilitary(bt))
            {
                // Dann darf es nicht neu gebaut sein!
                if(!static_cast<const nobMilitary*>(building)->IsNewBuilt())
                    out.tabs.attack = true;
            }
            // oder ein HQ oder Hafen?
            else if(bt == BuildingType::Headquarters || bt == BuildingType::HarborBuilding)
                out.tabs.attack = true;
            out.tabs.sea_attack = out.tabs.attack && viewer.GetWorld().GetGGS().isEnabled(AddonId::SEA_ATTACK);
        }
    }
    return out;
}

brief::NodeVerdict dskGameInterface::JudgeNode(PlayerView& view, const MapPoint pt)
{
    // ZWEI Herkuenfte, und der Kommentar hat frueher nur die erste genannt ("es wird NICHTS ein
    // zweites Mal entschieden"). Das stimmt fuer die untere Haelfte dieser Funktion und nicht
    // fuer die obere:
    //
    //  - Was das Aktionsfenster ANBIETEN wuerde (Bauplatz, Flagge, Strasse), wird aus
    //    ComputeActionOptions abgelesen und nirgends nachgerechnet. Waere es nachgerechnet,
    //    koennte der Klartext "hier passt eine Huette" sagen, waehrend das Fenster gleich darauf
    //    keinen Baureiter zeigt.
    //  - Die vier Faelle darueber (Nebel, Niemandsland, fremdes Gebiet, eigenes Gebaeude)
    //    entscheidet diese Funktion SELBST. ComputeActionOptions kennt sie nicht auseinander:
    //    fuer Nebel und Niemandsland liefert sie dieselbe leere Auswahl, und das eigene
    //    Gebaeude faengt OpenObjectWindow schon vor ihr ab.
    //
    // WAS DAS KOSTET, statt es zu verschweigen: auf FREMDEM Gebiet kann das Fenster sehr wohl
    // etwas anbieten - ComputeActionOptions setzt tabs.attack an einem sichtbaren feindlichen
    // Militaergebaeude, Hauptquartier oder Hafen. Der Klartext sagt dort trotzdem nur "hier
    // kannst du nicht bauen" und schweigt vom Angriff. Das ist unvollstaendig, aber nicht
    // falsch, und es ist die einzige bekannte Stelle, an der Text und Fenster verschieden viel
    // wissen. Sie zu schliessen hiesse, dem Klartext einen fuenften Zweig zu geben; das gehoert
    // in den Abschnitt ueber den Angriff und nicht in diese Phase.
    const GameWorldViewer& viewer = view.GetViewer();
    if(!viewer.IsOwner(pt))
    {
        // Reihenfolge: erst der Nebel. Wer den Knoten nie gesehen hat, weiss ueber seinen
        // Besitzer nichts Verlaessliches - die gemerkten Daten koennen beliebig alt sein.
        if(viewer.GetVisibility(pt) != Visibility::Visible)
            return brief::NodeVerdict::Unexplored;
        return viewer.GetWorld().GetNode(pt).owner == 0 ? brief::NodeVerdict::NoMansLand :
                                                          brief::NodeVerdict::ForeignTerritory;
    }
    // Eigenes Gebiet. Ein eigenes Gebaeude oder eine eigene Baustelle faengt A bereits vor
    // ComputeActionOptions ab (OpenObjectWindow), deshalb steht der Fall auch hier vorn.
    const NodalObjectType noType = viewer.GetWorld().GetNO(pt)->GetType();
    if(noType == NodalObjectType::Building || noType == NodalObjectType::Buildingsite)
        return brief::NodeVerdict::OwnBuilding;

    const ActionOptions opts = ComputeActionOptions(view, pt);
    if(opts.tabs.build)
    {
        switch(opts.tabs.build_tabs)
        {
            case iwAction::BuildTab::Mine: return brief::NodeVerdict::Mine;
            case iwAction::BuildTab::Hut: return brief::NodeVerdict::Hut;
            case iwAction::BuildTab::House: return brief::NodeVerdict::House;
            case iwAction::BuildTab::Castle: return brief::NodeVerdict::Castle;
            case iwAction::BuildTab::Harbor: return brief::NodeVerdict::Harbor;
        }
    }
    if(opts.tabs.flag)
        return brief::NodeVerdict::OwnFlag;
    if(opts.tabs.setflag)
        return brief::NodeVerdict::FlagOnly;
    if(opts.tabs.cutroad)
        return brief::NodeVerdict::OwnRoad;
    return brief::NodeVerdict::NoSpace;
}

void dskGameInterface::RefreshBrief(PlayerView& view)
{
    // NUR fuer Ansichten mit Pad. Der Mausspieler hat Tooltips, und die harte Randbedingung
    // dieser Phase heisst, dass sich fuer ihn nichts aendert - kein zusaetzlicher Kasten, kein
    // zusaetzlicher Text, kein zusaetzlicher Zeichenaufruf. Ohne angestecktes Pad ist dieser
    // Zweig der einzige, der laeuft, und er setzt einen leeren Block.
    if(!view.HasPadCursor())
    {
        view.SetBrief(brief::Brief());
        return;
    }
    // Steht der Spieler in einem Fenster, ist das fokussierte Control die Frage, die er gerade
    // stellt - nicht der Knoten unter seinem Zeiger. Das ist der Kern: die Auskunft folgt dem
    // FOKUS, und der Fokus ist je Ansicht gefuehrt (FocusPath). Deshalb koennen vier Spieler
    // gleichzeitig vier verschiedene Texte lesen; mit dem einen WindowManager::curTooltip
    // waere das konstruktiv unmoeglich.
    if(view.GetFocus().IsActive())
    {
        view.SetBrief(brief::ForControl(view.GetFocus().GetFocused()));
        return;
    }
    // Der Strassenbau ist ein MODUS, in dem A, X und B etwas anderes tun als sonst. Ohne Ansage
    // ist er die Sackgasse, aus der PadRejection ueberhaupt entstanden ist.
    if(view.GetRoad().mode != RoadBuildMode::Disabled)
    {
        view.SetBrief(brief::ForRoadBuilding(view.GetRoad().mode == RoadBuildMode::Boat));
        return;
    }
    const MapPoint pt = view.GetView().GetSelectedPt();
    if(!pt.isValid())
    {
        view.SetBrief(brief::Brief());
        return;
    }
    view.SetBrief(brief::ForNode(JudgeNode(view, pt)));
}

void dskGameInterface::DrawBrief(const PlayerView& view) const
{
    const brief::Brief& b = view.GetBrief();
    if(b.empty())
        return;
    const glFont& font = *NormalFont;
    const unsigned lineHeight = font.getHeight();
    const Rect viewport(view.GetView().GetPos(), view.GetView().GetSize());
    const Rect safeArea = tv::ActiveSafeAreaRect(VIDEODRIVER.GetRenderSize());

    // Das Aktionsfenster DIESER Ansicht ist das einzige, das dem Kasten regelmaessig im Weg
    // steht - es steht am Zeiger des Padspielers und ist genau dann offen, wenn der Kasten
    // gebraucht wird. Der Zeiger ist gueltig, solange das Fenster lebt: Msg_WindowClosed setzt
    // ihn beim Schliessen auf nullptr. Andere Fenster bleiben aussen vor; sie liegen ohnehin
    // ueber dem Kasten und gehoeren dorthin (siehe die Begruendung am Aufruf in Msg_PaintAfter).
    const Rect avoid = view.actionwindow ?
                         Rect(view.actionwindow->GetDrawPos(), view.actionwindow->GetSize()) :
                         Rect(Position(0, 0), Extent(0, 0));

    // Zweimal PanelRect: die BREITE haengt nicht von der Zeilenzahl ab, die Zeilenzahl aber von
    // der Breite (Umbruch). Erst den Kasten ohne Hoehe holen, damit umbrechen, dann den
    // endgueltigen Kasten. Beide Aufrufe sind rein und liefern dieselbe Waagerechte - das
    // Ausweichen aendert nur die Senkrechte, deshalb braucht die Probe das Hindernis nicht.
    const Rect probe = brief::PanelRect(viewport, safeArea, 0, 0);
    constexpr int textPadding = 5;
    const auto textWidth = static_cast<unsigned short>(
      std::max(16, static_cast<int>(probe.getSize().x) - 2 * textPadding));

    std::vector<std::string> wrapped;
    for(const std::string& line : b.lines)
    {
        for(std::string& part : font.GetWrapInfo(line, textWidth, textWidth).CreateSingleStrings(line))
            wrapped.push_back(std::move(part));
    }
    const unsigned numLines = static_cast<unsigned>(wrapped.size()) + (b.title.empty() ? 0u : 1u);
    if(numLines == 0)
        return;

    const Rect panel = brief::PanelRect(viewport, safeArea, numLines, lineHeight, avoid);
    DrawRectangle(panel, 0xB4000000);
    // Ein schmaler Streifen in der Spielerfarbe: bei vier Kaesten auf einem Fernseher ist das
    // der schnellste Weg zu erkennen, welcher der eigene ist. Dieselbe Farbe traegt schon der
    // Fokusrahmen (ClearFocusRing/AddFocusRing).
    const unsigned playerColor = worldViewer.GetWorld().GetPlayer(view.GetPlayerId()).color;
    DrawRectangle(Rect(panel.getOrigin(), Extent(2, panel.getSize().y)), playerColor);

    DrawPoint textPos = panel.getOrigin() + DrawPoint(textPadding, 3);
    if(!b.title.empty())
    {
        font.Draw(textPos, b.title, FontStyle{}, COLOR_YELLOW);
        textPos.y += static_cast<int>(lineHeight);
    }
    for(const std::string& line : wrapped)
    {
        font.Draw(textPos, line, FontStyle{}, COLOR_WHITE);
        textPos.y += static_cast<int>(lineHeight);
    }
}

bool dskGameInterface::Msg_LeftDown(const MouseCoords& mc)
{
    const glArchivItem_Bitmap& imgButtonBar = *LOADER.GetImageN("resource", 29);
    const auto btOrig = CalcButtonBarOrigin(VIDEODRIVER.GetRenderSize(), imgButtonBar.GetSize()) + btOffset;
    if(IsPointInRect(mc.pos, Rect(btOrig, btSize * 4u)))
        return false;

    if(!VIDEODRIVER.IsTouch())
    {
        // Start scrolling also on Ctrl + left click
        if(VIDEODRIVER.GetModKeyState().ctrl)
        {
            Msg_RightDown(mc);
            return true;
        } else if(isScrolling)
            StopScrolling();

        return ContextClick(mc);

    } else if(mc.num_tfingers < 2)
        touchDuration = VIDEODRIVER.GetTickCount();
    else if(isScrolling) // 2 fingers down -> zoom mode. Do not click or scroll map
        StopScrolling();

    return true;
}

bool dskGameInterface::Msg_LeftUp(const MouseCoords& mc)
{
    if(isScrolling)
    {
        StopScrolling();
        return true;
    }

    // num_tfingers is reduced after this function to check if it's still a touch event
    // Was touch duration short enough to trigger conext click?
    if(mc.num_tfingers == 1 && (VIDEODRIVER.GetTickCount() - touchDuration) < TOUCH_MAX_CLICK_INTERVAL)
        return ContextClick(mc);

    return false;
}

bool dskGameInterface::Msg_MouseMove(const MouseCoords& mc)
{
    if(!isScrolling)
    {
        if(mc.num_tfingers == 1)
            Msg_RightDown(mc);
        else
            return false;
    }

    // BEFUND 2: verschoben wird die Karte DER ANSICHT, in der der Zug angefangen hat - nicht
    // mehr die von primary(). Frueher stand hier gwv: stand die Maus ueber Ansicht 1 und der
    // Spieler zog, wanderte die Karte von Ansicht 0.
    //
    // Nicht ViewUnderMouse(mc.pos): siehe die Begruendung an scrollView_. Der Zug gehoert der
    // Startansicht, und im Modus ScrollOpposite/-Same wird der Zeiger gleich unten ohnehin auf
    // den Startpunkt zurueckgesetzt.
    if(!scrollView_)
        return true; // Zug ohne Empfaenger - Msg_RightDown hat keine Ansicht gefunden
    GameWorldView& scrolled = scrollView_->GetView();

    if(SETTINGS.interface.mapScrollMode == MapScrollMode::GrabAndDrag)
    {
        const Position mapPos = scrolled.ViewPosToMap(mc.pos);
        scrolled.MoveBy(-(mapPos - startScrollPt));
        startScrollPt = mapPos;
    } else
    {
        int acceleration = SETTINGS.global.smartCursor ? 2 : 3;

        if(SETTINGS.interface.mapScrollMode == MapScrollMode::ScrollSame)
            acceleration = -acceleration;

        scrolled.MoveBy((mc.pos - startScrollPt) * acceleration);
        VIDEODRIVER.SetMousePos(startScrollPt);

        if(!SETTINGS.global.smartCursor)
            startScrollPt = mc.pos;
    }

    return true;
}

bool dskGameInterface::Msg_RightDown(const MouseCoords& mc)
{
    // BEFUND 2: der Zug faengt in der Ansicht an, ueber der die Maus steht.
    //
    // Hier - und nur hier - wird entschieden, wem der Zug gehoert; Msg_MouseMove liest die
    // Entscheidung danach nur noch (scrollView_).
    //
    // CameraViewUnderMouse aus demselben Grund wie beim Rad: der Zug verschiebt nur ein Bild,
    // er liest keinen Zeiger und waehlt keinen Knoten aus. Ein Spieler mit Pad UND Maus muss
    // seine Karte weiter ziehen koennen.
    PlayerView* const target = CameraViewUnderMouse(mc.pos);
    if(!target)
        return true;
    scrollView_ = target;
    if(SETTINGS.interface.mapScrollMode == MapScrollMode::GrabAndDrag)
        StartScrolling(target->GetView().ViewPosToMap(mc.pos));
    else
        StartScrolling(mc.pos);
    return true;
}

bool dskGameInterface::Msg_RightUp(const MouseCoords& /*mc*/) //-V524
{
    if(isScrolling)
        StopScrolling();
    return false;
}

bool dskGameInterface::Msg_KeyDown(const KeyEvent& ke)
{
    // Es gibt genau EINE Tastatur. Alles, was von hier aus ein Fenster oeffnet, gehoert deshalb
    // der Hauptansicht - ausdruecklich. Ein zusaetzlicher lokaler Spieler bedient sein Pad.
    const ViewScope ownerScope(primary().GetIndex());

    cheatCommandTracker_.onKeyEvent(ke);

    switch(ke.kt)
    {
        default: break;
        case KeyType::Return: // Open chat
            WINDOWMANAGER.Show(std::make_unique<iwChat>(this));
            return true;

        case KeyType::Left: // Scroll left
            gwv.MoveBy({-30, 0});
            return true;
        case KeyType::Right: // Scroll right
            gwv.MoveBy({30, 0});
            return true;
        case KeyType::Up: // Scroll up
            gwv.MoveBy({0, -30});
            return true;
        case KeyType::Down: // Scroll down
            gwv.MoveBy({0, 30});
            return true;

        case KeyType::F2: // Open save game window
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwSave>());
            return true;
        case KeyType::F3: // Map debug window
        {
            const bool replayMode = GAMECLIENT.IsReplayModeOn();
            if(replayMode)
                DisableFoW(true);
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwMapDebug>(gwv, game_->world_.IsSinglePlayer() || replayMode));
            return true;
        }
        case KeyType::F8:
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwTextfile>("keyboardlayout.txt", _("Keyboard layout")));
            return true;
        case KeyType::F9:
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwTextfile>("readme.txt", _("Readme!")));
            return true;
        case KeyType::F10: WINDOWMANAGER.ToggleWindow(std::make_unique<iwSettings>()); return true;
        case KeyType::F11: // Music player (midi files)
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwMusicPlayer>());
            return true;
        case KeyType::F12: // Ingame options
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwOptionsWindow>(gwv.GetSoundMgr()));
            return true;
    }

    switch(ke.c)
    {
        case '+':
            if(GAMECLIENT.IsReplayModeOn() || game_->world_.IsSinglePlayer())
                GAMECLIENT.IncreaseSpeed();
            return true;
        case '-':
            if(GAMECLIENT.IsReplayModeOn() || game_->world_.IsSinglePlayer())
                GAMECLIENT.DecreaseSpeed();
            return true;

        // Switch to specific player
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        {
            unsigned playerIdx = ke.c - '1';
            if(GAMECLIENT.IsReplayModeOn())
            {
                unsigned oldPlayerId = worldViewer.GetPlayerId();
                GAMECLIENT.ChangePlayerIngame(worldViewer.GetPlayerId(), playerIdx);
                RTTR_Assert(worldViewer.GetPlayerId() == oldPlayerId || worldViewer.GetPlayerId() == playerIdx);
            } else if(playerIdx < worldViewer.GetWorld().GetNumPlayers())
            {
                // On multiplayer this currently asyncs, but as this is a debug feature anyway just disable it.
                // If this should be enabled again, look into the handling/clearing of accumulated GCs
                if(game_->world_.IsSinglePlayer())
                {
                    const GamePlayer& player = worldViewer.GetWorld().GetPlayer(playerIdx);
                    if(player.ps == PlayerState::AI && player.aiInfo.type == AI::Type::Dummy)
                        GAMECLIENT.RequestSwapToPlayer(playerIdx);
                }
            }
            return true;
        }

        case 'b': gwv.MoveToLastPosition(); return true;
        case 'v':
            if(game_->world_.IsSinglePlayer())
                GAMECLIENT.IncreaseSpeed(true);
            return true;
        case 'c': // Show/hide building names
            gwv.ToggleShowNames();
            return true;
        case 'd': // Enable/Disable fog of war (in replay mode)
            ToggleFoW();
            return true;
        case 'h': // Go to HQ
        {
            const GamePlayer& player = worldViewer.GetPlayer();
            // HQ might not exist anymore
            if(player.GetHQPos().isValid())
                gwv.MoveToMapPt(player.GetHQPos());
        }
            return true;
        case 'i': // Show inventory
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwInventory>(worldViewer.GetPlayer()));
            return true;
        case 'j': // Skip GFs (fast forward)
            if(game_->world_.IsSinglePlayer() || GAMECLIENT.IsReplayModeOn())
                WINDOWMANAGER.ToggleWindow(std::make_unique<iwSkipGFs>(gwv));
            return true;
        case 'l': // Show minimap
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwMinimap>(minimap, gwv));
            return true;
        case 'm': // Show main menu
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwMainMenu>(gwv, gcFactoryFor(primary())));
            return true;
        case 'n': // Show message window
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwPostWindow>(gwv, GetPostBox()));
            UpdatePostIcon(GetPostBox().GetNumMsgs(), false);
            return true;
        case 'p': // Pause
            GAMECLIENT.TogglePause();
            return true;
        case 'q': // Quit game (dialog)
            if(ke.alt)
                WINDOWMANAGER.ToggleWindow(std::make_unique<iwEndgame>());
            return true;
        case 's': // Show/hide productivity overlay
            gwv.ToggleShowProductivity();
            return true;
        case ' ': // Show/hide construction aid
            // workaround for Wayland which does not capture SDLK_SPACE when SDL_StartTextInput() was called
            gwv.ToggleShowBQ();
            return true;
        case 26: // ctrl+z
            gwv.SetZoomFactor(ZOOM_FACTORS[ZOOM_DEFAULT_INDEX]);
            return true;
        case 'z':
            if(ke.ctrl) // Reset zoom
                gwv.SetZoomFactor(ZOOM_FACTORS[ZOOM_DEFAULT_INDEX]);
            else // zoom in
                gwv.SetZoomFactor(getNextZoomLevel(gwv.GetCurrentTargetZoomFactor()));
            return true;
        case 'Z': // shift-z, zoom out
            gwv.SetZoomFactor(getPreviousZoomLevel(gwv.GetCurrentTargetZoomFactor()));
            return true;
    }

    return false;
}

bool dskGameInterface::Msg_WheelUp(const MouseCoords& mc)
{
    WheelZoom(mc.pos, ZOOM_WHEEL_INCREMENT);
    return true;
}
bool dskGameInterface::Msg_WheelDown(const MouseCoords& mc)
{
    WheelZoom(mc.pos, -ZOOM_WHEEL_INCREMENT);
    return true;
}

void dskGameInterface::WheelZoom(const Position& mousePos, const float step)
{
    // BEFUND 2: gezoomt wird die Ansicht unter der Maus. Frueher stand hier gwv, also immer
    // primary() - das Rad ueber Ansicht 1 zoomte Ansicht 0.
    //
    // Die Position kommt aus dem Ereignis selbst und nicht aus mouseView_: das Rad liest keinen
    // Zustand, den UpdateInput fuer diesen Frame berechnet hat (anders als ContextClick, das
    // GetSelectedPt() der Ansicht braucht), sondern nur die Geometrie. Die Ereignisposition ist
    // damit die unmittelbarere und um keinen Frame verzoegerte Antwort.
    //
    // CameraViewUnderMouse und nicht ViewUnderMouse: das Rad liest keinen Zeiger, es darf also
    // auch eine padgesteuerte Ansicht zoomen. Sonst verloere der einzelne Spieler mit Pad UND
    // Maus sein Rad, sobald er das Pad anfasst. Ausfuehrlich im Kopf an CameraViewUnderMouse.
    //
    // Einzelspieler: es gibt genau eine Ansicht, ihr Viewport IST die Renderflaeche.
    // CameraViewUnderMouse liefert sie ueber Regel (a), ausserhalb des Fensters ueber den
    // Rueckfall (c) auf primary() - in beiden Faellen dieselbe Ansicht wie das fruehere gwv.
    PlayerView* const target = CameraViewUnderMouse(mousePos);
    if(!target)
        return; // Luecke im Layout: die Maus zeigt sichtbar auf keine Ansicht
    GameWorldView& zoomed = target->GetView();

    auto targetZoomFactor = zoomed.GetCurrentTargetZoomFactor() * (1 + step);
    targetZoomFactor = std::clamp(targetZoomFactor, ZOOM_FACTORS.front(), ZOOM_FACTORS.back());
    if(targetZoomFactor > 1 - ZOOM_WHEEL_INCREMENT && targetZoomFactor < 1 + ZOOM_WHEEL_INCREMENT)
        targetZoomFactor = 1.f; // Snap to 100%

    zoomed.SetZoomFactor(targetZoomFactor);
}

void dskGameInterface::OnBuildingNote(const BuildingNote& note)
{
    switch(note.type)
    {
        case BuildingNote::Constructed:
        case BuildingNote::Destroyed:
        case BuildingNote::Lost:
            // Close the related window as the building does not exist anymore
            // In "Constructed" this means the buildingsite
            // CloseAll und nicht Close: das Gebaeude ist fuer JEDEN weg, der es offen hat.
            WINDOWMANAGER.CloseAll(CGI_BUILDING + MapBase::CreateGUIID(note.pos));
            break;
        default: break;
    }
}

bool dskGameInterface::IsInsideRenderArea(const Position& viewPos) const
{
    // Umschliessendes Rechteck aller Viewports. Es ist die Renderflaeche, solange das Layout sie
    // vollstaendig ausfuellt - und das tut es (CalcViewports). Bewusst hier gerechnet und nicht
    // aus VIDEODRIVER gelesen: waehrend eines Groessenwechsels sind Treibergroesse und
    // Ansichtsgeometrie fuer einen Moment verschieden, und massgeblich ist die Flaeche, auf der
    // wirklich Ansichten liegen.
    if(views_.empty())
        return false;
    Position topLeft = views_.front()->GetView().GetPos();
    Position bottomRight = topLeft + Position(views_.front()->GetView().GetSize());
    for(const auto& view : views_)
    {
        const Position pos = view->GetView().GetPos();
        const Position end = pos + Position(view->GetView().GetSize());
        topLeft = elMin(topLeft, pos);
        bottomRight = elMax(bottomRight, end);
    }
    return viewPos.x >= topLeft.x && viewPos.y >= topLeft.y && viewPos.x < bottomRight.x
           && viewPos.y < bottomRight.y;
}

PlayerView* dskGameInterface::CameraViewUnderMouse(const Position& viewPos)
{
    // (a) Ueber einer Ansicht: DIESE. Ohne die Padpruefung aus ViewUnderMouse - siehe die
    //     Begruendung im Kopf.
    for(auto& view : views_)
    {
        if(view->ContainsViewPos(viewPos))
            return view.get();
    }
    // (b) Auf dem Bildschirm, aber auf keiner Ansicht: eine Luecke im Layout. Keine Ansicht.
    if(IsInsideRenderArea(viewPos))
        return nullptr;
    // (c) Ausserhalb der Renderflaeche: die Hauptansicht - derselbe Sitzplatz, dem auch Tastatur
    //     und Knopfleiste gehoeren.
    return &primary();
}

PlayerView* dskGameInterface::ViewUnderMouse(const Position& viewPos)
{
    // (a) Ueber einer Ansicht: nur diese kommt in Frage, und nur padlos.
    for(auto& view : views_)
    {
        if(view->ContainsViewPos(viewPos))
            return view->HasPadCursor() ? nullptr : view.get();
    }
    // (b) Auf dem Bildschirm, aber auf keiner Ansicht: eine Luecke im Layout. Keine Ansicht.
    if(IsInsideRenderArea(viewPos))
        return nullptr;
    // (c) Ausserhalb der Renderflaeche: die erste padlose Ansicht.
    for(auto& view : views_)
    {
        if(!view->HasPadCursor())
            return view.get();
    }
    return nullptr;
}

void dskGameInterface::UpdateInput(const unsigned elapsedMs, const Position& mousePos)
{
    // --- 1. Gamepads: abholen, zuordnen, Zeiger fortschreiben --------------------------------
    // Die Zahl der Slots ist die Zahl der Ansichten. Zuerst setzen, damit ein in DIESEM Frame
    // angestecktes Pad sofort einen Slot bekommt.
    padRouter_.SetNumSlots(GetNumViews());
    if(IVideoDriver* driver = VIDEODRIVER.GetDriver())
    {
        padEvents_.clear();
        driver->FetchPadEvents(padEvents_);
        padRouter_.OnEvents(padEvents_);
        // BEFUND 4.1: Wer die Warteschlange leert, ist fuer den Geraetebestand ALLER
        // verantwortlich. Ein Pad, das waehrend der Partie abgezogen oder angesteckt wird,
        // waere dem Menue sonst fuer den Rest der Programmlaufzeit unbekannt - es gibt keine
        // Bestandsabfrage, die das spaeter nachholen koennte (WindowManager::NotifyPadDevices).
        WINDOWMANAGER.NotifyPadDevices(padEvents_);
    }
    // Ein Fenster kann seit dem letzten Frame minimiert worden sein - dann gehoert der Fokus
    // dort nicht mehr hin (Befund B3). Vor jeder Auslieferung von Eingaben, damit derselbe
    // Frame weder Knopfflanke noch Rahmen mehr durchlaesst.
    forEachView([this](PlayerView& view) { ValidateFocus(view); });
    // Ruft OnPadAssigned/OnPadMove zurueck. Ohne angestecktes Pad passiert hier exakt nichts.
    padStepMs_ = elapsedMs;
    padRouter_.UpdateMotion(elapsedMs, *this);

    // --- 2. Zeigerbesitz je Ansicht ----------------------------------------------------------
    // Regel, in dieser Reihenfolge:
    //  a) Eine Ansicht mit zugeordnetem Pad bekommt IMMER den Zeiger dieses Pads. Die Maus kann
    //     ihn nicht stehlen, auch nicht wenn sie ueber der Ansicht steht.
    //  b) Steht die Maus UEBER einer Ansicht, kommt nur DIESE Ansicht in Frage - und sie bekommt
    //     den Zeiger nur, wenn sie padlos ist. Hat sie ein Pad, bekommt ihn KEINE Ansicht.
    //  c) Steht die Maus ueber gar keiner Ansicht, faellt sie an die erste padlose Ansicht
    //     zurueck.
    //
    // BEFUND B: (b) hiess frueher "die erste padlose Ansicht, ueber der die Maus steht", und
    // griff (c) sonst. Stand die Maus MITTEN IN einer padbesetzten Ansicht, fiel sie damit an
    // den NACHBARN: der bekam einen cursorPos_ auf einem Punkt ausserhalb seines eigenen
    // Viewports, UpdateSelection machte daraus einen Randknoten, und ein Mausklick oeffnete dort
    // ein Fenster - auf einem Knoten, ueber dem die Maus sichtbar nicht stand. Die Begruendung
    // an GetMouseView() ("der Klick trifft die Ansicht, ueber der die Maus steht") galt fuer
    // genau diesen Fall nicht.
    //
    // BEFUND 1 dieser Runde: (c) hiess frueher "keine Ansicht enthaelt den Punkt", mit der
    // Begruendung, die Viewports deckten die Renderflaeche lueckenlos ab, das heisse also
    // "ausserhalb der Renderflaeche". Bei DREI Ansichten stimmte das nicht: das Quadrantenlayout
    // liess die vierte Zelle frei, und die liegt MITTEN AUF DEM BILDSCHIRM. Dort griff der
    // Rueckfall, und der Klick wirkte auf Ansicht 0, ueber der die Maus nicht stand - genau der
    // Fehler, den BEFUND B beheben sollte, nur in der leeren Zelle.
    //
    // Zwei Dinge sind daran repariert, bewusst getrennt:
    //  1. Das LAYOUT deckt jetzt bei jeder Ansichtszahl lueckenlos ab (CalcViewports: drei
    //     Ansichten bekommen zwei oben und eine ueber die volle Breite darunter).
    //  2. Die REGEL verlaesst sich nicht mehr darauf. (c) fragt ausdruecklich, ob die Maus
    //     ausserhalb der Renderflaeche steht, statt es aus "keine Ansicht enthaelt sie"
    //     abzuleiten. Damit bleibt der Rueckfall genau dort erhalten, wo er gebraucht wird
    //     (Einzelspieler mit Maus ausserhalb des Fensters,
    //     SingleViewFollowsTheMouseWhetherOrNotAPadIsPlugged), und kann in einer kuenftigen
    //     Luecke nicht wieder danebengreifen.
    // Der zweite Punkt allein haette den Befund auch behoben, aber eine unbemalte Zelle mitten
    // im Bild stehengelassen; der erste allein haette die falsche Begruendung wieder wahr
    // gemacht, ohne dass der naechste Layoutwechsel es merkt.
    //
    // Die Regel selbst steht in ViewUnderMouse() - nur noch dort. Die Eingaenge, die eine eigene
    // Mausposition mitbringen (Rad, Kartenzug), fragen dieselbe Funktion, statt wie frueher
    // stillschweigend primary() zu nehmen.
    //
    // Einzelspieler mit Maus und ohne Pad: es gibt genau eine Ansicht, sie ist padlos und ihr
    // Viewport IST die Renderflaeche - (a) bzw. (c) geben ihr den Zeiger immer, bit-identisch zu
    // vor Phase 3.
    //
    // BEFUND 2 (letzte Runde): WELCHE Ansicht die Maus haelt, ist eine gemerkte Tatsache und
    // nicht mehr eine Annahme des Mauspfads ("es ist primary()"). ContextClick liest genau das
    // hier - und es MUSS das hier lesen und nicht die Ereignisposition, weil es gleich darauf
    // GetSelectedPt() dieser Ansicht auswertet, und der Punkt stammt aus dem Zeiger, den die
    // Schleife unten aus eben diesem mouseView_ setzt.
    PlayerView* const mouseOwner = ViewUnderMouse(mousePos);
    mouseView_ = mouseOwner;

    for(auto& view : views_)
    {
        std::optional<Position> cursor;
        if(view->HasPadCursor())
            cursor = view->GetPadCursor();
        else if(view.get() == mouseOwner)
            cursor = mousePos;
        view->GetView().SetCursorPos(cursor);
        // Enthaelt keinen GL-Aufruf (world/GameWorldView.cpp:124-159). Draw() ruft es gleich
        // noch einmal; das ist eine reine Neuberechnung und damit idempotent.
        view->GetView().UpdateSelection();
    }

    // --- 3. Knopfflanken --------------------------------------------------------------------
    // Erst JETZT, denn eine Padaktion wirkt auf GetSelectedPt() - der Punkt muss aus dem
    // fortgeschriebenen Zeiger dieses Frames stammen und nicht aus dem des vorigen.
    padRouter_.DispatchButtons(*this);

    // --- 4. Klartext ------------------------------------------------------------------------
    // GANZ zum Schluss, aus demselben Grund wie Schritt 3: ein A-Druck kann in diesem Frame ein
    // Fenster geoeffnet, den Fokus gesetzt oder den Strassenbau gestartet haben. Der Text muss
    // den Zustand NACH der Eingabe beschreiben, sonst haengt er dem Spieler um einen Frame
    // hinterher - bei 110 ms Wiederholrate der Fokusnavigation waere das sichtbar.
    forEachView([this](PlayerView& view) { RefreshBrief(view); });
}

void dskGameInterface::OnPadAssigned(const unsigned slot, const bool assigned)
{
    if(slot >= views_.size())
        return;
    PlayerView& view = *views_[slot];
    // Ein frisch zugeordnetes Pad setzt seinen Zeiger in die Mitte SEINER Ansicht. Damit ist die
    // Ansicht ab dem ersten Frame sichtbar in Padbesitz, ohne dass der Stick bewegt wurde.
    view.SetPadCursor(assigned ? std::optional<Position>(view.GetViewCenter()) : std::nullopt);
    // BEFUND B4: das Geraet nimmt seinen Fokus mit. Sonst bliebe der Fokus im Fenster stehen,
    // der Rahmen des verschwundenen Spielers bliebe dort angemeldet, und das naechste Geraet,
    // das diesen Slot bekommt, erbte beides - sein erster A-Druck klickte einen Knopf, statt
    // eine Fahne zu setzen. Gilt in BEIDE Richtungen: auch ein neu zugeordnetes Pad startet
    // ausdruecklich ohne Fokus, in der Welt.
    ReleaseFocus(view);
    // Dasselbe fuer einen laufenden Strassenbau: verliert die Ansicht ihr Eingabegeraet, kann
    // niemand den Bau mehr zu Ende fuehren oder abbrechen. Ohne diesen Abbruch bliebe die
    // visuelle Strasse dieses Spielers fuer immer im Bild stehen und seine BQ dauerhaft falsch -
    // die Geisterstrasse nach Kabelbruch. Abgebrochen wird nur beim VERLIEREN des Geraets;
    // ein neu zugeordnetes Pad findet ohnehin keinen laufenden Bau vor.
    if(!assigned)
        CancelRoadBuilding(view);
}

void dskGameInterface::OnPadMove(const unsigned slot, const Position& delta)
{
    if(slot >= views_.size())
        return;
    PlayerView& view = *views_[slot];
    // Ab hier handelt DIESER Spieler - siehe die Klammer in OnPadButton.
    const ViewScope ownerScope(slot);
    // Steht dieser Spieler in einem Fenster, gehoert der Stick dem Fokus und NICHT dem
    // Weltzeiger. Ohne gesetzte Wurzel liefert das false und alles laeuft wie in Phase 3.
    if(view.GetFocus().OnPadMove(delta, padStepMs_))
        return;
    if(!view.HasPadCursor())
        return;
    const Position cursor = view.ClampToView(view.GetPadCursor() + delta);
    view.SetPadCursor(cursor);
    PushCameraAtEdge(view, cursor, delta);
}

void dskGameInterface::PushCameraAtEdge(PlayerView& view, const Position& cursor, const Position& delta)
{
    const Position origin = view.GetView().GetPos();
    const Extent size = view.GetView().GetSize();
    // Innenrahmen 60 Prozent: je 20 Prozent Rand auf beiden Seiten.
    const Position margin(static_cast<int>(size.x) * 20 / 100, static_cast<int>(size.y) * 20 / 100);

    // Je Achse: nur, wenn der Ausschlag NACH AUSSEN zeigt und der Zeiger auf DERSELBEN Seite
    // ueber dem Innenrahmen steht. Der Anteil waechst von 0 am Rahmen auf 1 am Viewportrand.
    const auto pushOn = [](const int pos, const int lo, const int hi, const int marg, const int d) {
        if(marg <= 0 || d == 0)
            return 0;
        const int over = (d > 0) ? pos - hi : lo - pos;
        if(over <= 0)
            return 0;
        return d * std::min(over, marg) / marg;
    };
    const Position push(pushOn(cursor.x, origin.x + margin.x, origin.x + static_cast<int>(size.x) - 1 - margin.x,
                               margin.x, delta.x),
                        pushOn(cursor.y, origin.y + margin.y, origin.y + static_cast<int>(size.y) - 1 - margin.y,
                               margin.y, delta.y));
    if(push != Position(0, 0))
        view.GetView().MoveBy(push);
}

void dskGameInterface::OnPadCamera(const unsigned slot, const Position& delta)
{
    if(slot >= views_.size())
        return;
    PlayerView& view = *views_[slot];
    // BEWUSST OHNE Ruecksicht auf den Fokus: FocusPath verbraucht den LINKEN Stick und die
    // Knoepfe, den rechten nie. Ein Spieler, der in seinem Lagerfenster steht, darf trotzdem
    // sehen, was auf der Karte passiert - dafuer muss er das Fenster nicht verlassen.
    view.GetView().MoveBy(delta);
    // Der ZEIGER bleibt, wo er ist - am BILDSCHIRMpunkt, nicht am Weltpunkt.
    //
    // Warum: der Padzeiger IST ein Bildschirmzeiger. PlayerView::ClampToView haelt ihn im
    // eigenen Viewport, damit er nicht in das Bild des Nachbarn wandert. Bliebe er am
    // WELTpunkt haengen, waere er nach einem Sekundenbruchteil Kamerafahrt aus dem Viewport
    // heraus, die Klemme zoege ihn zurueck - und der Weltpunkt waere dann trotzdem weg, nur
    // an einer Stelle, die der Spieler nicht vorhersagen kann. Weltverankerung ist bei einem
    // geklemmten Zeiger also gar nicht durchhaltbar, sondern nur bis zum Rand echt.
    //
    // Und sie waere auch das falsche Bedienbild: rechter Stick = Ausschnitt, linker Stick =
    // Punkt darin. Zwei Sticks fuer zwei Groessen. Waere der Zeiger weltverankert, verschoebe
    // der rechte Stick beide - der Spieler haette kein Mittel mehr, den Ausschnitt zu bewegen,
    // ohne sein Ziel zu verlieren. Genau so verhaelt sich auch die Maus heute: die Pfeiltasten
    // und das Ziehen mit der rechten Taste bewegen die Karte, der Mauszeiger bleibt liegen
    // (Msg_KeyDown, gwv.MoveBy).
    //
    // Der Randschub des LINKEN Sticks ist davon unberuehrt und steht an eigener Stelle
    // (PushCameraAtEdge): er greift nur, wenn der Ausschlag den Zeiger nach aussen drueckt.
}

void dskGameInterface::OnPadZoom(const unsigned slot, const float step)
{
    if(slot >= views_.size())
        return;
    PlayerView& view = *views_[slot];
    GameWorldView& gameView = view.GetView();
    float target = gameView.GetCurrentTargetZoomFactor() * (1.f + step);
    // Dieselben Grenzen wie beim Mausrad (WheelZoom) - der Padspieler bekommt keine Ansicht,
    // die der Mausspieler nicht auch einstellen kann.
    target = std::clamp(target, ZOOM_FACTORS.front(), ZOOM_FACTORS.back());
    if(target == gameView.GetCurrentTargetZoomFactor()) //-V550
        return; // schon am Anschlag
    // ZOOMMITTE IST DER ZEIGER, nicht die Viewportmitte. GameWorldView::SetZoomFactor allein
    // schneidet links und rechts gleich viel weg (CalcFxLx: diff/2 von beiden Seiten, und die
    // Projektionsmatrix in Draw rechnet genauso) - der Bildmittelpunkt bleibt also stehen.
    // Fuer die Maus ist das richtig und bleibt unveraendert. Fuer ein Pad am Fernseher waere es
    // falsch: der Zeiger steht dort dauernd am Rand seines Viertelbildschirms, und ein Zoom auf
    // die Mitte schoebe genau den Knoten aus dem Bild, den der Spieler gerade anvisiert.
    // Ohne Zeiger (kein Pad zugeordnet) bleibt es bei der Mitte - dann gibt es keinen besseren
    // Bezugspunkt.
    gameView.SetZoomFactorAt(target, view.HasPadCursor() ? view.GetPadCursor() : view.GetViewCenter());
}

PadRejection dskGameInterface::NothingHereReason(PlayerView& view)
{
    const MapPoint pt = view.GetView().GetSelectedPt();
    if(!pt.isValid())
        return PadRejection::NothingHere;
    // Derselbe Grund, den auch der Klartext unter der Ansicht nennt - eine Rechnung, zwei
    // Ausgaenge. Saehe der Spieler hier einen anderen Satz als dort, waere einer von beiden
    // falsch, und niemand wuesste welcher.
    switch(JudgeNode(view, pt))
    {
        case brief::NodeVerdict::Unexplored: return PadRejection::Unexplored;
        case brief::NodeVerdict::NoMansLand: return PadRejection::NoMansLand;
        case brief::NodeVerdict::ForeignTerritory: return PadRejection::ForeignTerritory;
        default: return PadRejection::NothingHere;
    }
}

void dskGameInterface::PadReject(PlayerView& view, const PadRejection reason)
{
    const bool isNew = view.NoteRejection(reason);
    // 1. Ton - immer. Siehe die Begruendung am Kopf.
    if(SoundEffectItem* sound = LOADER.GetSoundN("sound", 113))
        sound->Play(255, false);
    if(!isNew)
        return;
    // 2. Chatzeile - nur beim Wechsel der Ursache.
    std::string text;
    switch(reason)
    {
        case PadRejection::RoadTooShort: text = _("The road is too short - extend it first."); break;
        case PadRejection::RoadAtLengthLimit: text = _("This waterway cannot get any longer."); break;
        case PadRejection::RoadNoWay: text = _("No road can be built to that point."); break;
        case PadRejection::RoadOutsideTerritory: text = _("You cannot build outside your own territory."); break;
        case PadRejection::RoadEndBlocked: text = _("A road has to end where a flag can stand."); break;
        // Die vier Faelle darunter waren bis Phase 9 EIN Satz. Der Auftraggeber hat nach seinem
        // ersten Spieltest genau das benannt: er wusste nicht, "wann Flagge und wann Gebaeude
        // kommt" - und die Antwort des Spiels darauf war ein Satz, der zu jeder Ursache gleich
        // gut passte und aus dem sich deshalb keine einzige Handlung ableiten liess.
        case PadRejection::NothingHere:
            text = _("Nothing fits on this spot - not even a flag. Move on a field or two.");
            break;
        case PadRejection::Unexplored:
            text = _("You have never seen this place. Send out a scout from one of your flags.");
            break;
        case PadRejection::NoMansLand:
            text = _("This ground belongs to nobody. Build a guard post towards it and your border will "
                     "grow over it.");
            break;
        case PadRejection::ForeignTerritory: text = _("This land belongs to another player."); break;
    }
    messenger.AddMessage(worldViewer.GetWorld().GetPlayer(view.GetPlayerId()).name,
                         worldViewer.GetWorld().GetPlayer(view.GetPlayerId()).color, ChatDestination::System, text,
                         COLOR_RED);
}

void dskGameInterface::OnPadButton(const unsigned slot, const PadButton button, const bool down)
{
    if(slot >= views_.size())
        return;
    PlayerView& view = *views_[slot];
    // BEFUND B1: Ab hier und bis zum Ende dieser Verarbeitung handelt DIESER Spieler.
    //
    // Ein Fensterknopf nennt beim Ausloesen keinen Spieler (ctrlButton::Activate ruft
    // Msg_ButtonClick), und die Fenster erzeugen ihre GameCommands ueber die Fabrik, die sie
    // beim Oeffnen bekommen haben - GAMECLIENT. Ohne diese Klammer buchte GameClient::AddGC
    // unbedingt auf den Hauptspieler, und jeder lokale Spieler, der mit Y ein Fenster betritt
    // und A drueckt, stellte die Produktion, die Reserve und das Lager von Spieler 0 um.
    //
    // Die Klammer sitzt hier und nicht im Fenster, weil hier - und nur hier - bekannt ist, WER
    // drueckt: die Slotnummer ist die Nummer der Ansicht, und die Ansicht kennt ihren Spieler.
    //
    // Sie setzt zugleich den Fensterbesitzer: oeffnet dieser Spieler von hier aus ein Fenster,
    // gehoert es ihm. Ein einziger Wert, aus dem beides faellt - der handelnde Spieler kann
    // gar nicht mehr vom Fensterbesitz abweichen.
    const ViewScope ownerScope(slot);
    // --- ZWEI Knoepfe werden VOR dem Fokus abgefragt ----------------------------------------
    //
    // Die Regel darunter lautet sonst: steht der Spieler in einem Fenster, sieht die Welt seine
    // Flanken nicht (FocusPath::OnPadButton gibt fuer JEDEN Knopf true zurueck). Fuer A, B, das
    // Steuerkreuz und die Schultern ist das richtig - sie haben im Fenster eine eigene
    // Bedeutung. Fuer diese beiden hier ist es eine SACKGASSE, und beide Sackgassen sind in
    // dieser Phase gemessen worden:
    //
    //  - BACK oeffnet das Systemmenue. Betritt der Spieler es (das tut er automatisch), ist
    //    Back von da an verbraucht und das Menue mit demselben Knopf nicht mehr zu schliessen.
    //    Ein Menueknopf, der nur in eine Richtung wirkt, ist keiner.
    //  - Y betritt das oberste eigene Fenster. Oeffnet ein Knopf IN einem Fenster ein ZWEITES
    //    (Postfenster -> Tagebuch, Hauptauswahl -> Statistik), lag das neue Fenster bisher
    //    unerreichbar obenauf: Y wurde vom Fokus im alten geschluckt. Genau das ist der Befund
    //    des Auftraggebers zum Tagebuch.
    //
    // Beide sind rein ADDITIV: FocusPath hat fuer Back und Y keinen Fall (default: break), sie
    // waren dort also wirkungslos. Y faellt ausserdem nur dann heraus, wenn das oberste eigene
    // Fenster ein ANDERES ist als das, in dem der Spieler schon steht - sonst bliebe es beim
    // alten Verhalten, und ein Y im eigenen Fenster wuerde den Fokus zurueck auf das erste
    // Control werfen.
    if(button == PadButton::Back && view.GetRoad().mode == RoadBuildMode::Disabled)
    {
        // Im Baumodus bleibt Back wirkungslos, wie LB und RB auch: dort ist der Modus die
        // Bedeutung, und ein Menue mitten in einer halb gelegten Strasse waere eine Falle.
        if(down)
            PadOpenSystemMenu(view);
        return;
    }
    if(down && button == PadButton::Y)
    {
        IngameWindow* const top = WINDOWMANAGER.GetTopMostWindow(view.GetIndex());
        if(top && top != view.GetFocus().GetRoot())
        {
            EnterWindow(view, top);
            return;
        }
    }
    // Erst der Fokus dieses Spielers. Verbraucht er die Flanke, sieht die Welt sie nie - ein
    // A-Druck auf einem Knopf legt keine Fahne.
    Window* const rootBefore = view.GetFocus().GetRoot();
    if(view.GetFocus().OnPadButton(button, down))
    {
        // Der Fokus kann sich durch B/Start aufgeloest haben; dann faellt der Rahmen weg.
        if(!view.GetFocus().IsActive())
            ClearFocusRing(view, rootBefore);
        return;
    }
    if(!down)
        return;
    // Der Strassenbau ist ein MODUS, und er gehoert dieser einen Ansicht. Solange er laeuft,
    // haben A, X und B eine zweite Bedeutung - genau wie beim Mausspieler, dessen Linksklick im
    // Baumodus einen voellig anderen Zweig nimmt (ContextClick).
    const bool inRoadMode = view.GetRoad().mode != RoadBuildMode::Disabled;
    switch(button)
    {
        // A OEFFNET und erzeugt selbst NIE ein Kommando: es ist das Gegenstueck zum Linksklick
        // des Mausspielers auf einen Knoten (ContextClick) und laeuft durch dieselbe
        // Entscheidung (OpenObjectWindow). Im Lockstep gibt es kein Rueckgaengig; der
        // Hauptknopf darf deshalb nichts festschreiben, was Rohstoffe kostet. Was etwas kostet
        // - abreissen, Gold sperren, Produktion stoppen -, steht danach als BESCHRIFTETER
        // Knopf im geoeffneten Fenster.
        //
        // Im Baumodus verlaengert A den Weg bis zum Zeiger. Auch das schreibt nichts fest: es
        // aendert ausschliesslich die VISUELLE Vorschau auf dem Viewer dieses Spielers. Die
        // Invariante des A-Knopfes bleibt damit woertlich erhalten.
        //
        // Ausserhalb des Baumodus faellt A auf den Strassenbau durch, WENN unter dem Zeiger
        // kein Fenster zu oeffnen war. Auf einer Flagge tat A bisher nichts - OpenObjectWindow
        // kennt nur Schiff, Gebaeude und Baustelle -, und eine eigene Flagge ist genau der
        // Punkt, an dem auch der Mausspieler seinen Strassenbau beginnt. Der Knopf bekommt hier
        // also keine zweite Bedeutung, sondern eine erste.
        //
        // Dritte Stufe: bleibt auch der Strassenbau aus, geht das AKTIONSFENSTER auf
        // (PadOpenActionWindow) - der Weg, auf dem ein Padspieler Gebaeude setzt. Die
        // Reihenfolge ist Absicht: auf einer EIGENEN FLAGGE bietet iwAction ebenfalls etwas an,
        // aber dort ist der Strassenbau die gewachsene Bedeutung des Knopfes und die weitaus
        // haeufigste Handlung. Dass die uebrigen Knoepfe des Flaggenreiters damit hinter A
        // verschwinden, ist seit dieser Runde KEIN Verlust mehr: sie stehen auf der rechten
        // Schulter (siehe dort).
        //
        // Geht auch das nicht, ANTWORTET der Knopf (PadReject). Ein Druck, der nichts tut und
        // nichts sagt, sieht aus wie ein totes Pad - derselbe Befund, aus dem NoteRejection
        // entstanden ist.
        case PadButton::A:
            if(inRoadMode)
                PadExtendRoad(view);
            else if(!PadOpenWindow(view) && !PadStartRoad(view, /*waterRoad*/ false)
                    && !PadOpenActionWindow(view))
                PadReject(view, NothingHereReason(view));
            break;
        // X setzt eine Flagge, bewusst als Ausnahme von der Regel darueber und bewusst NICHT
        // auf A: die Flagge ist das einzige Primitiv ohne Kosten - sie laesst sich im eigenen
        // Fenster sofort wieder abreissen -, und ohne sie ist Strassenbau am Pad unbedienbar.
        // Sie auf einem Nebenknopf zu fuehren haelt "der Hauptknopf schreibt nichts fest"
        // trotzdem ein.
        //
        // Im Baumodus schreibt X den Weg fest. Das ist die konsequente Fortsetzung derselben
        // Regel: X ist der Knopf, der etwas in die Welt schickt, und er ist es an genau einer
        // Stelle je Modus.
        case PadButton::X:
            if(inRoadMode)
                PadCommitRoad(view);
            else
                PadPlaceFlag(view);
            break;
        // B nimmt im Baumodus ein Wegstueck zurueck und bricht auf leerer Strecke ab. B ist der
        // Zurueck-Knopf, den FocusPath schon INNERHALB eines Fensters so benutzt
        // (FocusPath::OnPadButton, case B -> Clear). Damit braucht der Padspieler das
        // mausgebundene iwRoadWindow gar nicht erst: dessen beide Knoepfe sind X und B.
        //
        // BEFUND C: ausserhalb des Baumodus SCHLIESST B jetzt das oberste Fenster dieses
        // Spielers. Vorher konnte er ein geoeffnetes iwAction gar nicht mehr loswerden, ohne zu
        // handeln - B loeste nur den Fokus, und die Titelleistenknoepfe sind keine Controls und
        // damit fuer FocusPath unsichtbar. Das Fenster blieb stehen, bis er etwas kaufte,
        // abriss oder anderswo A drueckte.
        //
        // Warum B und kein neuer Knopf: B ist auf jeder Ebene der Zurueck-Knopf - im Fenster
        // "raus aus dem Fenster", im Baumodus "ein Stueck zurueck", in der Welt jetzt "das
        // Fenster weg". Die Staffelung bleibt dabei erhalten und kostet nichts: steht der
        // Fokus noch im Fenster, verbraucht FocusPath die Flanke (Fokus loesen), und erst der
        // NAECHSTE Druck schliesst. Ein Padspieler kann ein Fenster also weiterhin stehen
        // lassen und nur den Fokus abgeben.
        //
        // Geschlossen wird nach GENAU DERSELBEN Regel wie beim Rechtsklick des Mausspielers
        // (WindowManager::Msg_RightDown): nur eigene bzw. besitzerlose Fenster, nur
        // CloseBehavior::Regular, nie ein angeheftetes. Damit kann ein Padspieler nichts
        // wegwerfen, was der Mausspieler mit der Maus auch nicht wegwerfen koennte.
        //
        // Bewusst OHNE PadReject: anders als bei A ist ein wirkungsloses B kein Sackgassen-
        // Signal. Es hat auf jeder Ebene eine sichtbare Wirkung, sobald es etwas zu verlassen
        // oder zu schliessen gibt; eine Chatzeile "hier gibt es nichts zu schliessen" waere
        // reine Stoerung.
        case PadButton::B:
            if(inRoadMode)
                PadStepBackRoad(view);
            else
                PadCloseTopMostWindow(view);
            break;
        // BEFUND B: die rechte Schulter oeffnet das Aktionsfenster unter dem Zeiger.
        //
        // Sie ist der Ausweg aus der Luecke, die A hinterlaesst: auf einer eigenen Flagge faengt
        // A den Strassenbau an und kommt gar nicht bis zu PadOpenActionWindow. Damit waren
        // "Flagge abreissen", "Geologe rufen" und "Spaeher rufen" am Pad UNERREICHBAR - und an
        // einer Wasserflagge zusaetzlich der Wasserweg-Knopf des Reiters. Ohne "Flagge
        // abreissen" wird eine falsch gesetzte Flagge unbeseitigbar; das ist eine Sackgasse und
        // kein Randfall.
        //
        // Warum die Schulter und nicht A: Strassenbau ist die haeufigste Handlung an einer
        // Flagge und muss bei EINEM Druck bleiben. Wuerde A das Fenster oeffnen, kostete jeder
        // Strassenbau zusaetzlich Y und mindestens einen Fokusschritt - bei einer Handlung, die
        // in einer Partie dutzende Male vorkommt. Umgekehrt kostet der seltene Griff in den
        // Flaggenreiter jetzt genau einen Druck mehr als der haeufige.
        //
        // Warum die RECHTE Schulter: LB traegt schon den Wasserweg, RB war in der Welt als
        // einziger Knopf neben Back/Guide/Sticks ueberhaupt noch frei. Innerhalb eines Fensters
        // verbraucht FocusPath beide Schultern (Move Prev/Next), aber dort laeuft dieser Zweig
        // gar nicht erst - der Fokus verbraucht die Flanke vorher.
        //
        // GUIDE bleibt unbelegt, und der Grund gilt unveraendert: manche Treiber fangen ihn
        // selbst ab, ein Spiel kann sich also nicht auf ihn verlassen.
        //
        // BACK dagegen ist seit dieser Phase belegt - er oeffnet das Systemmenue (siehe die
        // Vorabfrage oben in dieser Funktion und PadOpenSystemMenu). Frueher stand hier, er sei
        // "bewusst nicht genommen, weil auf vielen Geraeten unbeschriftet"; das galt fuer eine
        // WELTHANDLUNG, die man blind treffen muss. Fuer den Weg zu einem beschrifteten Menue
        // gilt es nicht: das Menue nennt sich selbst, ein Fehldruck kostet einen zweiten Druck
        // auf denselben Knopf, und ein Menue braucht den Knopf, den ein Spieler an dieser Stelle
        // sucht. Fuer den GEGENSTAND dieses Zweiges - das Aktionsfenster unter dem Zeiger -
        // bleibt die alte Wahl richtig, und deshalb bleibt sie hier stehen.
        //
        // Im Baumodus bleibt die Schulter wirkungslos - dort ist der Modus die Bedeutung, wie
        // bei A, X und B auch.
        case PadButton::RightShoulder:
            if(!inRoadMode && !PadOpenActionWindow(view))
                PadReject(view, NothingHereReason(view));
            break;
        // Der Wasserweg bekommt einen eigenen Knopf statt einer zweiten Bedeutung von A.
        // Grund: an einer Wasserflagge bietet iwAction BEIDE Wege an (iwAction.cpp, Knopf 1 und
        // Knopf 2); waere die Flaggenart die Entscheidung, koennte der Padspieler von dort aus
        // keine Landstrasse mehr bauen. Die Schulter ist frei - innerhalb eines Fensters
        // verbraucht FocusPath sie zuerst (Move(Dir::Prev)), in der Welt tat sie bisher nichts.
        case PadButton::LeftShoulder:
            if(!inRoadMode)
                PadStartRoad(view, /*waterRoad*/ true);
            break;
        // Y betritt das oberste Fenster. Bewusst NICHT Start: Start ist seit Phase 3 der
        // Knopf, mit dem ein Spieler sein Pad in die Hand nimmt (PadRouter, Uebernahme durch
        // Benutzung), und muss dafuer wirkungslos bleiben.
        case PadButton::Y: EnterTopMostWindow(view); break;
        // Radialmenue und HUD sind ausdruecklich nicht Ziel dieser Phase. Alle uebrigen
        // Knoepfe bleiben deshalb bewusst wirkungslos.
        default: break;
    }
}

void dskGameInterface::ClearFocusRing(PlayerView& view, Window* root)
{
    // Der Rahmen muss am ehemaligen Wurzelfenster abgemeldet werden - und das kennt der Fokus
    // nach einem Clear() nicht mehr. Deshalb wird die Wurzel vom Aufrufer uebergeben.
    // Abgemeldet wird GENAU der Rahmen dieses Spielers; die Rahmen der anderen bleiben stehen.
    if(auto* wnd = dynamic_cast<IngameWindow*>(root))
        wnd->RemoveFocusRing(view.GetFocus());
    view.GetFocus().Clear();
}

void dskGameInterface::ReleaseFocus(PlayerView& view)
{
    ClearFocusRing(view, view.GetFocus().GetRoot());
}

void dskGameInterface::ValidateFocus(PlayerView& view)
{
    // BEFUND B3: EnterTopMostWindow prueft IsMinimized() nur beim Betreten. Minimiert der
    // Mausspieler danach - was er jederzeit kann und der Padspieler nicht verhindern -, bediente
    // dieser sonst weiter ein Fenster, das nicht gezeichnet wird, und traefe Knoepfe, die er
    // nicht sieht. Der Mauspfad ist an derselben Stelle gesperrt
    // (IngameWindow::IsMessageRelayAllowed), der Padpfad muss es auch sein.
    const auto* wnd = dynamic_cast<const IngameWindow*>(view.GetFocus().GetRoot());
    if(wnd && wnd->IsMinimized())
        ReleaseFocus(view);
}

bool dskGameInterface::EnterTopMostWindow(PlayerView& view)
{
    // Das oberste Fenster, das DIESER Spieler bedienen darf: seine eigenen und die, die
    // keiner Ansicht gehoeren (Nachrichtenboxen, Systemfenster - die sieht jeder). Ohne den
    // Besitzerbezug betraete Spieler 2 mit Y das Fenster von Spieler 1 und verstellte es
    // anschliessend in seinem eigenen Namen.
    return EnterWindow(view, WINDOWMANAGER.GetTopMostWindow(view.GetIndex()));
}

bool dskGameInterface::EnterWindow(PlayerView& view, IngameWindow* const wnd)
{
    // Herausgezogen aus EnterTopMostWindow, weil das Padmenue ein BESTIMMTES Fenster betreten
    // muss - naemlich das, das es gerade selbst geoeffnet hat. Ueber "das oberste" ginge das
    // nicht sicher: liegt ein modales Fenster im Stapel, wird ein neues Fenster DAVOR
    // eingefuegt (WindowManager::DoShow) und ist gar nicht oben.
    if(!wnd || wnd->IsMinimized())
        return false;
    // Erst den alten Rahmen abmelden: SetRoot() vergisst die bisherige Wurzel, und ein dort
    // stehen gebliebener Eintrag zeichnete danach einen Rahmen um ein Control, das gar nicht
    // mehr in diesem Fenster liegt.
    ReleaseFocus(view);
    if(!view.GetFocus().SetRoot(wnd))
        return false; // in diesem Fenster gibt es nichts zu bedienen
    wnd->AddFocusRing(view.GetFocus(), view.GetViewer().GetPlayer().color);
    return true;
}

/// DER BACK-KNOPF, und warum er es ist und nicht Start:
///
///  - CONTROLLER-UX.md 2.2 legt genau diesen Inhalt auf `Select` (= Back): "Reich-Radial
///    (Inventar, Gebaeudestatistik, Verteilung, Werkzeuge, Transport, Militaer, Post-Archiv,
///    Ansicht)". Start ist dort mit Absicht anders belegt (getippt die eigene Hilfe, gehalten
///    das GLOBALE Pausenmenue), und die Begruendung steht in der Spezifikation daneben: Start
///    ist der Reflexknopf des Anfaengers und darf nicht drei Mitspielern das Spiel anhalten.
///  - Start ist seit Phase 3 der Knopf, mit dem ein Spieler sein Pad in die Hand nimmt. Die
///    Uebernahmeflanke wird INGAME nicht geschluckt (PadRouter::OnEvent reiht sie nach der
///    Zuordnung ein, anders als MenuPadInput::swallowFrame_ im Menue) - Start mit einer
///    Weltbedeutung zu belegen hiesse, dass jeder Aufnahmedruck sofort ein Menue aufreisst.
///
/// Was der Spieler durch die Belegung verliert: nichts. Back war in der Welt gemessen
/// wirkungslos, und in FocusPath hat er keinen Fall.
bool dskGameInterface::PadOpenSystemMenu(PlayerView& view)
{
    // Die Besitzklammer ist hier bereits offen (OnPadButton): das Menue gehoert DIESEM
    // Sitzplatz, und jeder Knopf darin wirkt auf SEINE Ansicht und bucht auf SEINEN Spieler.
    //
    // Zweiter Druck schliesst wieder - ToggleWindow sucht nach (GUI_ID, Besitzer), das Menue
    // eines Nachbarn bleibt dabei unangetastet.
    if(auto* old = WINDOWMANAGER.FindNonModalWindow(CGI_PADMENU, view.GetIndex()))
    {
        // Steht der Fokus dieses Spielers noch im Menue, muss er MIT verschwinden - sonst
        // bliebe eine Wurzel stehen, deren Fenster gleich zerfaellt.
        if(view.GetFocus().GetRoot() == old)
            ReleaseFocus(view);
        old->Close();
        return true;
    }
    // Solange DIESER Sitzplatz ein MODALES Fenster vor sich hat, geht sein Menue NICHT auf.
    //
    // Der Grund bleibt derselbe: WindowManager::DoShow fuegt jedes neue Fenster VOR dem ersten
    // modalen ein - das Menue laege also HINTER dem modalen und waere unsichtbar, waehrend der
    // Fokus dieses Spielers hineinspringt. Er navigierte dann blind. Der Fall ist kein Randfall:
    // das Tagebuch selbst ist modal (iwMissionStatement, IngameWindow(..., modal = true)).
    //
    // BESITZERBEZOGEN, und das ist nachgemessen und keine Annahme. Vorher stand hier
    // GetTopMostWindow() OHNE Besitzer, und damit sperrte das Tagebuch von Sitzplatz 1 das Menue
    // der Sitzplaetze 0, 2 und 3 mit. Drei Gruende, warum die besitzerlose Form falsch war:
    //
    //  1. Der PADPFAD kennt Modalitaet ueberhaupt nicht. IsModal() wird im ganzen Baum nur an
    //     drei Stellen gelesen: WindowManager (Maus/Tastatur/Einsortierung), IngameWindow
    //     (Minimieren) und dskGameInterface::SetActive (Mausscrollen). FocusPath, PadRouter und
    //     jeder andere Padzweig lesen es NIE. Ein fremdes Modales hindert diesen Spieler also
    //     weder am Strassenbau noch am Aktionsfenster (RB) noch am Schliessen (B) - nur das
    //     Menue war gesperrt. Das war die Ausnahme, nicht die Regel.
    //  2. Es war eine SACKGASSE ohne Ausweg. Ein Modales eines fremden Sitzplatzes kann dieser
    //     Spieler nicht wegraeumen: GetTopMostWindow(view.GetIndex()) liefert es nicht, also
    //     betritt Y es nicht und schliesst B es nicht. Er musste warten, bis der Nachbar handelt.
    //     Beim EIGENEN (oder besitzerlosen) Modalen ist genau das anders - er kommt mit Y hinein
    //     und mit A auf "Weiter" wieder heraus, und danach geht sein Menue auf. Die Sperre bleibt
    //     dort also eine Reihenfolge und wird nirgends zur Falle.
    //  3. Der Rest des Padpfades fragt schon lange besitzerbezogen: EnterTopMostWindow und
    //     PadCloseTopMostWindow benutzen beide GetTopMostWindow(view.GetIndex()). Diese eine
    //     Stelle war der Ausreisser.
    //
    // Die Ueberladung genuegt fuer die Frage "hat DIESER Sitzplatz ein Modales vor sich":
    // GetTopMostWindow(owner) laeuft von hinten und nimmt das erste eigene oder besitzerlose
    // Fenster, und weil Nicht-Modale immer VOR dem ersten Modalen einsortiert werden, ist dieses
    // Fenster genau dann modal, wenn es ein eigenes oder besitzerloses Modales gibt.
    if(const IngameWindow* top = WINDOWMANAGER.GetTopMostWindow(view.GetIndex()); top && top->IsModal())
        return false;
    // Am ZEIGER DIESES SPIELERS, also in seinem Viewport - dieselbe Rechnung wie beim
    // Aktionsfenster (PadOpenActionWindow). IngameWindow::MoveToCenter zentriert weiterhin auf
    // die volle Renderflaeche; ein Menue dort waere im Splitscreen im Bild des Nachbarn.
    const DrawPoint wndPos = view.HasPadCursor() ? DrawPoint(view.GetPadCursor()) : DrawPoint(view.GetViewCenter());
    auto& wnd = WINDOWMANAGER.Show(std::make_unique<iwPadSystemMenu>(*this, view, wndPos));
    // BEWUSST sofort betreten, anders als bei jedem anderen Fenster (dort braucht es Y).
    //
    // Ein Menue ist kein Weltfenster: es hat keinen Bezug zu einem Knoten, der Spieler hat es
    // gerade ausdruecklich aufgerufen, und der einzige Grund, es offen zu haben, ist, darin
    // etwas auszuwaehlen. Ihn danach erst noch Y druecken zu lassen waere genau die
    // unausgesprochene Regel, an der der Auftraggeber beim Tagebuch gescheitert ist ("ich
    // druecke B und es passiert nichts").
    EnterWindow(view, &wnd);
    view.ClearRejection();
    return true;
}

bool dskGameInterface::PadCloseTopMostWindow(PlayerView& view)
{
    // Dasselbe Fenster, das auch Y betreten wuerde (GetTopMostWindow(view.GetIndex())): seine
    // eigenen und die, die keiner Ansicht gehoeren. Ohne den Besitzerbezug schloesse Spieler 2
    // mit B das Fenster von Spieler 1.
    IngameWindow* const wnd = WINDOWMANAGER.GetTopMostWindow(view.GetIndex());
    if(!wnd || wnd->ShouldBeClosed())
        return false;
    // WOERTLICH die Regel des Rechtsklicks (WindowManager::Msg_RightDown): nur Fenster, die
    // sich ueberhaupt so schliessen lassen, und kein angeheftetes. Ein Fenster mit
    // CloseBehavior::Custom regelt sein Ende selbst (es hat dafuer einen beschrifteten Knopf im
    // Fensterinneren, den der Padspieler mit Y und A erreicht).
    if(wnd->getCloseBehavior() != CloseBehavior::Regular || wnd->IsPinned())
        return false;
    // Die Besitzklammer ist hier bereits offen (OnPadButton) - das Schliessen und alles, was es
    // ausloest, laeuft im Namen DIESES Spielers.
    wnd->Close();
    return true;
}

bool dskGameInterface::PadPlaceFlag(PlayerView& view)
{
    const MapPoint pt = view.GetView().GetSelectedPt();
    if(!pt.isValid())
        return false;
    // Der Kommandopfad DIESES Spielers - nicht GAMECLIENT direkt. Genau hier entscheidet sich,
    // fuer wen der GameCommand erzeugt wird (network/LocalPlayerGCFactory.cpp).
    GameCommandFactory* const factory = GAMECLIENT.GetGCFactory(static_cast<uint8_t>(view.GetPlayerId()));
    if(!factory)
        return false;
    return factory->SetFlag(pt);
}

bool dskGameInterface::PadStartRoad(PlayerView& view, const bool waterRoad)
{
    // Zwei Baumodi uebereinander gibt es nicht: der laufende muesste sonst still verworfen
    // werden und liesse seine visuelle Vorschau stehen.
    if(view.GetRoad().mode != RoadBuildMode::Disabled)
        return false;
    const MapPoint pt = view.GetView().GetSelectedPt();
    if(!pt.isValid())
        return false;

    // SCHUTZ: der Startpunkt MUSS eine Flagge DIESES Spielers sein.
    //
    // Im Mauspfad stellt das ausschliesslich die Bedienoberflaeche sicher: der Knopf "Strasse
    // bauen" existiert nur im Flaggenreiter von iwAction, und den setzt ContextClick nur bei
    // IsOwner(cSel) und NodalObjectType::Flag. GI_StartRoadBuilding selbst prueft nichts.
    // Ohne diese Zeilen koennte ein Padspieler den Baumodus auf jedem beliebigen Knoten
    // starten; die Simulation faenge das erst nach einem Netzwerkumlauf ab
    // (world/GameWorld.cpp:196-201) - mit einer stillen ConstructionFailed-Notiz und ohne
    // jede Rueckmeldung an den Spieler.
    const noFlag* const flag = view.GetViewer().GetWorld().GetSpecObj<noFlag>(pt);
    if(!flag || flag->GetPlayer() != static_cast<unsigned char>(view.GetPlayerId()))
        return false;
    // Wasserwege gibt es nur an einer Wasserflagge - dieselbe Bedingung, unter der iwAction den
    // zweiten Knopf ueberhaupt anbietet (iwAction.cpp: FlagType::WaterFlag, gesetzt aus
    // noFlag::GetFlagType() == FlagType::Water).
    if(waterRoad && flag->GetFlagType() != FlagType::Water)
        return false;

    view.ClearRejection();
    StartRoadBuilding(view, pt, waterRoad);
    return true;
}

bool dskGameInterface::PadExtendRoad(PlayerView& view)
{
    RoadBuildState& rb = view.GetRoad();
    if(rb.mode == RoadBuildMode::Disabled)
        return false;
    const MapPoint pt = view.GetView().GetSelectedPt();
    // Der Zeiger steht auf dem Wegende - der HAEUFIGSTE Zustand, weil GameWorldView::DrawGUI
    // genau diesen Punkt hervorhebt. Hier ist nichts zu verlaengern; BuildRoadPart faengt es
    // zwar auch ab, aber dieser Zweig sagt es aus, statt sich darauf zu verlassen.
    if(!pt.isValid() || pt == rb.point)
        return false;

    // Zeigt der Spieler auf ein Stueck, das er selbst schon gelegt hat, ist das ein Rueckbau
    // bis dorthin - dieselbe Entscheidung, die auch der Mausklick trifft (ContextClick,
    // GetIdInCurBuildRoad -> DemolishRoad). Beides ist rein visuell.
    if(const unsigned idOnRoad = GetIdInCurBuildRoad(view, pt))
    {
        DemolishRoad(view, idOnRoad);
        return true;
    }

    // SCHUTZ (BEFUND A): der Zielknoten muss auf EIGENEM Gebiet liegen.
    //
    // Der Mauspfad prueft das ausdruecklich (ContextClick: IsRoadAvailable(...) &&
    // IsPlayerTerritory(selPt), und die uebrigen Zweige haengen an GetBQ != Nothing bzw. an
    // einer Flagge) - er kann in diesen Zustand gar nicht geraten. Der Padpfad konnte es, weil
    // FindPathForRoad seine Wegbedingung fuer jeden Knoten AUSSER Start und Ziel auswertet: der
    // Zielknoten darf jenseits der Grenze liegen, der Weg wird gefunden, die Vorschau entsteht -
    // und GameWorld::BuildRoad verwirft die fertige Strasse anschliessend still, weil dort keine
    // Flagge stehen kann (world/GameWorld.cpp:222-241).
    //
    // Die Pruefung sitzt HIER und nicht erst beim Festschreiben, weil der Spieler es beim ersten
    // A erfahren soll und nicht erst nach acht Kanten Vorschau.
    if(!IsRoadTargetAllowed(view, pt))
    {
        PadReject(view, PadRejection::RoadOutsideTerritory);
        return false;
    }

    // Ab hier: verlaengern. BuildRoadPart aendert AUSSCHLIESSLICH den Viewer und den
    // RoadBuildState dieser Ansicht - es entsteht kein GameCommand. Das ist die Invariante des
    // A-Knopfes, und sie gilt im Baumodus genauso wie ausserhalb.
    MapPoint target = pt;
    switch(BuildRoadPart(view, target))
    {
        case RoadPartResult::Built: return true;
        // BEFUND 3/4: hier - und nicht erst beim Festschreiben - merkt der Spieler zum ersten
        // Mal, dass sein Weg nicht weitergeht. Ohne Rueckmeldung drueckt er A und sieht nichts.
        case RoadPartResult::AtLengthLimit: PadReject(view, PadRejection::RoadAtLengthLimit); return false;
        case RoadPartResult::Rejected: PadReject(view, PadRejection::RoadNoWay); return false;
    }
    return false;
}

bool dskGameInterface::IsRoadTargetAllowed(const PlayerView& view, const MapPoint pt) const
{
    const GameWorldViewer& viewer = view.GetViewer();
    if(viewer.IsPlayerTerritory(pt))
        return true;
    // Eine EIGENE Flagge bleibt auch dann ein zulaessiges Ziel, wenn sie nach einer
    // Gebietsverschiebung nicht mehr im Inneren liegt: GameWorld::BuildRoad laesst eine Strasse
    // an einer Flagge des eigenen Spielers ausdruecklich enden, ohne die Bauqualitaet zu fragen.
    const noFlag* const flag = viewer.GetWorld().GetSpecObj<noFlag>(pt);
    return flag && flag->GetPlayer() == static_cast<unsigned char>(view.GetPlayerId());
}

bool dskGameInterface::CanRoadEndAt(const PlayerView& view, const MapPoint pt) const
{
    const GameWorldViewer& viewer = view.GetViewer();
    // WOERTLICH die Endpunktregel von GameWorld::BuildRoad (world/GameWorld.cpp:222-241),
    // gelesen auf dem Viewer DIESES Spielers - also auf dem Bild, das er vor sich hat.
    if(const noFlag* const flag = viewer.GetWorld().GetSpecObj<noFlag>(pt))
        return flag->GetPlayer() == static_cast<unsigned char>(view.GetPlayerId());
    return viewer.GetBQ(pt) != BuildingQuality::Nothing && !viewer.GetWorld().IsFlagAround(pt);
}

bool dskGameInterface::PadCommitRoad(PlayerView& view)
{
    RoadBuildState& rb = view.GetRoad();
    // SCHUTZ (BEFUND A, zweite Haelfte): eine Strecke, deren Ende keine Flagge tragen kann,
    // geht gar nicht erst ins Netz.
    //
    // CommitRoad selbst darf das nicht pruefen: es ist auch der Weg des MAUSSPIELERS
    // (GI_BuildRoad), und dort haengt der Knopf schon an derselben Bedingung - iwRoadWindow
    // bekommt sein `enable_flag` aus GetBQ(road.point) != Nothing. Eine zweite Pruefung dort
    // waere wirkungslos, hier ist sie es nicht.
    if(rb.mode != RoadBuildMode::Disabled && rb.route.size() >= 2 && !CanRoadEndAt(view, rb.point))
    {
        PadReject(view, PadRejection::RoadEndBlocked);
        return false;
    }
    // Die uebrige Absicherung sitzt in CommitRoad, weil sie fuer JEDEN Aufrufer gilt - auch
    // fuer den Knopf im Strassenfenster des Mausspielers.
    if(CommitRoad(view))
        return true;
    // BEFUND 3, die Sackgasse: A einmal von der eigenen Flagge aus -> route.size() == 1, X ->
    // CommitRoad -> route.size() < 2 -> false, Modus bleibt Normal. Noch einmal X: dasselbe,
    // beliebig oft, und nichts sagte es dem Spieler. Der einzige Ausweg war B.
    //
    // Der Modus bleibt bewusst weiter stehen (ein X darf einen laufenden Bau nicht abbrechen -
    // das ist B), aber der Spieler erfaehrt jetzt, warum nichts geschieht.
    if(rb.mode != RoadBuildMode::Disabled && rb.route.size() < 2)
        PadReject(view, PadRejection::RoadTooShort);
    return false;
}

bool dskGameInterface::PadStepBackRoad(PlayerView& view)
{
    RoadBuildState& rb = view.GetRoad();
    if(rb.mode == RoadBuildMode::Disabled)
        return false;
    // SCHUTZ: auf leerer Strecke gibt es kein Stueck mehr zurueckzunehmen. DemolishRoad laeuft
    // rueckwaerts mit unsigned; mit start_id == 0 liefe die Schleife in den Unterlauf. Statt
    // dessen ist der Schritt zurueck an dieser Stelle der Abbruch - der Spieler kann sich also
    // mit demselben Knopf vollstaendig aus dem Baumodus herausdruecken und braucht dafuer kein
    // Fenster.
    if(rb.route.empty())
    {
        CancelRoadBuilding(view);
        return true;
    }
    DemolishRoad(view, static_cast<unsigned>(rb.route.size()));
    return true;
}

void dskGameInterface::Run()
{
    // Reset draw counter of the trees before drawing
    noTree::ResetDrawCounter();

    unsigned water_percent = 0;
    const Position mousePos = VIDEODRIVER.GetMousePos();
    // Draw mouse only if not on window
    const bool drawMouse = WINDOWMANAGER.FindWindowAtPos(mousePos) == nullptr;

    // Vergangene Zeit seit dem letzten Frame. Der erste Frame zaehlt als 0 ms.
    const unsigned now = static_cast<unsigned>(VIDEODRIVER.GetTickCount());
    const unsigned elapsedMs = (lastInputTick_ == 0 || now < lastInputTick_) ? 0u : now - lastInputTick_;
    lastInputTick_ = now;

    // Die EINZIGE Stelle, an der Zeigerbesitz entschieden wird. Danach wird er hier nur noch
    // gelesen - die Regel steht nicht ein zweites Mal in der Zeichenschleife.
    UpdateInput(elapsedMs, mousePos);

    for(auto& view : views_)
    {
        const bool hasCursor = view->GetView().GetCursorPos().has_value();
        view->GetView().Draw(view->GetRoad(),
                             view->actionwindow != nullptr ? view->actionwindow->GetSelectedPt() :
                                                             MapPoint::Invalid(),
                             drawMouse && hasCursor, view.get() == &primary() ? &water_percent : nullptr);
    }

    // Indicate that the game is paused by darkening the screen (dark semi-transparent overlay)
    if(GAMECLIENT.IsPaused())
        DrawRectangle(Rect(DrawPoint(0, 0), VIDEODRIVER.GetRenderSize()), COLOR_SHADOW);
    else
    {
        // Play ambient sounds if game is not paused
        worldViewer.GetSoundMgr().playOceanBrawling(water_percent);
        worldViewer.GetSoundMgr().playBirdSounds(noTree::QueryDrawCounter());
    }

    messenger.Draw();
}

void dskGameInterface::UpdateRoadCursor(const PlayerView& view)
{
    // Es gibt genau EINEN Mauszeiger (WindowManager::SetCursor). Er gehoert dem Mausspieler,
    // und der sitzt in der Hauptansicht - Aktionsfenster, Postfach und Knopfleiste haengen
    // ebenfalls an ihr. Baut ein Padspieler in Ansicht 1 eine Strasse, darf das dem
    // Mausspieler nicht das Zeigerbild auf "Abreissen" stellen.
    //
    // Fuer den Einzelspieler ist primary() die EINZIGE Ansicht: dort laeuft dieser Zweig
    // immer, und das Verhalten ist Bit fuer Bit das von vorher.
    if(&view != &primary())
        return;
    if(view.GetRoad().mode != RoadBuildMode::Disabled)
        WINDOWMANAGER.SetCursor(Cursor::Remove);
    else
        WINDOWMANAGER.SetCursor(view.isScrolling ? Cursor::Scroll : Cursor::Hand);
}

void dskGameInterface::StartRoadBuilding(PlayerView& view, const MapPoint startPt, const bool waterRoad)
{
    // Im Replay keine Straßen bauen
    if(GAMECLIENT.IsReplayModeOn())
        return;

    RoadBuildState& rb = view.GetRoad();
    rb.mode = waterRoad ? RoadBuildMode::Boat : RoadBuildMode::Normal;
    rb.route.clear();
    rb.start = rb.point = startPt;
    UpdateRoadCursor(view);
}

/// Die Ansicht, in deren Namen gerade ein Aktionsfenster handelt - sonst die Hauptansicht.
///
/// Das Gegenstueck zu RoadWindowOwner(), aus demselben Grund: iwAction ruft
/// gi.GI_StartRoadBuilding() ohne jeden Spielerbezug (GameInterface.h kennt keine Ansichten).
/// Seit ContextClick das Aktionsfenster fuer die Ansicht UNTER DER MAUS oeffnet - und seit ein
/// Padspieler es selbst oeffnen kann - waere primary() dort die falsche Antwort: der Bauknopf
/// startete den Strassenbau bei einem anderen Spieler, der davon nichts weiss, und der
/// Ausloeser saehe gar keine Wirkung.
///
/// Gefragt wird ZUERST die laufende Besitzklammer und nicht die Fensterliste. Der Grund ist
/// die Lage, in der eine Suche ueber views_ nachweislich falsch antwortet: zwei lokale Spieler
/// koennen GLEICHZEITIG ein Aktionsfenster offen haben (einer per Maus, einer per Pad). Die
/// Suche liefert dann das erste in der Liste - also unter Umstaenden die Ansicht, die gar
/// nicht gedrueckt hat. Die Klammer dagegen wird an genau der Stelle gesetzt, an der bekannt
/// ist, WER drueckt: WindowManager::RelayMouseMessage stempelt den Besitzer des Fensters,
/// das die Maus bedient, und dskGameInterface::ViewScope die Ansicht des Pads. Beides ist
/// dieselbe Zahl - die Nummer der Ansicht.
///
/// Die Suche bleibt als zweite Stufe stehen: Aufrufe, die von ausserhalb jeder Klammer kommen
/// (die spielerlose Signatur in den Nachweisen), verhalten sich damit unveraendert. Ohne
/// Fenster und ohne Klammer ist das Ergebnis primary() - der Einzelspieler und jeder andere
/// Fall bleiben exakt wie bisher.
PlayerView& dskGameInterface::ActionWindowOwner()
{
    const unsigned ambientOwner = WINDOWMANAGER.GetCurrentWindowOwner();
    if(ambientOwner < views_.size())
        return *views_[ambientOwner];
    for(auto& view : views_)
    {
        if(view->actionwindow)
            return *view;
    }
    return primary();
}

void dskGameInterface::GI_StartRoadBuilding(const MapPoint startPt, bool waterRoad)
{
    StartRoadBuilding(ActionWindowOwner(), startPt, waterRoad);
}

void dskGameInterface::CancelRoadBuilding(PlayerView& view)
{
    RoadBuildState& rb = view.GetRoad();
    if(rb.mode == RoadBuildMode::Disabled)
        return;
    rb.mode = RoadBuildMode::Disabled;
    view.GetViewer().RemoveVisualRoad(rb.start, rb.route);
    // Die Route gehoert zu einem Bau, den es nicht mehr gibt. Frueher blieb sie stehen; gelesen
    // wurde sie ausserhalb des Baumodus von niemandem (GameWorldView::DrawGUI kehrt bei
    // Disabled sofort zurueck, GI_BuildRoad war nur aus iwRoadWindow erreichbar). Am Padpfad
    // ist ein zweites RemoveVisualRoad auf derselben Route dagegen erreichbar - deshalb wird
    // sie hier geleert, statt sich auf den naechsten StartRoadBuilding zu verlassen.
    rb.route.clear();
    UpdateRoadCursor(view);
}

/// Die Ansicht, in deren Namen gerade ein Strassenfenster handelt - sonst die Hauptansicht.
///
/// iwRoadWindow ruft GI_BuildRoad/GI_CancelRoadBuilding ohne Spielerbezug (GameInterface.h
/// kennt keine Ansichten). Seit ContextClick das Fenster fuer die Ansicht unter der MAUS
/// oeffnet, waere primary() dort die falsche Antwort: die beiden Knoepfe wirkten auf den
/// Strassenbau eines anderen Spielers.
///
/// Gefragt wird ZUERST die laufende Besitzklammer (WINDOWMANAGER.GetCurrentWindowOwner()) und
/// nicht die Fensterliste - dieselbe Bauform wie bei ActionWindowOwner(), und aus demselben,
/// jetzt GEMESSENEN Grund.
///
/// Die frueher hier stehende Begruendung ("es kann nur ein iwRoadWindow geben, weil
/// ausschliesslich der Mauspfad es oeffnet") war falsch. Der erste Halbsatz stimmt, der zweite
/// nicht: ShowRoadWindow oeffnet fuer die Ansicht UNTER DER MAUS, und
/// WINDOWMANAGER.Close(CGI_ROADWINDOW, view.GetIndex()) raeumt nur das der EIGENEN Ansicht ab.
/// Zwei mausgesteuerte Ansichten koennen deshalb gleichzeitig je ein Strassenfenster halten -
/// mit der einen Maus in zwei Klicks erreicht. Die Suche ueber views_ lieferte dann die ERSTE
/// der Liste, also unter Umstaenden die Ansicht, die gar nicht gedrueckt hatte: der
/// Abbrechen-Knopf brach beim Falschen ab, und der Bau-Knopf schickte den GameCommand auf das
/// Konto des falschen Spielers (gemessen im Replay,
/// TwoMouseViewsCanHoldARoadWindowEachAndBuildBooksOnThePressedOne).
///
/// Die Klammer dagegen wird an genau der Stelle gesetzt, an der bekannt ist, WER drueckt:
/// WindowManager::RelayMouseMessage stempelt den Besitzer des Fensters, das die Maus bedient.
///
/// Die Suche bleibt als zweite Stufe stehen, damit Aufrufe von ausserhalb jeder Klammer sich
/// unveraendert verhalten. Ohne Fenster und ohne Klammer ist das Ergebnis primary() - der
/// Einzelspieler bleibt exakt wie bisher.
PlayerView& dskGameInterface::RoadWindowOwner()
{
    const unsigned ambientOwner = WINDOWMANAGER.GetCurrentWindowOwner();
    if(ambientOwner < views_.size())
        return *views_[ambientOwner];
    for(auto& view : views_)
    {
        if(view->roadwindow)
            return *view;
    }
    return primary();
}

void dskGameInterface::GI_CancelRoadBuilding()
{
    CancelRoadBuilding(RoadWindowOwner());
}

dskGameInterface::RoadPartResult dskGameInterface::BuildRoadPart(PlayerView& view, MapPoint& cSel)
{
    RoadBuildState& rb = view.GetRoad();
    GameWorldViewer& viewer = view.GetViewer();
    // SCHUTZ: FindPathForRoad haelt im Debugbau an, wenn Start und Ziel derselbe Punkt sind
    // (pathfinding/FindPathForRoad.cpp:36 RTTR_Assert(startPt != endPt)). Der Mauspfad faengt
    // das genau eine Ebene hoeher ab (ContextClick: selPt == road.point), der Padpfad koennte
    // es nicht - der Zeiger steht nach jedem Wegstueck genau auf rb.point, das ist dort der
    // HAEUFIGSTE Zustand. Die Pruefung gehoert deshalb hierher, wo sie fuer jeden Aufrufer
    // gilt. Fuer den Mauspfad ist sie unerreichbar und damit wirkungslos.
    if(!cSel.isValid() || cSel == rb.point)
        return RoadPartResult::Rejected;

    std::vector<Direction> new_route = FindPathForRoad(viewer, rb.point, cSel, rb.mode == RoadBuildMode::Boat, 100);
    // Weg gefunden?
    if(new_route.empty())
        return RoadPartResult::Rejected;

    // Test on water way length
    if(rb.mode == RoadBuildMode::Boat)
    {
        unsigned char index = viewer.GetWorld().GetGGS().getSelection(AddonId::MAX_WATERWAY_LENGTH);

        RTTR_Assert(index < waterwayLengths.size());
        const unsigned max_length = waterwayLengths[index];

        unsigned length = rb.route.size() + new_route.size();

        // max_length == 0 heißt beliebig lang, ansonsten
        // Weg zurechtstutzen.
        if(max_length > 0)
        {
            // SCHUTZ: !new_route.empty() in der Bedingung. Ohne sie liefe pop_back() auf einen
            // leeren Vektor, sobald rb.route.size() allein schon groesser als max_length ist -
            // undefiniertes Verhalten, kein Assert. Ueber den Mauspfad ist das nicht erreichbar
            // (rb.route waechst nur durch genau diese Funktion), ueber einen Padpfad, der
            // Routen anders zusammensetzt, sehr wohl.
            while(length > max_length && !new_route.empty())
            {
                new_route.pop_back();
                --length;
            }
        }
        // Vollstaendig weggekuerzt heisst: es passt kein Stueck mehr hinein. Frueher lief die
        // Schleife darunter dann null Mal, cSel wurde auf das UNVERAENDERTE Wegende gesetzt und
        // die Funktion meldete trotzdem Erfolg - ein stiller Fehlschlag, den der Aufrufer nur
        // ueber den Vergleich selPt == targetPt bemerken konnte.
        //
        // Jetzt sagt sie es aus - aber als EIGENES Ergebnis und nicht als Rejected: der
        // Mauspfad tut daraufhin nichts (wie frueher), der Padpfad sagt es dem Spieler.
        if(new_route.empty())
            return RoadPartResult::AtLengthLimit;
    }

    // Weg (visuell) bauen
    for(const auto dir : new_route)
    {
        viewer.SetVisiblePointRoad(rb.point, dir,
                                   (rb.mode == RoadBuildMode::Boat) ? PointRoad::Boat : PointRoad::Normal);
        viewer.RecalcBQForRoad(rb.point);
        rb.point = viewer.GetWorld().GetNeighbour(rb.point, dir);
    }
    viewer.RecalcBQForRoad(rb.point);

    // Zielpunkt updaten (für Wasserweg)
    cSel = rb.point;

    rb.route.insert(rb.route.end(), new_route.begin(), new_route.end());

    // Etwas ist gelungen: eine stehende Fehlermeldung dieser Ansicht gilt nicht mehr.
    view.ClearRejection();
    return RoadPartResult::Built;
}

bool dskGameInterface::BuildRoadPart(MapPoint& cSel)
{
    return BuildRoadPart(primary(), cSel) == RoadPartResult::Built;
}

unsigned dskGameInterface::GetIdInCurBuildRoad(const PlayerView& view, const MapPoint pt) const
{
    const RoadBuildState& rb = view.GetRoad();
    MapPoint curPt = rb.start;
    for(unsigned i = 0; i < rb.route.size(); ++i)
    {
        if(curPt == pt)
            return i + 1;

        curPt = view.GetViewer().GetNeighbour(curPt, rb.route[i]);
    }
    return 0;
}

unsigned dskGameInterface::GetIdInCurBuildRoad(const MapPoint pt)
{
    return GetIdInCurBuildRoad(primary(), pt);
}

void dskGameInterface::ShowRoadWindow(PlayerView& view, const Position& mousePos)
{
    // iwRoadWindow ist der mausgebundenste Teil des ganzen Pfads: es setzt im Konstruktor
    // VIDEODRIVER.SetMousePos auf seinen Vorgabeknopf und beim Klick ein zweites Mal zurueck -
    // es gibt aber nur EINE Maus. Genau deshalb oeffnet es AUSSCHLIESSLICH der Mauspfad, und
    // zwar fuer die Ansicht, die den Mauszeiger haelt (ContextClick). Der Padpfad benutzt es
    // gar nicht: seine beiden Knoepfe sind X (festschreiben) und B (zurueck bzw. abbrechen).
    view.roadwindow = &WINDOWMANAGER.Show(
      std::make_unique<iwRoadWindow>(*this, view.GetViewer().GetBQ(view.GetRoad().point) != BuildingQuality::Nothing,
                                     mousePos),
      true);
}

void dskGameInterface::ShowRoadWindow(const Position& mousePos)
{
    ShowRoadWindow(primary(), mousePos);
}

void dskGameInterface::ShowActionWindow(const iwAction::Tabs& action_tabs, MapPoint cSel, const DrawPoint& mousePos,
                                        const bool enable_military_buildings)
{
    ShowActionWindow(primary(), action_tabs, cSel, mousePos, enable_military_buildings);
}

void dskGameInterface::ShowActionWindow(PlayerView& view, const iwAction::Tabs& action_tabs, MapPoint cSel,
                                        const DrawPoint& mousePos, const bool enable_military_buildings,
                                        const iwAction::MousePointer mousePointer)
{
    GameWorldViewer& worldViewer = view.GetViewer();
    const GameWorldBase& world = worldViewer.GetWorld();

    iwAction::Params params;

    // Sind wir am Wasser?
    if(action_tabs.setflag)
    {
        auto isWater = [](const auto& desc) { return desc.kind == TerrainKind::Water; };
        if(world.HasTerrain(cSel, isWater))
            params = iwAction::FlagType::WaterFlag;
    }

    // Wenn es einen Flaggen-Tab gibt, dann den Flaggentyp herausfinden und die Art des Fensters entsprechende setzen
    if(action_tabs.flag)
    {
        if(world.GetNO(world.GetNeighbour(cSel, Direction::NorthWest))->GetGOT() == GO_Type::NobHq)
            params = iwAction::FlagType::HQ;
        else if(world.GetNO(cSel)->GetType() == NodalObjectType::Flag)
        {
            if(world.GetSpecObj<noFlag>(cSel)->GetFlagType() == FlagType::Water)
                params = iwAction::FlagType::WaterFlag;
        }
    }

    // Angriffstab muss wissen, wieviel Soldaten maximal entsendet werden können
    if(action_tabs.attack)
    {
        params = worldViewer.GetNumSoldiersForAttack(cSel);
    }

    // Fenster und Ansicht muessen zusammenpassen: iwAction rechnet mit der Ansicht, aus der es
    // geoeffnet wurde (Beobachtungsfenster, Angriffsziel). Mit der Uebergangsreferenz gwv waere
    // das im Splitscreen die Ansicht eines fremden Spielers gewesen.
    view.actionwindow = &WINDOWMANAGER.Show(
      std::make_unique<iwAction>(*this, view.GetView(), action_tabs, cSel, mousePos, params,
                                 enable_military_buildings, mousePointer),
      true);
}

void dskGameInterface::OnChatCommand(const std::string& cmd)
{
    cheatCommandTracker_.onChatCommand(cmd);

    if(cmd == "surrender")
        GAMECLIENT.Surrender();
    else if(cmd == "async")
        (void)RANDOM.Rand(RANDOM_CONTEXT2(0), 255);
    else if(cmd == "segfault")
    {
        char* x = nullptr;
        *x = 1; //-V522 // NOLINT
    } else if(cmd == "reload")
    {
        WorldDescription newDesc;
        GameDataLoader gdLoader(newDesc);
        if(gdLoader.Load())
        {
            const_cast<GameWorld&>(game_->world_).GetDescriptionWriteable() = newDesc;
            forEachView([](PlayerView& view) { view.GetViewer().InitTerrainRenderer(); });
        }
    }
}

bool dskGameInterface::CommitRoad(PlayerView& view)
{
    RoadBuildState& rb = view.GetRoad();
    if(rb.mode == RoadBuildMode::Disabled)
        return false;
    // SCHUTZ: GameWorld::BuildRoad haelt im Debugbau an, wenn die Route weniger als zwei
    // Richtungen hat (world/GameWorld.cpp:189-195 RTTR_Assert(false)) - und im Releasebau
    // kehrt es dort wortlos zurueck, OHNE eine RoadNote zu veroeffentlichen. Genau die Note
    // raeumt aber die visuelle Vorschau ab (GameWorldViewer::RoadConstructionEnded). Eine zu
    // kurze Route waere also im Debugbau ein Abbruch und im Releasebau eine Geisterstrasse,
    // die fuer immer im Bild dieses Spielers stehen bliebe.
    //
    // Ueber den Mauspfad ist das unerreichbar (die Spielregel "keine zwei Flaggen nebeneinander"
    // macht eine Ein-Kanten-Strasse von einer Flagge weg unmoeglich), abgesichert war es aber
    // nirgends - weder hier noch in GameCommandFactory::BuildRoad noch im Konstruktor von
    // gc::BuildRoad. Am Padpfad ist es mit zwei Knopfdruecken erreichbar.
    if(rb.route.size() < 2)
        return false;
    // Die Kommandofabrik DIESER Ansicht, nicht GAMECLIENT: hier - und nur hier - entscheidet
    // sich, auf WEN die Strasse gebucht wird. Ohne das schickte ein Padspieler, der mit Y/A den
    // Knopf im Strassenfenster drueckt, die Route des HAUPTSPIELERS in seinem eigenen Namen ab.
    if(!gcFactoryFor(view).BuildRoad(rb.start, rb.mode == RoadBuildMode::Boat, rb.route))
        return false;
    rb.mode = RoadBuildMode::Disabled;
    // Die Route bleibt hier bewusst STEHEN: die visuelle Vorschau liegt noch auf dem Viewer und
    // wird erst abgeraeumt, wenn die Simulation die Strasse gebaut oder abgelehnt hat
    // (RoadNote -> GameWorldViewer::RoadConstructionEnded). Sie ist der Schluessel dafuer.
    UpdateRoadCursor(view);
    return true;
}

void dskGameInterface::GI_BuildRoad()
{
    CommitRoad(RoadWindowOwner());
}

void dskGameInterface::Msg_WindowClosed(IngameWindow& wnd)
{
    forEachView([&](PlayerView& view) {
        if(view.actionwindow == &wnd)
            view.actionwindow = nullptr;
        else if(view.roadwindow == &wnd)
            view.roadwindow = nullptr;
        // Lebensdauer des Fokus: hier ist das Fenster noch am Leben, der Rahmen kann also
        // sauber abgemeldet werden. Der Destruktor von IngameWindow ist nur der Backstop.
        if(view.GetFocus().GetRoot() == &wnd)
            ClearFocusRing(view, &wnd);
    });
}

void dskGameInterface::GI_FlagDestroyed(const MapPoint pt)
{
    // Die Welt nennt hier keinen Spieler (world/GameWorld.cpp:117 ruft ohne Spielerbezug auf),
    // also muss jede Ansicht ihren EIGENEN Strassenbauzustand und ihr eigenes Aktionsfenster
    // pruefen.
    forEachView([&](PlayerView& view) {
        // Im Wegbaumodus und haben wir von hier eine Flagge gebaut?
        //
        // Frueher wurde nur bei primary() vollstaendig abgeraeumt; bei jeder anderen Ansicht
        // wurde blos der Modus abgeschaltet - RemoveVisualRoad blieb aus. Deren visuelle
        // Strasse waere fuer immer stehen geblieben und ihre BQ dauerhaft falsch. Jetzt nimmt
        // jede Ansicht denselben Weg.
        if(view.GetRoad().start == pt)
            CancelRoadBuilding(view);

        // Evtl Actionfenster schliessen, da sich das ja auch auf diese Flagge bezieht
        if(view.actionwindow && view.actionwindow->GetSelectedPt() == pt)
            view.actionwindow->Close();
    });
}

void dskGameInterface::CI_PlayerLeft(const unsigned playerId)
{
    // Info-Meldung ausgeben
    std::string text =
      helpers::format(_("Player '%s' left the game!"), worldViewer.GetWorld().GetPlayer(playerId).name);
    messenger.AddMessage("", 0, ChatDestination::System, text, COLOR_RED);
    // Im Spiel anzeigen, dass die KI das Spiel betreten hat
    text = helpers::format(_("Player '%s' joined the game!"), "KI");
    messenger.AddMessage("", 0, ChatDestination::System, text, COLOR_GREEN);
}

void dskGameInterface::CI_GGSChanged(const GlobalGameSettings& /*ggs*/)
{
    // TODO: print what has changed
    const std::string text = helpers::format(_("Note: Game settings changed by the server%s"), "");
    messenger.AddMessage("", 0, ChatDestination::System, text);
}

void dskGameInterface::CI_Chat(const unsigned playerId, const ChatDestination cd, const std::string& msg)
{
    messenger.AddMessage(worldViewer.GetWorld().GetPlayer(playerId).name,
                         worldViewer.GetWorld().GetPlayer(playerId).color, cd, msg);
}

void dskGameInterface::CI_Async(const std::string& checksums_list)
{
    messenger.AddMessage("", 0, ChatDestination::System,
                         _("The Game is not in sync. Checksums of some players don't match."), COLOR_RED);
    messenger.AddMessage("", 0, ChatDestination::System, checksums_list, COLOR_YELLOW);
    messenger.AddMessage("", 0, ChatDestination::System, _("A auto-savegame is created..."), COLOR_RED);
}

void dskGameInterface::CI_ReplayAsync(const std::string& msg)
{
    messenger.AddMessage("", 0, ChatDestination::System, msg, COLOR_RED);
}

void dskGameInterface::CI_ReplayEndReached(const std::string& msg)
{
    messenger.AddMessage("", 0, ChatDestination::System, msg, COLOR_BLUE);
}

void dskGameInterface::CI_GamePaused()
{
    messenger.AddMessage(_("SYSTEM"), COLOR_GREY, ChatDestination::System, _("Game was paused."));
}

void dskGameInterface::CI_GameResumed()
{
    messenger.AddMessage(_("SYSTEM"), COLOR_GREY, ChatDestination::System, _("Game was resumed."));
}

void dskGameInterface::CI_Error(const ClientError ce)
{
    messenger.AddMessage("", 0, ChatDestination::System, ClientErrorToStr(ce), COLOR_RED);
    GAMECLIENT.SetPause(true);
}

/**
 *  Status: Verbindung verloren.
 */
void dskGameInterface::LC_Status_ConnectionLost()
{
    messenger.AddMessage("", 0, ChatDestination::System, _("Lost connection to lobby!"), COLOR_RED);
}

/**
 *  (Lobby-)Status: Benutzerdefinierter Fehler
 */
void dskGameInterface::LC_Status_Error(const std::string& error)
{
    messenger.AddMessage("", 0, ChatDestination::System, error, COLOR_RED);
}

void dskGameInterface::CI_PlayersSwapped(const unsigned player1, const unsigned player2)
{
    // Meldung anzeigen
    std::string text = "Player '" + worldViewer.GetWorld().GetPlayer(player1).name + "' switched to player '"
                       + worldViewer.GetWorld().GetPlayer(player2).name + "'";
    messenger.AddMessage("", 0, ChatDestination::System, text, COLOR_YELLOW);

    // Sichtbarkeiten und Minimap neu berechnen, wenn wir einer von den beiden Spielern sind.
    // Jede Ansicht prueft ihre EIGENE Id; die visuellen Einstellungen und InitPlayer (Postfach,
    // Buttonleiste) haengen am Hauptspieler und laufen deshalb nur dort.
    forEachView([&](PlayerView& view) {
        const unsigned viewPlayerId = view.GetPlayerId();
        if(player1 != viewPlayerId && player2 != viewPlayerId)
            return;
        view.GetViewer().ChangePlayer(player1 == viewPlayerId ? player2 : player1);
        view.GetMinimap().UpdateAll();
        if(&view == &primary())
        {
            // Set visual settings back to the actual ones
            GAMECLIENT.ResetVisualSettings();
            InitPlayer();
        } else
            view.MoveToOwnHQ();
    });
}

/**
 *  Wenn ein Spieler verloren hat
 */
void dskGameInterface::GI_PlayerDefeated(const unsigned playerId)
{
    const std::string text =
      helpers::format(_("Player '%s' was defeated!"), worldViewer.GetWorld().GetPlayer(playerId).name);
    messenger.AddMessage("", 0, ChatDestination::System, text, COLOR_ORANGE);

    // Das Argument benennt den BESIEGTEN Spieler, nicht den empfangenden. Die Meldung oben gibt
    // es genau einmal, die Neuberechnung nur bei der Ansicht, die es selbst betrifft.
    forEachView([&](PlayerView& view) {
        if(playerId == view.GetPlayerId())
            view.RecalcAllColors();
    });
}

void dskGameInterface::GI_UpdateMinimap(const MapPoint pt)
{
    // Kein Spielerargument (Aufrufer: noBaseBuilding.cpp:90, nofForester.cpp:105,
    // nofStonemason.cpp:57, noTree.cpp:217) -> jede Minimap bekommt es.
    forEachView([&](PlayerView& view) { view.GetMinimap().UpdateNode(pt); });
}

void dskGameInterface::GI_UpdateMapVisibility()
{
    forEachView([](PlayerView& view) { view.RecalcAllColors(); });
}

/**
 *  Bündnisvertrag wurde abgeschlossen oder abgebrochen --> Minimap updaten
 */
void dskGameInterface::GI_TreatyOfAllianceChanged(unsigned playerId)
{
    // Nur wenn Team-Sicht aktiviert ist, koennen sich die Sichtbarkeiten auch aendern
    if(!worldViewer.GetWorld().GetGGS().teamView)
        return;
    forEachView([&](PlayerView& view) {
        if(playerId == view.GetPlayerId())
            view.RecalcAllColors();
    });
}

/**
 *  Baut Weg zurück von Ende bis zu start_id
 */
void dskGameInterface::DemolishRoad(PlayerView& view, const unsigned start_id)
{
    RTTR_Assert(start_id > 0);
    // SCHUTZ: die Schleife laeuft rueckwaerts mit unsigned. start_id == 0 liesse sie bis zum
    // Unterlauf laufen und griffe mit route[i - 1] ueber den Anfang des Vektors hinaus. Im
    // Releasebau faellt der Assert darueber weg; deshalb steht hier zusaetzlich ein echter
    // Ausstieg statt nur einer Behauptung.
    if(start_id == 0)
        return;
    RoadBuildState& rb = view.GetRoad();
    GameWorldViewer& viewer = view.GetViewer();
    for(unsigned i = rb.route.size(); i >= start_id; --i)
    {
        MapPoint t = rb.point;
        rb.point = viewer.GetWorld().GetNeighbour(rb.point, rb.route[i - 1] + 3u);
        viewer.SetVisiblePointRoad(rb.point, rb.route[i - 1], PointRoad::None);
        viewer.RecalcBQForRoad(t);
    }

    rb.route.resize(start_id - 1);
}

void dskGameInterface::DemolishRoad(const unsigned start_id)
{
    DemolishRoad(primary(), start_id);
}

/**
 *  Updatet das Post-Icon mit der Nachrichtenanzahl und der Taube
 */
void dskGameInterface::UpdatePostIcon(const unsigned postmessages_count, bool showPigeon)
{
    // Taube setzen oder nicht (Post)
    if(postmessages_count == 0 || !showPigeon)
        GetCtrl<ctrlImageButton>(3)->SetImage(LOADER.GetImageN("io", 62));
    else
        GetCtrl<ctrlImageButton>(3)->SetImage(LOADER.GetImageN("io", 59));

    // und Anzahl der Postnachrichten aktualisieren
    if(postmessages_count > 0)
    {
        GetCtrl<ctrlText>(ID_txtNumMsg)->SetText(std::to_string(postmessages_count));
    } else
        GetCtrl<ctrlText>(ID_txtNumMsg)->SetText("");
}

/**
 *  Neue Post-Nachricht eingetroffen
 */
void dskGameInterface::NewPostMessage(const PostMsg& msg, const unsigned msgCt)
{
    UpdatePostIcon(msgCt, true);
    SoundEffect soundEffect = msg.GetSoundEffect();
    switch(soundEffect)
    {
        case SoundEffect::Pidgeon: LOADER.GetSoundN("sound", 114)->Play(100, false); break;
        case SoundEffect::Fanfare: LOADER.GetSoundN("sound", 110)->Play(100, false);
    }
}

/**
 *  Es wurde eine Postnachricht vom Spieler gelöscht
 */
void dskGameInterface::PostMessageDeleted(const unsigned msgCt)
{
    UpdatePostIcon(msgCt, false);
}

/**
 *  Ein Spieler hat das Spiel gewonnen.
 */
void dskGameInterface::GI_Winner(const unsigned playerId)
{
    const std::string name = worldViewer.GetWorld().GetPlayer(playerId).name;
    const std::string text = (boost::format(_("Player '%s' is the winner!")) % name).str();
    messenger.AddMessage("", 0, ChatDestination::System, text, COLOR_ORANGE);
    WINDOWMANAGER.Show(std::make_unique<iwVictory>(std::vector<std::string>(1, name)));
}

/**
 *  Ein Team hat das Spiel gewonnen.
 */
void dskGameInterface::GI_TeamWinner(const unsigned playerMask)
{
    std::vector<std::string> winners;
    const GameWorldBase& world = worldViewer.GetWorld();
    for(unsigned i = 0; i < world.GetNumPlayers(); i++)
    {
        if(playerMask & (1 << i))
            winners.push_back(world.GetPlayer(i).name);
    }
    const std::string text =
      (boost::format(_("%1% are the winners!")) % helpers::join(winners, ", ", _(" and "))).str();
    messenger.AddMessage("", 0, ChatDestination::System, text, COLOR_ORANGE);
    WINDOWMANAGER.Show(std::make_unique<iwVictory>(winners));
}
