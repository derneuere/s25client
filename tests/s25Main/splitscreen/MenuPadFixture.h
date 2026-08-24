// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Settings.h"
#include "WindowManager.h"
#include "desktops/Desktop.h"
#include "driver/PadEvent.h"
#include "drivers/VideoDriverWrapper.h"
#include "input/MenuPadInput.h"
#include "input/PadRouter.h"
#include "mockupDrivers/MockupVideoDriver.h"
#include "uiHelper/uiHelpers.hpp"
#include <boost/test/unit_test.hpp>
#include <memory>
#include <string>

namespace rttr::test {

/// Ein Menuebildschirm, der NUR mit dem Gamepad bedient wird.
///
/// DIE EINZIGE NAHT liegt beim Treiber: der Test schreibt Ereignisse in
/// MockupVideoDriver::padEvents_ und ruft danach ausschliesslich WINDOWMANAGER.Draw() - genau
/// den Aufruf, den GameManager::Run im Spiel je Frame macht (GameManager.cpp:150-155). Alles
/// zwischen IVideoDriver::FetchPadEvents und der beobachteten Wirkung ist Produktivcode.
///
/// AUSDRUECKLICH NICHT ERLAUBT und in keinem Fall unten benutzt: ein FocusPath im Test,
/// FocusPath::SetRoot/Move/Activate, Window::Activate(), Desktop::Msg_ButtonClick(id),
/// ctrlTable::SetSelection, PadRouter::OnEvent. Jeder dieser Griffe waere ein Eingang, den das
/// Spiel nicht hat - genau der Fehlertyp, der in diesem Projekt zweimal gruene, aber wertlose
/// Nachweise erzeugt hat.
struct MenuPadFixture : uiHelper::Fixture
{
    MockupVideoDriver& video;
    /// Zeit laeuft nur, wenn der Test sie laufen laesst. tickCount_ ist der Ursprung, aus dem
    /// WindowManager::PumpPadInput sein elapsedMs bildet.
    unsigned frameMs = 16;

    MenuPadFixture() : video(*uiHelper::GetVideoDriver()), oldSubmitDebugData_(SETTINGS.global.submitDebugData)
    {
        // Der Geraetebestand lebt so lange wie der Prozess - das ist im Spiel richtig und
        // zwischen zwei Testfaellen falsch. Derselbe Grund, aus dem uiHelper::Fixture den
        // Desktop zuruecksetzt.
        WINDOWMANAGER.GetPadInput().Reset();
        video.padEvents_.clear();
        // dskMainMenu haengt einen 250ms-Timer an, der eine Nachrichtenbox oeffnet
        // (dskMainMenu.cpp:56-58). Die laege ueber dem Menue und faenge den Fokus ab.
        SETTINGS.global.submitDebugData = SubmitDebugData::Yes;
    }
    virtual ~MenuPadFixture()
    {
        SETTINGS.global.submitDebugData = oldSubmitDebugData_;
        WINDOWMANAGER.GetPadInput().Reset();
        video.padEvents_.clear();
    }

    // --- Eingabe ------------------------------------------------------------------------------
    void connect(PadDeviceId dev) { video.padEvents_.push_back(PadEvent::Connected(dev)); }
    void disconnect(PadDeviceId dev) { video.padEvents_.push_back(PadEvent::Disconnected(dev)); }
    void button(PadDeviceId dev, PadButton b, bool down)
    {
        video.padEvents_.push_back(PadEvent::Button(dev, b, down));
    }
    void tap(PadDeviceId dev, PadButton b)
    {
        button(dev, b, true);
        button(dev, b, false);
    }
    /// "Der Spieler nimmt das Pad in die Hand." Anstecken allein vergibt keinen Slot
    /// (input/PadRouter.h: Uebernahme durch Benutzung), es braucht eine Benutzung. Start ist
    /// dafuer bewusst gewaehlt: er hat im Hauptmenue keine zweite Wirkung.
    void pickUp(PadDeviceId dev)
    {
        connect(dev);
        tap(dev, PadButton::Start);
        frame();
    }

    // --- Frames -------------------------------------------------------------------------------
    /// GENAU EIN Frame des Spiels. Virtuell, weil der Abnahmenachweis zusaetzlich Server und
    /// Client laufen lassen muss - genau die Reihenfolge aus GameManager::Run
    /// (GameManager.cpp:110-153).
    virtual void frame()
    {
        video.tickCount_ += frameMs;
        WINDOWMANAGER.Draw();
    }
    /// Ein Knopfdruck und der Frame, in dem er wirkt.
    void press(PadDeviceId dev, PadButton b)
    {
        tap(dev, b);
        frame();
    }
    void pressN(PadDeviceId dev, PadButton b, unsigned n)
    {
        for(unsigned i = 0; i < n; ++i)
            press(dev, b);
    }

    // --- Beobachtung --------------------------------------------------------------------------
    static Desktop* desktop() { return WINDOWMANAGER.GetCurrentDesktop(); }
    template<class T>
    static T* desktopAs()
    {
        return dynamic_cast<T*>(WINDOWMANAGER.GetCurrentDesktop());
    }
    static MenuPadInput& padInput() { return WINDOWMANAGER.GetPadInput(); }
    static PadRouter& router() { return WINDOWMANAGER.GetPadInput().GetRouter(); }
    /// Das fokussierte Control dieses Slots - gefragt wird der Produktivzustand, nicht eine
    /// Buchhaltung des Tests.
    static Window* focused(unsigned slot) { return padInput().GetFocus(slot).GetFocused(); }
    static unsigned focusedId(unsigned slot)
    {
        const Window* f = focused(slot);
        return f ? f->GetID() : ~0u;
    }

private:
    SubmitDebugData oldSubmitDebugData_;
};

} // namespace rttr::test
