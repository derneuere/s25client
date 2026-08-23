// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "WindowManager.h"
#include "controls/ctrlTextButton.h"
#include "ingameWindows/IngameWindow.h"
#include "ogl/FontStyle.h"
#include "Loader.h"
#include "gameData/const_gui_ids.h"
#include <boost/test/unit_test.hpp>
#include <memory>
#include <vector>

namespace rttr::test {

/// Ein Fenster, das seinen eigenen Tod meldet und mitzaehlt, wie oft es gezeichnet wurde.
///
/// Beides ist noetig, weil der WindowManager seine Fensterliste nicht herausgibt: "das Fenster
/// des anderen Spielers wurde NICHT geschlossen" darf nicht ueber eine neu erfundene
/// Abfrage-API gemessen werden, die dieselbe Mutation gleich mit umlegen wuerde. Gemessen wird
/// deshalb an der Lebensdauer (alive_) und daran, dass WindowManager::Draw() das Fenster
/// weiterhin anfasst (paints).
struct OwnedWnd : IngameWindow
{
    OwnedWnd(unsigned id, bool& aliveFlag, const DrawPoint& pos = DrawPoint(0, 0))
        : IngameWindow(id, pos, Extent(200, 120), "", nullptr, false, CloseBehavior::Regular), alive_(aliveFlag)
    {
        alive_ = true;
        AddTextButton(1, DrawPoint(10, 10), Extent(80, 20), TextureColor::Green1, "A", NormalFont);
    }
    ~OwnedWnd() override { alive_ = false; }

    void Msg_PaintBefore() override
    {
        ++paints;
        IngameWindow::Msg_PaintBefore();
    }
    /// Ein Knopfdruck oeffnet ein Folgefenster - OHNE Elternzeiger und ohne jede Spielerangabe,
    /// genau wie iwMainMenu es fuer iwMilitary tut. Wem das Folgefenster gehoert, kann es also
    /// nur aus der offenen Klammer erfahren.
    void Msg_ButtonClick(unsigned) override
    {
        ++clicks;
        if(childAlive_)
            child = &WINDOWMANAGER.Show(std::make_unique<OwnedWnd>(childId_, *childAlive_, DrawPoint(300, 300)));
    }
    void OpenChildOnClick(unsigned childId, bool& childAlive)
    {
        childId_ = childId;
        childAlive_ = &childAlive;
    }

    unsigned paints = 0;
    unsigned clicks = 0;
    OwnedWnd* child = nullptr;

private:
    bool& alive_;
    unsigned childId_ = CGI_HELP;
    bool* childAlive_ = nullptr;
};

} // namespace rttr::test
