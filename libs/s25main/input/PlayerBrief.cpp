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
