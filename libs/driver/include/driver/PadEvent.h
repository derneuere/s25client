// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

/// Stabile Kennung eines VERBUNDENEN Gamepads. Der Treiber vergibt sie und verwendet sie
/// innerhalb einer Programmlaufzeit NIE wieder - auch nicht, nachdem ein Pad abgezogen wurde.
/// Damit koennen Ereignisse eines abgezogenen Pads niemals beim Nachfolger landen.
/// Bewusst KEIN Geraeteindex: der wandert beim Ab- und Anstecken (SDL_events.h:388-392, der
/// ADDED-Fall traegt einen Geraeteindex, alle anderen eine Instanz-ID).
/// 0 ist ungueltig.
using PadDeviceId = uint32_t;
constexpr PadDeviceId InvalidPadDevice = 0;

/// Analogachsen. Reihenfolge und Bedeutung wie SDL_GameControllerAxis
/// (external/dev-tools/msvc/include/SDL2/SDL_gamecontroller.h:259-266), aber als eigener Typ:
/// libs/driver darf nicht SDL-abhaengig werden, weil WinAPI und der MockupVideoDriver kein SDL
/// kennen.
enum class PadAxis : uint8_t
{
    LeftX,
    LeftY,
    RightX,
    RightY,
    TriggerLeft,
    TriggerRight
};
constexpr auto maxEnumValue(PadAxis)
{
    return PadAxis::TriggerRight;
}

/// Logische Knoepfe, wie SDL_GameControllerButton (SDL_gamecontroller.h:303-319).
enum class PadButton : uint8_t
{
    A,
    B,
    X,
    Y,
    Back,
    Guide,
    Start,
    LeftStick,
    RightStick,
    LeftShoulder,
    RightShoulder,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight
};
constexpr auto maxEnumValue(PadButton)
{
    return PadButton::DpadRight;
}

/// EIN Ereignis fuer Druecken UND Loslassen, und fuer An- und Abstecken.
///
/// Warum ein Struct mit Typfeld und nicht - wie bei der Maus - ein Methodenpaar je Knopf
/// (VideoDriverLoaderInterface.h:16-24)? Weil das 15 Knoepfe x 2 Flanken = 30 neue Slots waeren.
/// Und warum nicht wie bei der Tastatur nur "gedrueckt" (VideoDriverLoaderInterface.h:26)? Weil
/// es dort das Loslassen gar nicht gibt - ein Gamepad braucht beides. Das Loslassen reist hier
/// in `down == false` und beruehrt den Tastaturpfad nie.
///
/// Rein datenhaltig: nur <cstdint>-Typen, kein SDL, kein windows.h. Unter Linux und macOS
/// unveraendert uebersetzbar.
struct PadEvent
{
    enum class Type : uint8_t
    {
        Connected,
        Disconnected,
        Axis,
        Button
    };

    Type type = Type::Connected;
    PadDeviceId device = InvalidPadDevice;
    /// Nur bei Type::Axis gueltig
    PadAxis axis = PadAxis::LeftX;
    /// Nur bei Type::Axis gueltig. Bereits normiert: Sticks auf [-1,1], Trigger auf [0,1].
    /// ROH - keine Totzone, keine Kennlinie. Beides liegt bewusst in s25main (PadRouter), weil
    /// es sonst hinter der ABI-Grenze des Plugins saesse und nicht ohne Versionsbump aenderbar
    /// waere.
    float value = 0.f;
    /// Nur bei Type::Button gueltig
    PadButton button = PadButton::A;
    /// Nur bei Type::Button gueltig: true = gedrueckt, false = losgelassen
    bool down = false;

    static PadEvent Connected(PadDeviceId device)
    {
        PadEvent ev;
        ev.type = Type::Connected;
        ev.device = device;
        return ev;
    }
    static PadEvent Disconnected(PadDeviceId device)
    {
        PadEvent ev;
        ev.type = Type::Disconnected;
        ev.device = device;
        return ev;
    }
    static PadEvent Axis(PadDeviceId device, PadAxis axis, float value)
    {
        PadEvent ev;
        ev.type = Type::Axis;
        ev.device = device;
        ev.axis = axis;
        ev.value = value;
        return ev;
    }
    static PadEvent Button(PadDeviceId device, PadButton button, bool down)
    {
        PadEvent ev;
        ev.type = Type::Button;
        ev.device = device;
        ev.button = button;
        ev.down = down;
        return ev;
    }
};
