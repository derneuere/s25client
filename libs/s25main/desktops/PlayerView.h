// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "IngameMinimap.h"
#include "input/FocusPath.h"
#include "input/PadRing.h"
#include "input/PlayerBrief.h"
#include "world/GameWorldView.h"
#include "world/GameWorldViewer.h"
#include "world/ViewportLayout.h"
#include "gameTypes/RoadBuildState.h"
#include <optional>

class GameWorldBase;
class IngameWindow;
class iwAction;

/// Warum die letzte Padaktion einer Ansicht nichts bewirkt hat.
///
/// BEFUND 3: der Padspieler stand in einer Sackgasse. Ein A auf der eigenen Flagge, dann X ->
/// CommitRoad -> route.size() < 2 -> return false, Modus bleibt Normal. Noch einmal X: dasselbe.
/// Beliebig oft, ohne dass irgendetwas es ihm sagt; einziger Ausweg war B.
///
/// Es gibt in dieser Phase noch keine HUD-Schicht. Der Grund fuer den Fehlschlag wird deshalb
/// HIER gefuehrt - an der Ansicht, der er passiert ist - und von dskGameInterface::PadReject in
/// die Kanaele gegeben, die es heute gibt (Ton und Chatzeile). Ein spaeteres HUD liest denselben
/// Wert und braucht dafuer keine zweite Wahrheit.
enum class PadRejection
{
    /// X im Baumodus, aber die Strecke hat weniger als zwei Kanten - GameWorld::BuildRoad
    /// lehnt sie ab, bevor sie entsteht.
    RoadTooShort,
    /// Wasserweg am Laengenanschlag (AddonId::MAX_WATERWAY_LENGTH): es passt kein Stueck mehr.
    RoadAtLengthLimit,
    /// Vom Wegende fuehrt kein baubarer Weg zum Zeiger.
    RoadNoWay,
    /// BEFUND A: der Zeiger steht ausserhalb des EIGENEN Gebiets.
    ///
    /// PathConditionRoad::IsNodeOk verlangt IsPlayerTerritory - aber der Wegfinder ruft es fuer
    /// jeden Knoten AUSSER Start und Ziel. Der ZIELknoten darf also jenseits der eigenen Grenze
    /// liegen, FindPathForRoad findet trotzdem einen Weg dorthin, und die Vorschau entsteht.
    /// GameWorld::BuildRoad merkt es erst am Ende ("kann dort eine Flagge stehen") und verwirft
    /// dann still - nach einem vollen Netzwerkumlauf und ohne jede Rueckmeldung.
    RoadOutsideTerritory,
    /// Am Wegende kann keine Flagge stehen - GameWorld::BuildRoad wuerde die fertige Strasse
    /// deshalb ablehnen (world/GameWorld.cpp:222-241). Der Mausspieler sieht das daran, dass
    /// iwRoadWindow seinen Bauknopf gar nicht erst anbietet; der Padspieler hat kein Fenster,
    /// also muss es ihm der Knopf selbst sagen.
    RoadEndBlocked,
    /// A auf einem Knoten, auf dem dieser Spieler NICHTS tun kann: kein Fenster zu oeffnen,
    /// keine eigene Flagge zum Anfangen, und auch das Aktionsfenster haette nichts als den
    /// Reiter "Anzeigeoptionen" anzubieten.
    ///
    /// Der Mausspieler bekommt in derselben Lage ein leeres Aktionsfenster und SIEHT damit,
    /// dass hier nichts geht. Der Padspieler bekaeme ohne diese Meldung gar nichts - und ein
    /// Knopf, der nichts tut, sieht aus wie ein totes Pad (derselbe Befund, aus dem
    /// NoteRejection ueberhaupt entstanden ist).
    ///
    /// Seit Phase 9 ist das der RESTFALL: eigenes Gebiet, aber weder Platz noch Flagge noch
    /// Strasse. Die drei haeufigeren Ursachen stehen darunter und haben einen eigenen Satz.
    NothingHere,
    /// Der Knoten liegt im Nebel - der Spieler hat ihn nie gesehen.
    ///
    /// BEFUND PHASE 9: alle vier Faelle hier fielen frueher in EINEN Satz zusammen ("Nothing
    /// can be done here."). Der Ausloeser war woertlich "als Anfaenger ist auch nicht klar,
    /// wann Flagge und wann Gebaeude kommt" - und die Antwort des Spiels darauf war ein Satz,
    /// der zu jeder der vier Ursachen gleich gut und gleich schlecht passte. Ein Anfaenger
    /// lernt daraus nichts; er weiss danach nicht, ob er woanders hingehen, erst Gebiet erobern
    /// oder einfach ein Feld weiterruecken muss.
    Unexplored,
    /// Sichtbares Land, das niemandem gehoert. Der Ausweg ist ein Militaergebaeude in Richtung
    /// dieser Stelle - eine voellig andere Handlung als "einen Knoten weiter".
    NoMansLand,
    /// Das Gebiet eines anderen Spielers.
    ForeignTerritory
};

