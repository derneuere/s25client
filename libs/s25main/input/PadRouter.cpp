// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "input/PadRouter.h"
#include "helpers/EnumRange.h"
#include <algorithm>
#include <cmath>

PadRouter::Device* PadRouter::Find(const PadDeviceId device)
{
    const auto it = std::find_if(devices_.begin(), devices_.end(), [device](const Device& d) { return d.id == device; });
    return it == devices_.end() ? nullptr : &*it;
}

const PadRouter::Device* PadRouter::Find(const PadDeviceId device) const
{
    return const_cast<PadRouter*>(this)->Find(device);
}

bool PadRouter::IsSlotTaken(const unsigned slot) const
{
    return std::any_of(devices_.begin(), devices_.end(), [slot](const Device& d) { return d.slot == slot; });
}

unsigned PadRouter::FirstFreeSlot() const
{
    for(unsigned slot = 0; slot < numSlots_; ++slot)
    {
        if(!IsSlotTaken(slot))
            return slot;
    }
    return NoSlot;
}

void PadRouter::SetNumSlots(const unsigned numSlots)
{
    if(numSlots == numSlots_)
        return;
    numSlots_ = numSlots;

    // Erst alle zu hoch sitzenden Geraete freiraeumen, damit die frei gewordenen Slots im
    // zweiten Durchgang wieder vergeben werden koennen.
    for(Device& dev : devices_)
    {
        if(dev.slot != NoSlot && dev.slot >= numSlots_)
        {
            ReleaseAll(dev);
            pendingAssignments_.emplace_back(dev.slot, false);
            dev.slot = NoSlot;
        }
    }
    for(Device& dev : devices_)
    {
        // Nur BENUTZTE Pads bekommen einen Slot. Ein bloss angestecktes, nie angefasstes Pad
        // bleibt auch dann unversorgt, wenn Slots frei sind - siehe PadRouter.h.
        if(dev.slot != NoSlot || !dev.active)
            continue;
        const unsigned slot = FirstFreeSlot();
        if(slot == NoSlot)
            break;
        dev.slot = slot;
        dev.fraction = PointF(0.f, 0.f);
        pendingAssignments_.emplace_back(slot, true);
    }
}

void PadRouter::Activate(Device& dev)
{
    if(dev.active)
        return;
    dev.active = true;
    if(dev.slot != NoSlot)
        return; // Kann nicht vorkommen, kostet aber nichts und haelt die Zusicherung explizit
    const unsigned slot = FirstFreeSlot();
    if(slot == NoSlot)
        return; // Mehr benutzte Pads als Ansichten - dieses hier bleibt wirkungslos
    dev.slot = slot;
    dev.fraction = PointF(0.f, 0.f);
    pendingAssignments_.emplace_back(slot, true);
}

bool PadRouter::IsUsage(const Device& dev, const PadEvent& ev) const
{
    switch(ev.type)
    {
        case PadEvent::Type::Button: return ev.down;
        case PadEvent::Type::Axis:
            if(ev.axis == PadAxis::LeftX || ev.axis == PadAxis::LeftY)
            {
                // dev.axes ist zu diesem Zeitpunkt bereits fortgeschrieben
                const PointF stick = FilterStick(PointF(dev.axes[PadAxis::LeftX], dev.axes[PadAxis::LeftY]));
                return stick.x != 0.f || stick.y != 0.f; //-V550
            }
            if(ev.axis == PadAxis::RightX || ev.axis == PadAxis::RightY)
            {
                // Dieselbe RADIALE Totzone wie links, aus demselben Grund: "bewegt die Kamera"
                // und "uebernimmt die Ansicht" duerfen nicht auseinanderfallen.
                const PointF stick = FilterStick(PointF(dev.axes[PadAxis::RightX], dev.axes[PadAxis::RightY]));
                return stick.x != 0.f || stick.y != 0.f; //-V550
            }
            if(ev.axis == PadAxis::TriggerLeft || ev.axis == PadAxis::TriggerRight)
                return FilterTrigger(ev.value) > 0.f;
            return std::abs(ev.value) > Deadzone;
        default: return false;
    }
}

void PadRouter::OnEvents(const std::vector<PadEvent>& events)
{
    for(const PadEvent& ev : events)
        OnEvent(ev);
}

