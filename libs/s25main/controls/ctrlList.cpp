// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ctrlList.h"
#include "CollisionDetection.h"
#include "ctrlScrollBar.h"
#include "driver/MouseCoords.h"
#include "ogl/glFont.h"
#include <algorithm>

ctrlList::ctrlList(Window* parent, unsigned id, const DrawPoint& pos, const Extent& size, TextureColor tc,
                   const glFont* font)
    : Window(parent, id, pos, elMax(size, Extent(22, 4))), tc(tc), font(font)
{
    pagesize = (GetSize().y - 4) / font->getHeight();

    AddScrollBar(0, DrawPoint(GetSize().x - 20, 0), Extent(20, GetSize().y), 20, tc, pagesize);
}

ctrlList::~ctrlList()
{
    DeleteAllItems();
}

void ctrlList::SetSelection(const std::optional<unsigned>& selection)
{
    if(selection != selection_ && (!selection || *selection < lines.size()))
    {
        selection_ = selection;
        if(selection && GetParent())
            GetParent()->Msg_ListSelectItem(GetID(), *selection);
    }
}

bool ctrlList::Activate()
{
    if(lines.empty() || !IsVisible() || !GetParent() || !selection_)
        return false;
    // Entspricht dem Doppelklick im Mauspfad (Msg_LeftUp).
    GetParent()->Msg_ListChooseItem(GetID(), *selection_);
    return true;
}

std::optional<Window::ValueRange> ctrlList::GetValueRange() const
{
    if(lines.empty())
        return std::nullopt;
    return ValueRange{selection_.value_or(0u), static_cast<unsigned>(lines.size() - 1u), ValueAxis::Vertical};
}

bool ctrlList::StepValue(const Position& dir)
{
    if(dir.y == 0 || lines.empty())
        return false; // waagerecht: der Fokus wandert weiter
    const int last = static_cast<int>(lines.size()) - 1;
    int next = (selection_ ? static_cast<int>(*selection_) : (dir.y > 0 ? -1 : last + 1)) + dir.y;
    next = std::max(0, std::min(last, next));
    if(!selection_ || static_cast<int>(*selection_) != next)
    {
        SetSelection(static_cast<unsigned>(next));
        // Die Auswahl kann aus dem Sichtbereich laufen - die Scrollleiste muss mit.
        ScrollToSelection();
    }
    return true; // verbraucht, auch am Rand: der Fokus soll nicht aus der Liste springen
}

void ctrlList::ScrollToSelection()
{
    if(!selection_ || *selection_ >= lines.size() || pagesize == 0)
        return;
    auto* scrollbar = GetCtrl<ctrlScrollBar>(0);
    if(!scrollbar)
        return;
    // Bewusst SetScrollPos und nicht Scroll: die Liste liest die Position selbst beim Zeichnen,
    // eine Meldung nach oben gibt es beim Mauspfad an dieser Stelle auch nicht.
    const int sel = static_cast<int>(*selection_);
    const int pos = scrollbar->GetScrollPos();
    if(sel < pos)
        scrollbar->SetScrollPos(static_cast<unsigned short>(sel));
    else if(sel >= pos + static_cast<int>(pagesize))
        scrollbar->SetScrollPos(static_cast<unsigned short>(sel - static_cast<int>(pagesize) + 1));
}

bool ctrlList::Msg_MouseMove(const MouseCoords& mc)
{
    auto* scrollbar = GetCtrl<ctrlScrollBar>(0);

    mouseover_ = GetItemFromPos(mc.pos);
    // Wenn Maus in der Liste
    if(mouseover_)
    {
        const std::string itemTxt = GetItemText(*mouseover_);
        tooltip_.ShowTooltip((font->getWidth(itemTxt) > GetListDrawArea().getSize().x) ? itemTxt : "");
        return true;
    }

    tooltip_.HideTooltip();

    // Für die Scrollbar weiterleiten
    return scrollbar->Msg_MouseMove(mc);
}

bool ctrlList::Msg_LeftDown(const MouseCoords& mc)
{
    auto* scrollbar = GetCtrl<ctrlScrollBar>(0);

    const auto itemIdx = GetItemFromPos(mc.pos);
    if(itemIdx)
    {
        tooltip_.HideTooltip();
        SetSelection(*itemIdx);
        return true;
    }

    // Für die Scrollbar weiterleiten
    return scrollbar->Msg_LeftDown(mc);
}

bool ctrlList::Msg_RightDown(const MouseCoords& mc)
{
    auto* scrollbar = GetCtrl<ctrlScrollBar>(0);

    auto itemIdx = GetItemFromPos(mc.pos);
    if(itemIdx)
    {
        tooltip_.HideTooltip();
        SetSelection(*itemIdx);
        return true;
    }

    // Für die Scrollbar weiterleiten
    return scrollbar->Msg_RightDown(mc);
}

bool ctrlList::Msg_LeftUp(const MouseCoords& mc)
{
    auto* scrollbar = GetCtrl<ctrlScrollBar>(0);

    // Wenn Maus in der Liste
    if(IsPointInRect(mc.pos, GetListDrawArea()))
    {
        // Doppelklick? Dann noch einen extra Eventhandler aufrufen
        if(mc.dbl_click && GetParent() && selection_)
            GetParent()->Msg_ListChooseItem(GetID(), *selection_);

        return true;
    }

    // Für die Scrollbar weiterleiten
    return scrollbar->Msg_LeftUp(mc);
}

