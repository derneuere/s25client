// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Point.h"
#include "driver/PadEvent.h"
#include "helpers/EnumArray.h"
#include "input/IPadTarget.h"
#include <vector>

/// Verteilt Gamepad-Ereignisse auf lokale Ansichten (Slots).
///
/// Bewusst KEIN Singleton, kein OpenGL, kein Videotreiber, kein Bezug auf GameClient: die
/// gesamte Zuordnung, die Totzone und die Flankenerkennung sind damit ohne Hardware und ohne
/// laufende Partie pruefbar. Der Aufrufer holt die Ereignisse beim Treiber ab
/// (IVideoDriver::FetchPadEvents) und schiebt sie hier hinein.
///
/// Zuordnungsregel - UEBERNAHME DURCH BENUTZUNG: ein Pad bekommt einen Slot erst, wenn es
/// tatsaechlich BENUTZT wird (Knopfdruck, oder Stickausschlag jenseits der Totzone). Das
/// blosse Anstecken reserviert NICHTS. Wer benutzt, bekommt den niedrigsten freien Slot; die
/// Reihenfolge ist also die der ersten Benutzung, nicht die des Ansteckens.
///
/// Warum nicht schon beim Anstecken? Weil der Videotreiber auch fuer bereits gesteckte Pads
/// ein Connected meldet (SDL_CONTROLLERDEVICEADDED beim Start). Ein Slot beim Anstecken haette
/// im Einzelspieler dem Mauszeiger die einzige Ansicht abgenommen, sobald irgendein Controller
/// am Rechner haengt - der Nutzer haette ihn dafuer nicht einmal anfassen muessen. "Angesteckt"
/// ist eben keine Absicht, "benutzt" schon. Genau darum bleibt der Einzelspieler mit Pad am
/// Fernseher moeglich: er drueckt einmal einen Knopf und hat seine Ansicht.
///
/// Wird ein Pad abgezogen, wird sein Slot wieder frei; ein bereits benutztes, aber unversorgtes
/// Pad rueckt nach. Pads ueber der Slotzahl hinaus werden gefuehrt, aber nicht zugeordnet
/// (GetSlot == NoSlot) und erzeugen keine Wirkung. Eine feste Zuordnung setzt AssignSlot - sie
/// gilt selbst als Benutzung und ist die Stelle, an der spaeter Lobby oder Optionen ansetzen.
class PadRouter
{
public:
    static constexpr unsigned NoSlot = ~0u;

    /// Radiale Totzone auf dem Stickpaar (NICHT je Achse). Unterhalb passiert exakt nichts.
    static constexpr float Deadzone = 0.25f;
    /// Zeigergeschwindigkeit bei Vollausschlag, in View-Pixeln je Sekunde.
    static constexpr float PixelsPerSecond = 900.f;
    /// Kamerageschwindigkeit des RECHTEN Sticks bei Vollausschlag, in View-Pixeln je Sekunde.
    ///
    /// Bewusst schneller als der Zeiger: der Zeiger wird auf einen Knoten gesetzt und muss dafuer
    /// fein sein, die Kamera legt Strecke zurueck. Wer eine Strasse ueber mehrere Bildschirmbreiten
    /// baut, faehrt die Kamera und setzt den Zeiger - nicht umgekehrt.
    static constexpr float CameraPixelsPerSecond = 1400.f;
    /// Totzone der Trigger. Eigener Wert, weil ein Trigger einseitig ist (Ruhelage 0, nicht
    /// Mitte) und schon durch leichtes Aufliegen des Fingers Werte um 0.05 liefert.
    static constexpr float TriggerDeadzone = 0.15f;
    /// Relative Zoomaenderung je Sekunde bei voll durchgezogenem Trigger. 0.9 heisst: eine
    /// Sekunde ziehen vergroessert um etwa 90 Prozent - von 1.0 auf 1.9, also knapp zwei der
    /// sieben Stufen aus ZOOM_FACTORS. Das ist am Fernseher gerade noch verfolgbar und braucht
    /// keine zweite Bedienhandlung fuer den vollen Bereich.
    static constexpr float ZoomPerSecond = 0.9f;

    /// Zahl der Ansichten, die ueberhaupt ein Pad annehmen koennen. Schrumpft die Zahl, verlieren
    /// die Geraete auf zu hohen Slots ihre Zuordnung; wachsen sie, bekommen bisher unversorgte
    /// Geraete einen Slot. Beides wird beim naechsten Step gemeldet.
    void SetNumSlots(unsigned numSlots);
    unsigned GetNumSlots() const { return numSlots_; }

    void OnEvent(const PadEvent& ev);
    void OnEvents(const std::vector<PadEvent>& events);

