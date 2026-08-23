// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Window.h"

struct MouseCoords;
class glFont;
class ctrlTextDeepening;
struct KeyEvent;

class ctrlEdit : public Window
{
public:
    ctrlEdit(Window* parent, unsigned id, const DrawPoint& pos, const Extent& size, TextureColor tc, const glFont* font,
             unsigned short maxlength = 0, bool password = false, bool disabled = false, bool notify = false);
    /// setzt den Text.
    void SetText(const std::string& text);
    void SetText(unsigned text);

    std::string GetText() const;
    void SetFocus(bool focus = true);
    bool HasFocus() const { return focus_; }
    void SetDisabled(bool disabled = true) { this->isDisabled_ = disabled; }
    void SetNotify(bool notify = true) { this->notify_ = notify; }
    void SetNumberOnly(const bool activated) { this->numberOnly_ = activated; }

    void Resize(const Extent& newSize) override;

    bool Msg_LeftDown(const MouseCoords& mc) override;
    bool Msg_KeyDown(const KeyEvent& ke) override;

    /// KEINE Fokusstation der Padnavigation - bewusst, und als einziges bedienbares Control.
    ///
    /// focus_ ist ein Bit AM CONTROL, nicht am Spieler, und es entscheidet, wer die Tastatur
    /// bekommt: WindowManager::RelayKeyboardMessage schickt jeden Tastendruck an das oberste
    /// Fenster, und dort nimmt ihn genau das Feld an, dessen focus_ gesetzt ist. Liesse man
    /// einen Padspieler dieses Bit setzen, wanderte die Eingabe des Tastaturspielers in sein
    /// Feld - ohne dass einer der beiden es sieht.
    ///
    /// Dagegen steht kein Verlust: ein Pad kann in ein Textfeld nichts eintippen. Der einzige
    /// Weg, den es dort haette, ist der Textcursor links/rechts (FocusPath::Step). Der Tausch
    /// waere also "der Mausspieler verliert seine Tastatur" gegen "der Padspieler darf einen
    /// Cursor schieben, den er nicht braucht".
    ///
    /// Wieder aufmachen kann man das erst zusammen mit BEIDEM: einem Fokusbegriff je Spieler
    /// (focus_ muesste zu einer Menge von Spielern werden, so wie FocusPath eine Instanz je
    /// Spieler ist) UND einer Eingabemethode fuer Pads (Bildschirmtastatur). Bis dahin ist die
    /// klare Grenze besser als die stille Uebernahme.
    ///
    /// Der Mauspfad bleibt davon voellig unberuehrt: Msg_LeftDown/SetFocus/Msg_KeyDown sind
    /// unveraendert, der Einzelspieler merkt nichts.
    bool CanFocus() const override { return false; }
    bool WantsTextInput() const override { return focus_; }

protected:
    void Draw_() override;

private:
    void AddChar(char32_t c);
    void RemoveChar();
    void Notify();
    void UpdateInternalText();

    void CursorLeft();
    void CursorRight();

    unsigned short maxLength_;
    ctrlTextDeepening* txtCtrl;
    bool isPassword_;
    bool isDisabled_;
    bool focus_ = false;
    bool notify_;

    std::u32string text_;
    /// Position of cursor in text (in UTF32 chars)
    unsigned cursorPos_ = 0;
    /// Offset of the cursor from the start of the text start position
    unsigned cursorOffsetX_ = 0;
    unsigned viewStart_ = 0;

    bool numberOnly_ = false;
};