/// Alles, was in einer Splitscreen-Partie je lokalem Spieler GENAU EINMAL existiert:
/// ein eigener Viewer (eigener Fog of War, eigener TerrainRenderer), eine eigene Ansicht auf
/// einem eigenen Stueck Bildschirm, eine eigene Minimap, ein eigener Strassenbauzustand und der
/// Zustand des eigenen Eingabegeraets.
///
/// Bewusst KEINE Window-Ableitung: der Umbau des Fensterbesitzes ist Phase 4. Ein PlayerView
/// gehoert dem Container (dskGameInterface), der seine Methoden aufruft.
///
/// Was hier bewusst NICHT drin ist:
///  - Cheats: es gibt genau eine Instanz im Container. GamePlayer::IsBuildingEnabled liest sie
///    ueber world.GetGameInterface()->GI_GetCheats() IN DER SIMULATION (GamePlayer.cpp:2337) -
///    vier Instanzen waeren ein neuer Divergenzpfad.
///  - Messenger, Rahmen (customborderbuilder), Buttonleiste: einmal ueber die volle Flaeche.
class PlayerView
{
public:
    PlayerView(unsigned viewIdx, unsigned playerId, GameWorldBase& world, const Viewport& viewport);
    ~PlayerView();

    PlayerView(const PlayerView&) = delete;
    PlayerView& operator=(const PlayerView&) = delete;

    GameWorldViewer& GetViewer() { return worldViewer_; }
    const GameWorldViewer& GetViewer() const { return worldViewer_; }
    GameWorldView& GetView() { return view_; }
    const GameWorldView& GetView() const { return view_; }
    IngameMinimap& GetMinimap() { return minimap_; }
    RoadBuildState& GetRoad() { return road_; }
    const RoadBuildState& GetRoad() const { return road_; }
    unsigned GetPlayerId() const { return worldViewer_.GetPlayerId(); }
    /// Nummer DIESER Ansicht (Sitzplatz), 0..MAX_VIEWPORTS-1. Zugleich der Fensterbesitzer
    /// (IngameWindow::GetOwner) und die Slotnummer des Eingabegeraets. Bewusst getrennt von
    /// GetPlayerId(): der Sitzplatz bleibt, der Simulationsslot kann getauscht werden.
    unsigned GetIndex() const { return viewIdx_; }

    /// Legt Lage und Groesse dieser Ansicht fest. Einziger Weg, auf dem das Layout des
    /// Containers bei der Ansicht ankommt.
    void SetViewport(const Viewport& viewport);
    /// Liegt dieser Bildschirmpunkt (View-Koordinaten) in dieser Ansicht?
    bool ContainsViewPos(const Position& viewPos) const;
    /// Mitte dieser Ansicht in View-Koordinaten. Startpunkt eines neu zugeordneten Padzeigers.
    Position GetViewCenter() const;
    /// Klemmt einen Punkt auf diese Ansicht. Ohne diese Klemme wanderte der Zeiger eines Pads
    /// sichtbar in das Bild des Nachbarn.
    Position ClampToView(Position viewPos) const;

