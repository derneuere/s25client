// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "input/FocusPath.h"
#include "Window.h"
#include "driver/KeyEvent.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {
/// Sammelt rekursiv. Fokussierbare Controls sind Blaetter - in sie wird NICHT abgestiegen,
/// sonst waeren die Kopfknoepfe einer Tabelle und die +/- Knoepfe eines Fortschrittsbalkens
/// eigene Fokusstationen. Window::IsFocusLeaf haelt den Abstieg auch dann auf, wenn das
/// Control gerade selbst keinen Fokus annehmen kann - eine LEERE Tabelle ist so weder
/// Fokusstation noch ein Tor zu ihren Sortierkoepfen.
void collectFrom(Window& wnd, std::vector<unsigned>& cur, std::vector<FocusPath::Candidate>& out)
{
    for(Window* child : wnd.GetCtrls<Window>())
    {
        if(!child->IsVisible())
            continue;
        cur.push_back(child->GetID());
        if(child->CanFocus())
            out.push_back(FocusPath::Candidate{child, cur});
        else if(!child->IsFocusLeaf())
            collectFrom(*child, cur, out);
        cur.pop_back();
    }
}

Position centerOf(const Window& wnd)
{
    const Rect r = wnd.GetBoundaryRect();
    return Position((r.left + r.right) / 2, (r.top + r.bottom) / 2);
}
} // namespace

bool FocusPath::SetRoot(Window* root)
{
    Clear();
    root_ = root;
    if(!root_)
        return false;
    if(!FocusFirst())
    {
        root_ = nullptr;
        return false;
    }
    return true;
}

void FocusPath::Clear()
{
    if(Window* old = GetFocused())
        old->OnFocusLost();
    ClearSilently();
}

void FocusPath::ClearSilently()
{
    root_ = nullptr;
    path_.clear();
    travel_ = PointF(0.f, 0.f);
    cooldownMs_ = 0;
    repeating_ = false;
}

void FocusPath::SetPath(std::vector<unsigned> newPath)
{
    if(newPath == path_)
        return;
    // Das alte Blatt zuerst - erst danach zeigt path_ woandershin. Ein Control, das etwas
    // aufgeklappt hat, klappt hier zu.
    if(Window* old = GetFocused())
        old->OnFocusLost();
    path_ = std::move(newPath);
}

Window* FocusPath::GetFocused() const
{
    if(!root_ || path_.empty())
        return nullptr;
    Window* cur = root_;
    for(const unsigned id : path_)
    {
        cur = cur->GetCtrl<Window>(id);
        if(!cur || !cur->IsVisible())
            return nullptr;
    }
    return cur;
}

std::vector<FocusPath::Candidate> FocusPath::Collect() const
{
    std::vector<Candidate> result;
    if(!root_)
        return result;
    std::vector<unsigned> cur;
    collectFrom(*root_, cur, result);
    return result;
}

bool FocusPath::FocusFirst()
{
    const auto candidates = Collect();
    if(candidates.empty())
    {
        SetPath({});
        return false;
    }
    SetPath(candidates.front().path);
    return true;
}

bool FocusPath::FocusCtrl(Window* ctrl)
{
    if(!ctrl || !root_)
        return false;
    for(const auto& c : Collect())
    {
        if(c.ctrl == ctrl)
        {
            SetPath(c.path);
            return true;
        }
    }
    return false;
}

bool FocusPath::Move(const Dir dir)
{
    if(!root_)
        return false;
    const auto candidates = Collect();
    if(candidates.empty())
    {
        SetPath({});
        return false;
    }
    const Window* focused = GetFocused();
    if(!focused)
    {
        // Die Kette ist gerissen (Control geloescht, Reiter gewechselt). Statt den Fokus zu
        // verlieren, faengt der Spieler vorne an.
        SetPath(candidates.front().path);
        return true;
    }

    if(dir == Dir::Next || dir == Dir::Prev)
    {
        const auto it = std::find_if(candidates.begin(), candidates.end(),
                                     [focused](const Candidate& c) { return c.ctrl == focused; });
        if(it == candidates.end())
        {
            SetPath(candidates.front().path);
            return true;
        }
        const auto idx = static_cast<std::ptrdiff_t>(it - candidates.begin());
        const auto next = idx + (dir == Dir::Next ? 1 : -1);
        if(next < 0 || next >= static_cast<std::ptrdiff_t>(candidates.size()))
            return false; // kein Umlauf: der Fokus bleibt stehen
        SetPath(candidates[next].path);
        return true;
    }

    // Geometrisch. Kandidat ist das fokussierbare Control, dessen Mittelpunkt im gewuenschten
    // Halbraum liegt und die kleinste gewichtete Distanz hat. Die Querabweichung wiegt
    // schwerer als die Laengsabweichung, sonst springt der Fokus quer durch das Fenster.
    const Position from = centerOf(*focused);
    const Candidate* best = nullptr;
    long bestCost = 0;
    for(const auto& c : candidates)
    {
        if(c.ctrl == focused)
            continue;
        const Position to = centerOf(*c.ctrl);
        long along = 0;
        long across = 0;
        switch(dir)
        {
            case Dir::Left:
                along = from.x - to.x;
                across = to.y - from.y;
                break;
            case Dir::Right:
                along = to.x - from.x;
                across = to.y - from.y;
                break;
            case Dir::Up:
                along = from.y - to.y;
                across = to.x - from.x;
                break;
            case Dir::Down:
                along = to.y - from.y;
                across = to.x - from.x;
                break;
            default: break;
        }
        if(along <= 0)
            continue; // nicht im gewuenschten Halbraum
        const long cost = along + 3 * std::abs(across);
        if(!best || cost < bestCost)
        {
            best = &c;
            bestCost = cost;
        }
    }
    if(!best)
        return false;
    SetPath(best->path);
    return true;
}

