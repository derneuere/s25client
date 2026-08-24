// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "input/MenuPadInput.h"
#include "Window.h"
#include "desktops/Desktop.h"
#include "ingameWindows/IngameWindow.h"
#include "s25util/colors.h"
#include <algorithm>

MenuPadInput::MenuPadInput() = default;

PadDeviceId MenuPadInput::GetActingDevice() const
{
    if(actingSlot_ == NoSlot)
        return InvalidPadDevice;
    for(const PadDeviceId dev : router_.GetDevices())
    {
        if(router_.GetSlot(dev) == actingSlot_)
            return dev;
    }
    return InvalidPadDevice;
}

void MenuPadInput::Pump(const std::vector<PadEvent>& events, const unsigned elapsedMs, Desktop* desktop,
                        IngameWindow* topWnd)
{
    desktop_ = desktop;
    rootWnd_ = topWnd;
    // Im Menue ist das gerade bediente Ding oft ein IngameWindow und kein Desktop: iwConnecting
    // traegt den Uebergang von der Kartenauswahl in die Lobby, iwMsgbox jede Fehlermeldung.
    root_ = topWnd ? static_cast<Window*>(topWnd) : static_cast<Window*>(desktop);
    // Erst die Slotzahl, dann die Ereignisse: ein in DIESEM Frame angestecktes und sofort
    // benutztes Pad soll seinen Slot noch in diesem Frame bekommen. Dieselbe Reihenfolge wie in
    // dskGameInterface::UpdateInput.
    router_.SetNumSlots(desktop ? std::min(desktop->GetNumPadSlots(), MaxSlots) : 1u);
    router_.OnEvents(events);

    // Die Wurzel kann sich zwischen zwei Frames geaendert haben, ohne dass ein Padereignis
    // daran beteiligt war: ein Desktopwechsel, ein geoeffnetes Fenster, eine Nachrichtenbox.
    // Verglichen wird nur der ZEIGER, dereferenziert wird die alte Wurzel nie - und beim
    // Desktopwechsel raeumt der WindowManager sie ohnehin ausdruecklich weg.
    for(unsigned slot = 0; slot < MaxSlots; ++slot)
    {
        if(hasDevice_[slot] && focus_[slot].GetRoot() != root_)
            ResetFocus(slot);
    }

    stepMs_ = elapsedMs;
    // Erst Zuordnung und Bewegung, dann die Knopfflanken - genau die Reihenfolge, die
    // PadRouter zusichert und die dskGameInterface fuer den Weltzeiger braucht.
    router_.UpdateMotion(elapsedMs, *this);
    router_.DispatchButtons(*this);
    swallowFrame_.fill(false);
}

void MenuPadInput::ResetFocus(const unsigned slot)
{
    FocusPath& focus = focus_[slot];
    focus.SetRoot(root_);
    // Die Wurzel darf sagen, wo ein Pad anfangen soll. Die Lobby setzt damit den Fokus eines
    // neu hinzukommenden Spielers auf die erste freie Sitzkarte - er sieht seinen Beitritt
    // also dort, wo er ihn erwartet, statt auf "Spiel starten". Eine Rueckfrage nennt hier
    // ihre harmlose Vorgabeantwort (iwMsgbox::GetPadEntryCtrl).
    if(root_)
    {
        if(Window* entry = root_->GetPadEntryCtrl(slot))
            focus.FocusCtrl(entry);
    }
}

void MenuPadInput::OnPadAssigned(const unsigned slot, const bool assigned)
{
    if(slot >= MaxSlots)
        return;
    hasDevice_[slot] = assigned;
    if(assigned)
    {
        swallowFrame_[slot] = true;
        ResetFocus(slot);
    } else
        focus_[slot].Clear();
}

void MenuPadInput::OnPadMove(const unsigned slot, const Position& delta)
{
    if(slot < MaxSlots)
        focus_[slot].OnPadMove(delta, stepMs_);
}

void MenuPadInput::OnPadCamera(unsigned /*slot*/, const Position& /*delta*/)
{
    // Es gibt im Menue keine Welt, die sich verschieben liesse.
}

void MenuPadInput::OnPadZoom(unsigned /*slot*/, float /*step*/)
{
    // dito
}

void MenuPadInput::OnPadButton(const unsigned slot, const PadButton button, const bool down)
{
    if(slot >= MaxSlots)
        return;
    if(swallowFrame_[slot])
        return; // Aufnahmedruck, siehe MenuPadInput.h
    if(!down)
        return;

    actingSlot_ = slot;
    // B und Start erreichen die Fokusnavigation bewusst NICHT:
    //  - FocusPath::OnPadButton macht aus B ein Clear(), also "raus aus dem Fenster". Ingame ist
    //    das richtig (dahinter liegt die Welt), im Menue waere es eine Sackgasse: dahinter liegt
    //    nichts, und der Spieler haette keinen Weg zurueck.
    //  - Start ist ingame der Knopf, mit dem ein Spieler sein Pad in die Hand nimmt, und dort
    //    bewusst wirkungslos. Im Menue ist er die Vorgabeaktion des Bildschirms.
    // Beides beantwortet der Desktop; sagt er nichts dazu, passiert nichts.
    if(button == PadButton::B && rootWnd_)
    {
        // Steht der Spieler in einem Fenster, ist B das Fenster zu - dieselbe Wirkung, die die
        // Tastatur mit ESC hat, samt derselben Ausnahmen (WindowManager::RelayKeyboardMessage).
        if(!rootWnd_->IsPinned() && rootWnd_->getCloseBehavior() != CloseBehavior::Custom)
            rootWnd_->Close();
    } else if(button == PadButton::B || button == PadButton::Start)
    {
        if(desktop_)
            desktop_->Msg_PadCommand(slot, button);
    } else
        focus_[slot].OnPadButton(button, down);
    actingSlot_ = NoSlot;
}

void MenuPadInput::DrawRings() const
{
    for(unsigned slot = 0; slot < MaxSlots; ++slot)
    {
        if(!hasDevice_[slot])
            continue;
        // Dieselben Farben wie die Spielerfarben der Partie, damit ein Sitzplatz im Menue und
        // im Spiel dieselbe Farbe hat. Kein Bezug auf eine Partie - es gibt hier noch keine.
        focus_[slot].DrawRing(PLAYER_COLORS[slot % PLAYER_COLORS.size()]);
    }
}

void MenuPadInput::OnRootDestroyed(const Window* wnd)
{
    for(auto& focus : focus_)
        focus.OnRootDestroyed(wnd);
    if(root_ == wnd)
        root_ = nullptr;
    if(static_cast<const Window*>(rootWnd_) == wnd)
        rootWnd_ = nullptr;
    if(static_cast<const Window*>(desktop_) == wnd)
        desktop_ = nullptr;
}

void MenuPadInput::ClearFocus()
{
    for(auto& focus : focus_)
        focus.Clear();
    root_ = nullptr;
    rootWnd_ = nullptr;
}

void MenuPadInput::Reset()
{
    ClearFocus();
    router_.Clear();
    hasDevice_.fill(false);
    swallowFrame_.fill(false);
    desktop_ = nullptr;
}
