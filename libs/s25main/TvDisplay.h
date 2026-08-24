// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Point.h"
#include "Rect.h"

/// Alles, was fuer die Darstellung auf einem FERNSEHER gerechnet wird - und sonst nirgends.
///
/// Warum es diesen Ort ueberhaupt gibt: die Bedienoberflaeche dieses Spiels ist auf 640x480 bis
/// 800x600 ausgelegt und wird in View-Koordinaten PIXELWEISE gezeichnet (ogl/glFont.cpp:132-145
/// legt die Glyphen-Bitmaps 1:1 ab, ohne jeden Skalierungsparameter). Auf einem 4K-Fernseher
/// heisst das: Schrift und Fenster schrumpfen physisch auf ein Viertel dessen, was sie auf einem
/// 1080p-Geraet haben. Genau das war der Befund am Geraet ("zu klein").
///
/// Die einzige Groesse, die das Programm dagegen kennt, ist die Zahl der Zeilen des
/// Framebuffers. Bildschirmdiagonale und Sitzabstand sind NICHT ermittelbar - es gibt dafuer
/// keine verlaessliche Schnittstelle (SDL_GetDisplayDPI wird im ganzen Baum nirgends aufgerufen,
/// und Fernseher melden ueber HDMI meist keine brauchbare physische Groesse). Deshalb rechnet
/// hier alles gegen eine feste REFERENZHOEHE, und der Rest ist eine Einstellung.
///
/// Alle Funktionen hier sind rein: kein Videotreiber, kein OpenGL, kein Spielzustand. Die
/// einzigen Ausnahmen sind ausdruecklich als Einstellungs-Adapter gekennzeichnet.
namespace tv {

/// Hoehe der logischen Leinwand, auf die die Bedienoberflaeche gerechnet wird.
///
/// 1080 und nicht 600: bei 1080 ist der Faktor auf einem 1080p-Geraet exakt 1, dort aendert sich
/// also nichts. Und die Zahl hat eine zweite, wichtigere Eigenschaft (siehe
/// RecommendedGuiScalePercent).
constexpr unsigned UI_REFERENCE_HEIGHT = 1080;

/// Rand je Seite in Prozent, den ein Fernseher verschlucken kann (Overscan).
/// 5 % je Seite ist der Konsens von EBU R95 (Graphics Safe: 90 % x 90 %), Microsofts
/// "Designing for TV" (27/48 epx auf 960x540) und Android TV (27/48 dp auf 1920x1080).
constexpr unsigned SAFE_AREA_PERCENT_DEFAULT = 5;
/// Mehr als 10 % je Seite verschenkt mehr Bild, als je ein Fernseher abschneidet.
constexpr unsigned SAFE_AREA_PERCENT_MAX = 10;

/// Empfohlene GUI-Skalierung in Prozent fuer eine Renderflaeche dieser Hoehe (PHYSISCHE Pixel).
///
/// = renderHeight / UI_REFERENCE_HEIGHT, nach unten auf 100 % begrenzt. 1080p -> 100 %,
/// 1440p -> 133 %, 2160p -> 200 %.
///
/// Die Eigenschaft, auf der der SPLITSCREEN steht: die View-Koordinaten der Renderflaeche sind
/// physisch/Faktor, die RENDERFLAECHE ist also bei jeder Aufloesung genau UI_REFERENCE_HEIGHT
/// Zeilen hoch - so gross wie heute ein 1080p-Vollbild bei 100 %. Ein Fenster, das heute auf
/// den Bildschirm passt, passt deshalb bei jeder Aufloesung und jeder Ansichtszahl auf die
/// Renderflaeche; die Ansichtszahl kommt in der Skalierung ueberhaupt nicht vor. Die Skalierung
/// muss deshalb NICHT je nach Ansichtszahl gedeckelt werden; die Referenzhoehe erledigt das.
///
/// WAS HIER AUSDRUECKLICH NICHT STEHT: dass jedes Ingamefenster in einen VIEWPORT passt. Das
/// war eine falsche Zahl und ist zurueckgezogen. Nachgemessen ist das groesste Ingamefenster
/// 700 x 635 View-Einheiten (Breite: iwSave, ingameWindows/iwSave.cpp:46+129; Hoehe:
/// iwDiplomacy bei MAX_PLAYERS = 8, ingameWindows/iwDiplomacy.cpp:45) - ein Viertelbild ist nur
/// 540 hoch, das Fenster passt dort also NICHT hinein. Genau deshalb wird ein Fenster gegen die
/// ganze Renderflaeche geklemmt und nie gegen einen Viewport (siehe WindowBoundsRect).
///
/// Unterhalb 1080p liefert die Funktion 100 % und aendert damit nichts - dort ist ohnehin kein
/// Platz zu verteilen.
unsigned RecommendedGuiScalePercent(unsigned renderHeight);

/// Empfohlener Startzoom der KARTE fuer eine Renderflaeche dieser Hoehe (PHYSISCHE Pixel).
///
/// Zweiter, voellig getrennter Hebel: die GUI-Skalierung wirkt auf die Karte nachweislich GAR
/// NICHT - GameWorldView::updateEffectiveZoomFactor rechnet sie ausdruecklich wieder heraus
/// (world/GameWorldView.cpp:829-833), sodass ein Knoten bei jeder Skalierung TR_W = 56 physische
/// Pixel breit bleibt (gameData/MapConsts.h:8). Wer die WELT groesser will, muss zoomen.
///
/// Geliefert wird immer ein Wert aus ZOOM_FACTORS (gameData/GuiConsts.h:9), also keine neue
/// Zoomstufe: der groesste Eintrag <= renderHeight/UI_REFERENCE_HEIGHT, mindestens 1.0.
/// 1080p -> 1.0 (unveraendert), 1440p -> 1.25, 2160p -> 2.0.
///
/// 2.0 bei 4K ist dabei kein Kompromiss, sondern die exakte Reproduktion: ein Knoten ist dann
/// wieder so gross wie auf einem 1080p-Geraet bei Zoom 1, und weil alle Texturen mit GL_NEAREST
/// gefiltert werden (ogl/glSmartBitmap.cpp:217-218), ist es eine ganzzahlige Pixelverdopplung
/// und bleibt scharf.
float RecommendedZoomFactor(unsigned renderHeight);

/// Der Bereich der Renderflaeche, in dem Text, Fenster und Bedienelemente liegen duerfen.
/// Hintergrund und Karte werden bewusst weiter bis an den Rand gezeichnet (sonst entstuende der
/// "boxed-in"-Letterbox-Effekt, vor dem Microsofts TV-Richtlinie ausdruecklich warnt).
///
/// DER RAND IST EINE EIGENSCHAFT DES BILDSCHIRMS, NICHT EINER ANSICHT. Overscan schneidet an
/// den vier Kanten des Bildes ab; die Kante zwischen zwei Splitscreen-Ansichten wird von nichts
/// abgeschnitten und braucht deshalb auch keinen Schutz. Der Parameter heisst darum renderSize
/// und nicht viewportSize, und es gibt bewusst keine Ueberladung fuer einen Viewport: wuerde
/// man je Viewport klemmen, schrumpfte der Kasten fuer ein Fenster in der oberen Reihe eines
/// 2x2-Bildes auf 540 - 54 = 486 View-Einheiten - weniger als die 635, die das hoechste
/// Ingamefenster braucht (iwDiplomacy bei acht Spielern). Der Viertelausschnitt reicht dafuer
/// schon OHNE jeden Rand nicht (540 < 635); die Klemme wuerde also genau das kaputtmachen, was
/// sie schuetzen soll.
///
/// percentPerSide == 0 liefert exakt Rect(0, 0, renderSize) - der Aufrufer rechnet dann
/// bit-identisch weiter wie ohne Safe Area. Das ist der Auslieferungszustand.
Rect SafeAreaRect(const Extent& renderSize, unsigned percentPerSide);

/// Macht aus dem ROHWERT einer Konfigurationsdatei einen gueltigen Randwert.
///
/// Der Wert kommt als INT aus der ini und darf dort alles sein. Frueher wurde er ungeprueft nach
/// unsigned gewandelt: aus tv_safe_area=-1 wurde 4294967295 und daraus per Deckelung das
/// MAXIMUM - der groesste Rand also, wo offensichtlich keiner gemeint war.
///
/// Die Regel unterscheidet die beiden Faelle nach der erkennbaren Absicht:
///   < 0             -> keine deutbare Absicht: SAFE_AREA_PERCENT_DEFAULT.
///   > MAX           -> deutbare Absicht "so viel wie geht": SAFE_AREA_PERCENT_MAX.
///   sonst           -> unveraendert, 0 eingeschlossen (= Rand ausdruecklich aus).
unsigned SanitizeSafeAreaPercent(int rawPercent);

// ------------------------------------------------------------------------------------------
// Einstellungs-Adapter. Nicht rein: sie lesen SETTINGS. Bewusst genau zwei, damit die Regel
// "wo wirkt der Fernsehmodus" an einer Stelle steht und nicht in jedem Aufrufer.
// ------------------------------------------------------------------------------------------

/// Ist der Fernsehmodus eingeschaltet? (SETTINGS.video.tvMode)
bool IsTvModeEnabled();
/// Tatsaechlich wirksamer Safe-Area-Rand je Seite: 0, solange der Fernsehmodus aus ist.
unsigned ActiveSafeAreaPercent();
/// SafeAreaRect mit dem wirksamen Rand. Bei ausgeschaltetem Fernsehmodus die volle Flaeche.
Rect ActiveSafeAreaRect(const Extent& renderSize);

/// Der Kasten, in dem ein Fenster dieser Groesse liegen darf - die einzige Stelle, an der
/// IngameWindow klemmt.
///
/// Das ist ActiveSafeAreaRect, ABER je Achse getrennt auf die volle Renderflaeche
/// zurueckgenommen, sobald das Fenster in den Safe-Area-Kasten nicht mehr hineinpasst.
///
/// Warum der Rand nachgibt und nicht das Fenster: ein Fenster, dessen Titelleiste oder
/// Schliessknopf oberhalb des Kastens landet, ist unbedienbar; ein Fenster, das in den
/// Overscan-Bereich ragt, ist im schlechtesten Fall an einer Kante angeschnitten - und nur auf
/// Geraeten, die ueberhaupt Overscan machen. Sichtbarkeit schlaegt Randschutz.
///
/// Daraus folgt die Zusicherung, die diese Funktion traegt und die der Test festnagelt: die
/// Klemme kann ein Ingamefenster NIE aus dem Bild schieben - bei jeder Aufloesung, jeder
/// Ansichtszahl und jedem erlaubten Randwert. Ohne Fernsehmodus liefert sie Rect(0, 0,
/// renderSize) und rechnet damit Zahl fuer Zahl wie vor dieser Phase.
Rect WindowBoundsRect(const Extent& renderSize, const Extent& windowSize);

/// Kleinste Flaeche, fuer die CustomBorderBuilder::buildBorder ueberhaupt einen Rahmen
/// zusammensetzen kann (CustomBorderBuilder.cpp:133: alles darunter wird mit einem Fehler
/// abgelehnt, und dskGameInterface::Msg_PaintBefore haette dann vier Nullzeiger zu zeichnen).
constexpr Extent SCREEN_CHROME_MIN_SIZE(640, 480);

/// Der Kasten fuer die BILDSCHIRMRAHMUNG: der gekachelte Rahmen, die vier Eckstatuen und die
/// untere Knopfleiste.
///
/// Warum die drei EINEN gemeinsamen Kasten haben: sie sind ein Bild. Die Knopfleiste sitzt auf
/// dem Mittelstueck des unteren Rahmens, das der Rahmenbauer eigens dafuer bei size.x/2 - 118
/// einsetzt ("das Mittelstueck, damit das Bedienfeld passt", CustomBorderBuilder.cpp:154).
/// Zieht man nur die Leiste um den Safe-Rand herein und laesst den Rahmen an der Bildkante,
/// klafft dazwischen bei 5 % eine Luecke von 54 View-Einheiten - die Leiste schwebt.
///
/// Warum hereinziehen und nicht die Leiste an der Kante lassen: die Knopfleiste ist das am
/// haeufigsten gebrauchte Bedienelement des Spiels. Ein Fernseher mit Overscan schneidet sie
/// sonst an oder ganz ab, und dann ist der Fernsehmodus an seiner Hauptaufgabe gescheitert.
/// Der Rahmen ist dagegen reine Zier: er wird aus Kacheln fuer JEDE Groesse >= 640x480 neu
/// gebaut, kostet also nichts ausser einem Neuaufbau bei Groessenaenderung - der ohnehin
/// passiert (dskGameInterface::Resize). Die Welt wird weiterhin ueber die VOLLE Flaeche
/// gezeichnet; im Streifen ausserhalb des Rahmens steht also Bild und kein schwarzer Balken -
/// genau die Aufteilung, die die TV-Richtlinien verlangen (Bild bis an die Kante, Bedienung
/// innen).
///
/// Der Rand gibt nach derselben Regel nach wie bei einem Fenster, nur ist der "Mindestbedarf"
/// hier nicht die Fenstergroesse, sondern das, was der Rahmenbauer noch bauen kann. Ohne
/// Fernsehmodus ist das Ergebnis Rect(0, 0, renderSize), also Zahl fuer Zahl das heutige Bild.
Rect ScreenChromeRect(const Extent& renderSize);

} // namespace tv
