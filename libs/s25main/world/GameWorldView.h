// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Cheats.h"
#include "DrawPoint.h"
#include "Rect.h"
#include "gameTypes/MapCoordinates.h"
#include "gameTypes/MapTypes.h"
#include <boost/signals2.hpp>
#include <optional>
#include <vector>

class GameWorldBase;
class GameWorldViewer;
class noBaseBuilding;
class SoundManager;
class TerrainRenderer;
struct RoadBuildState;

class IDrawNodeCallback
{
public:
    virtual ~IDrawNodeCallback() = default;
    /// Called when a node is going to be drawn at displayPt
    /// Can e.g. print coordinates
    virtual void onDraw(const MapPoint& pt, const DrawPoint& displayPt) = 0;
};

struct ObjectBetweenLines;

/// WIEVIEL BAUHILFE DIESE ANSICHT ZEIGT - Welle 14.
///
/// DER BEFUND, woertlich: "Sobald ich ein Gebaeude gebaut habe sieht es immer so aus. Ich wuerde
/// das gerne mit dem Controller toggeln ob ich alles sehe oder nur den Indikator unter meinem
/// Zeiger."
///
/// GEMESSEN auf einer echten Karte (80x48, zwei Ansichten, echter Nebel des Krieges): von 260
/// sichtbaren Knoten tragen 142 ein Symbol - jeder zweite. Bei vier Ansichten auf einem 4K-
/// Fernseher sind es 168 je Viertel. Die Landschaft darunter ist damit nicht mehr zu erkennen,
/// und die zwei Gebaeude, die der Spieler wirklich gebaut hat, gehen in dem Feld unter.
///
/// WARUM DREI STUFEN UND NICHT ZWEI: der Klartextkasten aus Phase 9 beschreibt GENAU EINEN
/// Knoten - den unter dem Zeiger ("Platz fuer ein grosses Gebaeude. Hier passt alles, bis hin zu
/// Bauernhof, Festung oder Katapult."). Die Lektion, um die es Phase 9 ging, ist die Deckung von
/// Satz und Symbol: "'Platz fuer eine kleine Huette' steht dort, wo im Bild die Huette liegt."
/// Genau EIN Symbol deckt diesen Satz. Die 141 weiteren decken ihn zu.
///
/// Deshalb ist `Cursor` das, was Phase 9 erzwingt (ForceShowBQ), und nicht `All` - und genau
/// dieser Zwang war die Ursache dafuer, dass es "immer so aussieht, sobald ich ein Gebaeude
/// gebaut habe".
///
/// KEIN LEISTUNGSARGUMENT: der Unterschied ist gemessen 63 Mikrosekunden je Bild und Ansicht,
/// also 1,5 % eines Kerns bei vier Ansichten und 60 Bildern. Das ist eine LESBARKEITSfrage.
enum class BqMode
{
    /// Gar keine Symbole - freie Sicht auf die Landschaft.
    Off,
    /// NUR der Knoten unter dem Zeiger DIESER Ansicht. Ohne Zeiger: gar keiner.
    Cursor,
    /// Jeder sichtbare Knoten, wie seit jeher.
    All
};

class GameWorldView
{
    /// Currently selected point (where the mouse points to)
    MapPoint selPt;
    /// Offset to selected point
    Position selPtOffset;
    /// Zeigerposition DIESER Ansicht, in View-Koordinaten wie MouseCoords::pos - also im selben
    /// Raum wie origin_. Splitscreen: jede Ansicht hat ihren eigenen Zeiger, die Ansicht fragt
    /// deshalb NICHT mehr VIDEODRIVER.GetMousePos() ab. std::nullopt = kein Zeiger auf dieser
    /// Ansicht, dann gibt es auch keinen selektierten Punkt.
    std::optional<Position> cursorPos_;

    /// Callbacks called when node is printed
    std::vector<IDrawNodeCallback*> drawNodeCallbacks;

