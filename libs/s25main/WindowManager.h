// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Rect.h"
#include "SnapOffset.h"
#include "driver/VideoDriverLoaderInterface.h"
#include "s25util/Singleton.h"
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Window;
class Desktop;
class IngameWindow;
struct MouseCoords;
struct KeyEvent;
struct PadEvent;
class ctrlBaseTooltip;
class MenuPadInput;

// Cursor types with values equal to indices in resource.idx
enum class Cursor : unsigned
{
    None,
    Hand = 30,
    Scroll = 32,
    Moon = 33,
    Remove = 34
};

/// Besitzer eines Ingame-Fensters: die Nummer der ANSICHT (nicht die Spieler-ID).
///
/// Fenster gehoeren zum Sitzplatz vor dem Fernseher, nicht zum Simulationsslot - damit
/// ueberlebt der Besitz einen Ingame-Spielertausch (CI_PlayersSwapped), und der Wert ist klein
/// und beschraenkt (MAX_VIEWPORTS).
///
/// SHARED_WINDOW_OWNER heisst: dieses Fenster gehoert keiner Ansicht, sondern dem Bildschirm -
/// Nachrichtenboxen, Chat, Menuefenster, alles ausserhalb einer Partie. Das ist die Vorgabe,
/// solange keine Eingabeklammer offen ist; ein Einzelspieler und jeder Menuedesktop laufen
/// damit exakt wie bisher.
inline constexpr unsigned SHARED_WINDOW_OWNER = ~0u;

/// Wird gerufen, wenn sich der ambiente Fensterbesitzer aendert.
///
/// Der WindowManager kennt weder Spieler noch GameCommands; er kennt nur Ansichtsnummern. Wer
/// aus einer Ansichtsnummer einen Spieler machen kann - dskGameInterface -, meldet sich hier an
/// und zieht den handelnden Spieler mit. Ohne angemeldeten Beobachter (jeder Menuedesktop, jeder
/// UI-Test ohne Partie) passiert nichts.
class IWindowOwnerObserver
{
public:
    virtual ~IWindowOwnerObserver() = default;
    virtual void OnWindowOwnerChanged(unsigned ownerIdx) = 0;
};

/// Verwaltet alle (offenen) Fenster bzw Desktops samt ihren Controls und Messages
class WindowManager : public Singleton<WindowManager>, public VideoDriverLoaderInterface
{
public:
    using KeyboardMsgHandler = bool (Window::*)(const KeyEvent&);
    using MouseMsgHandler = bool (Window::*)(const MouseCoords&);

    WindowManager();
    ~WindowManager();
    void CleanUp();

    /// Klammert eine Eingabeverarbeitung, die GENAU EINER Ansicht gehoert.
    ///
    /// Innerhalb der Klammer bekommt jedes NEU KONSTRUIERTE IngameWindow diese Ansicht als
    /// Besitzer (IngameWindow-Konstruktor liest GetCurrentWindowOwner()). Das ist der Hebel,
    /// mit dem die 100+ Erzeugungsstellen unveraendert bleiben koennen: keine von ihnen nennt
    /// einen Spieler, aber jede laeuft unter einer Klammer, die ihn kennt.
    ///
    /// Bauform absichtlich identisch zu GameClient::ScopedActingPlayer: verschachtelbar, stellt
    /// im Destruktor den vorigen Wert wieder her.
    class ScopedWindowOwner
    {
    public:
        ScopedWindowOwner(WindowManager& wm, unsigned ownerIdx);
        ~ScopedWindowOwner();
        ScopedWindowOwner(const ScopedWindowOwner&) = delete;
        ScopedWindowOwner& operator=(const ScopedWindowOwner&) = delete;

    private:
        WindowManager& wm_;
        unsigned previous_;
    };

    /// Wem gehoeren gerade neu erzeugte Fenster? Ausserhalb jeder Klammer SHARED_WINDOW_OWNER.
    unsigned GetCurrentWindowOwner() const { return curWindowOwner_; }
    /// Anmelden/Abmelden des Beobachters. Nur EINER; der Anmelder ist fuer das Abmelden
    /// zustaendig (dskGameInterface tut es in seinem Destruktor).
    void SetWindowOwnerObserver(IWindowOwnerObserver* observer) { ownerObserver_ = observer; }
    IWindowOwnerObserver* GetWindowOwnerObserver() const { return ownerObserver_; }

    /// Zeichnet Desktop und alle Fenster.
    void Draw();

    /// Die Gamepadbedienung ausserhalb einer Partie.
    ///
    /// Sie liegt hier und nicht bei einem Desktop, weil der GERAETEBESTAND Desktopwechsel
    /// ueberleben muss und weil das gerade bediente Ding im Menue oft ein IngameWindow ist
    /// (iwConnecting traegt den Uebergang von der Kartenauswahl in die Lobby). Beides sieht nur
    /// der WindowManager. Siehe input/MenuPadInput.h.
    MenuPadInput& GetPadInput() { return *padInput_; }
    const MenuPadInput& GetPadInput() const { return *padInput_; }

