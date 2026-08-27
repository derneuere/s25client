// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Rect.h"
#include "driver/PadEvent.h"
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

/// Was ein Knopf in DIESEM Zustand bewirkt - als Wert, nicht als Satz.
///
/// Der Text ist die zweite Sache (KeyLabel). Der Wert ist die erste, und zwar aus genau einem
/// Grund: ein Nachweis kann ihn DRUECKEN und nachsehen, ob wirklich geschieht, was hier steht.
/// Ein Hinweis, der luegt, ist schlimmer als keiner - und ein Hinweis, den nur ein Mensch mit
/// dem Code vergleichen kann, luegt frueher oder spaeter. Jeder Wert hier hat in
/// tests/s25Main/splitscreen/testPadKeyHints.cpp eine Zusicherung, die den Knopf ueber den
/// PRODUKTIVEN Weg drueckt und die Wirkung misst.
enum class KeyAction
{
    /// A in der Welt: das Fenster des Objekts unter dem Zeiger (Schiff, eigenes Gebaeude,
    /// eigene Baustelle).
    OpenWindow,
    /// A auf einer eigenen Flagge: der Strassenbau faengt an.
    StartRoad,
    /// A oder RB: das Aktionsfenster geht auf.
    OpenActionMenu,
    /// X: eine Flagge setzen. Das EINZIGE Kommando, das in der Welt an einem Knopf haengt.
    PlaceFlag,
    /// LB an einer eigenen Wasserflagge: der Wasserweg faengt an.
    StartWaterway,
    /// Y: in das offene Fenster hinein - der Schritt, der bisher nirgends stand.
    EnterWindow,
    /// B in der Welt: das oberste eigene Fenster zu.
    CloseWindow,
    /// Back: das Systemmenue auf (und mit demselben Knopf wieder zu).
    SystemMenu,
    /// A im Baumodus: ein Stueck weiter bis zum Zeiger.
    ///
    /// BEFUND P1, gemessen: dieser Eintrag stand im Baumoduszweig BEDINGUNGSLOS da. Am Wegende -
    /// im ersten Augenblick jedes Strassenbaus, wenn der Zeiger noch auf der Startflagge steht -
    /// tut A gar nichts, und zwar schweigend. Er steht jetzt nur noch, wo
    /// dskGameInterface::PlanRoadStep Extend sagt, und das ist woertlich die Rechnung, mit der
    /// PadExtendRoad einsteigt.
    ExtendRoad,
    /// A im Baumodus AUF EINEM SCHON GELEGTEN STUECK: die eigene Vorschau bis dorthin zurueck.
    ///
    /// BEFUND P1b, in derselben Messung gefunden: dort BAUT A ZURUECK (PadExtendRoad ->
    /// GetIdInCurBuildRoad -> DemolishRoad), und die Leiste nannte es "Verlaengern". Das ist
    /// nicht dasselbe wie B: B nimmt IMMER genau ein Stueck, A springt bis zum Zeiger.
    ShortenRoad,
    /// X im Baumodus: die Strasse festschreiben.
    CommitRoad,
    /// B im Baumodus mit gelegter Strecke: ein Stueck zurueck.
    StepBackRoad,
    /// B im Baumodus auf leerer Strecke: der Modus endet.
    CancelRoad,
    /// A im Fenster: den fokussierten Knopf ausloesen.
    Choose,
    /// RB im Fenster: eine Fokusstation weiter.
    NextControl,
    /// LB im Fenster: eine Fokusstation ZURUECK (FocusPath::Move(Dir::Prev)).
    ///
    /// BEFUND N3, gemessen: LB wirkt seit Phase 4, stand aber nie in der Leiste, waehrend RB
    /// darin stand. Wer einen Knopf ueberschossen hatte, musste ihn mit RB umrunden - im
    /// Aktionsfenster viermal.
    PrevControl,
    /// B im Fenster: den Fokus wieder abgeben.
    LeaveFocus,
    /// DAS STEUERKREUZ auf der Werteachse eines Schiebereglers, einer Bildlaufleiste, einer
    /// Liste oder einer Tabelle - der Knopf, der dort WIRKT.
    ///
    /// BEFUND B1, gemessen: an genau diesen Controls versprach die Leiste "A Waehlen". A tut
    /// dort nichts (ctrlProgress und ctrlScrollBar haben kein Activate()), und was wirkt - das
    /// Steuerkreuz, FocusPath::Step -> Window::StepValue - wurde nie genannt. Seit Befund N4
    /// steht ausserdem da, was es auf der GEGENACHSE tut: den Fokus bewegen (KeyAction::MoveFocus).
    AdjustValue,
    /// DAS STEUERKREUZ, wo es KEINEN Wert aendert: dort wandert der FOKUS
    /// (FocusPath::Step -> Move).
    ///
    /// BEFUND N4, gemessen und vom Nachpruefer als die folgenreichste Auslassung fuer einen
    /// Anfaenger benannt: in JEDEM gewoehnlichen Fenster las der Spieler "A Waehlen - RB
    /// Weiter - B Zurueck - Back Menue" und erfuhr nirgends, dass Steuerkreuz und linker Stick
    /// den Fokus bewegen. Beide laufen durch DIESELBE Funktion (FocusPath::Step), der Stick
    /// ueber OnPadMove; die Leiste kann nur den Knopf nennen, weil ein Stickausschlag kein
    /// PadButton ist.
    MoveFocus,
    /// B auf einem Control mit OFFENER Eingabe (aufgeklappte Auswahlliste): die Liste klappt zu
    /// und der alte Wert bleibt stehen. Der Fokus bleibt dabei, wo er ist - deshalb ist das
    /// nicht LeaveFocus (FocusPath::OnPadButton, case B: erst Cancel(), dann erst Clear()).
    CancelChoice
};
constexpr auto maxEnumValue(KeyAction)
{
    return KeyAction::CancelChoice;
}

