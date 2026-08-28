// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "input/PadRing.h"
#include <cmath>

namespace padring {

namespace {
constexpr float Pi = 3.14159265358979323846f;
float ToRad(float deg)
{
    return deg * Pi / 180.f;
}
/// Auf [0,360) normieren.
float Wrap360(float deg)
{
    deg = std::fmod(deg, 360.f);
    if(deg < 0.f)
        deg += 360.f;
    return deg;
}
} // namespace

std::vector<Sector> MakeSectors(const unsigned count)
{
    std::vector<Sector> out;
    if(count == 0)
        return out;
    const float width = 360.f / static_cast<float>(count);
    for(unsigned i = 0; i < count; ++i)
    {
        // -90 Grad ist oben (y waechst nach unten). Der ERSTE Eintrag sitzt mittig oben, der
        // Sektor reicht eine halbe Breite nach links und nach rechts.
        const float mid = -90.f + width * static_cast<float>(i);
        out.push_back(Sector{mid - width / 2.f, mid + width / 2.f});
    }
    return out;
}

int SectorAt(const unsigned count, const PointF aim)
{
    if(count == 0)
        return -1;
    if(aim.x == 0.f && aim.y == 0.f) //-V550
        return -1;
    const float angle = std::atan2(aim.y, aim.x) * 180.f / Pi;
    const float width = 360.f / static_cast<float>(count);
    // +90, weil Sektor 0 bei -90 Grad sitzt; +0.5 rundet auf den naechsten Sektormittelpunkt.
    const auto idx = static_cast<int>(std::floor(Wrap360(angle + 90.f + width / 2.f) / width));
    return idx % static_cast<int>(count);
}

PointF PointOnRing(const PointF center, const float radius, const float angleDeg)
{
    return PointF(center.x + radius * std::cos(ToRad(angleDeg)), center.y + radius * std::sin(ToRad(angleDeg)));
}

void Ring::Open()
{
    open_ = true;
    page_ = 0;
    aim_ = PointF(0.f, 0.f);
}

void Ring::Close()
{
    open_ = false;
    page_ = 0;
    aim_ = PointF(0.f, 0.f);
}

void Ring::SetPage(const unsigned page)
{
    page_ = page;
    aim_ = PointF(0.f, 0.f);
}

void Ring::Aim(const Position& delta)
{
    aim_ += PointF(delta);
    const float len = std::sqrt(aim_.x * aim_.x + aim_.y * aim_.y);
    if(len > AimMaxRadius)
        aim_ *= AimMaxRadius / len;
}

PointF Ring::GetAim() const
{
    const float len = std::sqrt(aim_.x * aim_.x + aim_.y * aim_.y);
    if(len <= AimDeadRadius)
        return PointF(0.f, 0.f);
    return aim_;
}

void Ring::AimAtSector(const unsigned count, const unsigned sector)
{
    if(count == 0)
        return;
    const std::vector<Sector> sectors = MakeSectors(count);
    aim_ = PointOnRing(PointF(0.f, 0.f), AimMaxRadius, sectors[sector % count].midAngle());
}

} // namespace padring
