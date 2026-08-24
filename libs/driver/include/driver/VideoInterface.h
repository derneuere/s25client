// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "GuiScale.h"
#include "KeyEvent.h"
#include "PadEvent.h"
#include "Point.h"
#include "VideoMode.h"
#include "exportImport.h"
#include <string>
#include <type_traits>
#include <vector>

/// Function type for loading OpenGL methods
using OpenGL_Loader_Proc = void* (*)(const char*);

class BOOST_SYMBOL_VISIBLE IVideoDriver
{
public:
    virtual ~IVideoDriver() = 0;

    /// Funktion zum Auslesen des Treibernamens.
    virtual const char* GetName() const = 0;

    virtual bool Initialize() = 0;

    /// Erstellt das Fenster mit entsprechenden Werten.
    virtual bool CreateScreen(const std::string& title, VideoMode newSize, DisplayMode displayMode) = 0;

    virtual bool ResizeScreen(VideoMode newSize, DisplayMode displayMode) = 0;

    /// Schliesst das Fenster.
    virtual void DestroyScreen() = 0;

    /// Wechselt die OpenGL-Puffer.
    virtual bool SwapBuffers() = 0;

    /// Die Nachrichtenschleife.
    virtual bool MessageLoop() = 0;

    /// Return the current tick count (time since epoch in ms)
    virtual unsigned long GetTickCount() const = 0;

    /// Funktion zum Holen einer Subfunktion.
    virtual OpenGL_Loader_Proc GetLoaderFunction() const = 0;

    virtual std::vector<VideoMode> ListVideoModes() const = 0;

    /// Funktion zum Auslesen der Mauskoordinaten.
    virtual Position GetMousePos() const = 0;

    /// Funktion zum Setzen der Mauskoordinaten.
    virtual void SetMousePos(Position pos) = 0;

    /// Return true when left mouse button is pressed
    virtual bool GetMouseStateL() const = 0;
    /// Return true when right mouse button is pressed
    virtual bool GetMouseStateR() const = 0;
    /// Return true if at least 1 finger is on screen
    virtual bool IsTouchEvent() const = 0;

    /// Get the size of the window in screen coordinates
    virtual VideoMode GetWindowSize() const = 0;
    /// Get the size of the render region in pixels
    virtual Extent GetRenderSize() const = 0;
    virtual DisplayMode GetDisplayMode() const = 0;

    /// Get the factor required to scale "normal" DPI to the display DPI
    virtual float getDpiScale() const = 0;

    /// Get the scale applied to the user interface
    virtual const GuiScale& getGuiScale() const = 0;

    /// Set the scale applied to the user interface in percent
    virtual void setGuiScalePercent(unsigned percent) = 0;

    /// Get minimum, maximum, and recommended GUI scale percentages for the current window and render size
    virtual GuiScaleRange getGuiScaleRange() const = 0;

    /// Get state of the modifier keys
    virtual KeyEvent GetModKeyState() const = 0;

    /// Get pointer to window (device-dependent!), HWND unter Windows
    virtual void* GetMapPointer() const = 0;

    virtual bool IsInitialized() const = 0;
    /// Shall we support OpenGL? (Disabled for tests)
    virtual bool IsOpenGL() const = 0;

    // Display the problem to the gamer
    virtual void ShowErrorMessage(const std::string& title, const std::string& message) = 0;

    /// Holt die seit dem letzten Aufruf aufgelaufenen Gamepad-Ereignisse ab und leert `out`
    /// vorher. Standard: keine - ein Treiber ohne Gamepadunterstuetzung (WinAPI, Mockup) meldet
    /// schlicht nichts und braucht keine Zeile Code.
    ///
    /// Bewusst eine ABHOLNAHT und bewusst NICHT ueber VideoDriverLoaderInterface: dessen
    /// einziger Implementierer im ganzen Baum ist der WindowManager - ein Singleton mit genau
    /// einem Desktop, einem Fokus und einem Cursor (WindowManager.h:35). Vier Spieler koennen
    /// sich diesen einen Zustand nicht teilen. Da die Pad-Ereignisse hier abgeholt statt
    /// zugestellt werden, bleibt VideoDriverLoaderInterface.h unveraendert und der
    /// Maus-/Tastaturpfad strukturell unberuehrt.
    ///
    /// Achsen reisen als Ereignis, werden aber vom Empfaenger als ZUSTAND gehalten: SDL feuert
    /// SDL_CONTROLLERAXISMOTION nur bei Aenderung, ein auf Anschlag gehaltener Stick erzeugt
    /// also genau ein Ereignis und danach nichts mehr.
    ///
    /// Neu in DRIVERAPIVERSION 9.
    virtual void FetchPadEvents(std::vector<PadEvent>& out) { out.clear(); }

    /// Hoehe der logischen Leinwand, gegen die die EMPFOHLENE GUI-Skalierung gerechnet wird.
    /// 0 (Standard) = altes Verhalten: die Empfehlung kommt aus getDpiScale().
    ///
    /// Warum das hier und nicht beim Aufrufer steht: die Empfehlung wird auch TREIBERINTERN
    /// gebraucht - setGuiScalePercent(0) und jedes SetNewSize holen sie sich aus
    /// getGuiScaleRange(). Saesse die Regel im Spielcode, bliebe die Automatik beim alten Wert
    /// stehen, sobald sich die Fenstergroesse aendert.
    ///
    /// Neu in DRIVERAPIVERSION 10.
    virtual void setUiReferenceHeight(unsigned) {}
    virtual unsigned getUiReferenceHeight() const { return 0; }
};

class VideoDriverLoaderInterface;

/// Instanzierungsfunktion der Treiber.
RTTR_DECL IVideoDriver* CreateVideoInstance(VideoDriverLoaderInterface* CallBack);
RTTR_DECL void FreeVideoInstance(IVideoDriver* driver);

using CreateVideoInstance_t = decltype(CreateVideoInstance);
using FreeVideoInstance_t = decltype(FreeVideoInstance);