bool FocusPath::Activate()
{
    Window* focused = GetFocused();
    return focused && focused->Activate();
}

bool FocusPath::Cancel()
{
    Window* focused = GetFocused();
    return focused && focused->CancelInput();
}

bool FocusPath::Step(const Position& dir)
{
    if(dir == Position(0, 0))
        return false;
    Window* focused = GetFocused();
    if(!focused)
        return Move(Dir::Next);

    // Textmodus: solange ein Eingabefeld Freitext will, wandert der Fokus waagerecht nicht
    // mehr, sondern der Cursor im Feld. Genau der Weg, den auch die Tastatur nimmt.
    //
    // Zurzeit laeuft hier nichts durch: das einzige Control mit WantsTextInput() ist ctrlEdit,
    // und das ist bewusst KEINE Fokusstation der Padnavigation (ctrlEdit::CanFocus, mit
    // Begruendung). Der Zweig bleibt als der vorgesehene Anschluss stehen - er haengt an
    // Window::WantsTextInput und nicht an ctrlEdit, gilt also fuer jedes kuenftige Textcontrol.
    if(focused->WantsTextInput() && dir.y == 0)
    {
        focused->Msg_KeyDown(KeyEvent{dir.x < 0 ? KeyType::Left : KeyType::Right});
        return true;
    }

    // Erst fragt das Control, ob es den Schritt als Wertaenderung verbraucht.
    if(focused->StepValue(dir))
        return true;

    if(dir.x < 0)
        return Move(Dir::Left);
    if(dir.x > 0)
        return Move(Dir::Right);
    if(dir.y < 0)
        return Move(Dir::Up);
    return Move(Dir::Down);
}

bool FocusPath::OnPadMove(const Position& delta, const unsigned elapsedMs)
{
    if(!root_)
        return false;

    cooldownMs_ = (cooldownMs_ > elapsedMs) ? cooldownMs_ - elapsedMs : 0u;

    if(delta == Position(0, 0))
    {
        // Stick losgelassen: der naechste Ausschlag beginnt wieder mit der langen Wartezeit.
        travel_ = PointF(0.f, 0.f);
        repeating_ = false;
        cooldownMs_ = 0;
        return true;
    }

    travel_ += PointF(delta);
    if(cooldownMs_ > 0)
        return true;

    Position step(0, 0);
    if(std::abs(travel_.x) >= std::abs(travel_.y))
    {
        if(std::abs(travel_.x) >= static_cast<float>(StepDistance))
            step.x = travel_.x > 0 ? 1 : -1;
    } else if(std::abs(travel_.y) >= static_cast<float>(StepDistance))
        step.y = travel_.y > 0 ? 1 : -1;

    if(step == Position(0, 0))
        return true;

    travel_ = PointF(0.f, 0.f);
    cooldownMs_ = repeating_ ? RepeatIntervalMs : RepeatDelayMs;
    repeating_ = true;
    Step(step);
    return true;
}

bool FocusPath::OnPadButton(const PadButton button, const bool down)
{
    if(!root_)
        return false;
    // Solange der Spieler in einem Fenster steht, sieht die Welt seine Knopfflanken NICHT -
    // ein A-Druck auf einem Knopf legt keine Fahne.
    if(!down)
        return true;
    switch(button)
    {
        case PadButton::A: Activate(); break;
        // Erst fragen, ob das Blatt eine offene Eingabe hat (aufgeklappte Liste). Nur wenn
        // nicht, heisst B "raus aus dem Fenster".
        case PadButton::B:
            if(!Cancel())
                Clear();
            break;
        case PadButton::DpadLeft: Step(Position(-1, 0)); break;
        case PadButton::DpadRight: Step(Position(1, 0)); break;
        case PadButton::DpadUp: Step(Position(0, -1)); break;
        case PadButton::DpadDown: Step(Position(0, 1)); break;
        case PadButton::LeftShoulder: Move(Dir::Prev); break;
        case PadButton::RightShoulder: Move(Dir::Next); break;
        // Start bleibt bewusst wirkungslos - er ist seit Phase 3 der Knopf, mit dem ein
        // Spieler sein Pad in die Hand nimmt, und darf keine zweite Bedeutung bekommen.
        default: break;
    }
    return true;
}

void FocusPath::DrawRing(const unsigned color) const
{
    const Window* focused = GetFocused();
    if(!focused)
        return;
    const Rect r = focused->GetBoundaryRect();
    if(r.right <= r.left || r.bottom <= r.top)
        return;
    constexpr int w = 2;
    const auto fullWidth = static_cast<unsigned>(r.right - r.left + 2 * w);
    const auto height = static_cast<unsigned>(r.bottom - r.top);
    constexpr auto thickness = static_cast<unsigned>(w);
    Window::DrawRectangle(Rect(Position(r.left - w, r.top - w), Extent(fullWidth, thickness)), color);
    Window::DrawRectangle(Rect(Position(r.left - w, r.bottom), Extent(fullWidth, thickness)), color);
    Window::DrawRectangle(Rect(Position(r.left - w, r.top), Extent(thickness, height)), color);
    Window::DrawRectangle(Rect(Position(r.right, r.top), Extent(thickness, height)), color);
}