    /// Sichtbarkeiten und Minimap dieses Spielers komplett neu berechnen.
    void RecalcAllColors();
    /// Springt auf das HQ dieses Spielers, falls es eins gibt.
    void MoveToOwnHQ();

    /// Fokus DIESES Spielers innerhalb eines Fensters. Vier Ansichten = vier unabhaengige
    /// Fokusse; kein Control weiss davon. Ist keine Wurzel gesetzt, ist der Fokus aus und der
    /// Padzeiger bewegt sich wie in Phase 3 durch die Welt.
    FocusPath& GetFocus() { return focus_; }
    const FocusPath& GetFocus() const { return focus_; }

    /// Das Kreismenue DIESES Sitzplatzes. Vier Ansichten = vier unabhaengige Ringe; ein Ring
    /// weiss von den anderen nichts, genau wie der Fokus. Daran haengt die Zusicherung, dass
    /// Spieler 1 mit seiner Ringauswahl an Spieler 0 nichts aendert.
    padring::Ring& GetRing() { return ring_; }
    const padring::Ring& GetRing() const { return ring_; }

    /// "NUR ZUSCHAUEN" - die zweite woertliche Bitte des Auftraggebers.
    ///
    /// In diesem Zustand ist alles Erklaerende und alles Bedienende dieser Ansicht aus: die
    /// Bauhilfe, die Gebaeudenamen, die Auslastung, der Klartextkasten (bis auf die eine Zeile,
    /// die wieder herausfuehrt) und die Bodenmarkierung des Padzeigers. Was BLEIBT, sind Welt,
    /// Figuren, Grenzsteine - und beide Sticks samt Trigger: "zuschauen" heisst herumschauen,
    /// also muessen Kamera und Zoom weiterlaufen.
    ///
    /// Der Ausgang ist GENAU EINER und er ist sichtbar (B, und der Kasten sagt es). Ein Zustand
    /// ohne sichtbaren Ausgang ist die Falle, die Phase 11 und Phase 12 je einmal gebaut und
    /// wieder ausgebaut haben.
    bool IsWatchOnly() const { return watchOnly_; }
    void SetWatchOnly(bool watchOnly) { watchOnly_ = watchOnly; }
    /// Die drei Anzeigeschalter, wie sie VOR dem Zuschauen standen. Beim Verlassen werden genau
    /// diese Werte wiederhergestellt - "B bringt alles zurueck" ist woertlich das, was der
    /// Kasten dazu verspricht, und es muss stimmen.
    struct WatchOnlySaved
    {
        bool showBQ = false;
        bool showNames = false;
        bool showProductivity = false;
    };
    WatchOnlySaved watchOnlySaved;

    /// Aktuell geoeffnetes Aktionsfenster DIESES Spielers. Besitzer bleibt der WINDOWMANAGER,
    /// hier steht nur der Zeiger. Solange die Fenster-IDs global sind
    /// (gameData/const_gui_ids.h), kann nur eine Ansicht gleichzeitig eins offen haben - das
    /// aufzuloesen ist der Phase-4-Umbau des WindowManagers.
    iwAction* actionwindow = nullptr;
    /// Aktuell geoeffnetes Strassenbaufenster dieses Spielers
    IngameWindow* roadwindow = nullptr;