void PadRouter::OnEvent(const PadEvent& ev)
{
    switch(ev.type)
    {
        case PadEvent::Type::Connected:
        {
            if(ev.device == InvalidPadDevice || Find(ev.device))
                return; // Idempotent: ein bereits bekanntes Geraet behaelt seinen Slot
            // BEWUSST ohne Slot: Anstecken ist keine Benutzung. Erst das erste Ereignis, das
            // IsUsage bejaht, macht daraus eine Zuordnung.
            Device dev;
            dev.id = ev.device;
            devices_.push_back(dev);
            break;
        }
        case PadEvent::Type::Disconnected:
        {
            Device* dev = Find(ev.device);
            if(!dev)
                return;
            ReleaseAll(*dev);
            const unsigned freedSlot = dev->slot;
            devices_.erase(devices_.begin() + (dev - devices_.data()));
            if(freedSlot != NoSlot)
            {
                pendingAssignments_.emplace_back(freedSlot, false);
                // Ein bisher unversorgtes Geraet rueckt nach. Ohne das bliebe ein Slot leer,
                // obwohl ein Pad danebenliegt.
                for(Device& other : devices_)
                {
                    if(other.slot == NoSlot && other.active)
                    {
                        other.slot = freedSlot;
                        other.fraction = PointF(0.f, 0.f);
                        pendingAssignments_.emplace_back(freedSlot, true);
                        break;
                    }
                }
            }
            break;
        }
        case PadEvent::Type::Axis:
        {
            Device* dev = Find(ev.device);
            if(!dev)
                return;
            dev->axes[ev.axis] = ev.value;
            if(IsUsage(*dev, ev))
                Activate(*dev);
            break;
        }
        case PadEvent::Type::Button:
        {
            Device* dev = Find(ev.device);
            if(!dev)
                return;
            // Flankenerkennung: nur die tatsaechliche Zustandsaenderung zaehlt. Zwei Druecken
            // ohne Loslassen dazwischen sind EIN Ereignis. Der Zustand wird auch fuer
            // unversorgte Geraete gefuehrt, damit ein spaeter zugeordnetes Pad nicht mit einem
            // haengenden "gedrueckt" startet.
            if(dev->pressed[ev.button] == ev.down)
                return;
            dev->pressed[ev.button] = ev.down;
            // Erst die Uebernahme, dann die Flanke: sonst faende der allererste Knopfdruck sein
            // Geraet noch ohne Slot vor und ginge verloren.
            if(IsUsage(*dev, ev))
                Activate(*dev);
            if(dev->slot != NoSlot)
                pendingButtons_.push_back(ButtonEdge{dev->slot, ev.button, ev.down});
            break;
        }
    }
}

void PadRouter::ReleaseAll(Device& dev)
{
    for(const PadButton button : helpers::EnumRange<PadButton>{})
    {
        if(!dev.pressed[button])
            continue;
        dev.pressed[button] = false;
        if(dev.slot != NoSlot)
            pendingButtons_.push_back(ButtonEdge{dev.slot, button, false});
    }
}

unsigned PadRouter::GetSlot(const PadDeviceId device) const
{
    const Device* dev = Find(device);
    return dev ? dev->slot : NoSlot;
}

bool PadRouter::IsActive(const PadDeviceId device) const
{
    const Device* dev = Find(device);
    return dev && dev->active;
}

bool PadRouter::AssignSlot(const PadDeviceId device, const unsigned slot)
{
    Device* dev = Find(device);
    if(!dev || slot >= numSlots_)
        return false;
    // Eine ausdrueckliche Zuordnung IST die Absichtsbekundung, auf die die Uebernahme wartet.
    dev->active = true;
    if(dev->slot == slot)
        return true;
    for(Device& other : devices_)
    {
        if(&other != dev && other.slot == slot)
        {
            ReleaseAll(other);
            other.slot = NoSlot;
            pendingAssignments_.emplace_back(slot, false);
        }
    }
    if(dev->slot != NoSlot)
    {
        ReleaseAll(*dev);
        pendingAssignments_.emplace_back(dev->slot, false);
    }
    dev->slot = slot;
    dev->fraction = PointF(0.f, 0.f);
    pendingAssignments_.emplace_back(slot, true);
    return true;
}

std::vector<PadDeviceId> PadRouter::GetDevices() const
{
    std::vector<PadDeviceId> result;
    result.reserve(devices_.size());
    for(const Device& dev : devices_)
        result.push_back(dev.id);
    return result;
}

