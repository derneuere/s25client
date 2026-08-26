// Copyright (C) 2005 - 2024 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "desktops/Desktop.h"
#include "network/CreateServerInfo.h"
#include <boost/signals2/connection.hpp>

struct CampaignDescription;

class dskCampaignMissionSelection : public Desktop
{
public:
    dskCampaignMissionSelection(CreateServerInfo csi, const CampaignDescription& campaign);

    /// Siehe dskCampaignSelection: ohne das holt der WindowManager hier keine Padereignisse ab.
    bool WantsPadInput() const override { return true; }
    /// B ist derselbe Zweig wie der Knopf "Zurueck".
    bool Msg_PadCommand(unsigned slot, PadButton button) override;
    /// Ein Pad faengt dort an, wo die MISSION gewaehlt wird - auf der Weltkarte bzw. auf dem
    /// ersten Missionsknopf der aktuellen Seite. "Zurueck" hat die kleinste Id und gewaenne
    /// sonst; bei zehn Missionsknoepfen waere das eine lange Reise in die falsche Richtung.
    Window* GetPadEntryCtrl(unsigned slot) override;

private:
    void UpdateMissionPage();
    void Msg_ButtonClick(unsigned ctrl_id) override;
    void Msg_Group_ButtonClick(unsigned group_id, unsigned ctrl_id) override;
    void StartServer(unsigned missionIdx);
    void UpdateStateOfNavigationButtons();
    CreateServerInfo csi_;
    std::unique_ptr<CampaignDescription> campaign_;
    const unsigned missionsPerPage_;
    /// current page (zero based) in the paged mission list
    unsigned currentPage_;
    /// last page in the paged mission list
    const unsigned lastPage_;
    boost::signals2::scoped_connection onErrorConnection_;
};
