// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Window.h"
#include "ctrlList.h"
#include <optional>
struct MouseCoords;
class glFont;

/// Aufklappmenue: ein Feld mit dem aktuellen Wert und einer Liste, die darunter aufklappt.
///
/// AUFKLAPPEN, BLAETTERN, BESTAETIGEN, VERWERFEN sind seit Phase 10 vier verschiedene Dinge -
/// vorher gab es nur "Wert setzen". Am Pad heisst das A / Steuerkreuz / A / B; mit der Maus
/// Klick aufs Feld / Zeiger bewegen / Klick auf den Eintrag / Klick daneben. Beide Wege enden
/// in denselben zwei Stellen (Activate bzw. Msg_ListSelectItem) und melden dasselbe.
class ctrlComboBox final : public Window
{
public:
    ctrlComboBox(Window* parent, unsigned id, const DrawPoint& pos, const Extent& size, TextureColor tc,
                 const glFont* font, unsigned short max_list_height, bool readonly);
    ~ctrlComboBox() override;

    void Resize(const Extent& newSize) override;

    bool isReadOnly() const { return readonly; }

    void AddItem(const std::string& text);
    void DeleteAllItems();
    /// Set selection to an item if within bounds. Does not trigger a notification.
    void SetSelection(unsigned selection);
    const std::optional<unsigned>& GetSelection() const { return GetCtrl<ctrlList>(0)->GetSelection(); };
    unsigned GetNumItems() const { return GetCtrl<ctrlList>(0)->GetNumLines(); }
    const std::string& GetText(unsigned item) const { return GetCtrl<ctrlList>(0)->GetItemText(item); }
    void SetText(unsigned item, const std::string& text) { GetCtrl<ctrlList>(0)->SetItemText(item, text); }
    std::optional<std::string> GetSelectedText() const;

    void Msg_PaintAfter() override;
    bool Msg_MouseMove(const MouseCoords& mc) override;
    bool Msg_LeftDown(const MouseCoords& mc) override;
    bool Msg_LeftUp(const MouseCoords& mc) override;
    bool Msg_RightDown(const MouseCoords& mc) override;
    bool Msg_WheelUp(const MouseCoords& mc) override;
    bool Msg_WheelDown(const MouseCoords& mc) override;

    void Msg_ListSelectItem(unsigned ctrl_id, int selection) override;

    /// Steht die Liste gerade offen?
    bool IsListOpen() const { return GetCtrl<ctrlList>(0)->IsVisible(); }

    /// Solange die Liste offen ist, gehoert der Fokusrahmen um sie herum - sonst umrahmte er
    /// das geschlossene Feld, waehrend der Spieler in der Liste blaettert.
    Rect GetBoundaryRect() const override;
    /// Eine offene Liste muss mit dem Control verschwinden. Ohne das bliebe sie sichtbar UND
    /// ihre Sperre liegen, wenn der Bildschirm sie nur wegblendet.
    void SetVisible(bool visible) override;

    bool CanFocus() const override { return !readonly && IsVisible(); }
    bool Activate() override;
    bool CancelInput() override;
    void OnFocusLost() override;
    bool StepValue(const Position& dir) override;
    std::optional<ValueRange> GetValueRange() const override;

protected:
    void Draw_() override;
    /// Show or hide the list.
    void ShowList(bool show);
    Rect GetFullDrawRect(const ctrlList* list);

private:
    /// Die Auswahl von vor dem Aufklappen wiederherstellen und zuklappen - ohne eine einzige
    /// Meldung nach oben. Der Weg von B, von OnFocusLost und vom Wegblenden.
    void RestoreAndClose();
    /// Flaeche, die eine offene Liste sperrt: bis zur Wurzel (Bildschirm bzw. Ingamefenster).
    Rect GetLockRect() const;
    /// Wird dieses Control ueberhaupt gezeichnet - also es selbst und alle seine Eltern?
    bool IsEffectivelyVisible() const;

    TextureColor tc;
    const glFont* font;
    unsigned short max_list_height;
    bool readonly;
    bool suppressSelectEvent;
    /// Es wird gerade nur GEBLAETTERT: die Liste bleibt offen und das Elternfenster hoert
    /// nichts. Nur der Padpfad setzt das - der Mausklick auf einen Eintrag ist unveraendert
    /// sofort die Wahl.
    bool browsing_ = false;
    /// Auswahl im Moment des Aufklappens. Grundlage fuer "verwerfen" (zurueckstellen) und fuer
    /// "bestaetigen" (nur melden, wenn sich wirklich etwas geaendert hat).
    std::optional<unsigned> selectionOnOpen_;
};
