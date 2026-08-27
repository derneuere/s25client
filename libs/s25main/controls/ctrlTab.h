// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Window.h"
#include <array>

class ctrlGroup;
struct MouseCoords;
class glArchivItem_Bitmap;

class ctrlTab : public Window
{
public:
    ctrlTab(Window* parent, unsigned id, const DrawPoint& pos, unsigned short width);

    /// fügt eine Tab hinzu.
    ctrlGroup* AddTab(glArchivItem_Bitmap* image, const std::string& tooltip, unsigned id);
    /// löscht alle Tabs.
    void DeleteAllTabs();
    /// aktiviert eine bestimmte Tabseite.
    void SetSelection(unsigned short nr, bool notify = false);
    /// Gibt ID des aktuell gewählten Tabs zurück
    unsigned GetCurrentTab() const { return tabs[tab_selection]; }
    /// Gibt Tab-Group zurück, über die die Steuerelemente der Tab angesprochen werden können
    ctrlGroup* GetGroup(unsigned tab_id);
    /// Dasselbe lesend. Gebraucht vom Padpfad: iwAction::GetPadBrief ist const und muss einen
    /// fokussierten Knopf seinem Reiter zuordnen koennen.
    const ctrlGroup* GetGroup(unsigned tab_id) const;
    /// Gibt aktuell ausgewählte Tab-Gruppe zürck
    ctrlGroup* GetCurrentGroup() { return GetGroup(GetCurrentTab()); }

    /// Zahl der angelegten Reiter.
    ///
    /// Neu fuer den Padpfad: die Reiterkoepfe sind Fokusstationen (es sind ctrlButton mit den
    /// IDs 0..n-1), und der Klartextkasten muss zu einem fokussierten Kopf sagen koennen, WELCHER
    /// Reiter das ist. Ohne diese beiden Auskuenfte bliebe nur die ID des Knopfes, und die ist
    /// die Position und nicht die Reiterkennung - die Zuordnung liegt allein in `tabs`.
    /// Reine Lesezugriffe; am Verhalten fuer Maus und Tastatur aendert sich nichts.
    unsigned short GetNumTabs() const { return tab_count; }
    /// Die Kennung des Reiters an Position `nr` - dieselbe, die AddTab bekommen hat und mit der
    /// GetGroup arbeitet.
    unsigned GetTabIdAt(unsigned short nr) const { return tabs[nr]; }

    void Msg_Group_ButtonClick(unsigned group_id, unsigned ctrl_id) override;
    void Msg_Group_EditEnter(unsigned group_id, unsigned ctrl_id) override;
    void Msg_Group_EditChange(unsigned group_id, unsigned ctrl_id) override;
    void Msg_Group_TabChange(unsigned group_id, unsigned ctrl_id, unsigned short tab_id) override;
    void Msg_Group_ListSelectItem(unsigned group_id, unsigned ctrl_id, int selection) override;
    void Msg_Group_ComboSelectItem(unsigned group_id, unsigned ctrl_id, unsigned selection) override;
    void Msg_Group_CheckboxChange(unsigned group_id, unsigned ctrl_id, bool checked) override;
    void Msg_Group_ProgressChange(unsigned group_id, unsigned ctrl_id, unsigned short position) override;
    void Msg_Group_ScrollShow(unsigned group_id, unsigned ctrl_id, bool visible) override;
    void Msg_Group_OptionGroupChange(unsigned group_id, unsigned ctrl_id, unsigned selection) override;
    void Msg_Group_Timer(unsigned group_id, unsigned ctrl_id) override;
    void Msg_Group_TableSelectItem(unsigned group_id, unsigned ctrl_id,
                                   const std::optional<unsigned>& selection) override;
    void Msg_Group_TableRightButton(unsigned group_id, unsigned ctrl_id,
                                    const std::optional<unsigned>& selection) override;
    void Msg_Group_TableLeftButton(unsigned group_id, unsigned ctrl_id,
                                   const std::optional<unsigned>& selection) override;
    void Msg_ButtonClick(unsigned ctrl_id) override;
    /// BEFUND P3: ein Klick auf den SCHON GEWAEHLTEN Reiterkopf laesst alles, wie es ist.
    ///
    /// SetSelection setzt fuer nr == tab_selection Schritt fuer Schritt dieselben Werte noch
    /// einmal: erst Red1, dann Green1 auf denselben Knopf; erst unsichtbar, dann sichtbar auf
    /// dieselbe Gruppe; tab_selection bekommt seinen eigenen Wert. Nach aussen bleibt allein die
    /// Meldung Msg_TabChange, und die traegt die Kennung des Reiters, der ohnehin schon offen
    /// ist. GEMESSEN an den beiden einzigen Traegern eines ctrlTab im ganzen Baum: iwAction
    /// rechnet daraus eine Fensterhoehe, also dieselbe wie vorher; iwDistribution ueberschreibt
    /// Msg_TabChange gar nicht erst.
    ///
    /// Die Antwort gilt nur fuer die REITERKOEPFE. Es sind die Kinder mit den Kennungen
    /// 0..tab_count-1 (AddTab: AddImageButton(tab_count, ...)); die Reitergruppen tragen die
    /// Kennung tabs.size() + 1 + id und sind keine Knoepfe, ihre eigenen Knoepfe haben die
    /// GRUPPE zum Elternteil und nicht diesen Reiter.
    ///
    /// WAS DAS FUER DEN MAUSSPIELER AENDERT: nichts. Der Mausklick laeuft ueber
    /// ctrlButton::Msg_LeftUp und nicht ueber Activate(); diese Frage wird auf seinem Weg
    /// nirgends gestellt.
    bool WouldChildClickDoAnything(unsigned ctrlId) const override
    {
        return ctrlId < static_cast<unsigned>(tab_count) && ctrlId != static_cast<unsigned>(tab_selection);
    }
    bool Msg_LeftDown(const MouseCoords& mc) override;
    bool Msg_LeftUp(const MouseCoords& mc) override;
    bool Msg_WheelUp(const MouseCoords& mc) override;
    bool Msg_WheelDown(const MouseCoords& mc) override;
    bool Msg_MouseMove(const MouseCoords& mc) override;

protected:
    void Draw_() override;

private:
    unsigned short tab_count;
    unsigned short tab_selection;

    std::array<unsigned, 20> tabs; //-V730_NOINIT
};
