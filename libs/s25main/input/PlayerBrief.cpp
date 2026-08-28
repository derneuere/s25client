// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "input/PlayerBrief.h"
#include "Window.h"
#include "controls/ctrlBaseTooltip.h"
#include "controls/ctrlBuildingIcon.h"
#include "mygettext/mygettext.h"
#include "gameData/BuildingBriefs.h"
#include "gameData/BuildingConsts.h"
#include "gameData/GoodConsts.h"
#include <algorithm>
#include <sstream>

namespace brief {

std::string Brief::joined() const
{
    std::string out = title;
    for(const std::string& line : lines)
    {
        if(!out.empty())
            out += ' ';
        out += line;
    }
    return out;
}

namespace {

    void addIfSet(Brief& b, const char* text)
    {
        if(text && *text)
            b.lines.emplace_back(_(text));
    }

    /// "Costs: 2 boards, 3 stones" - WOERTLICH die Formatierung des vorhandenen Tooltips
    /// (iwAction.cpp), damit derselbe Spieler nicht zwei verschiedene Schreibweisen derselben
    /// Zahl liest. Die Zahlen selbst kommen aus BUILDING_COSTS und nicht aus einer Kopie.
    std::string costLine(const BuildingType bld)
    {
        const BuildingCost cost = BUILDING_COSTS[bld];
        if(cost.boards == 0 && cost.stones == 0)
            return std::string();
        std::stringstream out;
        out << _("Costs: ");
        if(cost.boards > 0)
            out << unsigned(cost.boards) << _(" boards");
        if(cost.stones > 0)
        {
            if(cost.boards > 0)
                out << ", ";
            out << unsigned(cost.stones) << _(" stones");
        }
        return out.str();
    }

