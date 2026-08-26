// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "iwPadSystemMenu.h"
#include "Loader.h"
#include "controls/ctrlTextButton.h"
#include "desktops/PlayerView.h"
#include "desktops/dskGameInterface.h"
#include "ogl/glFont.h"
#include "world/GameWorldView.h"
#include "gameData/const_gui_ids.h"

namespace {
/// Breite des Textknopfes. Bewusst grosszuegig: die Beschriftungen sind ganze Aussagesaetze
/// ("Bauhilfe: an"), und sie sollen aus drei Metern lesbar sein - das ist der ganze Zweck
/// dieses Fensters gegenueber der Symbolleiste.
constexpr Extent btnSize(232, 26);
constexpr int margin = 8;
constexpr int gap = 4;
constexpr unsigned numButtons = 5;
} // namespace

iwPadSystemMenu::iwPadSystemMenu(dskGameInterface& dsk, PlayerView& view, const DrawPoint& pos)
    : IngameWindow(CGI_PADMENU, pos,
                   Extent(btnSize.x + 2 * margin, numButtons * (btnSize.y + gap) - gap + 2 * margin), _("Menu"),
                   LOADER.GetImageN("resource", 41)),
      dsk_(dsk), view_(view), lastShowBQ_(view.GetView().IsShowingBQ()),
      lastShowNames_(view.GetView().IsShowingNamesAndProductivity())
{
    // Die Tooltips sind hier KEIN Mauskomfort, sondern der Klartext des Padspielers:
    // brief::ForControl liest den Tooltip des fokussierten Controls und schreibt ihn in den
    // Kasten unter DESSEN Viewport (PlayerBrief.cpp). Ein Padspieler bekommt den Satz also,
    // ohne je eine Maus zu bewegen - genau der Weg, den Phase 9 fuer die Gebaeude gebaut hat.
    DrawPoint p(margin, margin);
    AddTextButton(ID_MINIMAP, p, btnSize, TextureColor::Green2, _("Outline map"), NormalFont,
                  _("Shows the whole map in small. Useful to see where your land ends."));
    p.y += btnSize.y + gap;
    AddTextButton(ID_POST, p, btnSize, TextureColor::Green2, _("Post office"), NormalFont,
                  _("Your messages: why a mine stopped, where ore was found, who is attacking - and the diary of the "
                    "campaign."));
    p.y += btnSize.y + gap;
    AddTextButton(ID_CONSTRUCTION_AID, p, btnSize, TextureColor::Green2, "", NormalFont,
                  _("Draws a symbol on every spot: what fits there - a flag, a hut, a house, a castle or a mine."));
    p.y += btnSize.y + gap;
    AddTextButton(ID_NAMES_PRODUCTIVITY, p, btnSize, TextureColor::Green2, "", NormalFont,
                  _("Writes the name and the output of every building onto the map."));
    p.y += btnSize.y + gap;
    AddTextButton(ID_MAIN_SELECTION, p, btnSize, TextureColor::Green2, _("Main selection"), NormalFont,
                  _("Statistics, goods, tools, military - and saving, giving up and leaving the game."));

    UpdateToggleLabels();
}

void iwPadSystemMenu::UpdateToggleLabels()
{
    lastShowBQ_ = view_.GetView().IsShowingBQ();
    lastShowNames_ = view_.GetView().IsShowingNamesAndProductivity();
    GetCtrl<ctrlTextButton>(ID_CONSTRUCTION_AID)
      ->SetText(lastShowBQ_ ? _("Construction aid: on") : _("Construction aid: off"));
    GetCtrl<ctrlTextButton>(ID_NAMES_PRODUCTIVITY)
      ->SetText(lastShowNames_ ? _("Names and output: on") : _("Names and output: off"));
}

void iwPadSystemMenu::Msg_PaintBefore()
{
    IngameWindow::Msg_PaintBefore();
    // Die beiden Schalter lassen sich auch ANDERSWO umlegen (Leertaste des Mausspielers, Reiter
    // "Anzeigeoptionen" im Aktionsfenster, erzwungene Bauhilfe beim Oeffnen des Baumenues).
    // Stuende die Beschriftung nur beim Bau fest, behauptete das Menue danach das Gegenteil
    // dessen, was der Spieler sieht.
    if(lastShowBQ_ != view_.GetView().IsShowingBQ()
       || lastShowNames_ != view_.GetView().IsShowingNamesAndProductivity())
        UpdateToggleLabels();
}

void iwPadSystemMenu::Msg_ButtonClick(const unsigned ctrl_id)
{
    // KEINE eigene Regel: jeder Fall ruft genau die Methode, die auch der Mausknopf der
    // Knopfleiste ruft (dskGameInterface::Msg_ButtonClick). Der Unterschied ist allein die
    // ANSICHT, auf die sie wirkt - dort primary(), hier der Sitzplatz, dem dieses Fenster
    // gehoert.
    switch(ctrl_id)
    {
        // Die drei fensteroeffnenden Punkte SCHLIESSEN das Menue und geben den Fokus in das
        // neue Fenster (PadMenuLeaveTo). Ab hier lebt dieses Objekt nur noch bis zum naechsten
        // WindowManager::Draw - es wird deshalb danach nichts mehr angefasst.
        case ID_MINIMAP: dsk_.PadMenuLeaveTo(view_, dsk_.OpenMinimapFor(view_)); break;
        case ID_POST: dsk_.PadMenuLeaveTo(view_, dsk_.OpenPostOfficeFor(view_)); break;
        case ID_MAIN_SELECTION: dsk_.PadMenuLeaveTo(view_, dsk_.OpenMainMenuFor(view_)); break;
        // Die beiden ANZEIGESCHALTER lassen das Menue stehen: der Spieler soll die
        // Beschriftung umspringen sehen (das ist die Rueckmeldung, dass etwas passiert ist -
        // die Symbole selbst liegen unter dem Menue) und gleich noch den zweiten legen koennen.
        case ID_CONSTRUCTION_AID:
            dsk_.ToggleConstructionAidFor(view_);
            UpdateToggleLabels();
            break;
        case ID_NAMES_PRODUCTIVITY:
            dsk_.ToggleNamesAndProductivityFor(view_);
            UpdateToggleLabels();
            break;
    }
}