    /// Show building quality icons. DIE EINSTELLUNG DES MENSCHEN - und nur sie geht je nach
    /// SETTINGS.ingame.showBQ zurueck (SaveIngameSettingsValues).
    ///
    /// WELLE 14: war ein bool, ist jetzt dreistufig (BqMode). Die ini kennt weiterhin nur ein
    /// Ja/Nein (`show_building_quality`); `Cursor` und `All` kommen beim Laden beide als `All`
    /// zurueck. Das ist bewusst und nicht vergessen - die ini gehoert allen Sitzplaetzen
    /// gemeinsam, und ein Padspieler soll dem Mausspieler dort keine Stufe hinterlassen, die
    /// dessen Knopf gar nicht erreichen kann. Der offene Punkt steht im Bericht.
    BqMode bqMode_;
    /// Bauhilfe, die dieser Ansicht fuer DIESE PARTIE aufgezwungen wurde (ForceShowBQ).
    ///
    /// Getrennt von bqMode_ und nicht mit ihm verrechnet, weil SaveIngameSettingsValues ALLE
    /// drei HUD-Werte dieser Ansicht in die ini schreibt und nicht nur den gerade geaenderten.
    /// Lebte die erzwungene Bauhilfe in bqMode_, truege der naechste HUD-Umschalter DIESER
    /// Ansicht sie dort hinein - iwAction, Reiter "Anzeigeoptionen" ruft
    /// ToggleShowNamesAndProductivity() auf der Ansicht, aus der das Fenster geoeffnet wurde,
    /// also auf der des PADSPIELERS. Der Mausspieler faende die Bauhilfe danach dauerhaft
    /// eingeschaltet vor, ohne sie je angefasst zu haben.
    /// WELLE 14: aus einem bool ein Modus geworden. ForceShowBQ erzwingt `Cursor` - siehe die
    /// Begruendung an BqMode.
    BqMode forcedBqMode_ = BqMode::Off;
    /// Der Mensch hat die Bauhilfe AUSDRUECKLICH ausgeschaltet (SetBqMode auf BqMode::Off).
    ///
    /// BEFUND PHASE 13, gemessen: ohne dieses Feld war jeder Schalter im Systemmenue nur die
    /// halbe Antwort. dskGameInterface::PadOpenActionWindow ruft ForceShowBQ() bei JEDEM A auf
    /// Bauland, und forcedBqMode_ faellt ausschliesslich in SetBqMode. Wer die Bauhilfe also
    /// ausschaltete und danach einmal A drueckte, hatte sie wieder an - und zwar fuer immer,
    /// weil derselbe Griff sie jedes Mal aufs Neue erzwingt. Genau die Schleife, die der
    /// Auftraggeber mit "ich verstehe nicht, wie ich die Symbole ausschalte" beschrieben hat.
    ///
    /// Eine Bequemlichkeitsvorgabe darf einen ausgesprochenen Willen nicht ueberstimmen. Sie
    /// darf ihn auch nicht fuer immer festschreiben: schaltet der Mensch die Bauhilfe spaeter
    /// wieder ein, faellt das Feld, und die Vorgabe wirkt wieder.
    bool bqExplicitlyOff_ = false;
    /// DIESE ANSICHT DARF DIE INI SCHREIBEN.
    ///
    /// BEFUND K3 der Welle 14, gemessen: SaveIngameSettingsValues schreibt in SETTINGS.ingame -
    /// und SETTINGS ist EINE Datei fuer ALLE Sitzplaetze. Solange die Bauhilfe ein Ja/Nein war,
    /// war das bloss laestig (ein Padspieler stellte dem Mausspieler die Vorgabe um); mit der
    /// dreistufigen Bauhilfe wird es inhaltlich falsch: ein Padspieler auf "nur am Zeiger"
    /// schreibt in das gemeinsame Ja/Nein ein "ja", und der Mausspieler faengt die naechste
    /// Partie mit "alles" an - einer Stufe, die er selbst nie gewaehlt hat und die sein
    /// zweistufiger Knopf gar nicht meint.
    ///
    /// DIE ENTSCHEIDUNG: nur der HAUPTSITZPLATZ schreibt (dskGameInterface::CreateViews setzt
    /// das Feld fuer jede weitere Ansicht auf false). Die zweite angebotene Loesung - ein
    /// EIGENES ini-Feld fuer die Padstufe - loest den Befund nicht: auch ein neues Feld waere
    /// EIN Wert fuer VIER Sitzplaetze, und der zweite Padspieler ueberschriebe damit den ersten.
    /// Sie benennt das Leck um, statt es zu schliessen.
    ///
    /// WAS DER ZWEITE SITZPLATZ DAFUER VERLIERT: seine Anzeigewahl gilt nur fuer diese Partie.
    /// Das ist die richtige Reihenfolge - eine Vorgabe, die allen gehoert, darf nicht von dem
    /// geaendert werden, der sie nicht besitzt. Der Mausspieler (immer die Hauptansicht) und der
    /// Einzelspieler merken von alledem nichts; fuer sie ist das Verhalten Bit fuer Bit das alte.
    ///
    /// VORGABE true, damit JEDE andere GameWorldView im Baum (iwObservate, Minimap-Vorschau,
    /// Einzelspieler) sich unveraendert verhaelt - abgeschaltet wird ausdruecklich und an genau
    /// einer Stelle.
    bool persistsHudSettings_ = true;
    /// Show building names
    bool show_names;
    /// Show productivities
    bool show_productivity;