    /// Meldet den GERAETEBESTAND aus einer Treiberwarteschlange, die jemand ANDERES geleert hat.
    ///
    /// Es gibt genau einen solchen anderen: dskGameInterface holt waehrend einer Partie selbst
    /// ab (Desktop::WantsPadInput). Ohne diese Meldung verpasste der Menuerouter jedes
    /// Connected und Disconnected einer laufenden Partie - und nachfragen kann er nicht, weil
    /// der Bestand FLANKENBASIERT ist: SDL meldet ein Geraet genau einmal beim Anstecken
    /// (VideoSDL2.cpp, SDL_CONTROLLERDEVICEADDED) und kennt keine Bestandsabfrage. Eine
    /// verpasste Flanke ist damit fuer immer verpasst.
    ///
    /// Bewusst NUR Connected/Disconnected: Achsen und Knoepfe gehoeren dem, der abgeholt hat.
    /// Der Menuerouter lernt das Geraet also, ordnet ihm aber keinen Slot zu - dafuer braucht es
    /// wie immer eine Benutzung, die er hier gerade nicht sieht.
    void NotifyPadDevices(const std::vector<PadEvent>& events);
    /// liefert ob der aktuelle Desktop den Focus besitzt oder nicht.
    bool IsDesktopActive() const;

    /// schickt eine Nachricht an das aktive Fenster bzw den aktiven Desktop.
    /// Sendet eine Tastaturnachricht an die Steuerelemente.
    void RelayKeyboardMessage(KeyboardMsgHandler msg, const KeyEvent& ke);
    /// Sendet eine Mausnachricht weiter an alle Steuerelemente
    void RelayMouseMessage(MouseMsgHandler msg, const MouseCoords& mc, Window* window = nullptr);

    /// Öffnet ein IngameWindow und fügt es zur Fensterliste hinzu.
    IngameWindow& DoShow(std::unique_ptr<IngameWindow> window, bool mouse = false);
    template<typename T>
    T& Show(std::unique_ptr<T> window, bool mouse = false)
    {
        return static_cast<T&>(DoShow(std::move(window), mouse));
    }
    /// Ersetzt das Fenster derselben Art DESSELBEN Besitzers. Der Besitzer steht am neuen
    /// Fenster bereits fest (er wurde beim Konstruieren gestempelt), bevor gesucht wird -
    /// deshalb bleiben alle Aufrufstellen unveraendert richtig.
    template<typename T>
    T& ReplaceWindow(std::unique_ptr<T> window)
    {
        auto* oldWnd = FindNonModalWindow(window->GetID(), window->GetOwner());
        if(oldWnd)
            oldWnd->Close();
        return Show(std::move(window));
    }
    template<typename T>
    T* ToggleWindow(std::unique_ptr<T> window)
    {
        auto* oldWnd = FindNonModalWindow(window->GetID(), window->GetOwner());
        if(oldWnd)
        {
            oldWnd->Close();
            return nullptr;
        } else
            return &Show(std::move(window));
    }
    /// Registers a window to be shown after a desktop switch
    IngameWindow* ShowAfterSwitch(std::unique_ptr<IngameWindow> window);
    /// Schliesst die Fenster mit dieser ID, die DIESER Ansicht gehoeren.
    /// Der Normalfall: ein Spieler bricht seinen eigenen Strassenbau ab.
    void Close(unsigned id, unsigned owner);
    /// Schliesst die Fenster mit dieser ID in ALLEN Ansichten.
    /// Fuer Weltereignisse: verschwindet ein Gebaeude, muss das Fenster darauf bei JEDEM
    /// lokalen Spieler zugehen, der es offen hat - sonst bliebe dort ein Fenster auf ein
    /// zerstoertes Gebaeude stehen.
    void CloseAll(unsigned id);
    /// Close the window right away and free it.
    void CloseNow(IngameWindow* window);
    /// merkt einen Desktop zum Wechsel vor.
    Desktop* Switch(std::unique_ptr<Desktop> desktop);
    /// Process press of left mouse button
    void Msg_LeftDown(MouseCoords mc) override;
    /// Process release of left mouse button
    void Msg_LeftUp(MouseCoords mc) override;
    /// Process press of right mouse button
    void Msg_RightDown(const MouseCoords& mc) override;
    /// Process release of right mouse button
    void Msg_RightUp(const MouseCoords& mc) override;
    /// Process press of middle mouse button
    void Msg_MiddleDown(const MouseCoords& mc) override;
    /// Process release of middle mouse button
    void Msg_MiddleUp(const MouseCoords& mc) override;
    /// Verarbeitung des Drückens des Rad hoch.
    void Msg_WheelUp(const MouseCoords& mc) override;
    /// Verarbeitung Rad runter.
    void Msg_WheelDown(const MouseCoords& mc) override;
    /// Verarbeitung des Verschiebens der Maus.
    void Msg_MouseMove(const MouseCoords& mc) override;
    /// Verarbeitung Keyboard-Event
    void Msg_KeyDown(const KeyEvent& ke) override;
    // Show a tooltip
    // ttw: Window that the tooltip is for, used when updating current tooltip
    // tooltip: The tooltip text, empty to hide
    // updateCurrent: If true, only update if the current tooltip is for ttw
    void SetToolTip(const ctrlBaseTooltip* ttw, const std::string& tooltip, bool updateCurrent = false);

