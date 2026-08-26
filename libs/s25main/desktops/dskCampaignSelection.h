// Copyright (C) 2005 - 2024 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "desktops/Desktop.h"
#include "network/CreateServerInfo.h"
#include <boost/filesystem/path.hpp>
#include <vector>

struct CampaignDescription;

class dskCampaignSelection : public Desktop
{
public:
    dskCampaignSelection(CreateServerInfo csi);
    ~dskCampaignSelection() noexcept;

    /// Die Kampagnenauswahl liegt mitten auf dem Weg in eine Partie und war bisher der einzige
    /// Bildschirm dieses Weges, den der WindowManager gar nicht erst mit Padereignissen
    /// versorgte (WindowManager::PumpPadInput fragt WantsPadInput). Wer mit dem Pad hierher
    /// kam, kam mit dem Pad nicht mehr weiter.
    bool WantsPadInput() const override { return true; }
    /// B ist derselbe Zweig wie der Knopf "Zurueck".
    bool Msg_PadCommand(unsigned slot, PadButton button) override;
    /// Ein Pad faengt auf der Kampagnentabelle an und nicht auf "Zurueck". Die Tabelle
    /// entsteht erst aus dem Timer unten, deshalb darf der Einstieg nachziehen
    /// (MenuPadInput::focusUntouched_).
    Window* GetPadEntryCtrl(unsigned slot) override;

protected:
    void Draw_() override;

private:
    class CampaignDataHolder;

    void Msg_TableChooseItem(unsigned, unsigned) override;
    void Msg_TableSelectItem(unsigned ctrl_id, const std::optional<unsigned>& selection) override;
    void Msg_ButtonClick(unsigned ctrl_id) override;
    void Msg_Timer(unsigned ctrl_id) override;
    void FillCampaignsTable();
    void showCampaignInfo(bool show);
    void showCampaignMissionSelectionScreen();
    void loadCampaigns();

    CreateServerInfo csi_;
    glArchivItem_Bitmap* campaignImage_ = nullptr;
    std::vector<CampaignDescription> campaigns_;
};