unsigned PadRouter::GetNumAssigned() const
{
    return static_cast<unsigned>(
      std::count_if(devices_.begin(), devices_.end(), [](const Device& d) { return d.slot != NoSlot; }));
}

PointF PadRouter::FilterStick(PointF stick)
{
    const float len = std::sqrt(stick.x * stick.x + stick.y * stick.y);
    if(!(len > Deadzone))
        return PointF(0.f, 0.f);
    // Diagonalen duerfen nicht schneller sein als die Achsen. Bei reiner Achsauslenkung
    // (len <= 1) bleibt der Wert damit exakt erhalten: halber Ausschlag = halber Weg.
    if(len > 1.f)
        stick /= len;
    return stick;
}

float PadRouter::FilterTrigger(const float value)
{
    if(!(value > TriggerDeadzone))
        return 0.f;
    // Linear gedehnt: bei TriggerDeadzone genau 0, bei Vollzug genau 1. Werte oberhalb 1
    // (verrutschte Kalibrierung) werden gekappt, damit der Zoom nicht schneller laeuft, als der
    // Aufrufer erwartet.
    const float scaled = (value - TriggerDeadzone) / (1.f - TriggerDeadzone);
    return scaled > 1.f ? 1.f : scaled;
}

void PadRouter::UpdateMotion(const unsigned elapsedMs, IPadTarget& target)
{
    for(const auto& [slot, assigned] : pendingAssignments_)
        target.OnPadAssigned(slot, assigned);
    pendingAssignments_.clear();

    const float dt = static_cast<float>(elapsedMs) / 1000.f;
    for(Device& dev : devices_)
    {
        if(dev.slot == NoSlot)
            continue;
        // --- linker Stick: der Zeiger ---------------------------------------------------
        const PointF stick = FilterStick(PointF(dev.axes[PadAxis::LeftX], dev.axes[PadAxis::LeftY]));
        if(stick.x == 0.f && stick.y == 0.f) //-V550
            dev.fraction = PointF(0.f, 0.f);
        else
        {
            const PointF move = stick * (PixelsPerSecond * dt) + dev.fraction;
            const Position delta(Position::Truncate, move);
            dev.fraction = move - PointF(delta);
            if(delta != Position(0, 0))
                target.OnPadMove(dev.slot, delta);
        }

        // --- rechter Stick: die Kamera ---------------------------------------------------
        // Dieselbe Kennlinie und dieselbe Subpixelbuchhaltung wie beim Zeiger, nur mit eigener
        // Geschwindigkeit. Bewusst KEINE Beschleunigung: eine Kamera, die erst anfaehrt, macht
        // das Anvisieren eines Knotens am Fernseher unberechenbar.
        const PointF camStick = FilterStick(PointF(dev.axes[PadAxis::RightX], dev.axes[PadAxis::RightY]));
        if(camStick.x == 0.f && camStick.y == 0.f) //-V550
            dev.cameraFraction = PointF(0.f, 0.f);
        else
        {
            const PointF move = camStick * (CameraPixelsPerSecond * dt) + dev.cameraFraction;
            const Position delta(Position::Truncate, move);
            dev.cameraFraction = move - PointF(delta);
            if(delta != Position(0, 0))
                target.OnPadCamera(dev.slot, delta);
        }

        // --- Trigger: der Zoom -----------------------------------------------------------
        // Rechts naeher heran, links weiter weg - dieselbe Richtung, die auch das Mausrad
        // hat (Msg_WheelUp = hineinzoomen). Beide gleichzeitig gezogen heben sich auf; das ist
        // kein Sonderfall, sondern faellt aus der Differenz.
        const float zoom = FilterTrigger(dev.axes[PadAxis::TriggerRight])
                           - FilterTrigger(dev.axes[PadAxis::TriggerLeft]);
        if(zoom != 0.f && dt > 0.f) //-V550
            target.OnPadZoom(dev.slot, zoom * ZoomPerSecond * dt);
    }
}

void PadRouter::DispatchButtons(IPadTarget& target)
{
    // Kopie, damit ein Empfaenger, der waehrend der Zustellung neue Ereignisse einspeist,
    // die Schleife nicht invalidiert.
    const std::vector<ButtonEdge> edges = std::move(pendingButtons_);
    pendingButtons_.clear();
    for(const ButtonEdge& edge : edges)
        target.OnPadButton(edge.slot, edge.button, edge.down);
}

void PadRouter::Clear()
{
    devices_.clear();
    pendingButtons_.clear();
    pendingAssignments_.clear();
}