/// Was EINE Steuerkreuzrichtung im Fenster bewirkt.
///
/// Eine eigene kleine Aufzaehlung statt FocusPath::StepEffect, damit dieser Kopf weiterhin nur
/// `class Window;` vorwaerts kennt und kein Kopfteil der Eingabe einliest - dieselbe Regel, aus
/// der auch NodeVerdict entstanden ist. Der Aufrufer bildet FocusPath::PeekStep darauf ab.
///
/// WAS HIER VORHER STAND und warum es weg ist: eine Achse (FocusValue::Horizontal/Vertical),
/// abgelesen an Window::GetValueRange. Das war Befund N8 - GetValueRange ist NICHT die
/// Bedingung, unter der das Steuerkreuz wirkt, und es war zugleich Befund N4: auf der
/// Gegenachse wandert der Fokus, und davon stand nichts da. Jetzt wird jede der vier
/// Richtungen einzeln gefragt, und zwar an der Stelle, an der auch der Druck entscheidet.
enum class DpadEffect
{
    /// In dieser Richtung geschieht nichts.
    None,
    /// Das fokussierte Control aendert seinen Wert (Schieberegler, Liste, Tabelle).
    AdjustValue,
    /// Der Fokus wandert auf ein anderes Control.
    MoveFocus
};

/// Was A im BAUMODUS bewirkt.
///
/// Dieselbe Bauform und derselbe Grund wie bei DpadEffect: der Aufrufer bildet
/// dskGameInterface::PlanRoadStep darauf ab, damit dieser Kopf nichts vom Spielfeld einliest.
/// Die drei Werte sind woertlich die drei Zweige jenes Plans (Befund P1 und P1b).
enum class RoadStep
{
    /// A tut nichts und sagt auch nichts - der Zeiger steht auf dem Wegende.
    None,
    /// A haengt ein Stueck an.
    Extend,
    /// A baut die eigene Vorschau bis zum Zeiger zurueck.
    ShortenTo
};

/// Ein Eintrag der Tastenhinweisleiste: dieser Knopf tut das.
struct KeyHint
{
    PadButton button;
    KeyAction action;

    friend bool operator==(const KeyHint& a, const KeyHint& b)
    {
        return a.button == b.button && a.action == b.action;
    }
    friend bool operator!=(const KeyHint& a, const KeyHint& b) { return !(a == b); }
};

