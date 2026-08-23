// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "desktops/PlayerView.h"
#include "GamePlayer.h"
#include "world/GameWorldBase.h"
#include <algorithm>

PlayerView::PlayerView(const unsigned viewIdx, const unsigned playerId, GameWorldBase& world,
                       const Viewport& viewport)
    : viewIdx_(viewIdx), worldViewer_(playerId, world), view_(worldViewer_, viewport.origin, viewport.size),
      minimap_(worldViewer_)
{
    road_.mode = RoadBuildMode::Disabled;
    road_.point = MapPoint(0, 0);
    road_.start = MapPoint(0, 0);
}

PlayerView::~PlayerView() = default;

void PlayerView::SetViewport(const Viewport& viewport)
{
    view_.SetPos(viewport.origin);
    view_.Resize(viewport.size);
}

bool PlayerView::ContainsViewPos(const Position& viewPos) const
{
    const Position origin = view_.GetPos();
    const Extent size = view_.GetSize();
    return viewPos.x >= origin.x && viewPos.y >= origin.y && viewPos.x < origin.x + static_cast<int>(size.x)
           && viewPos.y < origin.y + static_cast<int>(size.y);
}

Position PlayerView::GetViewCenter() const
{
    return view_.GetPos() + Position(view_.GetSize() / 2u);
}

Position PlayerView::ClampToView(Position viewPos) const
{
    const Position origin = view_.GetPos();
    const Extent size = view_.GetSize();
    // Eine Ansicht ohne Ausdehnung gibt es nicht (CalcViewports liefert immer >= 1 Pixel), der
    // Rand ist deshalb origin + size - 1 und nie kleiner als origin.
    const Position maxPt(origin.x + static_cast<int>(size.x) - 1, origin.y + static_cast<int>(size.y) - 1);
    viewPos.x = std::clamp(viewPos.x, origin.x, std::max(origin.x, maxPt.x));
    viewPos.y = std::clamp(viewPos.y, origin.y, std::max(origin.y, maxPt.y));
    return viewPos;
}

void PlayerView::RecalcAllColors()
{
    worldViewer_.RecalcAllColors();
    minimap_.UpdateAll();
}

void PlayerView::MoveToOwnHQ()
{
    if(worldViewer_.GetPlayer().GetHQPos().isValid())
        view_.MoveToMapPt(worldViewer_.GetPlayer().GetHQPos());
}