    /// Haelt fest, dass eine Padaktion dieser Ansicht wirkungslos geblieben ist.
    ///
    /// Liefert true, wenn das eine NEUE Meldung ist (andere Ursache als die zuletzt stehende).
    /// Genau darauf stuetzt sich die Unterdrueckung der Chatzeile bei Dauerdruecken: der Ton
    /// kommt jedes Mal, der Text nur beim Wechsel. Ohne das schriebe ein Spieler, der X
    /// festhaelt, die Chatzeile fuer alle vier voll.
    bool NoteRejection(PadRejection reason)
    {
        ++rejectionCount_;
        const bool isNew = !rejection_ || *rejection_ != reason;
        rejection_ = reason;
        return isNew;
    }
    /// Jede gelungene Aktion loescht die stehende Meldung - danach ist dieselbe Ursache wieder
    /// eine neue Nachricht.
    void ClearRejection() { rejection_.reset(); }
    const std::optional<PadRejection>& GetRejection() const { return rejection_; }
    /// Wie oft in Folge etwas wirkungslos blieb. Nur zum Messen; die Anzeige braucht es nicht.
    unsigned GetRejectionCount() const { return rejectionCount_; }

    /// Der Klartext, den DIESER Spieler gerade unter seiner Ansicht liest.
    ///
    /// Warum hier und nicht im Tooltip des Controls: es gibt genau EINEN Tooltip fuer den
    /// ganzen Bildschirm (WindowManager::curTooltip), er wird an der Mausposition gezeichnet,
    /// und ohne je bewegte Maus gar nicht. Vier lokale Spieler koennten darueber nie
    /// gleichzeitig verschiedene Texte lesen. Die ausfuehrliche Begruendung steht am Kopf von
    /// input/PlayerBrief.h.
    ///
    /// Gesetzt wird der Wert im Produktivcode an genau EINER Stelle
    /// (dskGameInterface::RefreshBrief, einmal je Frame und Ansicht) und gelesen an genau einer
    /// (dskGameInterface::DrawBrief). Damit ist das, was ein Nachweis hier liest, die QUELLE
    /// dessen, was gezeichnet wird, und keine zweite Rechnung daneben. Zeichen fuer Zeichen
    /// dasselbe ist es nicht: DrawBrief bricht die Zeilen erst auf die Kastenbreite um. Und
    /// SetBrief ist oeffentlich - ein Testfall darf einen Block auch von Hand setzen, um den
    /// Zeichenweg mit einem bestimmten Inhalt zu durchlaufen.
    const brief::Brief& GetBrief() const { return brief_; }
    void SetBrief(brief::Brief brief) { brief_ = std::move(brief); }

    /// Zustand des Eingabegeraets DIESER Ansicht.
    unsigned touchDuration = 0;
    bool isScrolling = false;
    Position startScrollPt{0, 0};

    /// Zeiger des Gamepads DIESER Ansicht, in View-Koordinaten (derselbe Raum wie
    /// GameWorldView::cursorPos_ und MouseCoords::pos). std::nullopt = dieser Ansicht ist kein
    /// Pad zugeordnet; dann bekommt sie ihren Zeiger von der Maus oder gar keinen.
    /// Bewusst getrennt von GameWorldView::cursorPos_: das dort ist der EFFEKTIVE Zeiger des
    /// laufenden Frames und wird jeden Frame neu gesetzt; dies hier ist der gehaltene Zustand
    /// des Pads, der Frames ueberdauert.
    bool HasPadCursor() const { return padCursor_.has_value(); }
    Position GetPadCursor() const { return *padCursor_; }
    void SetPadCursor(const std::optional<Position>& viewPos) { padCursor_ = viewPos; }

private:
    const unsigned viewIdx_;
    FocusPath focus_;
    padring::Ring ring_;
    bool watchOnly_ = false;
    std::optional<Position> padCursor_;
    std::optional<PadRejection> rejection_;
    unsigned rejectionCount_ = 0;
    brief::Brief brief_;

    GameWorldViewer worldViewer_;
    GameWorldView view_;
    IngameMinimap minimap_;
    RoadBuildState road_;
};