    /// Offset from world origin in screen units (not map units): "scroll position"
    DrawPoint offset;
    /// Last scroll position (before jump)
    DrawPoint lastOffset;
    /// First drawn map point (might be slightly outside map -> Wrapping)
    DrawPoint firstPt;
    /// Last drawn map point
    DrawPoint lastPt;

    const GameWorldViewer& gwv;

    /// Top-Left position of the view (window)
    Position origin_;
    /// Size of the view
    Extent size_;

    /// How much the view is scaled (1=normal, >1=bigger, >0 && <1=smaller)
    float zoomFactor_;
    float effectiveZoomFactor_; ///< DPI scale corrected zoom factor
    float targetZoomFactor_;
    float zoomSpeed_;

public:
    GameWorldView(const GameWorldViewer& gwv, const Position& pos, const Extent& size);

    const GameWorldViewer& GetViewer() const { return gwv; }
    const GameWorldBase& GetWorld() const;
    SoundManager& GetSoundMgr();

    void SetPos(const Position& newPos) { origin_ = newPos; }
    Position GetPos() const { return origin_; }
    Extent GetSize() const { return size_; }

    /// Set target zoom factor and start zooming if smoothTransition is true
    /// Returns actual zoom factor used, potentially clamped
    float SetZoomFactor(float zoomFactor, bool smoothTransition = true);
    /// Zoomt SOFORT und haelt dabei den Weltpunkt unter `anchorViewPos` fest.
    ///
    /// SetZoomFactor allein zoomt auf die MITTE der Ansicht: der Zeichenpfad schneidet links und
    /// rechts gleich viel weg (CalcFxLx und die Projektionsmatrix in Draw rechnen beide mit
    /// diff/2). Fuer die Maus ist das richtig - sie steht beim Radzoom ohnehin meist mittig, und
    /// das Verhalten soll sich nicht aendern. Fuer ein Pad am Fernseher ist es falsch: der Zeiger
    /// steht dort dauernd am Rand seines Viertelbildschirms, und ein Zoom auf die Mitte schoebe
    /// genau den Punkt aus dem Bild, den der Spieler gerade betrachtet.
    ///
    /// Die Korrektur ist exakt und braucht keine Annahme ueber die Zoomformel: gemessen wird
    /// dieselbe Umrechnung vor und nach der Aenderung, die Differenz wandert in den Scrollstand.
    float SetZoomFactorAt(float zoomFactor, const Position& anchorViewPos);
    float GetCurrentTargetZoomFactor() const;
    void SetNextZoomFactor();

    // Converts a view coordinate to map position
    Position ViewPosToMap(Position pos) const;
    /// Umkehrung von ViewPosToMap: rechnet eine Kartenposition (im Raum von GetOffset(), also
    /// Weltpixel abzueglich des Scrollstands) zurueck in View-Koordinaten.
    ///
    /// Gebraucht ueberall dort, wo ein WELTpunkt auf dem Bildschirm gefunden werden muss statt
    /// umgekehrt: der Zoom auf den Zeiger (SetZoomFactorAt) und jede Padsteuerung, die einen
    /// Knoten anfahren will. Bewusst hier und nicht beim Aufrufer: die Zoomkorrektur steht damit
    /// genau einmal im Programm, in beiden Richtungen.
    Position MapPosToView(Position pos) const;

    /// Setzt den Zeiger DIESER Ansicht (View-Koordinaten, wie MouseCoords::pos).
    /// std::nullopt: diese Ansicht hat keinen Zeiger.
    void SetCursorPos(const std::optional<Position>& viewPos) { cursorPos_ = viewPos; }
    const std::optional<Position>& GetCursorPos() const { return cursorPos_; }
    /// Rechnet selPt aus dem EIGENEN Zeiger neu aus. Enthaelt keinen einzigen GL-Aufruf und wird
    /// von Draw() als erstes aufgerufen. Ohne Zeiger wird selPt zu MapPoint::Invalid().
    void UpdateSelection();

    /// Das Rechteck, mit dem Draw() glScissor aufruft: in echten Fensterpixeln (nicht in
    /// View-Koordinaten) und mit dem Ursprung UNTEN links, wie OpenGL es erwartet.
    /// Ausgelagert, damit die Begrenzung dieser Ansicht ohne OpenGL pruefbar ist.
    Rect GetScissorRect() const;

