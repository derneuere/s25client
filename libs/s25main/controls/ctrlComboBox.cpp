// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ctrlComboBox.h"
#include <algorithm>
#include "CollisionDetection.h"
#include "Loader.h"
#include "ctrlButton.h"
#include "ctrlList.h"
#include "driver/MouseCoords.h"
#include "ogl/FontStyle.h"
#include "ogl/SoundEffectItem.h"
#include "ogl/glFont.h"

ctrlComboBox::ctrlComboBox(Window* parent, unsigned id, const DrawPoint& pos, const Extent& size, TextureColor tc,
                           const glFont* font, unsigned short max_list_height, bool readonly)
    : Window(parent, id, pos, size), tc(tc), font(font), max_list_height(max_list_height), readonly(readonly),
      suppressSelectEvent(false)
{
    AddList(0, DrawPoint(0, size.y), Extent(size.x, 4), tc, font)->SetVisible(false);

    if(!readonly)
        AddImageButton(1, DrawPoint(size.x - size.y, 0), Extent(size.y, size.y), tc, LOADER.GetImageN("io", 34));

    Resize(size);
}

ctrlComboBox::~ctrlComboBox()
{
    // Die Sperre einer AUFGEKLAPPTEN Liste liegt beim ELTERNFENSTER, nicht hier. Ohne diese
    // Zeile bliebe sie nach dem Loeschen des Controls stehen, und mit ihr eine Flaeche, die
    // keinen Mausklick mehr annimmt.
    if(GetParent() && IsListOpen())
        GetParent()->FreeRegion(this);
}

void ctrlComboBox::Resize(const Extent& newSize)
{
    Window::Resize(newSize);

    auto* button = GetCtrl<ctrlButton>(1);
    if(button)
    {
        button->SetPos(DrawPoint(newSize.x - newSize.y, 0));
        button->Resize(Extent(newSize.y, newSize.y));
    }

    auto* list = GetCtrl<ctrlList>(0);

    Extent listSize(newSize.x, 4);

    // Langsam die Höhe der maximalen annähern
    for(unsigned i = 0; i < list->GetNumLines(); ++i)
    {
        // zu große geworden?
        listSize.y += font->getHeight();
        unsigned short scaledMaxHeight = ScaleIf(Extent(0, max_list_height)).y;

        if(listSize.y > scaledMaxHeight)
        {
            // kann nicht mal ein Item aufnehmen, dann raus
            if(i == 0)
                return;

            // Höhe um eins erniedrigen, damits wieder kleiner ist als die maximale
            listSize.y -= font->getHeight();
            break;
        }
    }

    list->SetPos(DrawPoint(0, newSize.y));
    list->Resize(listSize);
}

std::optional<std::string> ctrlComboBox::GetSelectedText() const
{
    const std::optional<unsigned>& selection = GetSelection();
    if(selection)
        return GetText(*selection);
    else
        return std::nullopt;
}

bool ctrlComboBox::Msg_MouseMove(const MouseCoords& mc)
{
    // Für Button und Liste weiterleiten
    return RelayMouseMessage(&Window::Msg_MouseMove, mc);
}

bool ctrlComboBox::Msg_LeftDown(const MouseCoords& mc)
{
    auto* list = GetCtrl<ctrlList>(0);

    // Irgendwo anders hingeklickt --> Liste ausblenden
    if(!readonly && !IsPointInRect(mc.pos, GetFullDrawRect(list)))
    {
        // Der Klick VERWIRFT: was der Spieler beim Blaettern markiert hatte, gilt nicht.
        // Ausgeloest wird dabei nichts mehr, was darunter liegt - dafuer sperrt eine offene
        // Liste seit Phase 10 den ganzen Bildschirm (ShowList).
        if(list->IsVisible())
            RestoreAndClose();
        return false;
    }

    if(!readonly && IsPointInRect(mc.pos, GetDrawRect()))
    {
        // Liste wieder ein/ausblenden
        if(list->IsVisible())
            RestoreAndClose();
        else
            ShowList(true);
        return true;
    }

    // Für Button und Liste weiterleiten
    const bool ret = RelayMouseMessage(&Window::Msg_LeftDown, mc);

    // FEHLER: ein Klick auf den BEREITS AUSGEWAEHLTEN Eintrag aenderte nichts, also meldete
    // ctrlList::SetSelection nichts, also lief das einzige ShowList(false) des Auswahlpfads
    // (Msg_ListSelectItem) nie - die Liste blieb offen stehen, obwohl der Spieler gewaehlt
    // hatte. Mit der RECHTEN Maustaste ging es, weil Msg_RightDown genau diese Absicherung
    // seit jeher hat. Hier ist sie fuer die linke.
    if(!readonly && list->IsVisible() && IsPointInRect(mc.pos, list->GetDrawRect()))
        ShowList(false);
    return ret;
}

