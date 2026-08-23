// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "driver/VideoDriver.h"
#include <SDL.h>

class VideoDriverLoaderInterface;
struct VideoMode;

class VideoSDL2 final : public VideoDriver
{
    void CleanUp();

public:
    VideoSDL2(VideoDriverLoaderInterface* CallBack);

    ~VideoSDL2() override;

    /// Get the name of the driver
    const char* GetName() const override;

    bool Initialize() override;

    bool CreateScreen(const std::string& title, VideoMode size, DisplayMode displayMode) override;
    bool ResizeScreen(VideoMode newSize, DisplayMode displayMode) override;

    void DestroyScreen() override;

    /// Swap the OpenGL buffer
    bool SwapBuffers() override;

    bool MessageLoop() override;

    void ShowErrorMessage(const std::string& title, const std::string& message) override;

    /// Get a timestamp
    unsigned long GetTickCount() const override;

    OpenGL_Loader_Proc GetLoaderFunction() const override;

    /// Get supported video modes
    std::vector<VideoMode> ListVideoModes() const override;

    /// Set mouse position
    void SetMousePos(Position pos) override;

    /// Get state of the modifier keys
    KeyEvent GetModKeyState() const override;

    /// Get (device-dependent!) window pointer, HWND in Windows
    void* GetMapPointer() const override;

    static void PrintError();
    static void PrintError(const std::string& msg);

    /// Reicht die seit dem letzten Aufruf aufgelaufenen Gamepad-Ereignisse heraus (ABI 9).
    /// Der Treiber enthaelt bewusst KEINE Logik ausser der 1:1-Uebersetzung von SDL: keine
    /// Totzone, keine Kennlinie, keine Zuordnung zu Spielern oder Ansichten. Alles das liegt in
    /// s25main (input/PadRouter) und ist damit ohne Hardware pruefbar - der hier verbleibende
    /// Rest ist so klein, dass man ihn liest statt ihn zu testen.
    void FetchPadEvents(std::vector<PadEvent>& out) override;

private:
    void HandlePaste();
    void UpdateCurrentSizes();
    void MoveWindowToCenter();
    void UpdateCurrentDisplayMode();

    /// Gamepad-Subsystem hochfahren. Ein Fehlschlag ist NICHT fatal: ein Rechner ohne
    /// Joystick-Treiber (oder eine CI-Maschine) muss das Spiel trotzdem starten koennen.
    void InitGamepads();
    /// Alle offenen Controller schliessen und das Subsystem abmelden. Muss vor SDL_Quit()
    /// laufen, sonst bliebe gamepads_ mit toten Zeigern zurueck - CleanUp() wird auch aus
    /// DestroyScreen() gerufen, also nicht nur beim Programmende.
    void CleanUpGamepads();
    void OnGamepadAdded(int deviceIdx);
    void OnGamepadRemoved(SDL_JoystickID instanceId);
    /// SDL-Instanz-ID -> unsere Kennung. InvalidPadDevice, wenn das Geraet nicht (mehr) gefuehrt
    /// wird - dann kommt das Ereignis von einem Pad, das wir nie geoeffnet haben.
    PadDeviceId GetPadDeviceId(SDL_JoystickID instanceId) const;
    /// Ein Pad-Ereignis einreihen und die Warteschlange dabei begrenzen.
    ///
    /// Abgeholt wird sie nur von dskGameInterface, also nur waehrend einer Partie. Im
    /// Hauptmenue, in der Lobby und im Ladebildschirm holt niemand ab - ein liegengelassenes
    /// Pad mit driftendem Stick koennte die Warteschlange dort ueber Stunden beliebig gross
    /// werden lassen. Ueberlaeuft sie, fliegt die AELTERE Haelfte der Achsen- und
    /// Knopfereignisse raus; An- und Abstecken bleibt immer erhalten.
    ///
    /// Das ist verlustfrei genug: der Empfaenger fuehrt Achsen als Zustand (der neueste Wert
    /// ueberlebt) und erkennt Knopfflanken aus dem Zustandswechsel (ein "losgelassen" ohne
    /// vorheriges "gedrueckt" ist wirkungslos). Ein "gedrueckt" ohne sein spaeteres
    /// "losgelassen" kann nicht entstehen, weil immer nur AELTERE Ereignisse wegfallen.
    void EnqueuePadEvent(const PadEvent& ev);

    /// Ein geoeffnetes Gamepad.
    struct Gamepad
    {
        SDL_GameController* handle = nullptr;
        /// SDL-Instanz-ID. Sie traegt jedes Button- und Achsenereignis (SDL_events.h:360, :376)
        /// und den REMOVED-Fall; der ADDED-Fall traegt dagegen einen Geraeteindex
        /// (SDL_events.h:388-392). Genau diese Asymmetrie ist der Grund fuer OnGamepadAdded.
        SDL_JoystickID instanceId = 0;
        /// Unsere eigene, nach aussen sichtbare Kennung. Bewusst nicht die SDL-Instanz-ID:
        /// PadEvent verspricht "0 ist ungueltig" und "wird nie wiederverwendet", und das soll
        /// nicht von SDL-Interna abhaengen.
        PadDeviceId id = InvalidPadDevice;
    };

    SDL_Window* window;
    SDL_GLContext context;

    bool gamepadsInitialized_ = false;
    std::vector<Gamepad> gamepads_;
    /// Monoton wachsend, wird nie zurueckgesetzt: eine einmal vergebene Kennung kommt in dieser
    /// Programmlaufzeit nicht wieder. Damit koennen Ereignisse eines abgezogenen Pads niemals
    /// beim Nachfolger landen.
    PadDeviceId nextPadId_ = 1;
    std::vector<PadEvent> padEvents_;
};