    /// Verarbeitung Spielfenstergröße verändert (vom Betriebssystem aus)
    void WindowResized() override;
    /// Verarbeitung Spielfenstergröße verändert (vom Spiel aus)
    // Achtung: nicht dieselbe Nachricht, die die Window-Klasse empfängt
    void Msg_ScreenResize(const Extent& newSize);

    /// Return the window currently on the top (probably active)
    IngameWindow* GetTopMostWindow() const;
    /// Oberstes Fenster, das DIESE Ansicht bedienen darf: ihre eigenen und die, die keiner
    /// Ansicht gehoeren (Nachrichtenboxen, Systemfenster - die sieht und bedient jeder).
    IngameWindow* GetTopMostWindow(unsigned owner) const;
    IngameWindow* FindWindowAtPos(const Position& pos) const;
    /// Sucht ein nicht-modales Fenster dieser Art bei GENAU DIESEM Besitzer.
    /// Bewusst zweistellig und ohne einstellige Ueberladung: der Compiler soll jede Aufrufstelle
    /// zeigen, statt sie still auf "irgendein Fenster dieser Art" zurueckfallen zu lassen.
    IngameWindow* FindNonModalWindow(unsigned id, unsigned owner) const;

    Desktop* GetCurrentDesktop() { return curDesktop.get(); }
    /// Makes the given window (desktop or ingame window) active and all others inactive
    void SetActiveWindow(Window&);

    void SetCursor(Cursor cursor = Cursor::Hand);
    Cursor GetCursor() const { return cursor_; }

    SnapOffset snapWindow(Window* wnd, const Rect& wndRect) const;

private:
    class Tooltip;

    /// Find the active window (desktop or ingame window)
    /// If mc is given get the window at the mouse position unless a modal window is active
    Window* findAndActivateWindow(Position mousePos);
    /// Get the active window (desktop or ingame window)
    Window* getActiveWindow() const;

    void DrawCursor();
    void DrawToolTip();

    void TakeScreenshot() const;
    /// wechselt einen Desktop
    void DoDesktopSwitch();
    /// Actually close all ingame windows marked for closing
    void CloseMarkedIngameWnds();
    /// Close the window and remove it from the window list
    void DoClose(IngameWindow* window);
    /// Setzt den ambienten Besitzer und meldet den Wechsel dem Beobachter.
    void setWindowOwner(unsigned ownerIdx);
    /// Ein Eingabeframe fuer das Gamepad im Menue. Holt NUR ab, wenn der aktuelle Desktop es
    /// will - waehrend einer Partie holt dskGameInterface selbst ab.
    void PumpPadInput();

    /// Ansicht, der neu erzeugte Fenster gehoeren; siehe ScopedWindowOwner.
    unsigned curWindowOwner_;
    IWindowOwnerObserver* ownerObserver_;

    Cursor cursor_;
    std::unique_ptr<Desktop> curDesktop;  /// aktueller Desktop
    std::unique_ptr<Desktop> nextdesktop; /// der nächste Desktop
    bool disable_mouse;                   /// Mausdeaktivator, zum beheben des "Switch-Anschließend-Drück-Bug"s

    std::list<std::unique_ptr<IngameWindow>> windows; /// Fensterliste
    /// Windows that will be shown after desktop switch
    /// Otherwise the window will not be shown, if it was added after a switch request
    std::vector<std::unique_ptr<IngameWindow>> nextWnds;
    Position lastMousePos;
    std::unique_ptr<Tooltip> curTooltip;
    Extent curRenderSize; /// current render size

    // Für Doppelklick merken:
    unsigned lastLeftClickTime; /// Zeit des letzten Links-Klicks
    Position lastLeftClickPos;  /// Position beim letzten Links-Klick

    std::unique_ptr<MenuPadInput> padInput_; /// Gamepad im Menue
    std::vector<PadEvent> padEvents_;        /// Puffer fuer genau einen Frame
    unsigned lastPadTick_;                   /// Zeitstempel des letzten Padframes
    bool hasPadTick_;                        /// ... und ob es ueberhaupt schon einen gab
};

#define WINDOWMANAGER WindowManager::inst()