bool ctrlComboBox::Msg_LeftUp(const MouseCoords& mc)
{
    // Für Button und Liste weiterleiten
    return RelayMouseMessage(&Window::Msg_LeftUp, mc);
}

bool ctrlComboBox::Msg_RightDown(const MouseCoords& mc)
{
    auto* list = GetCtrl<ctrlList>(0);

    // Für Button und Liste weiterleiten (und danach erst schließen)
    bool ret = RelayMouseMessage(&Window::Msg_RightDown, mc);

    // Clicked on list -> close it
    if(!readonly && IsPointInRect(mc.pos, list->GetDrawRect()))
    {
        // Liste wieder ausblenden
        ShowList(false);
    }

    return ret;
}

bool ctrlComboBox::Msg_WheelUp(const MouseCoords& mc)
{
    if(readonly)
        return false;

    auto* list = GetCtrl<ctrlList>(0);
    if(list->IsVisible() && IsPointInRect(mc.pos, list->GetDrawRect()))
        return RelayMouseMessage(&Window::Msg_WheelUp, mc);

    if(IsPointInRect(mc.pos, GetDrawRect()))
    {
        // Don't scroll too far down
        if(list->GetSelection().value_or(0u) > 0u)
            list->SetSelection(*list->GetSelection() - 1u);
        return true;
    }

    return false;
}

bool ctrlComboBox::Msg_WheelDown(const MouseCoords& mc)
{
    if(readonly)
        return false;

    auto* list = GetCtrl<ctrlList>(0);

    if(list->IsVisible() && IsPointInRect(mc.pos, list->GetDrawRect()))
    {
        // Scrolled in opened list ->
        return RelayMouseMessage(&Window::Msg_WheelDown, mc);
    }

    if(IsPointInRect(mc.pos, GetDrawRect()))
    {
        // Will be ignored by the list if to high
        list->SetSelection(list->GetSelection() ? *list->GetSelection() + 1u : 0u);
        return true;
    }

    return false;
}

Rect ctrlComboBox::GetFullDrawRect(const ctrlList* list)
{
    Rect myRect = GetDrawRect();
    myRect.bottom = list->GetDrawRect().bottom;
    return myRect;
}

void ctrlComboBox::Msg_ListSelectItem(unsigned, const int selection)
{
    // BLAETTERN ist noch keine Wahl: die Liste bleibt offen und das Elternfenster hoert nichts.
    // Nur der Padpfad kommt so herein (StepValue bei offener Liste); der Mausklick auf einen
    // Eintrag ist unveraendert sofort die Wahl.
    if(browsing_)
        return;

    // Liste wieder ausblenden
    ShowList(false);

    // ist in der Liste überhaupt was drin?
    if(selection >= 0 && !suppressSelectEvent)
    {
        // Nachricht an übergeordnetes Fenster verschicken
        GetParent()->Msg_ComboSelectItem(GetID(), selection);
    }
}

bool ctrlComboBox::Activate()
{
    if(readonly || !IsVisible() || !GetParent())
        return false;
    auto* list = GetCtrl<ctrlList>(0);
    if(!list->IsVisible())
    {
        // Aufklappen - exakt der Rumpf von Msg_LeftDown beim Klick auf das Feld. Es aendert
        // sich dabei KEIN Wert.
        ShowList(true);
        return true;
    }
    // Bestaetigen. Gemeldet wird nur, wenn sich seit dem Aufklappen wirklich etwas geaendert
    // hat - genau das tut der Mausklick auf einen Listeneintrag auch, weil
    // ctrlList::SetSelection bei unveraenderter Auswahl nichts meldet.
    const std::optional<unsigned> chosen = list->GetSelection();
    ShowList(false);
    if(chosen && chosen != selectionOnOpen_)
        GetParent()->Msg_ComboSelectItem(GetID(), *chosen);
    return true;
}