/// Ein Klartextblock, wie ihn ein Spieler unter seiner Ansicht liest.
///
/// Titel und Zeilen sind getrennt, weil sie verschieden gezeichnet werden (Titel gelb, Zeilen
/// weiss) - und weil ein Nachweis den Titel gegen BUILDING_NAMES zurueckschlagen kann, ohne den
/// Fliesstext zu zerlegen.
struct Brief
{
    std::string title;
    std::vector<std::string> lines;
    /// Die belegten Haupttasten in DIESEM Zustand - CONTROLLER-UX.md 6.2.
    ///
    /// Getrennt von `lines`, weil sie anders gezeichnet werden (eigene Farbe, eigene Zeile) und
    /// weil ein Nachweis sie als WERTE gegen das tatsaechliche Verhalten halten muss und nicht
    /// als Zeichenkette.
    std::vector<KeyHint> keys;

    /// LEER heisst: es wird gar kein Kasten gezeichnet.
    ///
    /// BEFUND B7, gemessen: seit die Tastenzeile mitzaehlt, gilt ein Block OHNE Titel und OHNE
    /// Zeilen, aber MIT Leiste, nicht mehr als leer - dort steht jetzt ein Kasten, wo Phase 9
    /// nichts zeichnete. Erreichbar ist das an zwei Stellen: ungueltiger Zeigerknoten in der
    /// Welt, und ein fokussiertes Control ohne Tooltip im Fenster.
    ///
    /// DAS IST GEWOLLT, und zwar aus dem Befund selbst heraus: der zweite dieser beiden Faelle
    /// IST der Befund von Pruefer 1 ("zusaetzlich ist der Klartextkasten an solchen Controls
    /// leer"). Ein Padspieler, der auf einer Bildlaufleiste steht, bekam bis Phase 11 gar nichts
    /// zu sehen - kein Titel, keine Zeile, und die Leiste gab es noch nicht. Jetzt liest er
    /// wenigstens, welche Knoepfe wirken. Das ist der ganze Zweck der Leiste, und sie ist genau
    /// dort am noetigsten, wo sonst nichts steht.
    ///
    /// Die Gegenprobe zur Randbedingung bleibt erhalten: eine Ansicht OHNE Pad bekommt gar keinen
    /// Block (dskGameInterface::UpdateInput ruft RefreshBrief nur fuer Ansichten mit Zeiger), und
    /// ein Block ohne Titel, ohne Zeilen und ohne Tasten ist weiterhin leer.
    bool empty() const { return title.empty() && lines.empty() && keys.empty(); }
    /// Alles hintereinander, durch Leerzeichen getrennt. Nur fuer Nachweise und Protokolle -
    /// gezeichnet wird nie daraus. Die Tasten stehen NICHT darin.
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
    /// Hier steht die Flagge des eigenen HAUPTQUARTIERS.
    ///
    /// GEMESSEN IN DER VORBEREITUNG ZU PHASE 12, und der Grund, warum dieser Wert vom
    /// gewoehnlichen OwnFlag getrennt ist: an dieser Flagge bietet das Aktionsfenster NUR
    /// "Strasse bauen" an (iwAction::FlagType::HQ, gesetzt in dskGameInterface::ShowActionWindow,
    /// wenn im Nordwesten ein nobHQ steht). Der Text zu OwnFlag versprach dort aber "abreissen,
    /// Geologen rufen, Spaeher aussenden" - und die HQ-Flagge ist fuer einen Anfaenger zu
    /// Spielbeginn die EINZIGE Flagge, die er besitzt. Ein Hinweis, der luegt, ist schlimmer als
    /// keiner; deshalb steht hier ein eigener Satz.
    OwnHQFlag,
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

/// Klartext zu einer HANDLUNG des Aktionsfensters.
///
/// DER BEFUND, woertlich vom Auftraggeber: "Um Eisenerz zu finden soll ich einen Gelehrten
/// losschicken, da ist noch nicht genau klar wie ich das mache." Die Funktion gibt es, sie ist
/// am Pad erreichbar (Zeiger auf eigene Flagge, RB, Y, Fokus auf den Knopf, A) - aber der Knopf
/// zeigt ein Icon, und sein Tooltip ist ein Bezeichner von zwei Woertern. Das ist dieselbe
/// Luecke wie beim Steinbruch aus Phase 9, nur eine Ebene weiter: nicht "welches Gebaeude",
/// sondern "welche Handlung".
///
/// Gemessen wurde in der Vorbereitung: JEDER Knopf in iwAction traegt einen Tooltip, und bei
/// ALLEN ausser den Gebaeudeicons ist er ein reiner Name ("Strasse bauen", "Gelehrten rufen").
/// Kein einziger sagt, was die Handlung bewirkt, was sie voraussetzt oder was danach passiert.
/// Genau das steht hier - und NUR das, was der Quelltext auch wirklich tut; die Belege stehen
/// am jeweiligen Rumpf in PlayerBrief.cpp.
enum class ActionBrief
{
    /// Der Reiterkopf des Baumenues.
    BuildMenuTab,
    /// Der Reiterkopf "Fahne setzen".
    SetFlagTab,
    /// Der Reiterkopf "Weg abreissen".
    CutRoadTab,
    /// Der Reiterkopf "Darstellungsmodus".
    WatchTab,
    // DER REITERKOPF "ANGRIFFSOPTIONEN" STEHT NICHT MEHR HIER, sondern in ForAttackMenu: sein
    // Inhalt haengt an den Knoepfen, die iwAction wirklich angelegt hat (Befund P4) - dieselbe
    // Bauform und derselbe Grund wie beim Flaggenreiter (Befund N6).
    /// TAB_FLAG 1: Strasse von dieser Flagge.
    BuildRoad,
    /// TAB_FLAG 2: Wasserweg von dieser Flagge.
    BuildWaterway,
    /// TAB_FLAG 3: Flagge abreissen - und zwar mit dem gemessenen Sonderfall, dass daraus die
    /// Abrissfrage fuer das GEBAEUDE nordwestlich wird.
    PullDownFlag,
    /// TAB_FLAG 4: der Geologe. Der Ausloeser dieser Phase.
    CallGeologist,
    /// TAB_FLAG 5: der Spaeher.
    SendScout,
    /// TAB_SETFLAG 1: hier eine Flagge aufstellen.
    ErectFlag,
    /// TAB_SETFLAG/TAB_CUTROAD 2: zur Eselstrasse aufwerten (nur mit Addon).
    UpgradeRoad,
    /// TAB_CUTROAD 1: die Strasse wieder ausgraben.
    DigUpRoad,
    /// TAB_WATCH 1: Beobachtungsfenster.
    Observe,
    /// TAB_WATCH 2: Haeusernamen und Auslastung.
    ToggleNames,
    /// TAB_WATCH 3: zum Hauptquartier springen.
    GoToHQ,
    /// TAB_WATCH 4: Verbuendete auf diese Stelle hinweisen.
    NotifyAllies
};
constexpr auto maxEnumValue(ActionBrief)
{
    return ActionBrief::NotifyAllies;
}

/// Klartext zu einer Handlung des Aktionsfensters.
Brief ForAction(ActionBrief action);

/// Welche Knoepfe der FLAGGENREITER wirklich traegt.
///
/// BEFUND N6, der schwerste dieser Runde und genau die Stelle, um die es dem Auftraggeber ging.
/// Hier stand bis eben ein KONSTANTER Satz ("... eine Strasse von ihr aus bauen, sie abreissen,
/// einen Geologen rufen, einen Spaeher aussenden"), und der Reiterkopf ist die ERSTE
/// Fokusstation nach Y - das Allererste also, was der Spieler im Fenster liest. iwAction baut
/// den Reiter aber je Flaggenart: an der HQ-Flagge steht dort GENAU EIN Knopf ("Strasse
/// bauen"), an einer Wasserflagge ein zusaetzlicher (Wasserweg).
///
/// Gemessen, deutsch, an der HQ-Flagge: am Knoten stand richtig "An DIESER Flagge gibt es keinen
/// Geologen und keinen Spaeher", einen Knopfdruck spaeter im Fenster stand das Gegenteil - an
/// der einzigen Flagge, die ein Anfaenger zu Spielbeginn besitzt, ueber genau die Funktion,
/// wegen der er festgesteckt ist.
///
/// DESHALB IST DAS HIER KEINE FLAGGENART, sondern die Liste der KNOEPFE: iwAction::GetPadBrief
/// liest sie aus der Reitergruppe ab, also aus denselben Controls, die der Spieler vor sich
/// sieht. Eine zweite Fallunterscheidung nach FlagType koennte neben der ersten veralten; ein
/// Knopf, den es nicht gibt, kann so gar nicht mehr im Text stehen.
struct FlagMenuButtons
{
    /// Knopf 1, "Strasse bauen" - steht an JEDER Flaggenart.
    bool road = false;
    /// Knopf 2, "Wasserweg bauen" - nur an FlagType::WaterFlag.
    bool waterway = false;
    /// Knopf 3, "Fahne abreissen". An FlagType::Storehouse heisst derselbe Knopf "Haus
    /// abreissen"; dieser Zweig wird in der Produktion nie gesetzt (siehe ActionBrief::
    /// PullDownFlag), und der Satz dort nennt den Fall ohnehin.
    bool pullDown = false;
    /// Knopf 4, der Geologe.
    bool geologist = false;
    /// Knopf 5, der Spaeher.
    bool scout = false;
};

/// Klartext zum Reiterkopf des Flaggenmenues - aus den Knoepfen, die WIRKLICH dastehen.
Brief ForFlagMenu(const FlagMenuButtons& buttons);

/// Welche Knoepfe der ANGRIFFSREITER wirklich traegt.
///
/// BEFUND P4: DERSELBE BAU WIE N6, AN EINER ZWEITEN STELLE. Der Kopftext des Angriffsreiters
/// versprach eine Soldatenwahl ("Choose how many soldiers march out and whether the strong or
/// the weak ones go."). Bei NULL erreichbaren Soldaten traegt der Reiter aber ueberhaupt keinen
/// Knopf: iwAction::AddAttackControls legt dann einen einzigen ctrlText an, woertlich "Attack
/// not possible." (iwAction.cpp, attackers_count == 0). Der Padspieler las also auf dem
/// Reiterkopf - der ERSTEN Fokusstation nach Y - eine Wahl, die es hinter dem Reiter gar nicht
/// gibt.
///
/// DESHALB IST DAS HIER KEINE SOLDATENZAHL, sondern die Liste der KNOEPFE, genau wie bei
/// FlagMenuButtons: iwAction::GetPadBrief liest sie aus der Reitergruppe ab, also aus denselben
/// Controls, die der Spieler vor sich sieht. Eine zweite Fallunterscheidung nach
/// available_soldiers_count koennte neben AddAttackControls veralten; ein Knopf, den es nicht
/// gibt, kann so gar nicht mehr im Text stehen.
///
/// Der Landangriff (TAB_ATTACK) und der Seeangriff (TAB_SEAATTACK) laufen durch DIESELBE
/// Funktion AddAttackControls und tragen deshalb dieselben Knopfnummern.
struct AttackMenuButtons
{
    /// Knopf 1 und 2: ein Angreifer weniger / mehr.
    bool fewer = false;
    bool more = false;
    /// Optionsgruppe 3: die starken oder die schwachen Soldaten.
    bool strength = false;
    /// Knopf 4: der Angriff selbst.
    bool attack = false;
    /// Knoepfe 10 bis 13: die Schnellauswahl der Anzahl. Wie viele es sind, haengt an der Zahl
    /// der verfuegbaren Soldaten (AddAttackControls: hoechstens vier).
    unsigned quickPicks = 0;

