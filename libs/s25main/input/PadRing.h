// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Point.h"
#include <vector>

/// DAS KREISMENUE EINES PADSPIELERS - der Ring, den der Auftraggeber zweimal bestellt hat.
///
/// Diese Datei ist der REINE Teil davon: sie kennt Point und sonst nichts. Kein OpenGL, kein
/// Window, kein Viewer, kein VIDEODRIVER. Damit ist die gesamte Ringrechnung - wieviele
/// Sektoren, wo liegt welcher, welcher ist gewaehlt - ohne Grafik und ohne laufende Partie
/// pruefbar, aus demselben Grund, aus dem PadRouter und FocusPath keine sind.
///
/// WAS DER RING NICHT IST: er ist KEIN zweites Regelwerk und KEIN eigenes Fenster.
///
/// Der Ring zeigt die Controls eines Fensters, das schon offen ist (iwAction, iwPadSystemMenu),
/// nur eben im Kreis statt im Gitter. Ausgeloest wird ein Sektor ueber FocusPath::Activate(),
/// also ueber genau denselben Weg, den ein Mausklick nimmt (ctrlButton::Activate ->
/// Msg_ButtonClick). Daraus faellt dreierlei GRATIS an, und das ist der ganze Grund fuer diese
/// Bauform:
///  - der Klartextkasten aus Phase 9 beschriftet den Ring, ohne eine Zeile neuen Text
///    (dskGameInterface::RefreshBrief liest den FOKUS, und der Ring setzt den Fokus),
///  - die Verfuegbarkeitsregeln (Addons, Militaergebaeude, Katapult) bleiben da, wo sie sind -
///    in iwAction. Eine zweite Kopie waere beim naechsten Addon still falsch.
///  - die GameCommands laufen unveraendert durch den bestehenden Pfad. Die Ringauswahl selbst
///    ist reine ANZEIGE und erzeugt nie ein Kommando.
///
/// WARUM DAS ZIELEN EIN ZEIGER AUF EINER SCHEIBE IST und keine Stickabfrage:
///
/// IPadTarget::OnPadMove liefert eine ganzzahlige Verschiebung je Frame und NIE (0,0) - bei
/// Stick in Ruhe kommt gar kein Aufruf. Die AUSLENKUNG ist daraus nicht zurueckzurechnen
/// (PixelsPerSecond * dt und der Subpixelrest verfaelschen den Betrag), und "Stick zurueck in
/// der Mitte" ist gar nicht beobachtbar.
///
/// Der Vorbereiter hat daraus geschlossen, es brauche einen neuen Lesezugriff am PadRouter.
/// GEMESSEN BRAUCHT ES DEN NICHT: der Ring sammelt die Verschiebungen einfach auf, wie ein
/// Mauszeiger es taete. Der aufgelaufene Punkt bleibt stehen, wenn der Stick losgelassen wird -
/// und genau das ist die Barrierefreiheitsvorgabe, die Anno 1800 auf der Konsole als eigene
/// Option fuehrt ("Auswahl bleibt stehen, wenn der Stick in die Mitte zurueckkehrt"). Es ist
/// also kein Notbehelf, sondern das gewuenschte Verhalten, und es kostet keine ABI-Aenderung.
namespace padring {

/// Ein Sektor, in Bildschirmwinkeln (Grad, 0 = rechts, wachsend im Uhrzeigersinn, weil y auf
/// dem Bildschirm nach unten waechst). Sektor 0 sitzt oben (12 Uhr).
struct Sector
{
    float startAngle = 0.f;
    float endAngle = 0.f;
    float midAngle() const { return (startAngle + endAngle) / 2.f; }
};

/// Die Sektoren eines Rings mit `count` Eintraegen, in Eintragsreihenfolge.
///
/// Sektor 0 liegt MITTIG OBEN und die weiteren folgen im Uhrzeigersinn. Das ist die einzige
/// Wahl, bei der die Sektorposition eines Eintrags nicht davon abhaengt, wieviele Eintraege es
/// gibt - der Erste ist immer oben. Muskelgedaechtnis ist der ganze Vorteil eines Rings
/// (CONTROLLER-UX.md 5.4), und es entsteht nur aus fester Lage.
std::vector<Sector> MakeSectors(unsigned count);

/// Welcher Sektor liegt in dieser Richtung? -1, wenn die Richtung (0,0) ist.
int SectorAt(unsigned count, PointF aim);

/// Ein Punkt auf dem Sektorbogen: Mittelpunkt + Radius in Richtung `angleDeg`.
PointF PointOnRing(PointF center, float radius, float angleDeg);

/// DER ZUSTAND DES RINGS EINES SITZPLATZES.
///
/// Er lebt auf PlayerView, wie der Fokus und der Klartext auch - vier Spieler sind vier
/// Instanzen, und keine von ihnen weiss von den anderen. Genau daran haengt die Zusicherung,
/// dass Spieler 1 mit seiner Ringauswahl nichts an Spieler 0 aendert.
class Ring
{
public:
    /// Hoechstens so viele Sektoren auf einer Seite. Mehr traegt der Bogen nicht:
    /// gerechnet mit Ringdurchmesser 0,55 * Viewporthoehe sind das bei 8 Sektoren rund 85
    /// Punkte je Sektor - ein 36er-Icon mit Rand. Bei 16 waeren es 42 und das Icon passt nicht
    /// mehr. Der Forschungskonsens fuer Radialmenues am Pad liegt bei 6 bis 8.
    static constexpr unsigned SectorsPerPage = 8;
    /// Innerhalb dieses Radius (View-Pixel) gilt der Zeiger als "in der Mitte" - dort wird
    /// NICHTS gewaehlt. Steam Input nennt die Mitte ausdruecklich den "nevermind"-Bereich.
    static constexpr float AimDeadRadius = 20.f;
    /// Weiter als das laeuft der Zeiger nicht nach aussen. Ohne die Klemme muesste ein Spieler,
    /// der den Stick lange gehalten hat, ihn erst wieder zurueckfahren, bevor die Gegenrichtung
    /// wirkt.
    static constexpr float AimMaxRadius = 140.f;

    bool IsOpen() const { return open_; }
    /// Oeffnet den Ring auf Seite 0 mit dem Zeiger in der Mitte.
    void Open();
    void Close();

    unsigned GetPage() const { return page_; }
    /// Seitenwechsel. Setzt den Zeiger zurueck in die Mitte: die Sektoren bedeuten danach etwas
    /// anderes, und eine stehengebliebene Auswahl waere eine, die der Spieler nie getroffen hat.
    void SetPage(unsigned page);

    /// Stickweg dieses Frames, in View-Pixeln - woertlich das, was IPadTarget::OnPadMove
    /// liefert.
    void Aim(const Position& delta);
    /// Zeigt in eine Richtung, oder (0,0) solange der Zeiger in der Mitte steht.
    PointF GetAim() const;
    /// Der Zeiger selbst, ungeklemmt gelesen. Nur fuer Nachweise und das Zeichnen.
    PointF GetAimRaw() const { return aim_; }
    void ResetAim() { aim_ = PointF(0.f, 0.f); }
    /// Setzt den Zeiger auf die Mitte dieses Sektors - der digitale Weg (Steuerkreuz).
    void AimAtSector(unsigned count, unsigned sector);

private:
    bool open_ = false;
    unsigned page_ = 0;
    PointF aim_{0.f, 0.f};
};

} // namespace padring