bool ctrlComboBox::CancelInput()
{
    if(readonly || !IsListOpen())
        return false;
    RestoreAndClose();
    return true;
}

void ctrlComboBox::OnFocusLost()
{
    // Der Fokus wandert weiter, waehrend die Liste noch offen steht - dieselbe Wirkung wie B.
    // Ohne das bliebe eine Liste sichtbar, die niemand mehr bedient, samt ihrer Sperre.
    if(IsListOpen())
        RestoreAndClose();
}

void ctrlComboBox::RestoreAndClose()
{
    auto* list = GetCtrl<ctrlList>(0);
    // browsing_ haelt Msg_ListSelectItem still: das Zuruecksetzen ist keine Wahl.
    browsing_ = true;
    list->SetSelection(selectionOnOpen_);
    browsing_ = false;
    list->ScrollToSelection();
    ShowList(false);
}

Rect ctrlComboBox::GetBoundaryRect() const
{
    Rect result = GetDrawRect();
    const auto* list = GetCtrl<ctrlList>(0);
    if(list->IsVisible())
        result.bottom = list->GetDrawRect().bottom;
    return result;
}

void ctrlComboBox::SetVisible(const bool visible)
{
    if(!visible && IsListOpen())
        RestoreAndClose();
    Window::SetVisible(visible);
}

bool ctrlComboBox::IsEffectivelyVisible() const
{
    for(const Window* wnd = this; wnd; wnd = wnd->GetParent())
    {
        if(!wnd->IsVisible())
            return false;
    }
    return true;
}

Rect ctrlComboBox::GetLockRect() const
{
    // Bis zur Wurzel hoch: bei einem Desktop ist das der ganze Bildschirm, bei einem
    // Ingamefenster das Fenster. Die Liste kann unten darueber hinausragen, deshalb die
    // Vereinigung.
    const Window* top = this;
    while(top->GetParent())
        top = top->GetParent();
    Rect result = top->GetDrawRect();
    const Rect listRect = GetCtrl<ctrlList>(0)->GetDrawRect();
    result.left = std::min(result.left, listRect.left);
    result.top = std::min(result.top, listRect.top);
    result.right = std::max(result.right, listRect.right);
    result.bottom = std::max(result.bottom, listRect.bottom);
    return result;
}

std::optional<Window::ValueRange> ctrlComboBox::GetValueRange() const
{
    const auto* list = GetCtrl<ctrlList>(0);
    if(list->GetNumLines() == 0)
        return std::nullopt;
    return ValueRange{list->GetSelection().value_or(0u), list->GetNumLines() - 1u, ValueAxis::Vertical};
}

bool ctrlComboBox::StepValue(const Position& dir)
{
    if(readonly)
        return false;
    auto* list = GetCtrl<ctrlList>(0);
    if(list->IsVisible())
    {
        // AUFGEKLAPPT: das Steuerkreuz blaettert nur, es waehlt nicht. Auch waagerecht
        // verbraucht - solange die Liste offen ist, soll der Fokus nicht unter ihr
        // wegrutschen und sie offen zuruecklassen. Heraus fuehren A (bestaetigen), B
        // (verwerfen) und die Schultertasten (verwerfen ueber OnFocusLost).
        if(dir.y != 0)
        {
            browsing_ = true;
            list->StepValue(dir);
            browsing_ = false;
        }
        return true;
    }
    if(dir.y == 0)
        return false;
    const int numLines = static_cast<int>(list->GetNumLines());
    if(numLines == 0)
        return false;
    const int last = numLines - 1;
    const auto& sel = list->GetSelection();
    int next = (sel ? static_cast<int>(*sel) : (dir.y > 0 ? -1 : last + 1)) + dir.y;
    next = std::max(0, std::min(last, next));
    // Bewusst ueber die Liste: das loest Msg_ListSelectItem auf DIESEM Control aus und damit
    // genau den Weg, den auch ein Mausklick auf einen Listeneintrag nimmt.
    list->SetSelection(static_cast<unsigned>(next));
    return true;
}

