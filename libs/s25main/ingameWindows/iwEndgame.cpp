// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "iwEndgame.h"
#include "GameManager.h"
#include "Loader.h"
#include "WindowManager.h"
#include "iwSave.h"
#include "gameData/const_gui_ids.h"

iwEndgame::iwEndgame()
    : IngameWindow(CGI_ENDGAME, IngameWindow::posLastOrCenter, Extent(240, 100), _("End game?"),
                   LOADER.GetImageN("resource", 41))
{
    // Ok
    AddImageButton(0, DrawPoint(16, 24), Extent(71, 57), TextureColor::Green2, LOADER.GetImageN("io", 32)); //-V525
    // Abbrechen
    AddImageButton(1, DrawPoint(88, 24), Extent(71, 57), TextureColor::Red1, LOADER.GetImageN("io", 40));
    // Ok + Speichern
    AddImageButton(2, DrawPoint(160, 24), Extent(65, 57), TextureColor::Grey, LOADER.GetImageN("io", 47));

    // BEFUND A: die Fensterkennung ist (GUI_ID, Besitzer). Dieser Dialog aber kann aus ZWEI
    // Pfaden entstehen - ALT+Q laeuft ueber dskGameInterface::Msg_KeyDown und damit unter der
    // Klammer der Hauptansicht, der Knopf "End game" in iwVictory unter dem Besitzer von
    // iwVictory, und das entsteht in GI_Winner ohne jede Klammer. Ohne diese Zeile stuenden
    // beide Dialoge gleichzeitig auf dem Bildschirm.
    //
    // Die richtige Antwort ist nicht, dem Siegdialog einen Besitzer zu geben (dann koennte ihn
    // der zweite lokale Spieler nicht mehr bedienen), sondern festzuhalten, was hier ohnehin
    // gilt: das Spiel zu beenden betrifft den BILDSCHIRM und nicht einen Sitzplatz. Deshalb
    // gehoert dieser Dialog immer dem Bildschirm, aus welchem Pfad er auch stammt.
    SetOwner(SHARED_WINDOW_OWNER);
}

void iwEndgame::Msg_ButtonClick(const unsigned ctrl_id)
{
    switch(ctrl_id)
    {
        case 0: // OK
        {
            GAMEMANAGER.ShowMenu();
        }
        break;
        case 1: // Abbrechen
        {
            Close();
        }
        break;
        case 2: // OK + Speichern
        {
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwSave>());
        }
        break;
    }
}
