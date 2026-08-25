// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Rect.h"
#include "gameTypes/BuildingType.h"
#include <string>
#include <vector>

class Window;

/// KLARTEXT FUER GENAU EINEN LOKALEN SPIELER.
///
/// Der Ausloeser, woertlich: "Ich hab den Steinbruch und den Holzfaeller verwechselt" und "als
/// Anfaenger ist auch nicht klar, wann Flagge und wann Gebaeude kommt".
///
/// Warum das nicht ueber Tooltips geht, obwohl es Tooltips gibt und sie sogar schon den
/// Gebaeudenamen und die Baukosten tragen (iwAction.cpp, AddBuildingIcon):
///
///  1. Es gibt im ganzen Programm GENAU EINEN Tooltip (WindowManager::curTooltip). Vier lokale
///     Spieler koennen damit nie gleichzeitig verschiedene Texte lesen - der zweite SetToolTip
///     ueberschreibt den ersten.
///  2. Er wird an lastMousePos gezeichnet, und lastMousePos setzt ausschliesslich
///     WindowManager::Msg_MouseMove. Ohne je bewegte Maus ist er Position::Invalid() und es wird
///     GAR NICHTS gezeichnet. Ein Padspieler bewegt keine Maus.
///  3. Ausgeloest wird er nur aus Maus-Bewegungshandlern (ctrlButton::Msg_MouseMove und vier
///     weitere). Aus dem Padpfad ruft ihn niemand, und das nachzuruesten hiesse, dem
///     Mausspieler den Zeiger aus der Hand zu reissen.
///
/// Also folgt der Text dem FOKUS und nicht der Maus, und er lebt je PlayerView - genauso wie der
/// Fokus selbst (FocusPath) und die Ablehnung (PadRejection).
///
/// Diese Datei ist der reine Teil davon: sie kennt Window und Spieldaten, aber keinen Viewer,
/// keine Welt, kein OpenGL und keinen VIDEODRIVER. Damit ist jeder Satz, den ein Spieler zu
/// sehen bekommt, ohne Partie und ohne Grafik pruefbar.
namespace brief {

/// Ein Klartextblock, wie ihn ein Spieler unter seiner Ansicht liest.
///
/// Titel und Zeilen sind getrennt, weil sie verschieden gezeichnet werden (Titel gelb, Zeilen
/// weiss) - und weil ein Nachweis den Titel gegen BUILDING_NAMES zurueckschlagen kann, ohne den
/// Fliesstext zu zerlegen.
struct Brief
{
    std::string title;
    std::vector<std::string> lines;