void ctrlComboBox::AddItem(const std::string& text)
{
    GetCtrl<ctrlList>(0)->AddItem(text);
    Resize(GetSize());
}

void ctrlComboBox::DeleteAllItems()
{
    GetCtrl<ctrlList>(0)->DeleteAllItems();
    Resize(GetSize());
}

void ctrlComboBox::SetSelection(unsigned selection)
{
    // Avoid sending the change method when this is invoked intentionally
    suppressSelectEvent = true;
    GetCtrl<ctrlList>(0)->SetSelection(selection);
    suppressSelectEvent = false;
}

void ctrlComboBox::Draw_()
{
    auto* liste = GetCtrl<ctrlList>(0);

    Draw3D(Rect(GetDrawPos(), GetSize()), tc, false);

    // Show selected item in the box
    if(liste->GetNumLines() > 0)
    {
        font->Draw(GetDrawPos() + DrawPoint(2, GetSize().y / 2), liste->GetSelItemText(), FontStyle::VCENTER,
                   COLOR_YELLOW, GetSize().x - 2 - GetSize().y, "");
    }

    // Draw button manually as we can't use DrawControls which would draw the list we do in Msg_PaintAfter
    auto* button = GetCtrl<ctrlButton>(1);
    if(button)
        button->Draw();
}

void ctrlComboBox::Msg_PaintAfter()
{
    // Msg_PaintAfter laeuft auch fuer Controls, die gerade NICHT gezeichnet werden: die
    // Schleife in Window::Msg_PaintAfter prueft keine Sichtbarkeit. Ein Reiterwechsel in
    // dskOptions blendet die ganze Gruppe weg (Msg_OptionGroupChange -> ctrlGroup::SetVisible),
    // ohne dass die Combobox darin davon erfaehrt - ihre offene Liste wuerde also weiter ueber
    // allem gezeichnet und ihre Sperre bliebe liegen. Das ist die eine Stelle, die es je Frame
    // zuverlaessig bemerkt.
    if(IsListOpen() && !IsEffectivelyVisible())
        RestoreAndClose();
    else
    {
        // Draw list now so it is on top of everything
        GetCtrl<ctrlList>(0)->Draw();
    }
    Window::Msg_PaintAfter();
}

void ctrlComboBox::ShowList(bool show)
{
    auto* list = GetCtrl<ctrlList>(0);
    if(list->IsVisible() == show)
        return;

    // list field
    list->SetVisible(show);
    // Arrow button
    if(auto* button = GetCtrl<ctrlButton>(1))
        button->SetChecked(show);

    if(show)
    {
        // Womit ist der Spieler hineingegangen? Nur so kann "verwerfen" den alten Wert
        // zurueckstellen und "bestaetigen" wissen, ob es ueberhaupt etwas zu melden gibt.
        selectionOnOpen_ = list->GetSelection();
        // FEHLER: die Liste klappte immer bei Scrollposition 0 auf. Bei den knapp 30 Sprachen
        // in dskOptions sah der Spieler seine EINGESTELLTE Sprache beim Aufklappen also gar
        // nicht und wusste nicht, wo er steht.
        list->ScrollToSelection();
    }

    // Lock/unlock region of extended list
    if(GetParent())
    {
        if(show)
        {
            // Ein offenes Aufklappmenue ist MODAL. Bisher sperrte es nur seine eigene Flaeche;
            // ein Klick DANEBEN schloss zwar die Liste, drueckte aber gleichzeitig den Knopf
            // darunter, weil Window::RelayMouseMessage nicht beim ersten Treffer abbricht.
            GetParent()->LockRegion(this, GetLockRect());
        } else
            GetParent()->FreeRegion(this);
    }

    LOADER.GetSoundN("sound", 113)->Play(255, false);
}
