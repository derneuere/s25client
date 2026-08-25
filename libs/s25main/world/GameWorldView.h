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
    bool show_bq;
    /// Bauhilfe, die dieser Ansicht fuer DIESE PARTIE aufgezwungen wurde (ForceShowBQ).
    ///
    /// Getrennt von show_bq und nicht mit ihm verrechnet, weil SaveIngameSettingsValues ALLE
    /// drei HUD-Werte dieser Ansicht in die ini schreibt und nicht nur den gerade geaenderten.
    /// Lebte die erzwungene Bauhilfe in show_bq, truege der naechste HUD-Umschalter DIESER
    /// Ansicht sie dort hinein - iwAction, Reiter "Anzeigeoptionen" ruft
    /// ToggleShowNamesAndProductivity() auf der Ansicht, aus der das Fenster geoeffnet wurde,
    /// also auf der des PADSPIELERS. Der Mausspieler faende die Bauhilfe danach dauerhaft
    /// eingeschaltet vor, ohne sie je angefasst zu haben.
    bool forcedShowBQ_ = false;
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
    void ToggleShowBQ();
    /// Erzwingt die Bauhilfe fuer DIESE Ansicht und DIESE Partie.
    ///
    /// Sie wird NICHT in SETTINGS.ingame.showBQ geschrieben - weder hier noch spaeter durch
    /// irgendeinen anderen HUD-Umschalter dieser Ansicht. Genau dafuer gibt es forcedShowBQ_ als
    /// eigenes Feld; die ausfuehrliche Begruendung steht dort.
    ///
    /// Ein Padspieler, der in Ansicht 2 das Baumenue oeffnet, aendert dem Mausspieler damit
    /// nichts - auch nicht nach einem Neustart.
    void ForceShowBQ();
    /// Zeigt diese Ansicht gerade die Bauhilfe? Einstellung ODER Zwang.
    bool IsShowingBQ() const { return show_bq || forcedShowBQ_; }
    /// Show or hide building names
    void ToggleShowNames();
    /// Show or hide productivity
    void ToggleShowProductivity();
    /// Toggle names and productivity completely on or off
    void ToggleShowNamesAndProductivity();

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
    void DrawConstructionAid(const MapPoint& pt, const DrawPoint& curPos);
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