    bool empty() const { return title.empty() && lines.empty(); }
    /// Alles hintereinander, durch Leerzeichen getrennt. Nur fuer Nachweise und Protokolle -
    /// gezeichnet wird nie daraus.
    std::string joined() const;
};

/// Was dieser Knoten fuer DIESEN Spieler hergibt.
///
/// Die Werte sind die Zweige von dskGameInterface::ComputeActionOptions, benannt statt
/// weggeworfen. Genau darin liegt der Unterschied zum heutigen Zustand: die Entscheidung wird
/// bereits so getroffen, nur behaelt niemand den GRUND, und am Ende steht fuer alle Faelle
/// derselbe Satz "Nothing can be done here."
///
/// Wo das Aktionsfenster etwas anzubieten hat, gibt es bewusst KEINE zweite Regelrechnung:
/// dskGameInterface::JudgeNode leitet NoSpace, FlagOnly, Hut, House, Castle, Mine, Harbor und
/// OwnRoad aus dem Ergebnis von ComputeActionOptions ab. Waeren sie unabhaengig gerechnet,
/// koennte der Text etwas anderes behaupten als das Fenster anbietet.
///
/// Die vier uebrigen Werte kommen NICHT von dort, und der Kommentar hat das frueher verschwiegen:
/// Unexplored, NoMansLand und ForeignTerritory liest JudgeNode selbst aus dem Viewer
/// (IsOwner, GetVisibility, Node::owner), OwnBuilding aus dem Knotenobjekt. ComputeActionOptions
/// unterscheidet diese vier gar nicht - fuer sie alle liefert sie dieselbe leere Auswahl. Genau
/// deshalb gibt es sie hier: der Sammelsatz "Nothing can be done here." war ihr gemeinsamer
/// Ausgang, und ihn aufzuteilen ist der Zweck dieser Aufzaehlung.
enum class NodeVerdict
{
    /// Der Knoten liegt im Nebel - der Spieler weiss ueber ihn nichts.
    Unexplored,
    /// Niemandsland: sichtbar, aber es gehoert keinem.
    NoMansLand,
    /// Das Gebiet eines anderen Spielers.
    ForeignTerritory,
    /// Eigenes Gebiet, aber hier ist fuer gar nichts Platz.
    NoSpace,
    /// Eigenes Gebiet, nur eine Flagge passt.
    FlagOnly,
    /// Eigenes Gebiet, Platz fuer eine Huette (und alles Kleinere).
    Hut,
    /// ... fuer ein Haus.
    House,
    /// ... fuer eine Burg.
    Castle,
    /// Ein Bergwerksknoten - hier passen ausschliesslich Minen.
    Mine,
    /// Ein Hafenplatz.
    Harbor,
    /// Hier steht eine eigene Flagge.
    OwnFlag,
    /// Hier steht ein eigenes Gebaeude oder eine eigene Baustelle.
    OwnBuilding,
    /// Hier laeuft eine eigene Strasse durch.
    OwnRoad
};
constexpr auto maxEnumValue(NodeVerdict)
{
    return NodeVerdict::OwnRoad;
}

/// Klartext zu einem Gebaeude: Name, wozu, woran es haengt, was es kostet, was angeliefert
/// werden muss.
///
/// Die Zahlen kommen aus BUILDING_COSTS und BLD_WORK_DESC, NICHT aus einer zweiten Tabelle -
/// ein Text, der die Kosten falsch nennt, waere schlimmer als gar keiner.
Brief ForBuilding(BuildingType bld);

/// Klartext zu einem Knoten: was hier geht, und wenn nichts geht, warum nicht.
Brief ForNode(NodeVerdict verdict);

/// Klartext zu einem fokussierten Control.
///
/// Gebaeudeicons bekommen den vollen Block; alles andere seinen eigenen Tooltiptext als Titel.
/// Damit entsteht nirgends eine zweite Beschriftungstabelle, die neben der ersten veralten kann.
///
/// WAS HIER FRUEHER STAND und der Code nicht haelt: "damit traegt JEDE Fokusstation Text". Tut
/// sie nicht. Der Block bleibt LEER, wenn das Control keine Tooltipbasis hat (ctrlTab und
/// ctrlGroup erben nur von Window) und ebenso, wenn es eine hat, deren Tooltip aber leer ist.
/// Der Zeichner ueberspringt einen leeren Block, der Spieler sieht dann gar keinen Kasten -
/// nicht falschen Text, aber eben auch keinen. Wer eine Fokusstation ohne Tooltip anlegt, muss
/// ihr einen geben; diese Funktion kann keinen erfinden. nullptr liefert ebenfalls leer.
Brief ForControl(const Window* ctrl);

/// Klartext waehrend des Strassenbaus - der Modus, in dem A, X und B eine andere Bedeutung
/// haben als sonst und in dem ein Anfaenger ohne Ansage nicht weiterkommt.
Brief ForRoadBuilding(bool waterRoad);

/// Der Kasten, in dem der Klartext einer Ansicht liegt.
///
/// Unten in ihrem Viewport, aber nie ausserhalb der Safe Area des BILDSCHIRMS: der Overscan
/// schneidet an den vier Kanten des Bildes ab, die Naht zwischen zwei Ansichten wird von nichts
/// abgeschnitten (dieselbe Begruendung wie in tv::SafeAreaRect). Passt der Kasten nicht mehr in
/// die Ueberschneidung, gewinnt die Sichtbarkeit: er wird an der Unterkante des Viewports
/// festgemacht und nicht auf null geklemmt.
///
/// `avoid` ist ein Rechteck, das den Kasten nicht verdecken darf - in der Praxis das
/// Aktionsfenster DIESER Ansicht. Ein leeres Rechteck heisst "nichts im Weg" und liefert exakt
/// den unteren Kasten.
///
/// WARUM ES DIESEN PARAMETER GIBT, nachgerechnet statt geschaetzt: das Aktionsfenster ist
/// 200 x 254 gross und steht am Zeiger des Padspielers, geklemmt auf tv::WindowBoundsRect. Bei
/// vier Ansichten auf 1080p ist ein Viewport 960 x 540; der Kasten liegt dann etwa bei
/// y = 940..1020, das Fenster reicht bis y = 1026. Ueberdeckung gibt es also, sobald der Zeiger
/// unterhalb von y = 686 steht - das sind 73 % der Zeigerhoehen einer Ansicht der unteren Reihe,
/// und waagerecht liegt das Fenster IMMER ueber dem Kasten, weil er die ganze Ansichtsbreite
/// einnimmt. Verdeckt sind dann 200 von 948 Punkten Breite, also ein knappes Fuenftel - und
/// zwar genau in dem Moment, in dem der Kasten gebraucht wird, naemlich waehrend der Spieler im
/// Baumenue navigiert.
///
/// Die Loesung ist, dass der KASTEN ausweicht und nicht das Fenster: das Fenster steht am
/// Zeiger, weil der Spieler dorthin sieht (dskGameInterface::OpenObjectWindow), und der Kasten
/// ist das einzige der beiden, dessen Lage keine Bedeutung traegt. Er hat zwei Plaetze - unten
/// und oben in seiner Ansicht -, und er nimmt den, der frei ist.
///
/// WIE WEIT DAS TRAEGT, und hier stand vorher eine Zusicherung, die der Code nicht haelt ("beide
/// zugleich kann das Fenster nicht verdecken, dafuer muesste es ueber 470 Punkte hoch sein"):
/// beide Plaetze zugleich trifft das Fenster genau dann, wenn es HOEHER ist als der Abstand
/// zwischen ihnen, und dieser Abstand haengt an der Ansichtshoehe:
///
///     Abstand = Ansichtshoehe - 12 (zwei Raender) - 2 x Kastenhoehe - Anteil der Safe Area
///
/// Der volle Gebaeudeblock ist 6 x 12 + 8 = 80 Punkte hoch. Auf 1080p mit vier Ansichten sind
/// das 540 - 12 - 160 - 54 = 314 Punkte gegen ein 254 Punkte hohes Fenster - es passt nicht auf
/// beide, dort gilt die starke Zusicherung. Auf 1280x720 mit vier Ansichten sind es
/// 360 - 12 - 160 - 36 = 152 gegen dieselben 254 - dort passt es, und der Kasten ist in JEDER
/// Zeigerstellung der unteren Reihe angeschnitten.
///
/// GEMESSEN (tests/s25Main/splitscreen/testPadBrief.cpp, TheTwoPlacesOnlyExistAboveThisImageHeight
/// und TheActionWindowNoLongerCoversTheTextPanel):
///
///   - Ab 952 Zeilen Bildhoehe liegen beide Plaetze in JEDER Ansichtszahl frei; ab dort gibt es
///     in keiner Zeigerstellung mehr eine Ueberdeckung. 1080p und alles darueber ist dort.
///   - Darunter - 1280x720 mit drei oder vier Ansichten ist der einzige Fall dieses Baums -
///     bleibt nur die schwaechere Aussage: der Kasten nimmt den Platz mit der kleineren
///     Ueberdeckung, und weil das Fenster 200 Punkte breit ist und der Kasten die ganze
///     Ansichtsbreite einnimmt, bleiben im schlimmsten gemessenen Fall 78 % von ihm stehen.
///     Angeschnitten, nicht verdeckt.
///
/// Das ist NICHT geloest, sondern benannt: bei 1280x720 auf vier Ansichten ist ein Viewport
/// 640 x 360, und das Aktionsfenster allein belegt 254 dieser 360 Zeilen. Fuer einen dritten
/// Platz bleiben 106 Zeilen, die sich das Fenster ausserdem beliebig auf beide Seiten aufteilen
/// kann - es gibt Zeigerstellungen, in denen rechnerisch kein freier Platz existiert. Die
/// Auswege waeren ein kleinerer Kasten oder ein Fenster, das nicht mehr am Zeiger steht; beides
/// nimmt dem Fall mehr, als es ihm gibt.
///
/// Rein - keine Einstellungen, kein VIDEODRIVER. Der Aufrufer reicht alle Rechtecke herein.
Rect PanelRect(const Rect& viewport, const Rect& safeArea, unsigned numLines, unsigned lineHeight,
               const Rect& avoid = Rect(Position(0, 0), Extent(0, 0)));

} // namespace brief
