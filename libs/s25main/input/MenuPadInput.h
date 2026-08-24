// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "input/FocusPath.h"
#include "input/IPadTarget.h"
#include "input/PadRouter.h"
#include "world/ViewportLayout.h"
#include <array>
#include <vector>

class Desktop;
class IngameWindow;
class Window;

/// Die Gamepadbedienung AUSSERHALB einer Partie: Hauptmenue, Kartenauswahl, Lobby, Optionen.
///
/// WARUM DIESES OBJEKT BEIM WINDOWMANAGER LIEGT und nicht bei einem Desktop:
///  - Der GERAETEBESTAND muss Desktopwechsel ueberleben. PadRouter lernt seine Geraete
///    ausschliesslich aus Connected/Disconnected; ein Router, der mit dem Desktop stirbt
///    (WindowManager::DoDesktopSwitch ersetzt curDesktop, einen unique_ptr<Desktop>), vergaesse
///    jedes Pad bei jedem Menuewechsel und koennte es nur wiederfinden, wenn der Spieler es neu
///    ansteckt.
///  - Wer gerade "dran" ist, ist im Menue oft ein IngameWindow und kein Desktop (iwConnecting
///    traegt den Uebergang von der Kartenauswahl in die Lobby, iwMsgbox jede Fehlermeldung).
///    Die Fensterliste liegt beim WindowManager; ein Desktop kann sein eigenes Modalfenster gar
///    nicht sehen.
///
/// WAS ES NICHT TUT: es holt die Ereignisse nicht selbst beim Treiber ab. Der Aufrufer
/// (WindowManager::PumpPadInput) uebergibt sie, und er tut das nur, wenn der aktuelle Desktop
/// sie ueberhaupt will (Desktop::WantsPadInput). Waehrend einer Partie liegt die Abholung
/// unveraendert bei dskGameInterface::UpdateInput - es gibt keinen Frame, in dem zwei Stellen
/// abholen.
///
/// EIN FOKUS JE SLOT. In den normalen Menues gibt es genau einen Slot: zwei Leute, die
/// gleichzeitig verschiedene Knoepfe druecken, wuerden zwei Desktopwechsel ausloesen, von denen
/// der zweite den ersten ueberholt. Nur der Zuordnungsbildschirm der Lobby meldet mehr
/// (Desktop::GetNumPadSlots), und dort ist genau das der Zweck.
class MenuPadInput final : public IPadTarget
{
public:
    static constexpr unsigned MaxSlots = MAX_VIEWPORTS;
    static constexpr unsigned NoSlot = PadRouter::NoSlot;

    MenuPadInput();

    /// Ein Eingabeframe. Navigiert wird im obersten offenen Fenster, sonst auf dem Desktop;
    /// die Knoepfe, die die Fokusnavigation nicht verbraucht, beantwortet der Desktop.
    void Pump(const std::vector<PadEvent>& events, unsigned elapsedMs, Desktop* desktop, IngameWindow* topWnd);

    /// Fokusrahmen aller Slots, die ein Geraet haben. Ruft ausschliesslich FocusPath::DrawRing.
    void DrawRings() const;

    /// Der Slot, dessen Knopfdruck GERADE zugestellt wird - NoSlot ausserhalb der Zustellung.
    /// Bauform wie WindowManager::GetCurrentWindowOwner: die Aufrufstelle (ein Msg_ButtonClick
    /// tief in einem Desktop) nennt den Spieler nicht, laeuft aber unter einer Klammer, die ihn
    /// kennt.
    unsigned GetActingSlot() const { return actingSlot_; }
    /// Das Geraet, das gerade handelt - InvalidPadDevice ausserhalb der Zustellung.
    PadDeviceId GetActingDevice() const;

    PadRouter& GetRouter() { return router_; }
    const PadRouter& GetRouter() const { return router_; }
    const FocusPath& GetFocus(unsigned slot) const { return focus_[slot]; }
    /// Hat dieser Slot ein Geraet?
    bool HasDevice(unsigned slot) const { return slot < MaxSlots && hasDevice_[slot]; }

    /// Ein Fenster oder Desktop verschwindet. Deterministischer Ersatz fuer den
    /// Destruktor-Backstop, den IngameWindow fuer die Ingame-Fokusse hat: der WindowManager
    /// meldet es beim Desktopwechsel und beim Schliessen eines Fensters.
    void OnRootDestroyed(const Window* wnd);
    /// Fokus aller Slots loeschen (der Geraetebestand bleibt).
    void ClearFocus();
    /// Alles vergessen, auch den Geraetebestand. Nur fuer WindowManager::CleanUp und fuer
    /// Tests, die einen frischen Prozesszustand brauchen.
    void Reset();

private:
    void OnPadAssigned(unsigned slot, bool assigned) override;
    void OnPadMove(unsigned slot, const Position& delta) override;
    void OnPadCamera(unsigned slot, const Position& delta) override;
    void OnPadZoom(unsigned slot, float step) override;
    void OnPadButton(unsigned slot, PadButton button, bool down) override;

    /// Wurzel dieses Slots neu setzen und den Einstiegspunkt des Desktops beruecksichtigen.
    void ResetFocus(unsigned slot);

    PadRouter router_;
    std::array<FocusPath, MaxSlots> focus_;
    std::array<bool, MaxSlots> hasDevice_{};
    /// Der AUFNAHMEDRUCK wirkt nicht. Wer sein Pad mit A in die Hand nimmt, bekommt in
    /// demselben Frame seinen Slot UND seinen Fokus - ohne diese Bremse wuerde derselbe
    /// Tastendruck sofort das erste Control ausloesen, in der Lobby also "Spiel starten".
    /// Bewusst frameweit und nicht knopfweit: die Uebernahme kann auch durch einen Stick
    /// geschehen, und dann darf kein gleichzeitig gedrueckter Knopf durchrutschen.
    std::array<bool, MaxSlots> swallowFrame_{};

    Window* root_ = nullptr;
    Desktop* desktop_ = nullptr;
    IngameWindow* rootWnd_ = nullptr;
    unsigned stepMs_ = 0;
    unsigned actingSlot_ = NoSlot;
};
