// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "iwPadSystemMenu.h"
#include "Loader.h"
#include "controls/ctrlTextButton.h"
#include "desktops/PlayerView.h"
#include "desktops/dskGameInterface.h"
#include "input/PlayerBrief.h"
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
constexpr unsigned numButtons = 7;
} // namespace

iwPadSystemMenu::iwPadSystemMenu(dskGameInterface& dsk, PlayerView& view, const DrawPoint& pos)
    : IngameWindow(CGI_PADMENU, pos,
                   Extent(btnSize.x + 2 * margin, numButtons * (btnSize.y + gap) - gap + 2 * margin), _("Menu"),
                   LOADER.GetImageN("resource", 41)),
      dsk_(dsk), view_(view), lastShowBQ_(view.GetView().GetBqMode()),
      lastShowNames_(view.GetView().IsShowingNames()),
      lastShowProductivity_(view.GetView().IsShowingProductivity()), lastWatchOnly_(view.IsWatchOnly())
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
    AddTextButton(
      ID_CONSTRUCTION_AID, p, btnSize, TextureColor::Green2, "", NormalFont,
      // WELLE 14: der Satz nennt jetzt die DREI Stufen. Er ist der einzige Ort, an dem ein
      // Anfaenger erfaehrt, dass es die mittlere ueberhaupt gibt - der Ringsektor traegt nur
      // den Zustandsnamen, der Kasten darunter traegt die Erklaerung (brief::ForControl).
      _("What fits on a spot - a flag, a hut, a house, a castle or a mine. Three steps: off, only under "
        "your pointer, or on every spot of the map."));
    p.y += btnSize.y + gap;
    AddTextButton(ID_NAMES, p, btnSize, TextureColor::Green2, "", NormalFont,
                  _("Writes the name of every building onto the map. Useful while you are still learning "
                    "which building is which."));
    p.y += btnSize.y + gap;
    AddTextButton(ID_PRODUCTIVITY, p, btnSize, TextureColor::Green2, "", NormalFont,
                  _("Writes the output of every building onto the map: how busy it is, how many soldiers are "
                    "inside, how far a construction site has come."));
    p.y += btnSize.y + gap;
    AddTextButton(ID_WATCH_ONLY, p, btnSize, TextureColor::Green2, "", NormalFont,
                  _("Hides every symbol and every text of this seat, so you can just look at your land. The "
                    "camera and the zoom keep working. B brings everything back."));
    p.y += btnSize.y + gap;
    AddTextButton(ID_MAIN_SELECTION, p, btnSize, TextureColor::Green2, _("Main selection"), NormalFont,
                  _("Statistics, goods, tools, military - and saving, giving up and leaving the game."));

    UpdateToggleLabels();
}