    /// NoSlot, wenn das Geraet unbekannt, unbenutzt oder unversorgt ist.
    unsigned GetSlot(PadDeviceId device) const;
    /// Wurde dieses Geraet schon benutzt? Ein benutztes Geraet bleibt benutzt, bis es abgezogen
    /// wird - wer das Pad einmal in der Hand hatte, verliert seine Ansicht nicht dadurch, dass
    /// er kurz nichts drueckt.
    bool IsActive(PadDeviceId device) const;
    /// Feste Zuordnung. Verdraengt ein evtl. dort sitzendes Geraet auf "unversorgt".
    /// false, wenn das Geraet unbekannt oder slot >= GetNumSlots() ist.
    bool AssignSlot(PadDeviceId device, unsigned slot);
    /// Alle bekannten (= angesteckten) Geraete in Ansteckreihenfolge.
    std::vector<PadDeviceId> GetDevices() const;
    /// Zahl der Geraete MIT Slot.
    unsigned GetNumAssigned() const;

    /// Erster von zwei Schritten je Frame: meldet Slotwechsel und schreibt die Zeiger fort.
    ///
    /// elapsedMs kommt vom AUFRUFER und nicht aus VIDEODRIVER - nur so ist der zurueckgelegte
    /// Weg im Test reproduzierbar.
    void UpdateMotion(unsigned elapsedMs, IPadTarget& target);
    /// Zweiter Schritt: liefert die aufgelaufenen Knopfflanken aus.
    ///
    /// Bewusst NACH UpdateMotion und bewusst getrennt: erst muss der Aufrufer den fortgeschriebenen
    /// Zeiger in seine Ansicht uebernommen haben, sonst wirkt ein Knopfdruck auf den Punkt des
    /// vorigen Frames.
    void DispatchButtons(IPadTarget& target);

    /// Nur die gefilterte Stickauslenkung. Oeffentlich, damit die Kennlinie ohne Umweg ueber
    /// einen Frame pruefbar ist. Laenge <= Deadzone -> (0,0), sonst der Vektor unveraendert
    /// (lineare Kennlinie, bewusst keine Beschleunigung).
    static PointF FilterStick(PointF stick);
    /// Dasselbe fuer einen Trigger: <= TriggerDeadzone -> 0, darueber linear auf [0,1]
    /// GEDEHNT. Ohne die Dehnung sprungt der Zoom beim Ueberschreiten der Totzone von 0 auf
    /// 15 Prozent Geschwindigkeit - fuehlbar als Ruck, gerade am Fernseher.
    static float FilterTrigger(float value);

    void Clear();

private:
    struct Device
    {
        PadDeviceId id = InvalidPadDevice;
        unsigned slot = NoSlot;
        /// Siehe Klassenkommentar: erst die Benutzung macht aus "steckt" ein "steuert".
        bool active = false;
        helpers::EnumArray<float, PadAxis> axes{};
        helpers::EnumArray<bool, PadButton> pressed{};
        /// Subpixelrest, damit kleine Auslenkungen nicht Frame fuer Frame verschluckt werden
        PointF fraction{0.f, 0.f};
        /// Derselbe Subpixelrest fuer die Kamera. Eigener Speicher, weil beide Sticks
        /// gleichzeitig geschoben werden koennen und sich ihre Reste sonst vermischten.
        PointF cameraFraction{0.f, 0.f};
    };

    struct ButtonEdge
    {
        unsigned slot;
        PadButton button;
        bool down;
    };

    Device* Find(PadDeviceId device);
    const Device* Find(PadDeviceId device) const;
    bool IsSlotTaken(unsigned slot) const;
    unsigned FirstFreeSlot() const;
    /// Das Geraet gilt ab jetzt als benutzt und bekommt, wenn einer frei ist, sofort einen Slot.
    /// Muss VOR dem Einreihen einer Knopfflanke laufen: sonst faende die Flanke ihr Geraet noch
    /// ohne Slot vor und der erste Knopfdruck ginge verloren.
    void Activate(Device& dev);
    /// Ist dieses Ereignis eine Benutzung? Knopf: jedes Druecken (kein Loslassen). Achse: erst
    /// jenseits der Totzone - ein driftender oder schief kalibrierter Stick soll keine Ansicht
    /// an sich reissen. Fuer das linke Stickpaar gilt dieselbe RADIALE Totzone, die auch die
    /// Bewegung ausloest, damit "bewegt sich" und "uebernimmt" nicht auseinanderfallen koennen.
    bool IsUsage(const Device& dev, const PadEvent& ev) const;
    /// Alle gedrueckten Knoepfe dieses Geraets kuenstlich loslassen, damit keine Aktion
    /// haengenbleibt, wenn ein Pad mitten im Druck abgezogen wird.
    void ReleaseAll(Device& dev);

    std::vector<Device> devices_;
    std::vector<ButtonEdge> pendingButtons_;
    /// (slot, assigned), in Entstehungsreihenfolge
    std::vector<std::pair<unsigned, bool>> pendingAssignments_;
    unsigned numSlots_ = 0;
};
