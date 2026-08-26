// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Point.h"
#include "driver/PadEvent.h"
#include <vector>

class Window;

/// Der Fokus GENAU EINES lokalen Spielers innerhalb GENAU EINES Fensters.
///
/// Warum ein eigenes Objekt und kein Bit auf Window: ein `bool focused_` je Control waere ein
/// GLOBALES Bit und broeche in dem Moment, in dem zwei Spieler in dasselbe Fenster schauen -
/// solange der Fensterbesitz nicht fertig ist, ist das der Normalfall. Es gaebe ausserdem
/// keinen Weg zu fragen "wer von den vieren". Vier Spieler sind vier Instanzen dieser Klasse,
/// und KEIN Control weiss davon. Genau daran haengt die harte Randbedingung: wenn kein Control
/// den Fokus kennt, kann sich fuer den Einzelspieler mit Maus nichts aendern.
///
/// Warum der Pfad eine ID-Kette und kein Zeiger ist: Controls werden zur Laufzeit geloescht
/// (Window::DeleteCtrl, ctrlTab::DeleteAllTabs, dskGameLobby). Ein gemerkter Window* hinge
/// danach. IDs sind unter Geschwistern eindeutig (erzwungen in Window::AddCtrl), also ist eine
/// Kette von der Wurzel zum Blatt die einzige Darstellung, die nicht dereferenzieren kann, was
/// es nicht mehr gibt. Bricht die Kette, ist der Fokus schlicht weg.
///
/// Kein Singleton, kein statischer Zustand, kein VIDEODRIVER, kein GAMECLIENT - nur Window und
/// Point. Damit ist die gesamte Navigation ohne Hardware, ohne Partie und ohne OpenGL pruefbar,
/// aus demselben Grund, aus dem PadRouter keiner ist.
class FocusPath
{
public:
    enum class Dir
    {
        Up,
        Down,
        Left,
        Right,
        /// Reihenfolge, in der der Autor des Fensters die Controls angelegt hat (ID-Ordnung).
        Next,
        Prev
    };

    /// Ein Control und sein Pfad von der Wurzel aus.
    struct Candidate
    {
        Window* ctrl;
        std::vector<unsigned> path;
    };

    /// Stickweg je Rasterschritt, in View-Pixeln.
    static constexpr int StepDistance = 60;
    /// Wartezeit bis zur ersten Wiederholung bzw. zwischen den folgenden.
    static constexpr unsigned RepeatDelayMs = 300;
    static constexpr unsigned RepeatIntervalMs = 110;

    /// Wurzel: das Fenster, in dem dieser Spieler gerade navigiert.
    /// Setzt den Fokus auf das erste fokussierbare Control. Gibt es keins, bleibt die Wurzel
    /// leer und die Methode liefert false - damit kann ein Spieler nie in einem Fenster
    /// festhaengen, in dem es nichts zu bedienen gibt.
    /// nullptr = der Spieler ist "in der Welt", der Fokus ist aus, und alles verhaelt sich wie
    /// vor Phase 4.
    bool SetRoot(Window* root);
    Window* GetRoot() const { return root_; }
    /// true, wenn dieser Spieler gerade in einem Fenster navigiert.
    bool IsActive() const { return root_ != nullptr; }

    /// Das Wurzelfenster wird gerade zerstoert. Backstop fuer die Lebensdauer: greift auch
    /// dann, wenn der Besitzer den Fokus nicht rechtzeitig aufloest (Desktopwechsel raeumt die
    /// Fensterliste, ohne Msg_WindowClosed zu rufen).
    void OnRootDestroyed(const Window* wnd)
    {
        // Bewusst NICHT Clear(): das benachrichtigt das fokussierte Control
        // (Window::OnFocusLost) und muesste dafuer die Kette durch eine Wurzel aufloesen, die
        // gerade zerfaellt.
        if(root_ == wnd)
            ClearSilently();
    }