    /// "Supplies needed: grain, water" bzw. bei Minen "... or ...": eine Mine verbraucht NICHT
    /// eine von jeder Ware, sondern die, von der sie am meisten hat (BldWorkDescription::
    /// useOneWareEach == false). Der Unterschied ist fuer den Anfaenger genau der zwischen
    /// "ich muss alle drei liefern" und "eine reicht".
    std::string suppliesLine(const BuildingType bld)
    {
        const BldWorkDescription& work = BLD_WORK_DESC[bld];
        if(work.waresNeeded.empty())
            return std::string();
        std::stringstream out;
        out << _("Supplies needed: ");
        bool first = true;
        for(const GoodType good : work.waresNeeded)
        {
            if(!first)
                out << (work.useOneWareEach ? _(" and ") : _(" or "));
            first = false;
            out << _(WARE_NAMES[good]);
        }
        return out.str();
    }

} // namespace

Brief ForBuilding(const BuildingType bld)
{
    Brief b;
    // Nicht _() auf den leeren Namen: gettext("") liefert den KOPF des Katalogs (Zeitstempel,
    // Zeichensatz, Uebersetzerzeile). BuildingType::Nothing9 hat weder Namen noch Icon, ist
    // aber ein gueltiger Enumwert und damit ueber enumRange<> erreichbar.
    if(const char* name = BUILDING_NAMES[bld]; name && *name)
        b.title = _(name);
    addIfSet(b, BUILDING_PURPOSE_STRINGS[bld]);
    addIfSet(b, BUILDING_SITE_STRINGS[bld]);
    if(std::string supplies = suppliesLine(bld); !supplies.empty())
        b.lines.push_back(std::move(supplies));
    if(std::string costs = costLine(bld); !costs.empty())
        b.lines.push_back(std::move(costs));
    return b;
}

Brief ForNode(const NodeVerdict verdict)
{
    Brief b;
    switch(verdict)
    {
        case NodeVerdict::Unexplored:
            b.title = _("Unknown ground");
            b.lines.emplace_back(_("You have never seen this place. Send a scout out from one of your flags, or "
                                   "build a lookout tower."));
            break;
        case NodeVerdict::NoMansLand:
            b.title = _("No man's land");
            b.lines.emplace_back(_("This ground belongs to nobody, so you cannot build on it."));
            b.lines.emplace_back(_("Your border only grows outwards when you put up a guard post near it: a "
                                   "barracks, a guardhouse, a watchtower or a fortress."));
            break;
        case NodeVerdict::ForeignTerritory:
            b.title = _("Another player's land");
            b.lines.emplace_back(_("You cannot build here. This ground becomes yours only when you take the guard "
                                   "post that holds it."));
            break;
        case NodeVerdict::NoSpace:
            b.title = _("Nothing fits here");
            b.lines.emplace_back(_("Too close to another building, too steep, or the ground is water or rock. Not "
                                   "even a flag fits."));
            b.lines.emplace_back(_("Keep moving until this line tells you that something fits."));
            break;
        case NodeVerdict::FlagOnly:
            b.title = _("Room for a flag, but for no building");
            b.lines.emplace_back(_("Flags are the junctions of your road network. Wares are handed over from flag "
                                   "to flag, so a long road needs many of them."));
            b.lines.emplace_back(_("A flag costs nothing and you can tear it down again. Press X to put one here."));
            break;
        case NodeVerdict::Hut:
            b.title = _("Room for a small hut");
            // VOLLZAEHLIG, nicht beispielhaft: der Doppelpunkt verspricht eine Liste, und der
            // Anfaenger trifft danach seine Wahl. Frueher fehlten Abdecker, Spaehturm und
            // Wachstube - drei von zehn. Ein Test haelt die Liste gegen BUILDING_SIZE fest
            // (TheHutListNamesEveryHutAndNothingElse), damit sie nicht wieder auseinanderlaeuft.
            b.lines.emplace_back(_("Small buildings fit here: woodcutter, forester, quarry, fishery, hunter, well, "
                                   "skinner, lookout tower, barracks and guardhouse."));
            b.lines.emplace_back(_("Press A for the build menu, or X to put a flag here instead."));
            break;
        case NodeVerdict::House:
            b.title = _("Room for a medium house");
            b.lines.emplace_back(_("Medium buildings fit here - sawmill, mill, bakery, iron smelter - and every "
                                   "small one as well."));
            b.lines.emplace_back(_("Press A for the build menu, or X to put a flag here instead."));
            break;
        case NodeVerdict::Castle:
            b.title = _("Room for a large building");
            b.lines.emplace_back(_("Everything fits here, up to a farm, a fortress or a catapult."));
            b.lines.emplace_back(_("Press A for the build menu, or X to put a flag here instead."));
            break;
        case NodeVerdict::Mine:
            b.title = _("Room for a mine");
            b.lines.emplace_back(_("This is inside a mountain. Only mines can be dug here - no hut, no house, "
                                   "nothing else."));
            b.lines.emplace_back(_("Press A for the build menu."));
            break;
        case NodeVerdict::Harbor:
            b.title = _("A harbour site");
            b.lines.emplace_back(_("A harbour building can be raised here, and every smaller building too. There "
                                   "are only a few such spots on any map."));
            b.lines.emplace_back(_("Press A for the build menu."));
            break;
        case NodeVerdict::OwnFlag:
            b.title = _("Your flag");
            b.lines.emplace_back(_("Roads start and end at flags."));
            b.lines.emplace_back(_("Press A to start a road from here. Press RB for the flag menu - tear it down, "
                                   "call a geologist, send out a scout."));
            break;
        // GEMESSEN: an der HQ-Flagge enthaelt der Flaggenreiter GENAU EINEN Knopf, "Strasse
        // bauen" (iwAction::FlagType::HQ). Der Satz zu OwnFlag versprach dort drei weitere
        // Handlungen, die es an dieser Flagge nicht gibt - an der einzigen Flagge, die ein
        // Anfaenger zu Spielbeginn besitzt.
        case NodeVerdict::OwnHQFlag:
            b.title = _("The flag of your headquarters");
            b.lines.emplace_back(_("Every ware of yours goes in and out through this flag. Press A to start your "
                                   "first road here."));
            b.lines.emplace_back(_("At THIS flag there is no geologist and no scout - its menu only builds roads. "
                                   "For those two you need an ordinary flag out in your land."));
            break;
        case NodeVerdict::OwnBuilding:
            b.title = _("Your building");
            b.lines.emplace_back(_("Press A to open it."));
            break;
        case NodeVerdict::OwnRoad:
            b.title = _("Your road");
            b.lines.emplace_back(_("Press A to open the menu - from there you can dig the road up again."));
            break;
    }
    return b;
}

Brief ForControl(const Window* const ctrl)
{
    Brief b;
    if(!ctrl)
        return b;
    // Ein Gebaeudeicon bekommt den vollen Block. Das ist der Fall, um den es dem Auftraggeber
    // ging: zwei Icons, die er nicht auseinanderhaelt.
    if(const auto* icon = dynamic_cast<const ctrlBuildingIcon*>(ctrl))
        return ForBuilding(icon->GetType());
    // Alles andere traegt seinen eigenen Tooltip als Titel. Der Querabstieg ist erlaubt, weil
    // Window polymorph ist; Controls ohne Tooltipbasis liefern nullptr und damit einen leeren
    // Block - der Zeichner ueberspringt ihn dann.
    if(const auto* tip = dynamic_cast<const ctrlBaseTooltip*>(ctrl))
        b.title = tip->GetTooltip();
    return b;
}

Brief ForRoadBuilding(const bool waterRoad)
{
    Brief b;
    b.title = waterRoad ? _("Building a waterway") : _("Building a road");
    b.lines.emplace_back(_("Move the pointer and press A to lay the next piece."));
    b.lines.emplace_back(_("X finishes the road, B takes one piece back. A road has to end at a flag or at a spot "
                           "where a flag can stand."));
    return b;
}

Brief ForAction(const ActionBrief action)
{
    Brief b;
    switch(action)
    {
        // --- die Reiterkoepfe -----------------------------------------------------------
        //
        // Sie sind die ERSTE Fokusstation nach Y (gemessen), und ihr Tooltip ist der Name des
        // Reiterbilds. Beim Flaggenreiter ist das woertlich "Erect flag" - dieselbe Zeichenkette
        // wie beim Reiter, der eine Flagge SETZT. Ein Anfaenger liest also zweimal dasselbe Wort
        // fuer zwei verschiedene Dinge; deshalb bekommen die Koepfe einen eigenen Satz.
        //
        // DER FLAGGENREITER STEHT NICHT MEHR HIER, sondern in ForFlagMenu: sein Inhalt haengt
        // an den Knoepfen, die iwAction wirklich angelegt hat (Befund N6). Alle Koepfe, die
        // hier geblieben sind, tragen an JEDER Stelle, an der es sie gibt, dieselben Knoepfe -
        // nachgesehen in iwAction.cpp, Konstruktor.
        case ActionBrief::BuildMenuTab:
            b.title = _("Build menu");
            b.lines.emplace_back(_("The buildings that fit on this spot. Move onto one and this box tells you what "
                                   "it does and what it costs."));
            break;
        case ActionBrief::SetFlagTab:
            b.title = _("Put up a flag");
            b.lines.emplace_back(_("Flags are the junctions of your road network."));
            break;
        case ActionBrief::CutRoadTab:
            b.title = _("Road menu");
            b.lines.emplace_back(_("What you can do with the road under the pointer."));
            break;
        case ActionBrief::WatchTab:
            b.title = _("View menu");
            b.lines.emplace_back(_("Watch window, building names, jump to your headquarters, point your allies "
                                   "here. None of these change anything in the world."));
            break;
        // --- der Flaggenreiter ----------------------------------------------------------
        case ActionBrief::BuildRoad:
            b.title = _("Build a road");
            b.lines.emplace_back(_("Starts a road at this flag. Move the pointer and press A for each piece, X "
                                   "finishes it."));
            b.lines.emplace_back(_("Wares are carried from flag to flag, so a building without a road to your "
                                   "warehouse gets nothing and delivers nothing."));
            break;
        case ActionBrief::BuildWaterway:
            b.title = _("Build a waterway");
            b.lines.emplace_back(_("Like a road, but across the water. It only starts at a flag that stands on the "
                                   "shore."));
            break;
        case ActionBrief::PullDownFlag:
            b.title = _("Pull down this flag");
            b.lines.emplace_back(_("The flag goes away and with it the roads that end here."));
            // GEMESSEN, iwAction::Msg_ButtonClick_TabFlag case 3: steht im Nordwesten ein
            // Gebaeude oder eine Baustelle, oeffnet dieser Knopf die Abrissfrage fuer das HAUS.
            // Der Knopf heisst trotzdem "Fahne abreissen". Der ehrliche Zweig, der "Haus
            // abreissen" beschriftet (FlagType::Storehouse), wird in der ganzen Produktion nie
            // gesetzt. Also sagt es der Text.
            b.lines.emplace_back(_("Careful: if a building stands right above this flag, this button asks whether "
                                   "to tear down THAT BUILDING instead."));
            break;
        // DER AUSLOESER DIESER PHASE. Jeder Satz hier ist im Quelltext nachgelesen, die Stellen
        // stehen daneben - eine erfundene Spielregel waere hier schlimmer als gar kein Text.
        case ActionBrief::CallGeologist:
            b.title = _("Call in a geologist");
            // GamePlayer::CallFlagWorker -> FindWarehouse(HasFigure(Geologist)) -> OrderJob:
            // er kommt aus einem Lagerhaus und geht zu DIESER Flagge.
            // nofGeologist::GoalReached: signs = 15. LookForNewNodes: Punkte im Radius 15 um
            // flag->GetPos(). ExamineNode: Schild auf jedem Grabungspunkt, Postmeldung je
            // Rohstoffart einmal je Reise.
            b.lines.emplace_back(_("He walks out from THIS flag and digs around it for iron, gold, coal, granite "
                                   "and water. He puts up 15 signs and reports each kind of find once."));
            // CallFlagWorker kehrt still zurueck, wenn FindWarehouse (RoadPathFinder) kein Lager
            // erreicht - gemessen: kein Mann, kein Schild, keine Meldung, kein Ton.
            b.lines.emplace_back(_("He comes out of a warehouse, so this flag needs a road to one. Without that "
                                   "road nothing happens at all - and nothing tells you so."));
            // BEFUND B5: hier stand "Ein Bergwerk foerdert nur dort, wo ein Schild steht". Das
            // ist eine erfundene Spielregel, und sie widersprach dem eigenen Nachsatz im selben
            // Satz. NACHGELESEN, was wirklich geschieht:
            //
            //  - Das Schild ist REINE ANZEIGE. noSign.cpp sagt es im Konstruktor woertlich
            //    ("As this is only for drawing"), und die Klasse traegt ausser Draw() und
            //    GetResource() nichts. Kein Bergwerk und kein Arbeiter fragt je nach einem
            //    noSign, bevor gefoerdert wird.
            //  - Was die Foerderung entscheidet, ist der BODEN: nofMiner::StartWorking ruft
            //    nofWorkman::FindPointWithResource, und das ist
            //    GetMatchingPointsInRadius(pos, MINER_RADIUS, NodeHasResource(res), true) -
            //    also die Knoten im Umkreis von MINER_RADIUS = 2 (gameData/GameConsts.h) um das
            //    Bergwerk EINSCHLIESSLICH seines eigenen, deren Node.resources den Rohstoff
            //    tragen. Findet sich keiner, meldet das Werk OnOutOfResources.
            //  - Das Schild kann sogar FEHLEN, wo Rohstoff liegt: nofGeologist::SetSign stellt
            //    keines auf, wenn auf dem Knoten schon ein Objekt steht.
            //  - Verschwinden tut das Schild (noSign: 8500 GF mal Addonfaktor), der Rohstoff
            //    nicht - der geht nur durch Foerdern zurueck (GameWorld::ReduceResource).
            b.lines.emplace_back(_("The sign is only a mark: a mine digs what lies in the ground within two steps "
                                   "of itself, sign or no sign. Build it right next to a sign - the signs fade "
                                   "away after a while, what lies underneath does not."));
            break;
        case ActionBrief::SendScout:
            b.title = _("Send out a scout");
            // nofScout_Free::GoalReached: rest_way = 80 + RANDOM_RAND(20), Sichtweite
            // VISUALRANGE_SCOUT = 3 (gameData/MilitaryConsts.h).
            b.lines.emplace_back(_("He walks out from THIS flag, wanders about a hundred steps through the fog and "
                                   "then comes home. What he walks past stays uncovered."));
            b.lines.emplace_back(_("Like the geologist he comes out of a warehouse, so this flag needs a road to "
                                   "one."));
            break;
        // --- Flagge setzen, Strasse ------------------------------------------------------
        case ActionBrief::ErectFlag:
            b.title = _("Put up a flag here");
            b.lines.emplace_back(_("A flag costs nothing and you can tear it down again. Roads start and end at "
                                   "flags, and wares are handed over there."));
            break;
        case ActionBrief::UpgradeRoad:
            b.title = _("Upgrade to a donkey road");
            b.lines.emplace_back(_("Donkeys carry the wares on this road instead of your carriers, which makes it "
                                   "faster."));
            break;
        case ActionBrief::DigUpRoad:
            b.title = _("Dig up this road");
            b.lines.emplace_back(_("The road disappears. Flags that then hang on nothing stay where they are."));
            break;
        // --- der Anzeigereiter: alles hier aendert NICHTS in der Welt ---------------------
        case ActionBrief::Observe:
            b.title = _("Watch window");
            b.lines.emplace_back(_("Opens a small second window that keeps watching this spot while you work "
                                   "somewhere else."));
            break;
        case ActionBrief::ToggleNames:
            b.title = _("Building names and workload");
            b.lines.emplace_back(_("Writes the name and the workload above every building. The workload tells you "
                                   "which of your works stand idle."));
            break;
        case ActionBrief::GoToHQ:
            b.title = _("Go to the headquarters");
            b.lines.emplace_back(_("Moves your view back to your headquarters."));
            break;
        case ActionBrief::NotifyAllies:
            b.title = _("Point your allies here");
            b.lines.emplace_back(_("Your allies get a message with this spot in it. Without an ally nothing "
                                   "happens."));
            break;
    }
    return b;
}

Brief ForFlagMenu(const FlagMenuButtons& buttons)
{
    Brief b;
    b.title = _("Flag menu");

    // Die Aufzaehlung entsteht aus den Knoepfen, die WIRKLICH dastehen - siehe die Begruendung
    // an FlagMenuButtons. Zusammengesetzt und nicht als fertige Saetze im Katalog, weil es
    // sonst fuenf feste Kombinationen waeren, die neben iwAction veralten koennen.
    std::vector<std::string> what;
    if(buttons.road)
        what.emplace_back(_("build a road from it"));
    if(buttons.waterway)
        what.emplace_back(_("build a waterway from it"));
    if(buttons.pullDown)
        what.emplace_back(_("tear it down"));
    if(buttons.geologist)
        what.emplace_back(_("call in a geologist"));
    if(buttons.scout)
        what.emplace_back(_("send out a scout"));

    if(what.empty())
    {
        // Kein einziger Knopf: heute unerreichbar (jede Flaggenart traegt mindestens "Strasse
        // bauen"), aber ein Doppelpunkt ohne Liste waere schlimmer als dieser Satz.
        b.lines.emplace_back(_("There is nothing to do at this flag."));
        return b;
    }
    std::string line = _("What you can do at this flag:");
    for(std::size_t i = 0; i < what.size(); ++i)
    {
        line += (i == 0) ? " " : ", ";
        line += what[i];
    }
    line += '.';
    b.lines.push_back(std::move(line));

    // DER SATZ, DER DEN AUFTRAGGEBER BETRIFFT. An der HQ-Flagge - der einzigen, die er zu
    // Spielbeginn hat - gibt es beide nicht, und er sucht genau den Geologen. Der Text sagt ihm
    // hier dasselbe wie der Knotentext (NodeVerdict::OwnHQFlag) und schickt ihn weiter, statt
    // ihn vor einem Reiter mit einem einzigen Knopf stehen zu lassen.
    if(!buttons.geologist && !buttons.scout)
    {
        b.lines.emplace_back(_("There is no geologist and no scout at THIS flag. For those two you need an "
                               "ordinary flag out in your land - one that you put up yourself."));
    }
    return b;
}

Brief ForAttackMenu(const AttackMenuButtons& buttons)
{
    Brief b;
    b.title = _("Attack menu");

    // BEFUND P4: DER GEMESSENE FALL, um den es geht. Bei null erreichbaren Soldaten legt
    // iwAction::AddAttackControls keinen einzigen Knopf an, sondern nur den Text "Attack not
    // possible." - der Kopf versprach trotzdem eine Soldatenwahl. Gefragt wird deshalb die
    // Reitergruppe selbst und nicht die Soldatenzahl (siehe AttackMenuButtons).
    if(!buttons.any())
    {
        b.lines.emplace_back(_("Nothing to do here: not one of your soldiers can reach this building. This tab "
                               "carries no button at all, only the notice that an attack is not possible."));
        // Woertlich iwAction::AddAttackControls: die Zahl kommt aus GetAvailableSoldiersForAttack
        // bzw. GetNumSoldiersForSeaAttack. Sie ist null, solange kein eigenes Militaergebaeude
        // nahe genug steht - deshalb dieser Hinweis und keine erfundene Spielregel.
        b.lines.emplace_back(_("Soldiers only march out from your own military buildings nearby. Build one closer "
                               "and come back."));
        return b;
    }

    // Sonst: die Aufzaehlung entsteht aus den Knoepfen, die WIRKLICH dastehen - dieselbe
    // Begruendung wie bei ForFlagMenu.
    std::vector<std::string> what;
    if(buttons.fewer || buttons.more || buttons.quickPicks > 0)
        what.emplace_back(_("set how many soldiers march out"));
    if(buttons.strength)
        what.emplace_back(_("send the strong ones or the weak ones"));
    if(buttons.attack)
        what.emplace_back(_("start the attack"));

    std::string line = _("What you can do in this tab:");
    for(std::size_t i = 0; i < what.size(); ++i)
    {
        line += (i == 0) ? " " : ", ";
        line += what[i];
    }
    line += '.';
    b.lines.push_back(std::move(line));
    return b;
}

const char* PadButtonLabel(const PadButton button)
{
    switch(button)
    {
        case PadButton::A: return "A";
        case PadButton::B: return "B";
        case PadButton::X: return "X";
        case PadButton::Y: return "Y";
        case PadButton::Back: return "Back";
        case PadButton::LeftShoulder: return "LB";
        case PadButton::RightShoulder: return "RB";
        case PadButton::DpadUp: return "Up";
        case PadButton::DpadDown: return "Down";
        case PadButton::DpadLeft: return "Left";
        case PadButton::DpadRight: return "Right";
        case PadButton::Start: return "Start";
        case PadButton::Guide: return "Guide";
        case PadButton::LeftStick: return "L3";
        case PadButton::RightStick: return "R3";
    }
    return "";
}

KeyAction ActionMenuAction(const ActionMenuKind kind)
{
    switch(kind)
    {
        case ActionMenuKind::Build: return KeyAction::OpenBuildMenu;
        case ActionMenuKind::Road: return KeyAction::OpenRoadMenu;
        case ActionMenuKind::Attack: return KeyAction::OpenAttackMenu;
        case ActionMenuKind::Flag: return KeyAction::OpenFlagMenu;
        case ActionMenuKind::Trade: return KeyAction::OpenTradeWindow;
        case ActionMenuKind::None:
        case ActionMenuKind::Generic: break;
    }
    return KeyAction::OpenActionMenu;
}

const char* KeyLabel(const KeyAction action)
{
    // KURZ, und das ist gemessen und keine Geschmacksfrage: die Zeile hat in einer Viertel-
    // Ansicht 852 Punkte Breite, und sie steht neben zwei bis vier Geschwistern. Mehr als ein
    // Wort je Eintrag traegt sie nicht.
    switch(action)
    {
        // "Open it" / "Close it" und nicht "Open" / "Close": beide kurzen Woerter stehen im
        // Katalog schon, und zwar als EIGENSCHAFTSWORT - "Open" ist dort "Offen", "Close" ist
        // "Nah". Ein Hinweis "A Offen" oder "B Nah" waere schlimmer als gar keiner, und
        // msgctxt gibt es in mygettext nicht.
        case KeyAction::OpenWindow: return _("Open it");
        case KeyAction::StartRoad: return _("Road");
        case KeyAction::OpenActionMenu: return _("Actions");
        // BEFUND K5 (Text): "Build menu" und nicht "Build" - der Klartextkasten daneben sagt woertlich
        // "Drueck A fuer das Baumenue", und zwei Woerter fuer denselben Knopf waren genau der
        // Befund. "Road" ist ausserdem schon vergeben (KeyAction::StartRoad, A auf einer eigenen
        // Flagge); zwei Knoepfe mit demselben Wort waeren die naechste Verwechslung.
        //
        // ALLE FUENF STEHEN SCHON IM KATALOG, und zwar mit genau den Woertern, die der
        // Klartextkasten daneben benutzt: "Baumenue", "Strassenmenue", "Angriffsmenue",
        // "Flaggenmenue", "Handel". Nachgesehen und nicht angenommen - es gibt fuer diesen
        // Befund also keine einzige neue Zeichenkette und keine ungeuebersetzte Leiste in
        // irgendeiner Sprache. Genau das macht das Auffaechern hier billiger als das Umbenennen.
        case KeyAction::OpenBuildMenu: return _("Build menu");
        case KeyAction::OpenRoadMenu: return _("Road menu");
        case KeyAction::OpenAttackMenu: return _("Attack menu");
        case KeyAction::OpenFlagMenu: return _("Flag menu");
        case KeyAction::OpenTradeWindow: return _("Trade");
        case KeyAction::PlaceFlag: return _("Flag");
        case KeyAction::StartWaterway: return _("Waterway");
        case KeyAction::EnterWindow: return _("Into the window");
        case KeyAction::CloseWindow: return _("Close it");
        case KeyAction::SystemMenu: return _("Menu");
        case KeyAction::ExtendRoad: return _("Extend");
        // "Back to here" und nicht noch einmal "One back": B nimmt genau EIN Stueck, A springt
        // bis zum Zeiger. Zwei Knoepfe mit demselben Wort waeren hier die naechste Luege.
        case KeyAction::ShortenRoad: return _("Back to here");
        case KeyAction::CommitRoad: return _("Finish");
        case KeyAction::StepBackRoad: return _("One back");
        case KeyAction::CancelRoad: return _("Cancel");
        case KeyAction::Choose: return _("Choose");
        case KeyAction::NextControl: return _("Next");
        // "Previous" steht im Katalog bisher NICHT (nachgesehen) - es gibt hier also keine
        // Kollision mit einem schon uebersetzten Wort anderer Bedeutung.
        case KeyAction::PrevControl: return _("Previous");
        case KeyAction::LeaveFocus: return _("Back out");
        // "Adjust" und "Discard" stehen im Katalog bisher NICHT (nachgesehen) - anders als bei
        // "Open"/"Close" gibt es hier also keine Kollision mit einem Eigenschaftswort.
        case KeyAction::AdjustValue: return _("Adjust");
        // "Move" steht im Katalog bisher NICHT (nachgesehen). Gemeint ist der FOKUSRAHMEN, der
        // von Knopf zu Knopf wandert - das einzige, was sich dabei bewegt.
        case KeyAction::MoveFocus: return _("Move");
        case KeyAction::CancelChoice: return _("Discard");
        // "Turn" steht im Katalog bisher NICHT (nachgesehen).
        case KeyAction::TurnRing: return _("Turn");
        // "Next" und "Previous" stehen schon oben fuer die FOKUSSTATION. Im Ring geht es um
        // die SEITE, und ein Spieler, der beide Zustaende nacheinander sieht, muss den
        // Unterschied lesen koennen - deshalb hier eigene Woerter.
        case KeyAction::RingNextPage: return _("Next page");
        case KeyAction::RingPrevPage: return _("Previous page");
        // B schliesst im Ring wirklich das Fenster dahinter (RingOnPadButton -> CloseRing mit
        // closeWindow), nicht nur den Fokus. Also dasselbe Wort wie beim Fensterschliessen.
        case KeyAction::CloseRing: return _("Close it");
        case KeyAction::LeaveWatchOnly: return _("Stop watching");
        // Was der Stick im Ring TUT, ist zeigen - der Fokus faellt auf den Sektor, in den er
        // zeigt (dskGameInterface::RingSyncFocus). "Waehlen" waere A und damit falsch.
        case KeyAction::AimRing: return _("Point");
    }
    return "";
}

const char* KeyInputLabel(const KeyHint& hint)
{
    switch(hint.input)
    {
        case KeyInput::Button: return PadButtonLabel(hint.button);
        // NICHT "LeftStick": das ist in PadButtonLabel der Stickklick (L3). Hier ist die ACHSE
        // gemeint, und ein Spieler, der beides nacheinander liest, muss den Unterschied sehen.
        case KeyInput::LeftStickAxis: return _("Left stick");
    }
    return "";
}

std::string KeyLine(const std::vector<KeyHint>& keys)
{
    std::string out;
    for(auto it = keys.begin(); it != keys.end();)
    {
        // Alle unmittelbar folgenden Eintraege mit DERSELBEN Wirkung gehoeren in einen Eintrag:
        // "Left/Right Adjust" statt zweimal dasselbe Wort. Heute trifft das genau das
        // Steuerkreuz auf einer Werteachse.
        auto last = it;
        while(last + 1 != keys.end() && (last + 1)->action == it->action)
            ++last;
        if(!out.empty())
            out += "  -  ";
        for(auto cur = it; cur != last + 1; ++cur)
        {
            if(cur != it)
                out += '/';
            out += KeyInputLabel(*cur);
        }
        out += ' ';
        out += KeyLabel(it->action);
        it = last + 1;
    }
    return out;
}

std::vector<KeyHint> HintsFor(const KeyContext& ctx)
{
    std::vector<KeyHint> out;
    const auto add = [&out](const PadButton button, const KeyAction action) {
        out.push_back(KeyHint{button, action, KeyInput::Button});
    };
    const auto addStick = [&out](const KeyAction action) {
        out.push_back(KeyHint{PadButton{}, action, KeyInput::LeftStickAxis});
    };

    // BEIM ZUSCHAUEN ist genau ein Knopf belegt - dskGameInterface::OnPadButton kehrt fuer
    // jeden anderen wirkungslos zurueck. Diese eine Zeile IST der sichtbare Ausgang.
    if(ctx.watchOnly)
    {
        add(PadButton::B, KeyAction::LeaveWatchOnly);
        return out;
    }
    // DER RING - Phase 13. Er wird VOR dem Fenster gefragt, weil OnPadButton ihn vor dem Fokus
    // fragt; die Reihenfolge hier ist woertlich die Reihenfolge dort.
    if(ctx.ringOpen)
    {
        // DER LINKE STICK ZUERST - Befund K2/4E. Er ist das Hauptzeigemittel des Rings, und er
        // stand nirgends: die Leiste konnte ihn konstruktiv nicht nennen, weil ein
        // Stickausschlag kein PadButton ist. Jetzt kann sie es (KeyInput::LeftStickAxis), und
        // damit steht der Eingang, mit dem der Spieler den Ring ueberhaupt bedient, an erster
        // Stelle - dort, wo ein Anfaenger zuerst hinsieht.
        addStick(KeyAction::AimRing);
        // A loest den gewaehlten Sektor aus - dieselbe Frage wie im Fenster (Window::CanActivate),
        // denn es ist derselbe Aufruf (FocusPath::Activate).
        if(ctx.focusCanActivate)
            add(PadButton::A, KeyAction::Choose);
        // DAS GANZE STEUERKREUZ, nicht die Haelfte davon - Befund K2/4C und 4D. Gemessen
        // wirkten alle VIER Richtungen (RingOnPadButton: Left und Up einen Sektor zurueck,
        // Right und Down einen weiter), genannt waren nur Left und Right. KeyLine zieht die
        // vier zu einem Eintrag zusammen ("Left/Right/Up/Down Drehen"), die WERTE bleiben
        // getrennt, damit ein Nachweis jede Richtung einzeln druecken kann.
        add(PadButton::DpadLeft, KeyAction::TurnRing);
        add(PadButton::DpadRight, KeyAction::TurnRing);
        add(PadButton::DpadUp, KeyAction::TurnRing);
        add(PadButton::DpadDown, KeyAction::TurnRing);
        // LB und RB blaettern - aber nur, wenn es ueberhaupt etwas zu blaettern gibt. Ein
        // einseitiger Ring (das Systemmenue) nennt sie deshalb nicht, UND dort tun sie seit
        // Befund K2/4A auch wirklich nichts mehr: dskGameInterface::RingTurnPage fragt fuer
        // seine Wirkung dieselbe Funktion, aus der `ringHasPages` kommt.
        if(ctx.ringHasPages)
        {
            add(PadButton::RightShoulder, KeyAction::RingNextPage);
            add(PadButton::LeftShoulder, KeyAction::RingPrevPage);
        }
        // B UND BACK schliessen den Ring samt Fenster - beide, und beide werden genannt
        // (Befund K2/4B: Back wirkte ungenannt). RingOnPadButton behandelt sie in EINEM Zweig,
        // die Leiste nennt sie in EINEM Eintrag: "B/Back Schliessen".
        add(PadButton::B, KeyAction::CloseRing);
        add(PadButton::Back, KeyAction::CloseRing);
        return out;
    }
    // Die drei Zustaende sind GENAU die drei Zweige von RefreshBrief und damit genau die
    // Reihenfolge, in der dskGameInterface::OnPadButton entscheidet. Eine vierte Ableitung gibt
    // es bewusst nicht: sie koennte neben der ersten veralten.
    if(ctx.inWindow)
    {
        // FocusPath::OnPadButton verbraucht JEDE Flanke, sobald eine Wurzel gesetzt ist -
        // ausser Back und Y, die OnPadButton davor abfragt.
        //
        // BEFUND B1: hier stand bis Phase 12 eine FESTE Liste. Sie zeigte in jedem
        // Fensterzustand woertlich dasselbe, fragte das fokussierte Control nie und log damit
        // an zwei gemessenen Stellen - "A Waehlen" auf einem Schieberegler, wo A nichts tut,
        // und Schweigen ueber das Steuerkreuz, wo es wirkt. Jede Zeile hier fragt jetzt GENAU
        // den Wert, an dem auch der Knopf selbst entscheidet.
        if(ctx.focusCanActivate)
            add(PadButton::A, KeyAction::Choose);
        // DAS STEUERKREUZ, RICHTUNG FUER RICHTUNG - Befund N4. Vorher stand es nur auf einer
        // Werteachse da; auf der Gegenachse und in jedem gewoehnlichen Fenster BEWEGT es den
        // Fokus, und davon stand nichts. Jede der vier Richtungen ist einzeln gefragt worden,
        // und zwar mit FocusPath::PeekStep - derselben Rechnung, die auch der Druck ausfuehrt.
        //
        // Die Reihenfolge Links, Rechts, Hoch, Runter ist die, in der KeyLine gleiche
        // Nachbarn zusammenzieht: "Left/Right Adjust  -  Up/Down Move".
        const auto addDpad = [&add](const PadButton button, const DpadEffect effect) {
            if(effect == DpadEffect::AdjustValue)
                add(button, KeyAction::AdjustValue);
            else if(effect == DpadEffect::MoveFocus)
                add(button, KeyAction::MoveFocus);
        };
        addDpad(PadButton::DpadLeft, ctx.dpadLeft);
        addDpad(PadButton::DpadRight, ctx.dpadRight);
        addDpad(PadButton::DpadUp, ctx.dpadUp);
        addDpad(PadButton::DpadDown, ctx.dpadDown);
        // BEFUND B3: Y wird im Fenster VOR dem Fokus abgefragt (dskGameInterface::OnPadButton)
        // und fuehrt in ein NEU obenauf gelegtes Fenster - Postfenster -> Tagebuch,
        // Hauptauswahl -> Statistik. Das ist der Fall, den Phase 11 eigens gebaut hat, und die
        // Leiste schwieg genau dort.
        if(ctx.canEnterWindow)
            add(PadButton::Y, KeyAction::EnterWindow);
        // Dir::Next kennt keinen Umlauf: auf der letzten Fokusstation tut die Schulter nichts.
        if(ctx.focusHasNextStation)
            add(PadButton::RightShoulder, KeyAction::NextControl);
        // BEFUND N3: LB geht eine Station ZURUECK (FocusPath::OnPadButton, case LeftShoulder ->
        // Move(Dir::Prev)). Der Knopf wirkt seit Phase 4, stand aber nie da - waehrend RB
        // danebenstand. Dieselbe Frage wie bei RB, nur in der Gegenrichtung.
        if(ctx.focusHasPrevStation)
            add(PadButton::LeftShoulder, KeyAction::PrevControl);
        // B ist im Fenster IMMER belegt - es verwirft entweder eine offene Auswahlliste oder es
        // gibt den Fokus ab (FocusPath::OnPadButton: erst Cancel(), sonst Clear()). Was von
        // beidem, entscheidet das Control - und deshalb entscheidet es hier die Beschriftung.
        add(PadButton::B, ctx.focusCanCancelInput ? KeyAction::CancelChoice : KeyAction::LeaveFocus);
        if(ctx.canOpenSystemMenu)
            add(PadButton::Back, KeyAction::SystemMenu);
        return out;
    }
    if(ctx.roadMode)
    {
        // BEFUND P1 UND P1b, gemessen: hier stand "A Verlaengern" BEDINGUNGSLOS. Beides war
        // falsch, und zwar an den beiden haeufigsten Stellen ueberhaupt:
        //
        //  - Am WEGENDE tut A gar nichts. Das ist der erste Augenblick jedes Strassenbaus (A
        //    auf der eigenen Flagge, der Zeiger steht noch auf ihr) - der Anfaenger drueckt A,
        //    liest "A Verlaengern", und nichts geschieht: kein Schritt, keine Meldung.
        //  - Auf einem SCHON GELEGTEN Stueck baut A zurueck statt zu verlaengern.
        //
        // Gefragt wird jetzt dskGameInterface::PlanRoadStep - der Plan, den PadExtendRoad
        // ausfuehrt, und nicht eine zweite Bedingung daneben.
        switch(ctx.roadStep)
        {
            case RoadStep::Extend: add(PadButton::A, KeyAction::ExtendRoad); break;
            case RoadStep::ShortenTo: add(PadButton::A, KeyAction::ShortenRoad); break;
            case RoadStep::None: break;
        }
        // PadCommitRoad lehnt unter zwei Kanten mit RoadTooShort ab und an einem Ende ohne
        // Flaggenplatz mit RoadEndBlocked. Beides sind Gruende, X hier NICHT zu versprechen.
        if(ctx.roadPieces >= 2 && ctx.roadCanEnd)
            add(PadButton::X, KeyAction::CommitRoad);
        // BEFUND N2, gemessen: die Y-Vorabfrage in dskGameInterface::OnPadButton hat KEINEN
        // Baumodusschutz - steht ein Fenster offen, fuehrt Y auch mitten im Strassenbau
        // hinein. Die Leiste zeigte hier nur "A Verlaengern - B Abbrechen".
        if(ctx.canEnterWindow)
            add(PadButton::Y, KeyAction::EnterWindow);
        // PadStepBackRoad: leere Strecke heisst Abbruch, sonst ein Stueck zurueck.
        add(PadButton::B, ctx.roadPieces > 0 ? KeyAction::StepBackRoad : KeyAction::CancelRoad);
        // Back, RB und LB sind im Baumodus ausdruecklich wirkungslos - dort ist der Modus die
        // Bedeutung. Also stehen sie auch nicht in der Leiste. Fuer Back ist das seit Befund N1
        // keine zweite Behauptung mehr: dskGameInterface::CanOpenSystemMenu liefert im Baumodus
        // selbst false, `ctx.canOpenSystemMenu` ist hier also ohnehin nicht gesetzt.
        return out;
    }
    // DIE WELT. Feste Reihenfolge A, X, Y, RB, LB, B, Back - XAG 112 verlangt, dass
    // wiederkehrende Bedienelemente "in derselben relativen Reihenfolge an derselben Stelle"
    // erscheinen. Was fehlt, faellt heraus; was da ist, rueckt nicht.
    //
    // A ist eine KASKADE (OnPadButton, case A), und die Reihenfolge hier ist dieselbe - sonst
    // verspraeche die Leiste den zweiten Zweig, wo schon der erste greift.
    if(ctx.canOpenObjectWindow)
        add(PadButton::A, KeyAction::OpenWindow);
    else if(ctx.verdict == NodeVerdict::OwnFlag || ctx.verdict == NodeVerdict::OwnHQFlag)
        add(PadButton::A, KeyAction::StartRoad);
    else if(ctx.actionMenu != ActionMenuKind::None)
        add(PadButton::A, ActionMenuAction(ctx.actionMenu));
    const bool aOpensActions =
      !out.empty() && ctx.actionMenu != ActionMenuKind::None && out.front().action == ActionMenuAction(ctx.actionMenu);

    if(ctx.canPlaceFlag)
        add(PadButton::X, KeyAction::PlaceFlag);

    // DER EINTRAG, UM DEN ES IN DIESER PHASE GEHT. Steht ein Fenster offen und der Fokus noch in
    // der Welt, ist Y der einzige Weg hinein - PadOpenActionWindow setzt bewusst keinen Fokus.
    // Der Auftraggeber ist genau hier steckengeblieben: das Aktionsfenster stand offen, der
    // Geologenknopf war sichtbar, und nichts auf dem Bildschirm nannte den Knopf, der hineinfuehrt.
    //
    // Die Weltzeilen bleiben dabei stehen und werden NICHT verdraengt: ein offenes Fenster
    // hindert den Padspieler an nichts, A und X wirken weiter auf den Knoten unter dem Zeiger
    // (OnPadButton faellt erst dann in den Fokus, wenn eine Wurzel gesetzt ist).
    if(ctx.canEnterWindow)
        add(PadButton::Y, KeyAction::EnterWindow);

    // RB steht nur da, wo es etwas tut, das A nicht schon tut. Auf einer eigenen Flagge ist das
    // der Fall - dort faengt A den Strassenbau an und kommt gar nicht bis zum Aktionsfenster.
    // Genau diese Stelle hat der Auftraggeber gesucht: hier liegen Geologe und Spaeher.
    if(ctx.actionMenu != ActionMenuKind::None && !aOpensActions)
        add(PadButton::RightShoulder, ActionMenuAction(ctx.actionMenu));

    if(ctx.canStartWaterway)
        add(PadButton::LeftShoulder, KeyAction::StartWaterway);

    // B in der Welt schliesst das oberste eigene Fenster - und tut ohne ein solches nichts.
    //
    // BEFUND B2: `canCloseWindow` ist NICHT dasselbe wie "ein Fenster ist offen". Das
    // Beobachtungsfenster traegt CloseBehavior::NoRightClick, ein angeheftetes Fenster laesst
    // sich ebenfalls nicht so schliessen - dort versprach die Leiste "B Schliessen", und der
    // Druck tat nichts.
    if(ctx.canCloseWindow)
        add(PadButton::B, KeyAction::CloseWindow);

    if(ctx.canOpenSystemMenu)
        add(PadButton::Back, KeyAction::SystemMenu);
    return out;
}

namespace {

