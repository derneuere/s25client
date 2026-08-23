// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "IngameWindow.h"
#include <cstdint>
#include <optional>

/// Basis der fuenf Wirtschaftsfenster: iwMilitary, iwDistribution, iwTools, iwTransport,
/// iwBuildOrder.
///
/// Ihre Besonderheit - und der Grund fuer diese Klasse: ein Knopfdruck erzeugt hier KEIN
/// Kommando. Er merkt nur vor, dass etwas offen ist. Gesendet wird erst spaeter, aus einem
/// 2-Sekunden-Timer oder beim Schliessen des Fensters.
///
/// Genau daran reicht GameClient::ScopedActingPlayer allein nicht: die Klammer, die
/// dskGameInterface::OnPadButton um die Verarbeitung EINER Padflanke legt, ist zum
/// Sendezeitpunkt laengst wieder zu. Msg_Timer kommt aus WindowManager::Draw, Close() vom
/// Schliesskreuz der Maus - beides ausserhalb jeder Klammer. Ohne Gegenmassnahme buchte
/// GameClient::AddGC die Aenderung auf den Hauptspieler: Spieler 1 bewegt den Regler,
/// Spieler 0 bekommt die neue Einstellung.
///
/// Die Loesung sitzt hier, weil hier - und nur hier - die Verzoegerung entsteht: zusammen mit
/// der offenen Aenderung wird festgehalten, WER sie ausgeloest hat, und beim Senden legt das
/// Fenster die Klammer selbst wieder an. Der gemerkte Spieler ist Teil derselben Vormerkung
/// wie das Flag und wird mit ihm gesetzt und geloescht - deshalb sind beide privat und nur
/// ueber MarkSettingsChanged()/OnSettingsTransmitted() erreichbar.
///
/// Bewusst NICHT geloest ueber eine eigene Fabrik im Fenster: welchem Spieler ein Fenster
/// gehoert, entscheidet der noch fehlende Fensterbesitz (Phase 4, Schritt 3). Solange der
/// fehlt, koennen zwei Spieler dasselbe Fenster bedienen; dann gewinnt der zuletzt
/// Aendernde, und die gesamte Einstellungsgruppe geht an ihn. Das ist die bewusst in Kauf
/// genommene Grenze dieser Loesung.
class TransmitSettingsIgwAdapter : public IngameWindow
{
public:
    TransmitSettingsIgwAdapter(unsigned id, const DrawPoint& pos, const Extent& size, const std::string& title,
                               glArchivItem_Bitmap* background, bool modal = false);

    /// Updates the control elements with values from visual settings
    virtual void UpdateSettings() = 0;
    /// sends potential changes to the client
    virtual void TransmitSettings() = 0;

    void Close() override;

    void Msg_Timer(unsigned ctrl_id) override;
    void Msg_MsgBoxResult(unsigned msgbox_id, MsgboxResult mbr) override;

protected:
    static constexpr unsigned firstCtrlID = 1000000u;

    /// Eine Aenderung ist aufgelaufen. Haelt zusaetzlich fest, WER sie ausgeloest hat
    /// (GameClient::GetActingPlayer): nullopt = Maus, Tastatur, Einzelspieler - also wie bisher
    /// der Hauptspieler.
    void MarkSettingsChanged();
    /// Liegt eine noch nicht uebertragene Aenderung vor?
    bool HasPendingSettings() const { return settingsChanged_; }
    /// Die Uebertragung ist geglueckt: Vormerkung und Spielerbezug sind erledigt.
    void OnSettingsTransmitted();

private:
    /// Ruft TransmitSettings() unter der Klammer des gemerkten Spielers. Einziger Weg, auf dem
    /// diese Fenster senden duerfen.
    void TransmitSettingsForPendingPlayer();

    /// whether any settings where changed after the last successful transmission
    bool settingsChanged_;
    /// Wer hat sie geaendert? Gilt nur zusammen mit settingsChanged_.
    std::optional<uint8_t> pendingPlayer_;
};
