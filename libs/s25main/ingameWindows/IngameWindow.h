// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "DrawPoint.h"
#include "Point.h"
#include "SnapOffset.h"
#include "Window.h"
#include "helpers/EnumArray.h"
#include "gameData/const_gui_ids.h"
#include <array>
#include <vector>

class FocusPath;
class glArchivItem_Bitmap;
struct MouseCoords;
struct PersistentWindowSettings;
template<typename T>
struct Point;

enum CloseBehavior
{
    /// Closeable via right-click, button, keyboard (ESC, ALT+W)
    Regular,
    /// Close behavior is managed by window, e.g. explicit button
    Custom,
    /// Same as Regular, but doesn't (auto-)close on right-click
    NoRightClick,
};

enum class IwButton
{
    Close,
    Title, /// Pseudo-button to respond to double-clicks on the title bar
    PinOrMinimize
};
constexpr auto maxEnumValue(IwButton)
{
    return IwButton::PinOrMinimize;
}

class IngameWindow : public Window
{
public:
    /// Special position that gets translated to the last know position or screen center when passed to the ctor
    static const DrawPoint posLastOrCenter;
    /// Special position that gets translated to the screen center when passed to the ctor
    static const DrawPoint posCenter;
    /// Special position that gets translated to the mouse position when passed to the ctor
    static const DrawPoint posAtMouse;

    static const Extent borderSize;

    IngameWindow(unsigned id, const DrawPoint& pos, const Extent& size, std::string title,
                 glArchivItem_Bitmap* background, bool modal = false,
                 CloseBehavior closeBehavior = CloseBehavior::Regular, Window* parent = nullptr);
    ~IngameWindow() override;

    /// Set background image
    void SetBackground(glArchivItem_Bitmap* background) { this->background = background; }
    /// Get background image
    glArchivItem_Bitmap* GetBackground() const { return background; }

    /// Set window title
    void SetTitle(const std::string& title) { this->title_ = title; }
    /// Get window title
    const std::string& GetTitle() const { return title_; }

    void Resize(const Extent& newSize) override;
    /// Set the size of the (expanded) content area
    void SetIwSize(const Extent& newSize);
    /// Get the size of the (expanded) content area
    Extent GetIwSize() const;
    /// Get the full size of the window, even when minimized
    Extent GetFullSize() const;
    /// Get the current lower right corner of the content area
    DrawPoint GetRightBottomBoundary();

    /// Set the position for the window after adjusting newPos so the window is in the visible area
    void SetPos(DrawPoint newPos, bool saveRestorePos = true);

    /// Queue the window for closing, will be done in next draw cycle
    virtual void Close();
    /// Return if the window will be closes
    bool ShouldBeClosed() const { return closeme; }

    /// Minimize window (only title bar and bottom border remains)
    void SetMinimized(bool minimized = true);
    /// Return whether the window is minimized
    bool IsMinimized() const { return isMinimized_; }

    void SetPinned(bool pinned = true);
    bool IsPinned() const { return isPinned_; }

    CloseBehavior getCloseBehavior() const { return closeBehavior_; }

    /// Modal windows cannot be minimized, are always active and stay on top of non-modal ones
    bool IsModal() const { return isModal_; }

    GUI_ID GetGUIID() const { return static_cast<GUI_ID>(Window::GetID()); }

    /// Nummer der ANSICHT, der dieses Fenster gehoert, oder SHARED_WINDOW_OWNER
    /// (WindowManager.h) fuer Fenster, die dem Bildschirm gehoeren.
    ///
    /// Die Fensterkennung ist ab jetzt das PAAR (GUI_ID, Besitzer). GetGUIID() behaelt seine
    /// bisherige Bedeutung "Art des Fensters" - Settings, iwHelp und die bestehenden Tests
    /// lesen sie unveraendert. Der Besitzer wird beim KONSTRUIEREN gestempelt, aus der offenen
    /// Eingabeklammer (WindowManager::ScopedWindowOwner); deshalb steht er schon fest, bevor
    /// ToggleWindow/ReplaceWindow nach einem Vorgaenger suchen.
    unsigned GetOwner() const { return ownerIdx_; }
    /// Nur fuer die wenigen Stellen, an denen der Besitzer NICHT aus der Klammer stammt.
    void SetOwner(unsigned ownerIdx) { ownerIdx_ = ownerIdx; }

