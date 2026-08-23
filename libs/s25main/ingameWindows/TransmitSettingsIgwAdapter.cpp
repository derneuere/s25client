// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TransmitSettingsIgwAdapter.h"

#include "WindowManager.h"
#include "iwMsgbox.h"
#include "network/GameClient.h"
#include <mygettext/mygettext.h>

TransmitSettingsIgwAdapter::TransmitSettingsIgwAdapter(unsigned id, const DrawPoint& pos, const Extent& size,
                                                       const std::string& title, glArchivItem_Bitmap* background,
                                                       bool modal)
    : IngameWindow(id, pos, size, title, background, modal), settingsChanged_(false)
{
    // Timer for transmitting changes every 2 seconds
    using namespace std::chrono_literals;
    AddTimer(firstCtrlID + 1u, 2s);
}

void TransmitSettingsIgwAdapter::MarkSettingsChanged()
{
    settingsChanged_ = true;
    // Der handelnde Spieler wird JETZT festgehalten, im Moment der Aenderung. Beim Senden -
    // zwei Sekunden spaeter oder beim Schliessen - weiss niemand mehr, wer den Regler bewegt
    // hat: der Eingabepfad ist dann laengst zurueckgekehrt.
    pendingPlayer_ = GAMECLIENT.GetActingPlayer();
}

void TransmitSettingsIgwAdapter::OnSettingsTransmitted()
{
    settingsChanged_ = false;
    pendingPlayer_.reset();
}

void TransmitSettingsIgwAdapter::TransmitSettingsForPendingPlayer()
{
    // Auch dann klammern, wenn nichts gemerkt ist: nullopt heisst dann ausdruecklich
    // "Hauptspieler". Ohne das schluege eine zufaellig offene fremde Klammer durch - etwa
    // wenn ein Padspieler ueber einen Knopf das Fenster eines anderen schliesst.
    const GameClient::ScopedActingPlayer acting(GAMECLIENT, pendingPlayer_);
    TransmitSettings();
}

void TransmitSettingsIgwAdapter::Close()
{
    TransmitSettingsForPendingPlayer();
    if(!HasPendingSettings())
    {
        IngameWindow::Close();
    } else
    {
        WINDOWMANAGER.Show(std::make_unique<iwMsgbox>(GetTitle(),
                                                      _("The changes could not be applied and will be discarded. A "
                                                        "potential reason for this is that the game is paused."),
                                                      this, MsgboxButton::OkCancel, MsgboxIcon::ExclamationRed,
                                                      firstCtrlID));
    }
}

void TransmitSettingsIgwAdapter::Msg_MsgBoxResult(unsigned msgbox_id, MsgboxResult mbr)
{
    if(msgbox_id == firstCtrlID && mbr == MsgboxResult::Ok)
        IngameWindow::Close();
}

void TransmitSettingsIgwAdapter::Msg_Timer(const unsigned /*ctrl_id*/)
{
    if(GAMECLIENT.IsReplayModeOn())
        UpdateSettings();
    else
        TransmitSettingsForPendingPlayer();
}