void iwPadSystemMenu::UpdateToggleLabels()
{
    lastShowBQ_ = view_.GetView().GetBqMode();
    lastShowNames_ = view_.GetView().IsShowingNames();
    lastShowProductivity_ = view_.GetView().IsShowingProductivity();
    lastWatchOnly_ = view_.IsWatchOnly();
    // Die Beschriftungen sind KURZ, weil sie im Kreismenue in einem Sektorbogen stehen (rund
    // 115 Punkte bei acht Sektoren). Der erklaerende Satz steht weiterhin im Klartextkasten
    // darunter - er kommt ueber den Tooltip dieses Knopfes dorthin (brief::ForControl) und ist
    // damit dieselbe Zeichenkette wie die des Mauspfades.
    // DREI Beschriftungen und nicht zwei (Welle 14). Sie nennen den ZUSTAND, weil der Spieler
    // sonst raten muss, wo im Umlauf er gerade steht - bei zwei Stufen genuegte "an/aus", bei
    // drei nicht mehr.
    //
    // BEFUND K4 DER WELLE 14, gemessen: "Construction aid: here" ist 264 Punkte breit,
    // "Bauhilfe: am Zeiger" 228 und "Bauhilfe: ueberall" 204. Bei 1280x720 im Fernsehmodus mit
    // VIER Ansichten ist neben dem Ring Platz fuer 184 - zwei deutsche Tuerschilder brachen dort
    // um. Die Nachbarschilder desselben Menues heissen "Names: on" und "Output: off", also EIN
    // kurzes Hauptwort; die Bauhilfe war der Ausreisser. Sie heisst deshalb jetzt genauso kurz.
    // Gemessen passen alle drei damit in EINE Zeile, an allen vier Sitzplaetzen, auf beiden
    // Fernsehgroessen und in sieben Sprachen (testPadRing:
    // NoRingLabelBreaksOnAnyTelevisionSizeAtFourSeats).
    GetCtrl<ctrlTextButton>(ID_CONSTRUCTION_AID)
      ->SetText(lastShowBQ_ == BqMode::Off    ? _("Build aid: off") :
                lastShowBQ_ == BqMode::Cursor ? _("Build aid: here") :
                                                _("Build aid: all"));
    GetCtrl<ctrlTextButton>(ID_NAMES)->SetText(lastShowNames_ ? _("Names: on") : _("Names: off"));
    GetCtrl<ctrlTextButton>(ID_PRODUCTIVITY)
      ->SetText(lastShowProductivity_ ? _("Output: on") : _("Output: off"));
    GetCtrl<ctrlTextButton>(ID_WATCH_ONLY)->SetText(_("Just watch"));
}

brief::Brief iwPadSystemMenu::GetPadBrief(const Window* const focused) const
{
    // DER TITEL IST DIE ANTWORT AUF EINE WORTLUECKE, nicht auf eine Weglueck: der Auftraggeber
    // sucht "die Symbole", und auf dem Schirm stand "Bauhilfe" und "Namen und Auslastung".
    // Keiner der beiden Begriffe enthaelt das Wort, mit dem er denkt. Der Ring nennt sich
    // deshalb nach dem, wonach gesucht wird.
    brief::Brief b = brief::ForControl(focused);
    b.title = _("What you see on the map");
    return b;
}

void iwPadSystemMenu::Msg_PaintBefore()
{
    IngameWindow::Msg_PaintBefore();
    // Die beiden Schalter lassen sich auch ANDERSWO umlegen (Leertaste des Mausspielers, Reiter
    // "Anzeigeoptionen" im Aktionsfenster, erzwungene Bauhilfe beim Oeffnen des Baumenues).
    // Stuende die Beschriftung nur beim Bau fest, behauptete das Menue danach das Gegenteil
    // dessen, was der Spieler sieht.
    if(lastShowBQ_ != view_.GetView().GetBqMode() || lastShowNames_ != view_.GetView().IsShowingNames()
       || lastShowProductivity_ != view_.GetView().IsShowingProductivity()
       || lastWatchOnly_ != view_.IsWatchOnly())
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
        // DREISTUFIG, nicht zweistufig (Welle 14): aus -> nur am Zeiger -> alles -> aus.
        // Woertlich der Wunsch des Auftraggebers. Der MAUSknopf bleibt zweistufig; die
        // Begruendung steht an dskGameInterface::CycleConstructionAidFor.
        case ID_CONSTRUCTION_AID:
            dsk_.CycleConstructionAidFor(view_);
            UpdateToggleLabels();
            break;
        case ID_NAMES:
            dsk_.ToggleNamesFor(view_);
            UpdateToggleLabels();
            break;
        case ID_PRODUCTIVITY:
            dsk_.ToggleProductivityFor(view_);
            UpdateToggleLabels();
            break;
        // "Nur zuschauen" SCHLIESST das Menue - anders als die drei Umschalter darueber, und
        // aus genau demselben Grund, aus dem jene stehenbleiben: was der Spieler danach sehen
        // will, ist die WELT. Ein Menue davor waere das Gegenteil der Bitte.
        case ID_WATCH_ONLY: dsk_.EnterWatchOnly(view_); break;
    }
}