    /// Traegt der Reiter ueberhaupt etwas Bedienbares?
    bool any() const { return fewer || more || strength || attack || quickPicks > 0; }
};

/// Klartext zum Reiterkopf des Angriffsmenues - aus den Knoepfen, die WIRKLICH dastehen.
Brief ForAttackMenu(const AttackMenuButtons& buttons);

/// Alles, woran sich die Tastenhinweisleiste entscheidet - und NICHTS sonst.
///
/// Rein hereingereicht statt selbst gelesen: dieselbe Regel wie bei ForNode. Der Aufrufer
/// (dskGameInterface::RefreshBrief) liest jeden Wert genau dort, wo ihn auch der Knopf selbst
/// liest, und HintsFor rechnet nichts nach. Damit kann die Leiste nicht behaupten, was der Knopf
/// nicht tut - und ein Nachweis kann jeden Fall ohne Partie und ohne Grafik durchspielen.
struct KeyContext
{
    /// Der Fokus dieses Spielers steht in einem Fenster (FocusPath::IsActive). Dann sieht die
    /// Welt seine Flanken gar nicht erst.
    bool inWindow = false;
    /// Der Strassenbaumodus dieser Ansicht laeuft (RoadBuildState::mode != Disabled).
    bool roadMode = false;
    /// Zahl der schon gelegten Wegstuecke (RoadBuildState::route.size()).
    unsigned roadPieces = 0;
    /// WAS A IM BAUMODUS TUT - Befund P1 und P1b. Der Aufrufer bildet
    /// dskGameInterface::PlanRoadStep darauf ab, also genau den Plan, den PadExtendRoad
    /// ausfuehrt. Eine eigene kleine Aufzaehlung aus demselben Grund wie DpadEffect: dieser
    /// Kopf liest kein Kopfteil des Spielfelds ein.
    RoadStep roadStep = RoadStep::None;
    /// Das Wegende kann eine Flagge tragen (dskGameInterface::CanRoadEndAt). Ohne das lehnt X
    /// mit RoadEndBlocked ab.
    bool roadCanEnd = false;
    /// Y FUEHRT JETZT WIRKLICH IN EIN FENSTER.
    ///
    /// WOERTLICH die Kette, an der dskGameInterface::OnPadButton (Vorabfrage Y) und EnterWindow
    /// entscheiden: es gibt ein oberstes Fenster dieses Sitzplatzes, es ist NICHT schon die
    /// Wurzel seines Fokus, es ist nicht minimiert, und es hat ueberhaupt eine Fokusstation
    /// (FocusPath::HasFocusableControl).
    ///
    /// BEFUND B2, gemessen: vorher stand hier ein blosses `windowOpen`, und das war SCHWAECHER
    /// als das, was Y verlangt - an einem Fenster ohne bedienbares Control nannte die Leiste Y,
    /// und der Druck liess den Fokus untaetig. BEFUND B3, ebenfalls gemessen: dieselbe Angabe
    /// fehlte im Fenster, obwohl Y dort in ein NEU obenauf gelegtes Fenster fuehrt - genau der
    /// Fall, den Phase 11 eigens gebaut hat.
    bool canEnterWindow = false;
    /// B SCHLIESST JETZT WIRKLICH EIN FENSTER (nur in der Welt - im Fenster gibt B den Fokus ab).
    ///
    /// WOERTLICH die Kette von PadCloseTopMostWindow: oberstes Fenster dieses Sitzplatzes, noch
    /// nicht im Abriss, CloseBehavior::Regular und nicht angeheftet.
    ///
    /// BEFUND B2, gemessen am BEOBACHTUNGSFENSTER: das traegt CloseBehavior::NoRightClick
    /// (iwObservate.cpp), also schliesst B es nicht - die Leiste versprach es trotzdem.
    bool canCloseWindow = false;
    /// Was der Knoten unter dem Zeiger hergibt.
    NodeVerdict verdict = NodeVerdict::NoSpace;
    /// Unter dem Zeiger laesst sich ein Objektfenster oeffnen (Schiff, eigenes Gebaeude, eigene
    /// Baustelle) - dskGameInterface::CanOpenObjectWindow.
    bool canOpenObjectWindow = false;
    /// Das Aktionsfenster haette hier etwas anzubieten (ActionOptions::hasAction).
    bool canOpenActionMenu = false;
    /// Hier kann eine Flagge stehen (ActionOptions::tabs.setflag) - dieselbe Bedingung, unter
    /// der das Aktionsfenster seinen Knopf "Fahne setzen" anbietet.
    bool canPlaceFlag = false;
    /// Hier faengt ein Wasserweg an: eigene Flagge vom Typ Wasser.
    bool canStartWaterway = false;
    /// Back erreicht das Systemmenue - woertlich dskGameInterface::CanOpenSystemMenu, und das
    /// ist seit Befund N1 auch die Bedingung, an der OnPadButton den Knopf abfaengt.
    ///
    /// BEFUND N1, gemessen: die Bedingung kannte den STRASSENBAU nicht. OnPadButton faengt Back
    /// nur ab, solange kein Baumodus laeuft; laeuft er, verschluckt ihn der Fokus. Auf dem Weg,
    /// den die Leiste selbst vorgibt (eigene Flagge, RB, A, Y), stand "Back Menue" da und der
    /// Druck tat nichts - genau der Knopf, den ein festgefahrener Anfaenger als Ausweg sucht.
    bool canOpenSystemMenu = true;

