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
#include <memory>
#include <vector>

class IngameWindow;
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
        /// Statt eines Aktionsfensters wurde bereits ein ANDERES gezeigt (das Handelsfenster
        /// am verbuendeten Lagerhaus). Der Aufrufer ist dann fertig.
        bool handled = false;
        /// Bietet dieses Fenster ueberhaupt eine HANDLUNG an - also mehr als den Reiter
        /// "Anzeigeoptionen", den ContextClick unbedingt setzt?
        ///
        /// Der Mauspfad braucht die Frage nicht: sein Klick oeffnet immer ein Fenster, und der
        /// Spieler sieht sofort, dass darin nichts steht. Der Padspieler dagegen braucht eine
        /// Antwort auf seinen Knopfdruck (PadReject), sonst sieht ein Druck, der nichts tut,
        /// aus wie ein totes Pad.
        bool hasAction() const;
    };
    /// Berechnet obiges fuer DIESE Ansicht. Nicht const: der Handelsfall zeigt ein Fenster.
    ActionOptions ComputeActionOptions(PlayerView& view, MapPoint cSel);

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
    /// Fokus dieser Ansicht aufloesen und den Rahmen am uebergebenen Wurzelfenster abmelden.
    void ClearFocusRing(PlayerView& view, Window* root);
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
    bool PadOpenActionWindow(PlayerView& view);

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