    /// Aufgeloestes Blatt oder nullptr, wenn die Kette gerissen ist.
    Window* GetFocused() const;
    const std::vector<unsigned>& GetPath() const { return path_; }

    /// Muss ein Nachfahre der Wurzel sein und CanFocus() liefern.
    bool FocusCtrl(Window* ctrl);
    /// Erstes fokussierbares Control der Wurzel.
    bool FocusFirst();
    /// Fokus aufloesen und dem Blatt Bescheid geben (Window::OnFocusLost). Nur benutzen,
    /// solange die Wurzel noch LEBT.
    void Clear();
    /// Wurzel und Pfad vergessen, OHNE das Blatt anzufassen. Genau dafuer, wenn die Wurzel
    /// schon zerfallen ist: WindowManager::DoDesktopSwitch raeumt die Fensterliste, bevor er
    /// die Padnavigation abmeldet - ein Clear() wuerde dort in eine geloeschte Wurzel greifen.
    void ClearSilently();

    /// Alle fokussierbaren Controls unterhalb der Wurzel, in ID-Reihenfolge, mit Abstieg in
    /// Container, die selbst nicht fokussierbar sind (ctrlGroup, ctrlOptionGroup, ctrlTab).
    /// In ein fokussierbares Control wird NICHT abgestiegen: die Kopfknoepfe einer Tabelle und
    /// die +/- Knoepfe eines Fortschrittsbalkens sind keine eigenen Fokusstationen.
    std::vector<Candidate> Collect() const;

    /// Einen Schritt weiter. false, wenn es in der Richtung nichts gibt (der Fokus bleibt
    /// stehen). Kein Umlauf ueber die Fenstergrenze: ein Spieler kann nie versehentlich in ein
    /// fremdes Fenster rutschen.
    bool Move(Dir dir);

    /// A-Knopf: Window::Activate() auf dem Blatt. false, wenn nichts fokussiert ist.
    bool Activate();

    /// B-Knopf, ERSTE Frage: hat das fokussierte Control eine begonnene Eingabe, die es selbst
    /// verwerfen will (Window::CancelInput)? true = verbraucht, der Aufrufer macht mit B nichts
    /// weiter. Ohne diese Frage waere B ueber einer aufgeklappten Liste "Fenster zu", und die
    /// halb getroffene Auswahl stuende ungefragt.
    bool Cancel();

    /// Stickweg dieses Spielers in View-Pixeln. Sammelt bis zur Rasterweite und schiebt dann
    /// GENAU EINEN Schritt, danach mit Wiederholrate.
    /// true = verbraucht, der Weltzeiger bewegt sich in diesem Frame nicht.
    bool OnPadMove(const Position& delta, unsigned elapsedMs);
    /// Knopfflanke. true = verbraucht, der Aufrufer faellt NICHT auf die Weltaktion durch.
    bool OnPadButton(PadButton button, bool down);

    /// Zeichnet den Rahmen um das fokussierte Control. Ruft ausschliesslich
    /// Window::DrawRectangle. Bewusst getrennt von der Logik, damit die Logik ohne OpenGL
    /// pruefbar bleibt.
    void DrawRing(unsigned color) const;

private:
    /// Ein Rasterschritt in Bildschirmrichtung: erst fragt das fokussierte Control, ob es den
    /// Schritt als Wertaenderung verbraucht (Fortschrittsbalken, Scrollleiste, Liste), sonst
    /// wandert der Fokus.
    bool Step(const Position& dir);

    /// Neues Blatt setzen und dem alten Bescheid geben, dass es den Fokus verliert.
    void SetPath(std::vector<unsigned> newPath);

    Window* root_ = nullptr;
    std::vector<unsigned> path_;
    /// Aufgelaufener Stickweg seit dem letzten Rasterschritt.
    PointF travel_{0.f, 0.f};
    /// Restzeit, in der kein weiterer Rasterschritt ausgeloest wird.
    unsigned cooldownMs_ = 0;
    /// Laeuft die Wiederholung schon (dann die kuerzere Wartezeit)?
    bool repeating_ = false;
};