    // --- Was das FOKUSSIERTE Control hergibt (nur im Fenster gelesen) ------------------------
    //
    // BEFUND B1, gemessen von zwei Pruefern: der inWindow-Zweig fragte das fokussierte Control
    // ueberhaupt nicht und zeigte in JEDEM Fensterzustand woertlich dasselbe. Diese vier Felder
    // sind die Antwort - und jedes von ihnen wird an der Stelle gelesen, an der auch der Knopf
    // selbst entscheidet.

    /// A bewirkt auf dem fokussierten Control etwas (Window::CanActivate).
    bool focusCanActivate = false;
    /// Was das Steuerkreuz in JEDER der vier Richtungen tut - woertlich
    /// FocusPath::PeekStep(Position(-1,0)) und so weiter, also dieselbe Rechnung, die auch der
    /// Druck ausfuehrt (FocusPath::PlanStep). Befund N4 und N8.
    DpadEffect dpadLeft = DpadEffect::None;
    DpadEffect dpadRight = DpadEffect::None;
    DpadEffect dpadUp = DpadEffect::None;
    DpadEffect dpadDown = DpadEffect::None;
    /// RB fuehrt auf eine WEITERE Fokusstation (FocusPath::CanMove(Dir::Next)). Dir::Next kennt
    /// keinen Umlauf; auf der letzten Station tut die Schulter nichts.
    bool focusHasNextStation = false;
    /// LB fuehrt auf eine VORHERIGE Fokusstation (FocusPath::CanMove(Dir::Prev)) - Befund N3.
    bool focusHasPrevStation = false;
    /// B verwirft eine offene Eingabe, statt den Fokus abzugeben (Window::CanCancelInput).
    bool focusCanCancelInput = false;
};

/// Die belegten Haupttasten in diesem Zustand, in fester Reihenfolge (A, X, Steuerkreuz, Y, RB,
/// LB, B, Back).
///
/// Die Reihenfolge ist fest, weil XAG 112 genau das verlangt: wiederkehrende Bedienelemente
/// erscheinen "in derselben relativen Reihenfolge an derselben Stelle". Der INHALT wechselt mit
/// der Lage, die LAGE nie.
///
/// Eine Taste, die zwar belegt, auf diesem Knoten aber wirkungslos ist, steht NICHT drin. Das
/// ist der ganze Zweck: ein Hinweis, der luegt, ist schlimmer als keiner.
std::vector<KeyHint> HintsFor(const KeyContext& ctx);

/// Der uebersetzte Text zu einer Tastenwirkung - kurz, weil er auf eine Zeile muss.
const char* KeyLabel(KeyAction action);

/// Der Name der Taste, wie er auf einem XInput-Pad steht. BEWUSST NICHT uebersetzt: es ist die
/// Beschriftung eines Geraets und keine Sprache. Auf einem DualSense oder einem Switch-Pad
/// stimmt sie physisch nicht - im Baum gibt es keine Erkennung des Padtyps
/// (SDL_GameControllerGetType ist nirgends angebunden), und das ist ein benannter, hinzunehmender
/// Bruch und keine Nachlaessigkeit.
const char* PadButtonLabel(PadButton button);

/// Die fertige Zeile, so wie sie unter dem Klartext steht: "A Strasse - RB Aktionen - Back Menue".
///
/// AUFEINANDERFOLGENDE Eintraege mit DERSELBEN Wirkung werden zu einem zusammengezogen:
/// "Left/Right Einstellen" statt "Left Einstellen - Right Einstellen". Das betrifft heute genau
/// das Steuerkreuz auf einer Werteachse, wo beide Richtungen dasselbe tun; die Leiste soll ihre
/// Breite nicht an eine Wiederholung verlieren. Die WERTE bleiben getrennt (`keys` enthaelt
/// weiterhin beide Knoepfe), damit ein Nachweis jeden einzeln druecken kann.
std::string KeyLine(const std::vector<KeyHint>& keys);

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