    /// Show or hide construction aid. Der ausdrueckliche Wille eines Menschen: er hebt eine
    /// erzwungene Bauhilfe auf (sonst liesse sie sich nie wieder abschalten) und er wird
    /// gespeichert.
    ///
    /// ZWEISTUFIG UND ES BLEIBT ZWEISTUFIG: aus <-> alles. Das ist Bit fuer Bit, was der
    /// MAUSSPIELER seit jeher an seinem Knopf (ID_btConstructionAid) und an der Leertaste
    /// erlebt, und was dskBenchmark braucht. Die dritte Stufe haengt am Ringschalter des
    /// Padspielers (CycleBqMode) - dort ist Platz fuer eine Beschriftung, die den Zustand nennt,
    /// am Mausknopf ist es ein unbeschriftetes Symbol von 1996 mit einem Tooltip.
    void ToggleShowBQ();
    /// DER DREISTUFIGE UMLAUF - aus, nur am Zeiger, alles, wieder aus.
    ///
    /// Woertlich der Wunsch des Auftraggebers ("ob ich alles sehe oder nur den Indikator unter
    /// meinem Zeiger"). Beginnt beim SICHTBAREN Zustand, nicht beim gespeicherten Feld: sieht
    /// der Mensch eine erzwungene Bauhilfe am Zeiger und drueckt weiter, kommt "alles" - und
    /// nicht ein Sprung zurueck an den Anfang.
    void CycleBqMode();
    /// Setzt den Modus GERADEHERAUS - der Weg, den "nur zuschauen" beim Verlassen nimmt
    /// (LeaveWatchOnly stellt genau den Modus wieder her, den es vorgefunden hat, und dafuer
    /// genuegt kein Umschalter).
    void SetBqMode(BqMode mode);
    /// Erzwingt die Bauhilfe fuer DIESE Ansicht und DIESE Partie.
    ///
    /// Sie wird NICHT in SETTINGS.ingame.showBQ geschrieben - weder hier noch spaeter durch
    /// irgendeinen anderen HUD-Umschalter dieser Ansicht. Genau dafuer gibt es forcedBqMode_ als
    /// eigenes Feld; die ausfuehrliche Begruendung steht dort.
    ///
    /// Ein Padspieler, der in Ansicht 2 das Baumenue oeffnet, aendert dem Mausspieler damit
    /// nichts - auch nicht nach einem Neustart.
    void ForceShowBQ();
    /// DIESE ANSICHT SCHREIBT NICHT MEHR IN DIE GEMEINSAME INI (Befund K3). Genau ein Aufrufer:
    /// dskGameInterface::CreateViews fuer jede Ansicht ausser der ersten. Die ausfuehrliche
    /// Begruendung steht an persistsHudSettings_.
    void StopPersistingHudSettings() { persistsHudSettings_ = false; }
    /// Schreibt diese Ansicht ihre HUD-Werte in SETTINGS? Lesbar, damit ein Nachweis die
    /// Trennung messen kann, statt sie zu glauben.
    bool PersistsHudSettings() const { return persistsHudSettings_; }
    /// WIEVIEL Bauhilfe diese Ansicht gerade zeigt: Einstellung, sonst Zwang.
    ///
    /// DIE EINE ZAHL, die der Zeichner liest - und dieselbe, die ein Nachweis liest. Eine
    /// Einstellung ungleich `Off` schlaegt den Zwang; ForceShowBQ greift ohnehin nur, solange
    /// gar nichts eingestellt ist.
    BqMode GetBqMode() const { return bqMode_ != BqMode::Off ? bqMode_ : forcedBqMode_; }
    /// Zeigt diese Ansicht ueberhaupt Bauhilfe? Einstellung ODER Zwang. Bleibt bestehen, weil
    /// sieben Lesestellen genau diese Frage stellen und keine feinere brauchen.
    bool IsShowingBQ() const { return GetBqMode() != BqMode::Off; }
    /// TRAEGT DIESER KNOTEN EIN SYMBOL? Die Entscheidung als eigene, benannte Rechnung.
    ///
    /// Sie steht hier und nicht im Zeichner, weil GameWorldView::Draw im Testprozess nicht
    /// laeuft (die Gelaendetexturen fehlen in den Testdaten). Verschmolzen statt abgeschrieben:
    /// DrawConstructionAid ruft sie selbst als erstes auf - es gibt also keine Bedingung beim
    /// Aufrufer, die neben dieser veralten koennte, und ein Nachweis kann den PRODUKTIVEN
    /// Zeichner Knoten fuer Knoten laufen lassen und die Symbole zaehlen.
    bool ShouldDrawConstructionAid(const MapPoint& pt) const;
    /// Zeichnet die Bauhilfe EINES Knotens - oder nichts, wenn er in dieser Stufe keins traegt.
    ///
    /// OEFFENTLICH, damit ein Nachweis die GEZEICHNETEN Symbole zaehlen kann und nicht nur den
    /// Zustand abfragt. Der ganze Zeichner GameWorldView::Draw laeuft im Testprozess nicht (die
    /// Gelaendetexturen fehlen in den Testdaten), diese Funktion aber schon - und sie ist
    /// dieselbe, die Draw je Knoten ruft, mitsamt ihrer Entscheidung.
    void DrawConstructionAid(const MapPoint& pt, const DrawPoint& curPos);
    /// Show or hide building names
    void ToggleShowNames();
    /// Show or hide productivity
    void ToggleShowProductivity();
    /// Toggle names and productivity completely on or off
    void ToggleShowNamesAndProductivity();
    /// Zeigt diese Ansicht Namen UND Auslastung? Genau die Frage, die
    /// ToggleShowNamesAndProductivity beantwortet - der Schalter kippt beide zusammen, also ist
    /// "beide an" der einzige Zustand, den ein Umschalter als "an" beschriften darf.
    bool IsShowingNamesAndProductivity() const { return show_names && show_productivity; }
    /// Die beiden EINZELN - gebraucht, seit der Ring sie getrennt schaltet (Phase 13). Reine
    /// Lesezugriffe auf Felder, die es seit jeher gibt; am Verhalten aendert sich nichts.
    bool IsShowingNames() const { return show_names; }
    bool IsShowingProductivity() const { return show_productivity; }

