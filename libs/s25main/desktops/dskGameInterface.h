// Copyright (C) 2005 - 2025 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "CheatCommandTracker.h"
#include "Cheats.h"
#include "Desktop.h"
#include "GameInterface.h"
#include "IngameMinimap.h"
#include "Messenger.h"
#include "customborderbuilder.h"
#include "desktops/PlayerView.h"
#include "ingameWindows/iwAction.h"
#include "ingameWindows/iwChat.h"
#include "input/IPadTarget.h"
#include "input/PadRing.h"
#include "input/PadRouter.h"
#include "network/ClientInterface.h"
#include "WindowManager.h"
#include "notifications/Subscription.h"
#include "world/GameWorldView.h"
#include "world/GameWorldViewer.h"
#include "world/ViewportLayout.h"
#include "gameTypes/MapCoordinates.h"
#include "gameTypes/RoadBuildState.h"
#include "liblobby/LobbyInterface.h"
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class IngameWindow;
class ITexture;
class glArchivItem_Bitmap;
class GlobalGameSettings;
struct MouseCoords;
class PostBox;
class PostMsg;
struct BuildingNote;
struct KeyEvent;
class NWFInfo;
class GameWorldBase;
class GameCommandFactory;
class nobBaseWarehouse;

class dskGameInterface :
    public Desktop,
    public ClientInterface,
    public GameInterface,
    public LobbyInterface,
    public IChatCmdListener,
    public IPadTarget,
    public IWindowOwnerObserver
{
public:
    dskGameInterface(std::shared_ptr<Game> game, std::shared_ptr<const NWFInfo> nwfInfo, unsigned playerIdx,
                     bool initOGL = true);
    ~dskGameInterface() override;

    void Resize(const Extent& newSize) override;
    void SetActive(bool activate = true) override;

    void LC_Status_ConnectionLost() override;
    void LC_Status_Error(const std::string& error) override;

    /// Strassenbauzustand der HAUPTansicht. Bewusst nur diese: das ist der Getter des
    /// Mauspfads (und seiner Nachweise). Je Ansicht steht er in PlayerView::GetRoad().
    RoadBuildMode GetRoadMode() const { return road.mode; }

    void CI_PlayerLeft(unsigned playerId) override;
    void CI_GGSChanged(const GlobalGameSettings& ggs) override;
    void CI_Chat(unsigned playerId, ChatDestination cd, const std::string& msg) override;
    void CI_Async(const std::string& checksums_list) override;
    void CI_ReplayAsync(const std::string& msg) override;
    void CI_ReplayEndReached(const std::string& msg) override;
    void CI_GamePaused() override;
    void CI_GameResumed() override;
    void CI_Error(ClientError ce) override;
    void CI_PlayersSwapped(unsigned player1, unsigned player2) override;

    void NewPostMessage(const PostMsg& msg, unsigned msgCt);
    void PostMessageDeleted(unsigned msgCt);

    /// Wird aufgerufen, wann immer eine Flagge zerstört wurde, da so evtl der Wegbau abgebrochen werden muss
    void GI_FlagDestroyed(MapPoint pt) override;
    /// Wenn ein Spieler verloren hat
    void GI_PlayerDefeated(unsigned playerId) override;
    /// Es wurde etwas Minimap entscheidendes geändert --> Minimap updaten
    void GI_UpdateMinimap(MapPoint pt) override;
    /// Update minimap and colors for whole map
    void GI_UpdateMapVisibility() override;

    /// Bündnisvertrag wurde abgeschlossen oder abgebrochen --> Minimap updaten
    void GI_TreatyOfAllianceChanged(unsigned playerId) override;
    void GI_Winner(unsigned playerId) override;
    void GI_TeamWinner(unsigned playerMask) override;
    void GI_StartRoadBuilding(MapPoint startPt, bool waterRoad) override;
    void GI_CancelRoadBuilding() override;
    /// Baut die gewünschte bis jetzt noch visuelle Straße (schickt Anfrage an Server)
    void GI_BuildRoad() override;

    Cheats& GI_GetCheats() override { return cheats_; }

    /// --- Strassenbau, auf GENAU EINE Ansicht bezogen ----------------------------------------
    ///
    /// Der Zustand (RoadBuildState), der Viewer, auf dem die visuelle Vorschau liegt, und die
    /// Kommandofabrik, die den fertigen Weg abschickt, gehoeren alle drei DERSELBEN Ansicht.
    /// Frueher las jede dieser Funktionen die Uebergangsreferenzen `road`/`worldViewer`, die
    /// auf primary() zeigen - ein Padspieler in Ansicht 1 haette damit die Strasse des
    /// Hauptspielers verlaengert und sie anschliessend in seinem eigenen Namen abgeschickt.
    ///
    /// Die parameterlosen GI_*-Fassungen darunter bleiben genau das, was sie waren: die
    /// MAUSfassungen. Sie rufen dieselben Funktionen mit primary() und aendern damit fuer den
    /// Einzelspieler kein Bit.
    void StartRoadBuilding(PlayerView& view, MapPoint startPt, bool waterRoad);
    void CancelRoadBuilding(PlayerView& view);
    /// Schickt den bis jetzt nur visuellen Weg ueber die Kommandofabrik DIESER Ansicht ab.
    /// false, wenn nichts abzuschicken war (siehe die Vorbedingungen in der Umsetzung).
    bool CommitRoad(PlayerView& view);

    // Sucht einen Weg von road_point_x/y zu cselx/y und baut ihn ( nur visuell )
    /// Was BuildRoadPart getan hat.
    ///
    /// BEFUND 4: frueher ein blosses bool. Am Laengenanschlag eines Wasserwegs meldete die
    /// Funktion einmal true (Weg voellig weggekuerzt, cSel auf das unveraenderte Wegende
    /// gesetzt) und nach dem letzten Umbau false - und ein false liest der Mauspfad als "der
    /// Zielpunkt taugt nicht" und oeffnet daraufhin das Strassenfenster, das im Konstruktor die
    /// Maus warpt. Aus "Klick am Anschlag tut nichts" wurde damit "Klick am Anschlag oeffnet ein
    /// Fenster und der Zeiger springt".
    ///
    /// Beides ist derselbe Denkfehler: ein einziges bool muss zwei verschiedene Dinge tragen.
    /// Der Padpfad BRAUCHT die Unterscheidung (nur so kann er dem Spieler sagen, warum nichts
    /// passiert ist), der Mauspfad braucht sie ebenso (nur so bleibt er bei seinem alten
    /// Verhalten). Also wird sie ausgesprochen.
    enum class RoadPartResult
    {
        /// Mindestens eine Kante ist dazugekommen. cSel steht auf dem NEUEN Wegende - beim
        /// Wasserweg kann das ein anderer Punkt sein als der angeklickte.
        Built,
        /// Der Zielpunkt taugt nicht: ungueltig, gleich dem Wegende, oder es fuehrt kein
        /// baubarer Weg dorthin. cSel bleibt unveraendert.
        Rejected,
        /// NUR Wasserweg: die Strecke ist am Laengenanschlag, es passt kein Stueck mehr hinein.
        /// cSel bleibt unveraendert. Ausdruecklich KEIN Rejected: der Zielpunkt waere in
        /// Ordnung, nur die Regel laesst ihn nicht mehr zu.
        AtLengthLimit
    };

    // Bei Wasserwegen kann die Reichweite nicht bis zum gewünschten
    // Punkt reichen. Dann werden die Zielkoordinaten geändert, daher
    // call-by-reference
    RoadPartResult BuildRoadPart(PlayerView& view, MapPoint& cSel);
    /// Die MAUSfassung (wirkt auf primary()). Bewusst weiter ein bool: sie ist die Signatur, an
    /// der der Bestandsnachweis testBuilding.cpp haengt, und "hat es gebaut" ist genau das, was
    /// er wissen will.
    bool BuildRoadPart(MapPoint& cSel);
    // Return the id (index + 1) of the point in the currently build road (1 = startPt)
    // If pt is not on the road, return 0
    unsigned GetIdInCurBuildRoad(const PlayerView& view, MapPoint pt) const;
    unsigned GetIdInCurBuildRoad(MapPoint pt);
    /// Die Ansicht, deren Strassenfenster gerade offen ist - sonst die Hauptansicht.
    /// Siehe die Begruendung an der Umsetzung.
    PlayerView& RoadWindowOwner();
    /// Die Ansicht, deren AKTIONSfenster gerade offen ist - sonst die Hauptansicht.
    /// Das Gegenstueck zu RoadWindowOwner() fuer GI_StartRoadBuilding; siehe die Begruendung
    /// an der Umsetzung.
    PlayerView& ActionWindowOwner();
    /// Baut Weg zurück von Ende bis zu start_id
    void DemolishRoad(PlayerView& view, unsigned start_id);
    void DemolishRoad(unsigned start_id);
    // Zeigt das Straäcnfenster an und entscheidet selbststäcdig, ob man eine Flagge an road_point_x/y bauen kann,
    // ansonsten gibt's nur nen Button zum Abbrechen
    void ShowRoadWindow(PlayerView& view, const Position& mousePos);
    void ShowRoadWindow(const Position& mousePos);
    /// Zeigt das Actionwindow an, bei Flaggen werden z.B. noch berücksichtigt, obs ne besondere Flagge ist usw
    void ShowActionWindow(PlayerView& view, const iwAction::Tabs& action_tabs, MapPoint cSel,
                          const DrawPoint& mousePos, bool enable_military_buildings,
                          iwAction::MousePointer mousePointer = iwAction::MousePointer::Warp);
    void ShowActionWindow(const iwAction::Tabs& action_tabs, MapPoint cSel, const DrawPoint& mousePos,
                          bool enable_military_buildings);

    /// Was auf einem Knoten aus Sicht EINER Ansicht ueberhaupt moeglich ist.
    ///
    /// Das ist die Entscheidung, die frueher mitten in ContextClick stand und damit nur dem
    /// Mauspfad gehoerte. Sie ist jetzt herausgezogen, weil der Padpfad sie MITBENUTZEN muss:
    /// ein zweites Regelwerk, das dieselbe Frage beantwortet ("was darf dieser Spieler hier"),
    /// laeuft garantiert vom ersten Zusatz an auseinander.
    struct ActionOptions
    {
        iwAction::Tabs tabs;
        bool enableMilitaryBuildings = false;
        /// Verbuendetes Lagerhaus unter dem Zeiger: dort gibt es statt eines Aktionsfensters
        /// das HANDELSfenster.
        ///
        /// Frueher hiess dieses Feld `handled` und ComputeActionOptions zeigte das Fenster
        /// selbst. Das ging, solange die Funktion ausschliesslich aus einem KLICK heraus lief.
        /// Seit der Klartext je Frame und Ansicht ausgerechnet wird (RefreshBrief), laeuft sie
        /// einmal pro Bild - und eine Funktion, die dabei ein Fenster oeffnet, oeffnet es
        /// sechzig Mal in der Sekunde. Das Zeigen gehoert deshalb zum Aufrufer, der als
        /// einziger weiss, dass gerade wirklich gedrueckt wurde; die Rechnung selbst ist jetzt
        /// nebenwirkungsfrei und darf beliebig oft laufen.
        const nobBaseWarehouse* tradeWarehouse = nullptr;
        /// Bietet dieses Fenster ueberhaupt eine HANDLUNG an - also mehr als den Reiter
        /// "Anzeigeoptionen", den ContextClick unbedingt setzt?
        ///
        /// Der Mauspfad braucht die Frage nicht: sein Klick oeffnet immer ein Fenster, und der
        /// Spieler sieht sofort, dass darin nichts steht. Der Padspieler dagegen braucht eine
        /// Antwort auf seinen Knopfdruck (PadReject), sonst sieht ein Druck, der nichts tut,
        /// aus wie ein totes Pad.
        bool hasAction() const;
    };
    /// Berechnet obiges fuer DIESE Ansicht. NEBENWIRKUNGSFREI - siehe tradeWarehouse.
    ActionOptions ComputeActionOptions(PlayerView& view, MapPoint cSel);

    /// Was dieser Knoten fuer DIESE Ansicht hergibt, als benannter Grund statt als bool.
    ///
    /// Alles, was das Aktionsfenster anbieten koennte, ist aus ComputeActionOptions abgeleitet
    /// und NICHT unabhaengig gerechnet: sonst koennte der Klartext etwas anderes behaupten, als
    /// das Fenster gleich anbietet. Die vier Faelle, in denen es NICHTS anzubieten gibt
    /// (Nebel, Niemandsland, fremdes Gebiet, eigenes Gebaeude), rechnet diese Funktion selbst -
    /// ComputeActionOptions unterscheidet sie nicht. Naeheres am Rumpf und an brief::NodeVerdict.
    brief::NodeVerdict JudgeNode(PlayerView& view, MapPoint pt);

    /// Wuerde A unter dem Zeiger ein Objektfenster oeffnen?
    ///
    /// WOERTLICH die drei Bedingungen, an denen OpenObjectWindow der Reihe nach entscheidet
    /// (Schiff, eigenes Gebaeude, eigene Baustelle) - nur ohne das Fenster zu zeigen. Gebraucht
    /// wird die Frage von der Tastenhinweisleiste, die vor dem Druck sagen muss, was A tut.
    ///
    /// Dass es zwei Rechnungen sind, ist ein benanntes Risiko und kein Versehen:
    /// OpenObjectWindow ist ein Kommando und laesst sich nicht folgenlos fragen. Es gibt deshalb
    /// einen Nachweis, der beide gegeneinander haelt, indem er A wirklich drueckt
    /// (testPadKeyHints.cpp, TheOpenHintAppearsExactlyWhereAReallyOpensAWindow).
    bool CanOpenObjectWindow(PlayerView& view, MapPoint pt) const;
    /// Wuerde Back jetzt etwas tun? Der Knopf schaltet um; er ist nur dann wirkungslos, wenn
    /// noch kein Menue offen ist UND ein eigenes modales Fenster davorliegt (PadOpenSystemMenu).
    bool CanOpenSystemMenu(PlayerView& view) const;
    /// WUERDE EnterWindow den Fokus wirklich in dieses Fenster setzen? Reine Frage.
    /// EINE Bedingung, zwei Aufrufer: der Knopf (EnterWindow) und die Leiste (RefreshBrief).
    /// Siehe Befund N5 am Rumpf.
    static bool CanEnterWindow(IngameWindow* wnd);

    const GameWorldView& GetView() const { return gwv; }

    void OnChatCommand(const std::string& cmd) override;

    /// Holt die Pad-Ereignisse beim Treiber ab, rechnet sie in Zeigerbewegungen und Aktionen um
    /// und entscheidet danach je Ansicht, WOHER ihr Zeiger kommt.
    ///
    /// Das ist die einzige Stelle im Programm, an der der Zeigerbesitz entschieden wird. Run()
    /// ruft sie als erstes und liest danach nur noch GameWorldView::GetCursorPos() - die Regel
    /// wird also nirgends ein zweites Mal formuliert.
    ///
    /// Oeffentlich und ohne einen einzigen OpenGL-Aufruf, damit sie ohne Run() pruefbar ist:
    /// Run() selbst ist im Testprozess nicht aufrufbar, weil GameWorldView::Draw ueber
    /// TerrainRenderer::Draw laeuft, das ohne geladene S2-Texturen nicht arbeiten kann.
    ///
    /// elapsedMs kommt vom Aufrufer und nicht aus VIDEODRIVER: nur so ist der zurueckgelegte
    /// Weg eines Padzeigers im Test reproduzierbar.
    void UpdateInput(unsigned elapsedMs, const Position& mousePos);

    /// Zuordnung Geraet -> Ansicht. Die Stelle, an der sie spaeter aus Lobby oder Optionen
    /// festgelegt wird (PadRouter::AssignSlot).
    PadRouter& GetPadRouter() { return padRouter_; }
    const PadRouter& GetPadRouter() const { return padRouter_; }

    /// Die Ansicht, die GERADE den Mauszeiger haelt - oder nullptr, wenn keine ihn hat.
    ///
    /// BEFUND 2: der Mauspfad las bisher durchgehend die HAUPTansicht. Hatte deren Spieler ein
    /// Pad in der Hand, gehoerte ihr Zeiger dem Pad (UpdateInput, Regel a), und ein Mausklick
    /// wirkte auf den PADpunkt - er baute Strasse dort, wo die Maus nicht war.
    ///
    /// Warum diese Loesung und nicht "letztes benutztes Geraet gewinnt": ein Padspieler am
    /// Fernseher darf seinen Zeiger nicht dadurch verlieren, dass jemand am Tisch die Maus
    /// anstoesst - mitten im Strassenbau waere das ein verlorener Zug, und der Zeiger spraenge
    /// sichtbar weg. Die Zuordnung "diese Ansicht gehoert diesem Geraet" bleibt deshalb stehen,
    /// wie sie ist; was sich aendert, ist die Frage, WELCHE Ansicht ein Mausklick trifft.
    ///
    /// Im Mehransichtsfall ist das ein Gewinn und keine Einschraenkung: der Klick trifft die
    /// Ansicht, ueber der die Maus steht, statt immer die erste. Im Einzelspieler ohne Pad ist
    /// es dieselbe Ansicht wie vorher - primary() ist die einzige und immer padlos.
    /// Halten alle Ansichten ein Pad, gibt es keinen Mauszeiger auf der Karte, und ein Klick
    /// auf die Karte ist wirkungslos. Das ist die richtige Antwort und nicht ein Verlust: es
    /// gibt dann keinen Punkt, den der Spieler mit der Maus gemeint haben koennte.
    ///
    /// BEFUND B: dasselbe gilt, sobald die Maus MITTEN IN einer Ansicht steht, die ein Pad
    /// haelt. Frueher fiel sie dann an den padlosen NACHBARN, und der Klick wirkte auf einen
    /// Knoten in dessen Bild - obwohl die Maus sichtbar woanders stand. Es gibt in diesem Fall
    /// genau eine Ansicht, die der Spieler gemeint haben kann, und die gehoert einem anderen
    /// Eingabegeraet; also gehoert der Klick keiner. Nur wenn die Maus ueber GAR KEINER Ansicht
    /// steht - also ausserhalb der Renderflaeche, denn die Viewports decken sie luecken- und
    /// ueberlappungsfrei ab - faellt sie weiter an die erste padlose Ansicht zurueck. Daran
    /// haengt der Einzelspieler, dessen Ansicht den Zeiger auch bei einer Mausposition
    /// ausserhalb des Fensters behalten muss. Siehe UpdateInput, Abschnitt 2.
    PlayerView* GetMouseView() { return mouseView_; }
    const PlayerView* GetMouseView() const { return mouseView_; }

    /// DIE Regel selbst, als Funktion: welche Ansicht darf eine Mauseingabe an `viewPos`
    /// bekommen?
    ///
    /// Genau EINE Stelle, an der die Regel steht - UpdateInput setzt mouseView_ damit, und die
    /// Eingaenge, die eine eigene Position mitbringen (Rad, Kartenzug), fragen sie damit
    /// unmittelbar. Vorher stand die Regel nur inline in UpdateInput; Rad und Kartenzug hatten
    /// gar keine und nahmen primary().
    ///
    ///  a) Ueber einer Ansicht: nur DIESE kommt in Frage, und nur wenn sie padlos ist. Hat sie
    ///     ein Pad, gehoert ihr Bild dem Padspieler - dann bekommt die Eingabe KEINE Ansicht.
    ///  b) Ueber gar keiner Ansicht, aber INNERHALB der Renderflaeche: nullptr. Das ist eine
    ///     Luecke im Layout; der Spieler zeigt sichtbar auf nichts, und eine Ersatzansicht waere
    ///     genau der Fehler, den (a) verhindert. Mit dem heutigen Layout kann dieser Fall nicht
    ///     eintreten (CalcViewports deckt lueckenlos ab) - aber die Regel verlaesst sich nicht
    ///     mehr darauf, denn genau diese Annahme war bei drei Ansichten falsch.
    ///  c) AUSSERHALB der Renderflaeche: Rueckfall auf die erste padlose Ansicht. Dort gibt es
    ///     keine fremde Ansicht, deren Punkt faelschlich getroffen werden koennte, und der
    ///     Einzelspieler verlangt ausdruecklich, dass seine eine Ansicht den Zeiger auch bei
    ///     einer Mausposition weit ausserhalb des Fensters behaelt (Nachweis
    ///     SingleViewFollowsTheMouseWhetherOrNotAPadIsPlugged).
    ///
    /// (b) und (c) waren frueher EIN Fall ("keine Ansicht enthaelt den Punkt"). Die Trennung ist
    /// der Kern der Korrektur: (c) ist eine Aussage ueber den BILDSCHIRM (die Maus ist gar nicht
    /// auf ihm), (b) eine ueber das LAYOUT (sie ist auf ihm, aber auf keiner Ansicht). Nur (c)
    /// darf ersetzen.
    PlayerView* ViewUnderMouse(const Position& viewPos);

    /// Dieselbe Frage fuer Eingaben, die AUSSCHLIESSLICH den Bildausschnitt bewegen: Mausrad
    /// und Kartenzug.
    ///
    /// Der Unterschied zu ViewUnderMouse ist genau die Padpruefung, und er ist begruendet:
    ///  - ViewUnderMouse entscheidet, wer den ZEIGER bekommt. Ein Zeiger waehlt einen Knoten
    ///    aus, und auf diesem Knoten handelt der Spieler (Fenster, Fahne, Strasse, Kommando).
    ///    Haelt die Ansicht ein Pad, zeigt ihr Zeiger schon woanders hin - eine Mauseingabe
    ///    wuerde dort auf einem Punkt handeln, den niemand mit der Maus gemeint hat. Deshalb:
    ///    keine Ansicht.
    ///  - Rad und Kartenzug lesen KEINEN Zeiger und KEINEN selektierten Punkt. Sie verschieben
    ///    und skalieren nur das Bild der Ansicht, ueber der die Maus steht. Ein falscher Punkt
    ///    kann dabei nicht entstehen, es wird nichts geoeffnet und nichts gebucht.
    ///
    /// Waeren sie an dieselbe Padpruefung gebunden, verloere der EINZELNE Spieler mit Pad UND
    /// Maus - der Regelfall am Fernseher - das Mausrad, sobald er das Pad in die Hand nimmt.
    /// Genau dieser Fall ist heute gemessen (Nachweis
    /// APadZoomStopsARunningSmoothZoomInsteadOfInheritingItsSpeed dreht am Rad, waehrend Ansicht
    /// 0 ein Pad haelt) und darf nicht wegfallen.
    ///
    /// Ausserhalb der Renderflaeche faellt es auf primary() zurueck - derselbe Sitzplatz, dem
    /// auch Tastatur (Msg_KeyDown), Knopfleiste (Msg_ButtonClick) und Zeigerbild
    /// (UpdateRoadCursor) gehoeren. Im Einzelspieler ist das immer die eine Ansicht, also
    /// wertgleich zu dem gwv, das frueher hier stand.
    PlayerView* CameraViewUnderMouse(const Position& viewPos);

    /// Liegt dieser Punkt auf der Renderflaeche, wie die ANSICHTEN sie sehen?
    ///
    /// Gemessen am umschliessenden Rechteck aller Viewports und nicht an
    /// VIDEODRIVER.GetRenderSize(): waehrend eines Groessenwechsels sind die beiden fuer einen
    /// Moment verschieden, und massgeblich ist die Flaeche, auf der wirklich Ansichten liegen.
    bool IsInsideRenderArea(const Position& viewPos) const;

    /// Klammert eine Eingabeverarbeitung, die GENAU EINER Ansicht gehoert.
    ///
    /// Sie setzt BEIDES zugleich - den Fensterbesitzer und den handelnden Spieler -, damit die
    /// zwei nicht auseinanderlaufen koennen: neu geoeffnete Fenster gehoeren dieser Ansicht,
    /// und GameCommands werden auf ihren Spieler gebucht. Der handelnde Spieler faellt dabei
    /// aus dem Besitzer ab, ueber OnWindowOwnerChanged - eine einzige Wahrheit, kein zweites
    /// Feld.
    class ViewScope
    {
    public:
        explicit ViewScope(unsigned viewIdx) : owner_(WindowManager::inst(), viewIdx) {}

    private:
        WindowManager::ScopedWindowOwner owner_;
    };

    // IWindowOwnerObserver - uebersetzt die Ansichtsnummer in den handelnden Spieler.
    void OnWindowOwnerChanged(unsigned ownerIdx) override;

    /// Die GameCommandFactory DIESER Ansicht. Jedes Fenster, das Kommandos erzeugen kann,
    /// bekommt sie beim Oeffnen herein - damit entscheidet sich schon dort, fuer wen es sendet,
    /// und nicht erst am Eingabepfad. Oeffentlich und statisch, damit die Nachweise dieselbe
    /// Zuordnung benutzen koennen wie der Produktivcode statt sie nachzubauen.
    static GameCommandFactory& gcFactoryFor(const PlayerView& view);

    // IPadTarget - die Slotnummer IST die Nummer der Ansicht.
    void OnPadAssigned(unsigned slot, bool assigned) override;
    void OnPadMove(unsigned slot, const Position& delta) override;
    void OnPadCamera(unsigned slot, const Position& delta) override;
    void OnPadZoom(unsigned slot, float step) override;
    void OnPadButton(unsigned slot, PadButton button, bool down) override;

    /// Diese Ansicht betritt das oberste Fenster und bedient es ab jetzt mit dem Pad.
    /// false, wenn es kein Fenster gibt oder darin nichts zu bedienen ist.
    bool EnterTopMostWindow(PlayerView& view);
    /// Dasselbe fuer ein BESTIMMTES Fenster. Gebraucht vom Padmenue, das genau das Fenster
    /// betreten muss, das es soeben geoeffnet hat - "das oberste" waere hinter einem modalen
    /// Fenster das falsche (WindowManager::DoShow fuegt vor dem ersten modalen ein).
    bool EnterWindow(PlayerView& view, IngameWindow* wnd);

    /// --- Die vier Handlungen der Knopfleiste, auf GENAU EINE Ansicht bezogen ---------------
    ///
    /// Zwei Aufrufer: der Mausknopf der einen Leiste (Msg_ButtonClick, immer primary()) und das
    /// Padmenue des jeweiligen Sitzplatzes (iwPadSystemMenu). Genau dafuer sind sie benannt und
    /// nicht mehr inline - fuer dieselbe Handlung darf es nicht zwei Regelwerke geben.
    ///
    /// KEINE von ihnen setzt die Besitzklammer; beide Aufrufer haben sie offen.
    /// Die drei fensteroeffnenden liefern das ENTSTANDENE Fenster - oder nullptr, wenn
    /// ToggleWindow ein bereits offenes zugemacht hat. Das Padmenue braucht den Zeiger, um den
    /// Fokus hineinzugeben (PadMenuLeaveTo); der Mauspfad wirft ihn wie bisher weg.
    IngameWindow* OpenMinimapFor(PlayerView& view);
    IngameWindow* OpenMainMenuFor(PlayerView& view);
    /// Bauhilfe (BQ-Symbole) dieser Ansicht an/aus. REINE ANZEIGE - kein GameCommand, kein
    /// Netzverkehr, kein Simulationszustand; die Begruendung steht an der Umsetzung.
    void ToggleConstructionAidFor(PlayerView& view);
    /// Gebaeudenamen und Auslastung dieser Ansicht an/aus. Ebenfalls reine Anzeige.
    /// Bleibt fuer die KNOPFLEISTE des Mausspielers stehen - dort gibt es nur einen Knopf.
    void ToggleNamesAndProductivityFor(PlayerView& view);
    /// Die beiden EINZELN - der Ring hat den Platz, den die Knopfliste nicht hatte (Phase 13).
    /// Ebenfalls reine Anzeige, und es sind dieselben Methoden, die der Mausspieler mit den
    /// Tasten c und s schon immer erreicht.
    void ToggleNamesFor(PlayerView& view);
    void ToggleProductivityFor(PlayerView& view);
    /// "NUR ZUSCHAUEN" an bzw. aus - der Sammelschalter, nach dem der Auftraggeber gefragt hat.
    /// EnterWatchOnly merkt sich die drei Anzeigeschalter und legt sie um; LeaveWatchOnly stellt
    /// GENAU DIESE Werte wieder her. Ohne das Zuruecklegen waere "nur zuschauen" eine Einbahn,
    /// die dem Spieler seine Einstellungen wegnimmt.
    void EnterWatchOnly(PlayerView& view);
    void LeaveWatchOnly(PlayerView& view);
    /// Postfenster DIESER Ansicht, mit IHREM Postfach.
    IngameWindow* OpenPostOfficeFor(PlayerView& view);
    /// Das Padmenue dieser Ansicht schliessen und den Fokus in das gerade geoeffnete Fenster
    /// geben. Gerufen aus iwPadSystemMenu, sobald ein Punkt gewaehlt wurde, der ein Fenster
    /// oeffnet.
    void PadMenuLeaveTo(PlayerView& view, IngameWindow* opened);

    /// Der Back-Knopf: oeffnet (oder schliesst) das Systemmenue DIESER Ansicht und betritt es
    /// sofort mit dem Fokus. Ersatz fuer die Knopfleiste, die ein Padspieler nicht erreicht.
    bool PadOpenSystemMenu(PlayerView& view);
    /// Fokus dieser Ansicht aufloesen und den Rahmen am uebergebenen Wurzelfenster abmelden.
    void ClearFocusRing(PlayerView& view, Window* root);

    // --- DAS KREISMENUE (Phase 13) ----------------------------------------------------------
    //
    // DREI SCHICHTEN, nach dem Vorbild LayoutBrief / EmitBriefLines / DrawBrief - und aus
    // demselben Grund (Befund B4 und N7): eine reine Rechnung, eine Schleife ohne Verzweigung
    // und ein Zeichner ohne Entscheidung. Ein Nachweis kann damit auf drei unabhaengigen Ebenen
    // messen, und nichts kann aus dem Zeichenweg fallen, ohne aus der Rechnung zu verschwinden.
    //
    // DER RING IST KEIN FENSTER. Er ist eine Zeichenschicht im Viewport wie der Klartextkasten.
    // Ein IngameWindow klemmt gegen tv::WindowBoundsRect, also gegen die GANZE Renderflaeche -
    // ein Ring am Viewportrand raegte ins Bild des Nachbarn.

    /// Die drei Sektorfarben. Halbdurchsichtiges Dunkelblau als Grund, der GEWAEHLTE Sektor
    /// deutlich heller und fast deckend, der GESPERRTE stark gedaempft. Aus drei Metern vor
    /// einem 55-Zoll-Fernseher ist der Helligkeitsunterschied das, was traegt - Farbtoene
    /// allein sind es nicht (TV-RECHERCHE.md). Oeffentlich, weil ein Nachweis sie am
    /// Zeichenaufruf wiederfinden muss.
    static constexpr unsigned ringSectorColor = 0xC8102040;
    static constexpr unsigned ringSelectedColor = 0xF0F0D060;
    static constexpr unsigned ringLockedColor = 0x60101820;

    /// Ein Eintrag des Rings, in Sektorreihenfolge.
    struct RingEntry
    {
        Window* ctrl = nullptr;
        /// Sektor dieses Eintrags, in Bildschirmwinkeln.
        padring::Sector sector;
        /// A wuerde hier etwas bewirken (Window::CanActivate). Gesperrte Eintraege bleiben AUF
        /// IHRER POSITION stehen und werden nur abgedunkelt - feste Lage ist die Bedingung
        /// fuer das Muskelgedaechtnis, und der Grund steht im Klartextkasten.
        bool enabled = true;
        /// Dieser Eintrag traegt gerade den Fokus dieses Spielers.
        bool selected = false;

        // --- DIE BESCHRIFTUNG, GERECHNET STATT GERATEN (Befund K1) ---------------------------
        //
        // Bis hierher rechnete DrawRing die Textlage selbst aus und niemand konnte sie messen;
        // der Umsetzer hat an genau dieser einen Stelle GERECHNET statt gemessen, und genau
        // dort war das Ergebnis verkehrt (sechs von sieben Beschriftungen zu breit fuer ihren
        // Sektor, eine breiter als der ganze Ring). Jetzt liegt der KASTEN, in den gezeichnet
        // wird, im Layout - dieselbe Zahl, die der Zeichner benutzt, kann ein Nachweis lesen.
        // Verschmolzen, nicht abgeschrieben: dieselbe Linie wie bei der Tastenhinweisleiste.

        /// Das Bild dieses Eintrags (Window::GetRingIcon), oder nullptr. Dann traegt der
        /// Eintrag Text.
        ITexture* icon = nullptr;
        /// Der umgebrochene Text dieses Eintrags. Leer, wenn er ein Bild traegt oder gar keine
        /// Beschriftung hat.
        std::vector<std::string> labelLines;
        /// GENAU DER KASTEN, in den die Zeilen gezeichnet werden. Bei einem Bild der Kasten des
        /// Bildes. Leeres Rechteck heisst: dieser Eintrag zeichnet nichts.
        Rect labelBox;
    };
    /// ALLES, was DrawRing zeichnet.
    struct RingLayout
    {
        Position center{0, 0};
        float rInner = 0.f;
        float rOuter = 0.f;
        std::vector<RingEntry> entries;
        unsigned page = 0;
        unsigned numPages = 1;
        /// Die freie Flaeche, in der der Ring MIT seinen Beschriftungen liegt: Viewport,
        /// geschnitten mit der Safe Area, oberhalb des Klartextkastens. Nichts darf hier heraus.
        Rect freeArea;
        bool empty() const { return entries.empty(); }
    };

    /// Die Fokusstationen des Wurzelfensters dieser Ansicht OHNE die Reiterkoepfe.
    ///
    /// Die Reiterkoepfe eines ctrlTab sind Kinder DES REITERS (ctrlTab::AddTab legt sie mit den
    /// Kennungen 0..n-1 an); alles andere haengt an einer Gruppe. Genau daran werden sie hier
    /// erkannt. Sie sind keine Ringeintraege, sondern die BLAETTERACHSE - LB und RB.
    static std::vector<FocusPath::Candidate> RingCandidates(const PlayerView& view);
    /// Die Eintraege der aktuellen Seite. Setzt numPages.
    static std::vector<Window*> RingPageCtrls(const PlayerView& view, unsigned& numPages);
    /// Lage, Groesse und Sektoren - rein, ohne einen Zeichenaufruf.
    RingLayout LayoutRing(const PlayerView& view) const;
    /// Setzt icon, labelLines und labelBox EINES Eintrags. Herausgezogen, damit die Textlage
    /// eine eigene, benannte Rechnung ist und nicht im Zeichner steckt (Befund K1).
    static void LayoutRingLabel(const RingLayout& layout, RingEntry& e);
    /// DIE SCHLEIFE, DIE WIRKLICH ZEICHNET. Statisch, ohne Ansicht, ohne Verzweigung.
    static void EmitRing(const RingLayout& layout,
                         const std::function<void(const RingEntry&)>& emitSector,
                         const std::function<void(const RingEntry&)>& emitLabel);
    void DrawRing(const PlayerView& view) const;

    /// Oeffnet den Ring auf diesem Fenster: Fokus hinein, Fenster unsichtbar. false, wenn das
    /// Fenster gar keine Fokusstation hat - dann bleibt alles, wie es war.
    bool OpenRing(PlayerView& view, IngameWindow* wnd);
    /// Ring zu. `closeWindow` schliesst auch das Fenster dahinter - das ist der Normalfall (B).
    void CloseRing(PlayerView& view, bool closeWindow);
    /// true = die Flanke ist verbraucht. Der Ring ist fuer SEINEN Sitzplatz modal: solange er
    /// offen ist, wirkt kein Weltknopf.
    bool RingOnPadButton(PlayerView& view, PadButton button, bool down);
    /// Zielrichtung fortschreiben und den Fokus auf den getroffenen Sektor setzen.
    void RingOnPadMove(PlayerView& view, const Position& delta);
    /// Gibt es im Wurzelfenster ueberhaupt einen Reiter mit mehr als einem Blatt? Woertlich
    /// die zweite Haelfte der Frage, die RingTurnPage beantwortet - und deshalb dieselbe
    /// Funktion und keine Abschrift: die Leiste darf LB/RB nur nennen, wo sie wirken.
    static bool RingHasMultipleTabs(const PlayerView& view);
    /// GIBT ES UEBERHAUPT ETWAS ZU BLAETTERN? Mehr als eine Seite oder mehr als ein Reiter.
    ///
    /// DIE EINE Frage, an der LB und RB haengen - und zwar an BEIDEN Enden: RingTurnPage tut
    /// nichts, wenn sie nein sagt, und die Tastenhinweisleiste nennt die Schultern nicht, wenn
    /// sie nein sagt. Verschmolzen und nicht abgeschrieben (Befund K2/4A); vorher stand die
    /// Bedingung nur in der Leiste, und die Wirkung kannte sie nicht.
    static bool RingHasPages(const PlayerView& view);
    /// Eine Seite weiter (dir > 0) oder zurueck. Am Seitenende wechselt der Reiter.
    void RingTurnPage(PlayerView& view, int dir);
    /// Einen Sektor weiter (Steuerkreuz).
    void RingTurnSector(PlayerView& view, int dir);
    /// Fokus auf den Sektor, in den der Zeiger zeigt. Ohne Zielrichtung bleibt er, wo er ist.
    void RingSyncFocus(PlayerView& view);

    /// Rechnet den Klartext DIESER Ansicht neu. Im Produktivcode die einzige Schreibstelle von
    /// PlayerView::SetBrief, gerufen einmal je Frame und Ansicht am Ende von UpdateInput -
    /// also nachdem der Fokus dieses Frames feststeht.
    void RefreshBrief(PlayerView& view);
    /// Eine Zeile des Klartextkastens, so wie sie gezeichnet wird: Text und Farbe.
    struct BriefLine
    {
        std::string text;
        unsigned color;
    };
    /// ALLES, was DrawBrief zeichnet - und zwar genau das, Zeile fuer Zeile.
    ///
    /// Eine leere Zeilenliste heisst "es wird gar kein Kasten gezeichnet".
    struct BriefLayout
    {
        Rect panel;
        DrawPoint textOrigin;
        unsigned lineHeight = 0;
        /// Titelzeile (gelb), umgebrochener Fliesstext (weiss), Tastenhinweisleiste (grau) -
        /// in Zeichenreihenfolge und ohne Kennzeichen, welche Zeile welche ist. Genau deshalb
        /// gibt es hier keine Kennzeichen: DrawBrief soll nicht entscheiden koennen, eine Sorte
        /// wegzulassen.
        std::vector<BriefLine> lines;
    };
    /// Die Farbe der Tastenhinweisleiste. Oeffentlich, weil ein Nachweis sie braucht, um IHRE
    /// Zeilen im Ergebnis von LayoutBrief zu finden (Befund B4).
    static constexpr unsigned keyLineColor = 0xFFC0C8D0;

    /// Der Umbruch und die Lage des Kastens DIESER Ansicht - rein, ohne einen Zeichenaufruf.
    ///
    /// Herausgezogen aus DrawBrief wegen Befund B4: solange die Tastenzeile ein eigener Zweig im
    /// Zeichner war, konnte sie dort ersatzlos verschwinden, ohne dass ein einziger von 306
    /// Faellen rot wurde. Jetzt entstehen ALLE Zeilen hier, und der Zeichner kennt den
    /// Unterschied zwischen ihnen gar nicht mehr.
    BriefLayout LayoutBrief(const PlayerView& view) const;

    /// ... und die einzige Lesestelle.
    ///
    /// Was ein Nachweis in PlayerView::GetBrief() liest, ist die QUELLE dessen, was hier
    /// gezeichnet wird - nicht Zeichen fuer Zeichen dasselbe. Dazwischen liegt der Umbruch
    /// (glFont::GetWrapInfo auf die Kastenbreite), und bei leerem Block wird gar nichts
    /// gezeichnet. Wer die gezeichneten Zeilen selbst braucht, nimmt LayoutBrief; was wirklich
    /// am Fernseher steht, kann weiterhin keiner der Faelle sehen (der DummyRenderer verwirft
    /// jeden Zeichenaufruf).
    void DrawBrief(const PlayerView& view) const;

    /// DIE SCHLEIFE, DIE WIRKLICH ZEICHNET - herausgezogen wegen Befund N7.
    ///
    /// Der Befund: Pruefer 3 hat zwei Sabotagen gefahren. (A) Tastenzeile aus LayoutBrief
    /// entfernt -> rot, der Waechter aus B4 hat Zaehne. (B) LayoutBrief unangetastet, in
    /// DrawBrief die LETZTE Zeile nicht mehr gezeichnet -> gruen, kein einziger von 316 Faellen
    /// sah es. Die Behauptung "die Zeile kann nicht mehr aus dem Zeichenweg fallen" galt also
    /// nur fuer LayoutBrief; die Schleife selbst war ungedeckt, weil der DummyRenderer jeden
    /// Zeichenaufruf verwirft und ein Nachweis nichts zu messen hatte.
    ///
    /// Jetzt hat er etwas zu messen: er reicht seinen EIGENEN Ausgeber herein und zaehlt, was
    /// hindurchlaeuft - durch dieselbe Schleife, die im Spiel den Zeichenaufruf ausloest.
    /// DrawBrief hat danach keine Zeilenlogik mehr; sein Ausgeber ist ein einziger Aufruf ohne
    /// Verzweigung, und DAS ist der ehrliche Rest, den diese Umgebung nicht messen kann.
    ///
    /// Statisch und ohne Ansicht: die Schleife braucht nur, was LayoutBrief geliefert hat.
    static void EmitBriefLines(const BriefLayout& layout,
                               const std::function<void(const DrawPoint&, const BriefLine&)>& emit);

    /// Dasselbe fuer die AKTUELLE Wurzel dieser Ansicht. Nach dem Aufruf steht dieser Spieler
    /// wieder in der Welt.
    void ReleaseFocus(PlayerView& view);
    /// Darf dieser Spieler noch dort stehen, wo er steht? Loest den Fokus auf, wenn sein
    /// Fenster inzwischen minimiert wurde (Befund B3). Wird je Frame in UpdateInput gerufen.
    void ValidateFocus(PlayerView& view);

protected:
    /// Wirft die Pad-Ereignisse weg, die aufgelaufen sind, BEVOR es diese Partie gab.
    /// Der Geraetebestand (Connected/Disconnected) bleibt erhalten, Achsen und Knoepfe nicht.
    void DiscardStalePadEvents();
    /// Uebernimmt Geraetebestand UND Sitzverteilung aus dem Menue.
    ///
    /// ZWEI Gruende, und der zweite ist der wichtigere:
    ///  1. Seit der Menuenavigation leert der WindowManager die Treiberwarteschlange schon im
    ///     Hauptmenue. Die Connected-Ereignisse, aus denen DiscardStalePadEvents bisher den
    ///     Bestand baute, sind beim Spielstart also laengst verbraucht - ohne diese Uebernahme
    ///     waere jedes Pad die ganze Partie ueber unbekannt.
    ///  2. Die Sitzverteilung des Zuordnungsbildschirms muss den Desktopwechsel ueberleben.
    ///     Ohne sie entschiede in der Partie wieder die Reihenfolge der ersten Benutzung
    ///     (PadRouter.h:20-23), und wer in der Lobby Sitz 3 genommen hat, saesse hier auf
    ///     Sitz 2 - der Zuordnungsbildschirm waere eine Luege.
    void AdoptPadAssignmentFromMenu();

    /// Initializes player specific stuff after start or player swap
    void InitPlayer();

    /// Lässt das Spiel laufen (zeichnen)
    void Run();

    /// Updatet das Post-Icon mit der Nachrichtenanzahl und der Taube
    void UpdatePostIcon(unsigned postmessages_count, bool showPigeon);

    /// Executed during left click. Checks click pos for buildings/roads
    bool ContextClick(const MouseCoords& mc);

    /// Oeffnet das Fenster des Objekts auf `cSel` fuer GENAU DIESE Ansicht: Schiff, eigenes
    /// Gebaeude, eigene Baustelle. false, wenn dort nichts steht, was ein Fenster hat.
    ///
    /// Der gemeinsame Kern von Maus- und Padpfad. Beide Aufrufer haben ihre Besitzklammer
    /// bereits offen; das Fenster gehoert deshalb dem Sitzplatz, aus dessen Sicht der Punkt
    /// ausgewaehlt wurde, arbeitet mit dessen Viewer und dessen Kommandofabrik.
    bool OpenObjectWindow(PlayerView& view, MapPoint cSel);

    /// Der A-Knopf: oeffnet das Fenster unter dem Zeiger DIESER Ansicht. Erzeugt selbst nie ein
    /// GameCommand - siehe die Begruendung an der Knopfbelegung in OnPadButton.
    bool PadOpenWindow(PlayerView& view);

    /// Der A-Knopf, dritte Stufe: das AKTIONSFENSTER auf dem Knoten unter dem Zeiger DIESER
    /// Ansicht - der Weg, auf dem ein Padspieler Gebaeude setzt.
    ///
    /// Erzeugt selbst kein GameCommand; das tut erst ein beschrifteter Knopf IM Fenster, und
    /// zwar ueber die Besitzklammer dieses Sitzplatzes (OnPadButton). Die Invariante "A
    /// schreibt nie etwas fest" bleibt damit woertlich erhalten.
    ///
    /// false, wenn hier nichts anzubieten ist - dann antwortet der Aufrufer mit PadReject.
    ///
    /// Gerufen von ZWEI Knoepfen: von A als dritter Stufe (nur, wenn weder ein Objektfenster
    /// noch der Strassenbau greift) und unbedingt von der rechten Schulter. Genau die zweite
    /// Stelle macht den Flaggenreiter erreichbar - hinter A verschwindet er auf einer eigenen
    /// Flagge dauerhaft, weil der Strassenbau vorher zuschlaegt (BEFUND B).
    bool PadOpenActionWindow(PlayerView& view);

    /// Der B-Knopf ausserhalb des Baumodus: schliesst das oberste Fenster, das DIESER Spieler
    /// bedienen darf - dieselbe Auswahl, die auch Y betritt, und dieselbe Schliessregel wie der
    /// Rechtsklick des Mausspielers (nur CloseBehavior::Regular, nie ein angeheftetes).
    ///
    /// false, wenn es nichts zu schliessen gibt. Der Aufrufer antwortet dann bewusst NICHT -
    /// die Begruendung steht am Knopf in OnPadButton.
    bool PadCloseTopMostWindow(PlayerView& view);

    /// Der X-Knopf: setzt eine Flagge auf dem selektierten Punkt DIESER Ansicht, ueber den
    /// GameCommand-Pfad IHRES Spielers (GameClient::GetGCFactory -> LocalPlayerGCFactory ->
    /// Server -> ExecuteNWF).
    bool PadPlaceFlag(PlayerView& view);

    /// --- Strassenbau am Gamepad -------------------------------------------------------------
    /// Vier Knopfhandler, jeder auf GENAU EINE Ansicht bezogen. Jeder bringt die
    /// Vorbedingungen selbst mit, die im Mauspfad die Bedienoberflaeche sicherstellt
    /// (iwAction zeigt den Strassenknopf nur auf einer eigenen Flagge, ContextClick faengt
    /// "Zeiger steht schon auf dem Wegende" ab). Fehlten sie, koennte ein Padspieler das
    /// Programm im Debugbau ueber RTTR_Assert anhalten - siehe die Begruendungen an den
    /// einzelnen Umsetzungen.
    ///
    /// Alle vier liefern false, wenn sie nichts getan haben; der Aufrufer faellt dann NICHT
    /// auf eine andere Bedeutung desselben Knopfes durch (ausser bei A, wo das ausdruecklich
    /// gewollt ist: erst Fenster, dann Strassenbau).

    /// A ausserhalb des Baumodus: Strassenbau auf der eigenen Flagge unter dem Zeiger starten.
    bool PadStartRoad(PlayerView& view, bool waterRoad);
    /// WAS A IM BAUMODUS JETZT TUT - als PLAN, bevor er ausgefuehrt wird.
    ///
    /// BEFUND P1, gemessen: die Leiste zeigte "A Verlaengern" im Baumoduszweig BEDINGUNGSLOS.
    /// Am Wegende - also im ERSTEN Augenblick jedes Strassenbaus, wenn der Zeiger noch auf der
    /// Startflagge steht - tut A gar nichts: kein Schritt, keine Ablehnungsmeldung, der Zustand
    /// bleibt Zeichen fuer Zeichen derselbe. Das ist der haeufigste Weg eines Anfaengers
    /// ueberhaupt (A auf der eigenen Flagge, dann A), und die Leiste log genau dort.
    ///
    /// BEFUND P1b, in derselben Messung gefunden: zeigt der Spieler auf ein Stueck, das er
    /// selbst schon gelegt hat, BAUT A ZURUECK statt zu verlaengern (DemolishRoad, dieselbe
    /// Entscheidung wie ContextClick). "A Verlaengern" war auch dort falsch.
    ///
    /// DESHALB EIN PLAN UND KEIN ZWEITES `bool`: PadExtendRoad steigt mit genau diesem Aufruf
    /// ein und fuehrt aus, was hier steht - dieselbe Bauform, mit der FocusPath::PeekStep und
    /// FocusPath::Step denselben PlanStep lesen (Befund N8). Eine Bedingung, die neben der
    /// ersten veralten koennte, gibt es damit nicht.
    ///
    /// WAS DER PLAN NICHT VORWEGNIMMT, und das steht hier, statt es zu verschweigen: die drei
    /// Ablehnungen von Extend (RoadOutsideTerritory, RoadNoWay, RoadAtLengthLimit). Zwei davon
    /// kosten einen vollen FindPathForRoad, und RefreshBrief laeuft je Frame und Ansicht. Sie
    /// bleiben deshalb im Rumpf von PadExtendRoad - und sie sind der Grund, warum "Verlaengern"
    /// dort keine Luege ist: jede von ihnen sagt dem Spieler ausdruecklich Bescheid
    /// (PadReject), waehrend die beiden Faelle oben SCHWEIGEN.
    struct RoadStepPlan
    {
        enum class Effect
        {
            /// A tut nichts und sagt auch nichts.
            None,
            /// A haengt ein Stueck bis zum Zeiger an (oder lehnt mit einer Meldung ab).
            Extend,
            /// A baut die eigene Vorschau bis zum Zeiger zurueck.
            ShortenTo
        };
        Effect effect = Effect::None;
        /// Nur bei ShortenTo: der Index in der laufenden Strecke (GetIdInCurBuildRoad).
        unsigned idOnRoad = 0;
    };
    /// Der Plan fuer A im Baumodus. Rein - er aendert nichts.
    RoadStepPlan PlanRoadStep(const PlayerView& view) const;
    /// A im Baumodus: bis zum Zeigerpunkt verlaengern. Erzeugt NIE ein GameCommand.
    bool PadExtendRoad(PlayerView& view);
    /// X im Baumodus: den Weg festschreiben. Genau hier - und nur hier - entsteht das Kommando.
    bool PadCommitRoad(PlayerView& view);
    /// Darf der Strassenbau DIESER Ansicht ueberhaupt auf diesen Knoten zeigen?
    ///
    /// BEFUND A: eigenes Gebiet - oder eine eigene Flagge. Genau die Bedingung, die der
    /// Mauspfad in ContextClick schon hat und die der Padpfad nicht hatte.
    bool IsRoadTargetAllowed(const PlayerView& view, MapPoint pt) const;
    /// Kann an diesem Knoten eine Strasse ENDEN? Woertlich die Endpunktregel von
    /// GameWorld::BuildRoad, gelesen auf dem Viewer dieser Ansicht.
    bool CanRoadEndAt(const PlayerView& view, MapPoint pt) const;
    /// B im Baumodus: ein Wegstueck zurueck; auf leerer Strecke den Bau abbrechen.
    bool PadStepBackRoad(PlayerView& view);

    /// RANDSCHUB: schiebt der Spieler seinen Zeiger ueber den Innenrahmen seines Viewports
    /// hinaus NACH AUSSEN, faehrt die Kamera mit.
    ///
    /// Die Frage war, ob der Zeiger die Kamera schieben soll, wenn er an den Viewportrand
    /// stoesst. Beim Mausspieler gibt es dafuer heute kein Vorbild: Msg_MouseMove scrollt
    /// AUSSCHLIESSLICH, solange isScrolling gesetzt ist, also beim Ziehen mit der rechten Taste
    /// oder Strg+links; ein Mauszeiger, der am Bildschirmrand liegt, bewegt die Karte nicht.
    /// Was der Mausspieler hat, ist etwas anderes: drei Ziehmodi (SETTINGS.interface.
    /// mapScrollMode) und den Beschleunigungsfaktor 2-3 samt smartCursor-Ruecksprung.
    ///
    /// Entschieden wird trotzdem FUER den Randschub - aber in der Form, die CONTROLLER-UX.md
    /// 2.2 vorgibt: nicht "der Zeiger LIEGT am Rand", sondern "der Ausschlag DRUECKT nach
    /// aussen". Der Unterschied ist der ganze Punkt:
    ///  - Der Padzeiger liegt nach jedem zu weit geschobenen Stick am Rand, weil
    ///    PlayerView::ClampToView ihn dort haelt. Waere die blosse LAGE das Merkmal, driftete
    ///    die Kamera bei jedem Uebersteuern des Zielens weiter - im Splitscreen ausgerechnet
    ///    an der Kante zum Bild des Nachbarn.
    ///  - Am Ausschlag kann das nicht passieren: ein losgelassener oder zitternder Stick
    ///    liefert gar kein OnPadMove (PadRouter::Deadzone), also auch keinen Schub.
    ///
    /// Der Schub waechst linear von null am Innenrahmen (60 Prozent der Viewportflaeche) auf
    /// den vollen Zeigerweg am Viewportrand. Der Zeiger selbst erreicht weiterhin jeden Punkt
    /// seines Viewports - er wird nicht am Innenrahmen festgehalten, sonst koennte niemand mehr
    /// auf einen Knoten dicht am Rand zeigen.
    ///
    /// Warum ueberhaupt, wo es doch den rechten Stick gibt: eine Strasse ueber mehrere
    /// Bildschirmbreiten baut man sonst im Wechsel aus Zielen und Schwenken. Mit dem Randschub
    /// reicht der linke Daumen fuer den ganzen Weg; der rechte Stick bleibt das schnelle Mittel
    /// fuer die grosse Strecke (PadRouter::CameraPixelsPerSecond).
    void PushCameraAtEdge(PlayerView& view, const Position& cursor, const Position& delta);

    /// Sagt dem Spieler dieser Ansicht, dass seine Padaktion wirkungslos geblieben ist.
    ///
    /// BEFUND 3. Eine HUD-Schicht gibt es noch nicht; benutzt wird deshalb, was da ist:
    ///  - TON, bei JEDEM Fehlschlag. Er ist der einzige Kanal, der sofort und ohne Blickwechsel
    ///    ankommt - genau das, was ein Spieler drei Meter vom Fernseher entfernt braucht, der
    ///    auf sein Viertelbild schaut und nicht auf die Chatzeile am unteren Rand.
    ///  - CHATZEILE, nur wenn sich die Ursache geaendert hat (PlayerView::NoteRejection).
    ///    Sie gilt fuer den ganzen Bildschirm; bei vier Spielern waere eine Zeile je Druck
    ///    Laerm. Der Spielername steht davor, sonst weiss bei vier Ansichten niemand, wen es
    ///    betrifft.
    ///
    /// KEINE SPERRZEIT auf dem Ton, gepruefte Entscheidung: PadRouter liefert ausschliesslich
    /// FLANKEN (PadRouter::DispatchButtons arbeitet pendingButtons_ ab, es gibt keine
    /// Autowiederholung). Ein gehaltener Knopf loest also gar nichts wiederholt aus; die Rate
    /// des Tons ist durch die Drueckrate des Spielers begrenzt und nicht durch die Bildrate.
    /// Eine Sperrzeit koennte deshalb nur DELIBERATE Druecke verschlucken - und dann sieht ein
    /// zweiter Druck, der wieder nichts tut, fuer den Spieler aus wie ein totes Pad. Genau das
    /// war BEFUND 3 (die Sackgasse), und der Nachweis EveryPressIsAnswered nagelt die Regel
    /// "jeder Druck wird beantwortet" ausdruecklich fest.
    ///
    /// GEMEINSAMER MESSENGER, gepruefte Entscheidung: es gibt genau EINE Chatzeile, gezeichnet
    /// ueber die volle Bildschirmbreite (Run() -> messenger.Draw()), und keine Schicht, in der
    /// eine Meldung je Viewport stehen koennte. "In der Chatzeile aller Ansichten" heisst hier
    /// also nicht "vier Meldungen", sondern "die eine Zeile, die alle vier ohnehin gemeinsam
    /// lesen" - so wie sie schon Beitritt, Niederlage und Chat gemeinsam lesen. Am Fernseher
    /// sitzen die Spieler nebeneinander; eine Meldung mit Name und Farbe des Betroffenen ist
    /// dort zuordenbar und nicht stoerend. Ein eigener Messenger je Ansicht ist erst sinnvoll,
    /// wenn es eine HUD-Schicht je Viewport gibt - dann liest sie denselben Wert
    /// (PlayerView::GetRejection) und braucht keine zweite Wahrheit.
    /// Bewusst NICHT benutzt: das Zeigerbild (es gibt genau EINEN Mauszeiger, ein Padspieler in
    /// Ansicht 1 darf ihn dem Mausspieler nicht umstellen - siehe UpdateRoadCursor) und das
    /// Postfach (eine Nachricht mit Umschlag und Taube fuer "nochmal druecken" waere aus jedem
    /// Verhaeltnis).
    void PadReject(PlayerView& view, PadRejection reason);
    /// Warum ein A auf dem Knoten unter dem Zeiger nichts bewirkt hat - der Grund statt des
    /// Sammelsatzes "Nothing can be done here.".
    PadRejection NothingHereReason(PlayerView& view);

    /// Setzt das globale Zeigerbild nach dem Strassenbauzustand DIESER Ansicht - aber nur,
    /// wenn sie die Hauptansicht ist. Es gibt genau EINEN Mauszeiger; ein Padspieler in
    /// Ansicht 1 darf dem Mausspieler nicht das Zeigerbild umstellen. Im Einzelspieler ist
    /// primary() die einzige Ansicht, dort aendert sich damit nichts.
    void UpdateRoadCursor(const PlayerView& view);

    void Msg_ButtonClick(unsigned ctrl_id) override;
    void Msg_PaintBefore() override;
    void Msg_PaintAfter() override;
    bool Msg_LeftDown(const MouseCoords& mc) override;
    bool Msg_LeftUp(const MouseCoords& mc) override;
    bool Msg_MouseMove(const MouseCoords& mc) override;
    bool Msg_RightDown(const MouseCoords& mc) override;
    bool Msg_RightUp(const MouseCoords& mc) override;
    bool Msg_KeyDown(const KeyEvent& ke) override;

    bool Msg_WheelUp(const MouseCoords& mc) override;
    bool Msg_WheelDown(const MouseCoords& mc) override;
    /// Zoomt die Ansicht unter `mousePos`. Frueher zoomte sie gwv, also IMMER primary(): stand
    /// die Maus ueber Ansicht 1 und der Spieler drehte am Rad, zoomte Ansicht 0.
    void WheelZoom(const Position& mousePos, float step);

    void Msg_WindowClosed(IngameWindow& wnd) override;

    void OnBuildingNote(const BuildingNote& note);

    void StopScrolling();
    void StartScrolling(const Position& mousePos);
    void ToggleFoW();              // Switch Fog of War mode if possible
    void DisableFoW(bool hideFOW); // Set Fog of War mode if possible
    void ShowPersistentWindowsAfterSwitch();

    PostBox& GetPostBox();
    /// Das Postfach DIESER Ansicht - legt es an, falls es noch keins gibt.
    PostBox& GetPostBox(const PlayerView& view);
    /// Der gemeinsame Rumpf der beiden darueber. Bewusst ueber die SPIELERnummer und nicht die
    /// Ansichtsnummer: PostManager fuehrt seine Faecher je Spieler.
    PostBox& GetPostBoxFor(unsigned playerId);

    /// Baut die Liste der lokalen Ansichten: erst der Hauptspieler, dann die zusaetzlichen
    /// lokalen Spieler aus GameClient::GetAdditionalLocalPlayers(). Statisch, weil das Ergebnis
    /// schon in der Initialisierungsliste gebraucht wird.
    static std::vector<std::unique_ptr<PlayerView>> CreateViews(unsigned mainPlayerIdx, GameWorldBase& world);
    /// Verteilt die Ansichten ueber die Renderflaeche (world/ViewportLayout.h).
    void LayoutViews(const Extent& renderSize);

    std::shared_ptr<const Game> game_;
    std::shared_ptr<const NWFInfo> nwfInfo_;

    /// Eine Ansicht je lokalem Spieler, mindestens eine. views_[0] ist der Hauptspieler.
    std::vector<std::unique_ptr<PlayerView>> views_;

public:
    /// Anzahl der dargestellten Ansichten (= lokal gesteuerte Spieler dieses Clients)
    unsigned GetNumViews() const { return static_cast<unsigned>(views_.size()); }
    PlayerView& GetPlayerView(unsigned idx) { return *views_.at(idx); }
    const PlayerView& GetPlayerView(unsigned idx) const { return *views_.at(idx); }

protected:
    PlayerView& primary() { return *views_.front(); }
    const PlayerView& primary() const { return *views_.front(); }
    /// Ruft f fuer jede Ansicht auf.
    template<class T_Func>
    void forEachView(T_Func f)
    {
        for(auto& view : views_)
            f(*view);
    }

    /// PHASE 2, bewusst und uebergangsweise: diese Referenzen zeigen auf primary(). Sie halten
    /// den bestehenden, auf genau einen Spieler geschriebenen Code unveraendert lesbar, waehrend
    /// der Zustand bereits sauber je Spieler in PlayerView liegt. Alles, was durch sie laeuft,
    /// betrifft ausschliesslich den Hauptspieler - Eingaberouting auf die richtige Ansicht ist
    /// Phase 3, Fensterbesitz Phase 4.
    GameWorldViewer& worldViewer;
    GameWorldView& gwv;
    IngameMinimap& minimap;
    RoadBuildState& road;
    iwAction*& actionwindow;
    IngameWindow*& roadwindow;
    unsigned& touchDuration;
    /// Es gibt genau EINE Maus und damit hoechstens EINEN laufenden Kartenzug. Diese beiden
    /// halten seinen Zustand (laeuft er, und wo hat er angefangen) - sie liegen weiter in
    /// primary(), weil dort der Mausspieler sitzt. WELCHE Ansicht der Zug verschiebt, steht
    /// dagegen in scrollView_ und ist seit BEFUND 2 nicht mehr zwangslaeufig primary().
    bool& isScrolling;
    Position& startScrollPt;

    /// EIN BuildingNote-Abo fuer alle lokalen Ansichten - und das ist jetzt auch die richtige
    /// Form: verschwindet ein Gebaeude, muss das Fenster darauf bei JEDEM lokalen Spieler
    /// zugehen, der es offen hat. Genau das tut der Rueckruf ueber WINDOWMANAGER.CloseAll().
    /// Ein Abo je Ansicht wuerde dasselbe mehrfach tun.
    Subscription evBld;

    /// Verteilt Gamepad-Ereignisse auf die Ansichten. Gehoert dem Desktop und nicht einem
    /// Singleton: er lebt genau so lange wie die Ansichten, auf die er verteilt.
    PadRouter padRouter_;
    /// Puffer fuer IVideoDriver::FetchPadEvents. Member, damit er nicht jeden Frame neu
    /// allokiert wird.
    std::vector<PadEvent> padEvents_;
    /// Vergangene Zeit des laufenden Frames, gesetzt in UpdateInput. IPadTarget::OnPadMove
    /// bekommt sie nicht mit; die Fokusnavigation braucht sie fuer ihre Wiederholrate.
    unsigned padStepMs_ = 0;
    /// Zeitstempel des letzten Run(); 0 = noch keiner.
    unsigned lastInputTick_ = 0;
    /// Siehe GetMouseView(). Gesetzt in UpdateInput, sonst nirgends. Zeigt in views_, das nach
    /// dem Konstruktor weder waechst noch schrumpft - der Zeiger kann also nicht haengen.
    PlayerView* mouseView_ = nullptr;

    /// Die Ansicht, deren Karte der LAUFENDE Kartenzug verschiebt - festgehalten beim Druecken
    /// der rechten Taste (bzw. Strg+links), geloescht in StopScrolling.
    ///
    /// Warum gemerkt und nicht bei jeder Mausbewegung neu gefragt: der Zug soll bei der Ansicht
    /// bleiben, in der er ANGEFANGEN hat. Sonst risse ein Zug ueber die Viewportgrenze hinweg
    /// mitten in der Bewegung die Karte des Nachbarn mit - und im Modus ScrollOpposite/-Same
    /// setzt Msg_MouseMove den Mauszeiger ohnehin staendig auf den Startpunkt zurueck
    /// (smartCursor), die aktuelle Position waere dort also gar kein brauchbares Merkmal.
    /// Es gibt genau EINE Maus, also hoechstens einen laufenden Zug.
    PlayerView* scrollView_ = nullptr;

    CustomBorderBuilder cbb;

    std::array<glArchivItem_Bitmap*, 4> borders;

    // Messenger fuer die Nachrichten. Genau einer: die Chatzeile gilt fuer den ganzen Bildschirm.
    Messenger messenger;

    /// Genau eine Instanz. Zwingend: GamePlayer::IsBuildingEnabled liest sie in der Simulation
    /// ueber world.GetGameInterface()->GI_GetCheats() (GamePlayer.cpp:2337).
    Cheats cheats_;
    CheatCommandTracker cheatCommandTracker_;
};
