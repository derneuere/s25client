// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "iwSurrender.h"
#include "Loader.h"
#include "WindowManager.h"
#include "network/GameClient.h"
#include "gameData/const_gui_ids.h"

iwSurrender::iwSurrender()
    : IngameWindow(CGI_ENDGAME, IngameWindow::posLastOrCenter, Extent(240, 100), _("Surrender game?"),
                   LOADER.GetImageN("resource", 41))
{
    // Ok
    AddImageButton(0, DrawPoint(85, 24), Extent(68, 57), TextureColor::Green2, LOADER.GetImageN("io", 32),
                   _("Surrender"));
    // Ok + Abbrennen
    AddImageButton(2, DrawPoint(16, 24), Extent(68, 57), TextureColor::Green2, LOADER.GetImageN("io", 23),
                   _("Destroy all buildings and surrender"));
    // Abbrechen
    AddImageButton(1, DrawPoint(158, 24), Extent(68, 57), TextureColor::Red1, LOADER.GetImageN("io", 40),
                   _("Don't surrender"));

    // Teilt sich die GUI_ID mit iwEndgame (beides CGI_ENDGAME) und muss deshalb dieselbe
    // Besitzregel haben - sonst waeren die beiden Dialoge gegeneinander nicht mehr
    // deduplizierbar. Siehe die Begruendung in iwEndgame.cpp.
    SetOwner(SHARED_WINDOW_OWNER);
}

void iwSurrender::Msg_ButtonClick(const unsigned ctrl_id)
{
    switch(ctrl_id)
    {
        case 0: // OK
        {
            if(GAMECLIENT.Surrender())
                Close();
        }
        break;
        case 1: // Abbrechen
        {
            Close();
        }
        break;
        case 2: // OK + Alles abbrennen
        {
            if(GAMECLIENT.DestroyAll())
                Close();
        }
        break;
    }
}