    /// Copy visibility of HUD elements from this view to another
    void CopyHudSettingsTo(GameWorldView& other, bool copyBQ) const;

    void Draw(const RoadBuildState& rb, MapPoint selected, bool drawMouse, unsigned* water = nullptr);

    /// Moves the map view by the given offset in pixels
    void MoveBy(const DrawPoint& numPixels);
    /// Moves a position on the map in pixels
    void MoveTo(const DrawPoint& newPos);
    /// Zentriert den Bildschirm auf ein bestimmtes Map-Object
    void MoveToMapPt(MapPoint pt);
    /// Springt zur letzten Position, bevor man "weggesprungen" ist
    void MoveToLastPosition();

    DrawPoint GetOffset() const { return offset; }

    /// Add a debug node printer
    void AddDrawNodeCallback(IDrawNodeCallback* newCallback);
    void RemoveDrawNodeCallback(IDrawNodeCallback* callbackToRemove);

    /// Gibt selektierten Punkt zurück
    MapPoint GetSelectedPt() const { return selPt; }

    /// Gibt ersten Punkt an, der beim Zeichnen angezeigt wird
    Position GetFirstPt() const { return firstPt; }
    /// Gibt letzten Punkt an, der beim Zeichnen angezeigt wird
    Position GetLastPt() const { return lastPt; }

    void Resize(const Extent& newSize);

    /// Triggered when visibility of HUD elements changes
    boost::signals2::signal<void()> onHudSettingsChanged;

private:
    void CalcFxLx();
    void DrawBoundaryStone(const MapPoint& pt, DrawPoint pos, Visibility vis);
    void DrawResource(const MapPoint& pt, DrawPoint curPos, Cheats::ResourceRevealMode resRevealMode);
    void DrawObject(const MapPoint& pt, const DrawPoint& curPos) const;
    void DrawFigures(const MapPoint& pt, const DrawPoint& curPos,
                     std::vector<ObjectBetweenLines>& objsBetweenRows) const;
    void DrawMovingFiguresFromBelow(const TerrainRenderer& terrainRenderer, const DrawPoint& curPos,
                                    std::vector<ObjectBetweenLines>& objsBetweenRows);

    void DrawNameProductivityOverlay(const TerrainRenderer& terrainRenderer);
    void DrawProductivity(const noBaseBuilding& no, const DrawPoint& curPos);
    void DrawGUI(const RoadBuildState& rb, const TerrainRenderer& terrainRenderer, const MapPoint& selectedPt,
                 bool drawMouse);

    void SaveIngameSettingsValues() const;
    void updateEffectiveZoomFactor();
};