bool ctrlList::Msg_WheelUp(const MouseCoords& mc)
{
    // If mouse in list or scrollbar
    if(IsPointInRect(mc.pos, GetFullDrawArea()))
    {
        auto* scrollbar = GetCtrl<ctrlScrollBar>(0);
        scrollbar->Scroll(-1);
        return true;
    }

    return false;
}

bool ctrlList::Msg_WheelDown(const MouseCoords& mc)
{
    // If mouse in list
    if(IsPointInRect(mc.pos, GetFullDrawArea()))
    {
        auto* scrollbar = GetCtrl<ctrlScrollBar>(0);
        scrollbar->Scroll(+1);
        return true;
    }

    return false;
}

void ctrlList::Draw_()
{
    if(lines.empty())
        return;

    // Box malen
    Draw3D(Rect(GetDrawPos(), GetSize()), tc, false);

    // Scrolleiste zeichnen
    Window::Draw_();

    // Wieviele Linien anzeigen?
    const unsigned show_lines = (pagesize > lines.size() ? unsigned(lines.size()) : pagesize);

    const unsigned scrollbarPos = GetCtrl<ctrlScrollBar>(0)->GetScrollPos();
    DrawPoint curPos = GetDrawPos() + DrawPoint(2, 2);
    // Listeneinträge zeichnen
    for(unsigned i = 0; i < show_lines; ++i)
    {
        // Schwarze Markierung, wenn die Maus drauf ist
        if(i + scrollbarPos == mouseover_)
            DrawRectangle(Rect(curPos, Extent(GetSize().x - 22, font->getHeight())), 0x80000000);

        // Text an sich
        font->Draw(curPos, lines[i + scrollbarPos], FontStyle{},
                   (selection_ == i + scrollbarPos ? 0xFFFFAA00 : COLOR_YELLOW), GetSize().x - 22);
        curPos.y += font->getHeight();
    }
}

void ctrlList::AddItem(const std::string& text)
{
    // lines-Array ggf vergrößern
    lines.push_back(text);

    GetCtrl<ctrlScrollBar>(0)->SetRange(static_cast<unsigned short>(lines.size()));
}

void ctrlList::SetItemText(const unsigned id, const std::string& text)
{
    lines.at(id) = text;
}

void ctrlList::DeleteAllItems()
{
    lines.clear();
    selection_ = std::nullopt;
}

const std::string& ctrlList::GetItemText(unsigned line) const
{
    RTTR_Assert(line < lines.size());
    return lines[line];
}

const std::string& ctrlList::GetSelItemText() const
{
    static const std::string EMPTY;
    if(selection_)
        return GetItemText(*selection_);
    else
        return EMPTY;
}

void ctrlList::Resize(const Extent& newSize)
{
    auto* scrollbar = GetCtrl<ctrlScrollBar>(0);
    scrollbar->SetPos(DrawPoint(newSize.x - 20, 0));
    scrollbar->Resize(Extent(20, newSize.y));

    pagesize = (newSize.y - 4) / font->getHeight();

    scrollbar->SetPageSize(pagesize);

    // If the size was enlarged we have to check that we don't try to
    // display more lines than present
    if(newSize.y > GetSize().y)
        while(lines.size() - scrollbar->GetScrollPos() < pagesize && scrollbar->GetScrollPos() > 0)
            scrollbar->SetScrollPos(scrollbar->GetScrollPos() - 1);

    Window::Resize(newSize);
}

void ctrlList::Swap(unsigned first, unsigned second)
{
    // Evtl Selection auf das jeweilige Element beibehalten?
    if(first == selection_)
        selection_ = second;
    else if(second == selection_)
        selection_ = first;

    // Strings vertauschen
    std::swap(lines[first], lines[second]);
}

void ctrlList::Remove(const unsigned index)
{
    if(index < lines.size())
    {
        lines.erase(lines.begin() + index);
        if(!selection_)
            return;

        // Keep current item selected
        if(*selection_ > index)
            --*selection_;
        else if(*selection_ == index)
        {
            // Current item deleted -> clear selection
            selection_ = std::nullopt;
            if(index < GetNumLines())
                SetSelection(index); // select item now at deleted position
            else if(index > 0u)
                SetSelection(index - 1u); // or previous if item at end deleted
        }
    }
}

Rect ctrlList::GetListDrawArea() const
{
    // Full area minus scrollbar
    Rect result = GetFullDrawArea();
    Extent size = result.getSize();
    size.x -= 20;
    result.setSize(size);
    return result;
}

std::optional<unsigned> ctrlList::GetItemFromPos(const Position& pos) const
{
    const Rect listDrawArea = GetListDrawArea();
    if(!IsPointInRect(pos, listDrawArea))
        return std::nullopt;
    const unsigned itemIdx =
      (pos.y - listDrawArea.getOrigin().y) / font->getHeight() + GetCtrl<ctrlScrollBar>(0)->GetScrollPos();
    if(itemIdx >= GetNumLines())
        return std::nullopt;
    return itemIdx;
}

Rect ctrlList::GetFullDrawArea() const
{
    return Rect(GetDrawPos() + DrawPoint(2, 2), GetSize() - Extent(2, 4));
}