    bool Msg_LeftDown(const MouseCoords&) override;
    bool Msg_LeftUp(const MouseCoords&) override;
    bool Msg_MiddleDown(const MouseCoords&) override;
    bool Msg_MiddleUp(const MouseCoords&) override;
    bool Msg_MouseMove(const MouseCoords&) override;

    /// Fokusrahmen, die dieses Fenster ueber seine Controls zeichnen soll.
    ///
    /// Ein Rahmen gehoert dem SPIELER (PlayerView::GetFocus()), nicht dem Fenster - deshalb ist
    /// es eine LISTE und kein einzelner Platz: solange die Fensterliste des WindowManagers
    /// gemeinsam ist, koennen mehrere lokale Spieler gleichzeitig in demselben Fenster stehen.
    /// Mit einem einzigen Platz ueberschriebe der zweite den Rahmen des ersten, und das
    /// Abmelden des einen risse den des anderen mit weg (Befund B4).
    /// Abgemeldet wird an derselben Stelle, an der auch PlayerView::actionwindow genullt wird
    /// (dskGameInterface::Msg_WindowClosed).
    /// Einzelspieler mit Maus: die Liste bleibt leer, es wird kein einziger Zeichenaufruf
    /// zusaetzlich abgesetzt.
    void AddFocusRing(FocusPath& focus, unsigned color);
    void RemoveFocusRing(const FocusPath& focus);
    /// Ist der Rahmen DIESES Fokus an diesem Fenster angemeldet?
    bool HasFocusRing(const FocusPath& focus) const;
    /// Wie viele lokale Spieler stehen gerade in diesem Fenster? Beobachtbar gemacht, weil an
    /// dieser Zahl die LEBENSDAUER haengt: bleibt hier ein Eintrag stehen, waehrend der
    /// zugehoerige FocusPath schon tot ist, greift ~IngameWindow auf freigegebenen Speicher zu.
    unsigned GetNumFocusRings() const { return static_cast<unsigned>(focusRings_.size()); }
    void Msg_PaintAfter() override;

protected:
    void Draw_() final;
    /// Called when not minimized before drawing the frame
    virtual void DrawBackground();
    /// Called when not minimized after the frame and background have been drawn
    virtual void DrawContent() {}

    /// Move window to center of screen
    void MoveToCenter();
    /// Move window next to current cursor position
    void MoveNextToMouse();

    /// Return if events (mouse move...) should be passed to controls of the window
    bool IsMessageRelayAllowed() const override;

    void SaveOpenStatus(bool isOpen) const;

    void StartDragging(const Position& pos);

    // Return true if dragging stopped
    bool StopDragging();

    unsigned short iwHeight;
    std::string title_;
    glArchivItem_Bitmap* background;
    DrawPoint lastMousePos;

    /// Offset from left and top to actual content
    Extent contentOffset;
    /// Offset from content to right and bottom boundary
    Extent contentOffsetEnd;
    CloseBehavior closeBehavior_;

private:
    /// Get bounds of given button
    Rect GetButtonBounds(IwButton btn) const;

    /// Siehe GetOwner(). Gesetzt im Konstruktor aus der offenen Eingabeklammer.
    unsigned ownerIdx_;
    bool isModal_;
    bool closeme;
    bool isPinned_;
    bool isMinimized_;
    bool isMoving;
    SnapOffset snapOffset_;
    helpers::EnumArray<ButtonState, IwButton> buttonStates_;
    PersistentWindowSettings* windowSettings_;
    DrawPoint restorePos_;
    struct FocusRing
    {
        FocusPath* focus;
        unsigned color;
    };
    /// Hoechstens so viele Eintraege, wie es lokale Spieler gibt (MAX_VIEWPORTS).
    std::vector<FocusRing> focusRings_;
};