    /// Flaeche der Ueberschneidung zweier Rechtecke. 0 heisst "beruehren sich nicht".
    ///
    /// Eigene kleine Rechnung statt Rect-Hilfsmitteln, weil Rect keine hat und weil das Ergebnis
    /// hier als ZAHL gebraucht wird und nicht als ja/nein: verdecken beide Plaetze etwas, gewinnt
    /// der mit der kleineren Ueberschneidung.
    long overlapArea(const Rect& a, const Rect& b)
    {
        const long w = std::min(a.right, b.right) - std::max(a.left, b.left);
        const long h = std::min(a.bottom, b.bottom) - std::max(a.top, b.top);
        return (w > 0 && h > 0) ? w * h : 0;
    }

} // namespace

Rect PanelRect(const Rect& viewport, const Rect& safeArea, const unsigned numLines, const unsigned lineHeight,
               const Rect& avoid)
{
    constexpr int margin = 6;
    constexpr int padding = 4;

    const int height = static_cast<int>(numLines * lineHeight) + 2 * padding;

    // Waagerecht: die Breite dieser Ansicht, aber nie ueber den Rand des BILDSCHIRMS hinaus.
    int left = std::max(viewport.left, safeArea.left) + margin;
    int right = std::min(viewport.right, safeArea.right) - margin;
    // Sichtbarkeit schlaegt Randschutz - dieselbe Regel wie in tv::WindowBoundsRect. Bleibt von
    // der Ueberschneidung nichts uebrig (eine Ansicht ganz im Overscanbereich), gilt der
    // Viewport allein; ein Kasten der Breite null waere kein Schutz, sondern ein weisser Fleck.
    if(right <= left)
    {
        left = viewport.left + margin;
        right = viewport.right - margin;
    }
    if(right < left)
        right = left;
    const auto width = static_cast<unsigned>(right - left);

    // Aus einer Ober- und einer Unterkante ein Rechteck machen, beides in den Viewport geklemmt.
    // Die Waagerechte ist fuer beide Plaetze dieselbe - deshalb liefern auch beide Aufrufe aus
    // DrawBrief (Probe und endgueltiger Kasten) dieselbe Textbreite.
    const auto box = [&](int boxTop, int boxBottom) {
        if(boxTop < viewport.top)
            boxTop = viewport.top;
        if(boxBottom > viewport.bottom)
            boxBottom = viewport.bottom;
        if(boxBottom < boxTop)
            boxBottom = boxTop;
        return Rect(Position(left, boxTop), Extent(width, static_cast<unsigned>(boxBottom - boxTop)));
    };

    // Platz 1, der Regelfall: unten in dieser Ansicht, aber nie unterhalb der Safe Area.
    int bottom = std::min(viewport.bottom, safeArea.bottom) - margin;
    if(bottom - height < viewport.top)
        bottom = viewport.bottom - margin; // s.o.: lieber angeschnitten als unsichtbar
    const Rect atBottom = box(bottom - height, bottom);
    if(overlapArea(atBottom, avoid) == 0)
        return atBottom;

    // Platz 2: oben in dieser Ansicht, nach derselben Regel und mit derselben Ausnahme.
    int top = std::max(viewport.top, safeArea.top) + margin;
    if(top + height > viewport.bottom)
        top = viewport.top + margin;
    const Rect atTop = box(top, top + height);

    // Kein freier Platz - dann der mit der kleineren Ueberdeckung. Erreichbar ist das, sobald das
    // Hindernis hoeher ist als der Abstand der beiden Plaetze. Das Aktionsfenster ist 254 hoch,
    // der Abstand ist bei vier Ansichten auf 1080p 314 Punkte (dort also nicht erreichbar) und
    // auf 1280x720 nur 152 (dort schon). Die Grenze liegt bei 952 Zeilen Bildhoehe; die Rechnung
    // und die Messung stehen im Kopf von PlayerBrief.h. Ein "sonst gar nichts zeichnen" waere
    // hier falsch: ein Kasten, dem ein Fuenftel fehlt, ist immer noch vier Fuenftel Auskunft.
    return overlapArea(atTop, avoid) < overlapArea(atBottom, avoid) ? atTop : atBottom;
}

} // namespace brief
