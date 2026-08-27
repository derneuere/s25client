<!--
Copyright (C) 2005 - 2026 Settlers Freaks <sf-team at siedler25.org>

SPDX-License-Identifier: GPL-2.0-or-later
-->
# Sprachkataloge

Die Uebersetzungen dieses Forks. `rttr.pot` ist die Vorlage, `rttr-<locale>.po` sind die
Kataloge je Sprache.

## Diese Dateien gehoeren zum Hauptprojekt

Upstream liegen sie in einem eigenen Repository, das als Submodul unter `external/languages`
eingehaengt wird. In diesem Fork nicht: sie sind normale, versionierte Dateien des
Hauptprojekts. Ein Auschecken des Branches bringt die Uebersetzungen mit, ohne dass ein
zweites Repository initialisiert werden muss.

Der Preis dieser Entscheidung: Uebersetzungen aus dem Upstream fliessen nicht mehr von selbst
zu, und ein spaeterer Beitrag zurueck an den Upstream muss von Hand aufbereitet werden. Siehe
[Upstream-Stand einspielen](#upstream-stand-einspielen).

## Wie der Bau sie benutzt

`CMakeLists.txt` im Wurzelverzeichnis sammelt die `.po` per Glob und uebergibt sie an
`rttr_create_translations` — die eigene Fassung von `gettext_create_translations`, siehe
[Warum eine eigene Fassung](#warum-eine-eigene-fassung-von-gettext_create_translations). Pro
Sprache laufen dann zwei Schritte:

1. `msgmerge --sort-output --no-wrap --quiet --no-fuzzy-matching --update --backup=none <po> <pot>`
   — dieser Schritt schreibt **in die Quelldatei zurueck**. Er ist idempotent: solange sich
   `rttr.pot` nicht aendert, bleiben die `.po` byte-gleich. Nach einer Aenderung an `rttr.pot`
   sind einmalig alle `.po` geaendert; diese Aenderung ist erwuenscht und gehoert committet.
2. `msgfmt` erzeugt `<build>/gen/languages/rttr-<locale>.mo`. **Ohne `--use-fuzzy`**: ein als
   `#, fuzzy` markierter Eintrag steht nicht in der `.mo` und damit nie auf dem Schirm.

## Warum eine eigene Fassung von `gettext_create_translations`

Wegen genau eines Schalters: **`--no-fuzzy-matching`**.

Ohne ihn raet `msgmerge` zu jeder NEUEN msgid eine Uebersetzung, indem es die aehnlichste ALTE
msgid sucht — Aehnlichkeit der englischen Zeichenkette, nicht der Bedeutung — und schreibt sie
als `#, fuzzy` in die `.po`. Fuer die 57 neuen Klartextsaetze aus Phase 12 hat es das in 27
Katalogen getan, rund 24 Eintraege je Katalog. Gemessen, woertlich aus `rttr-pl.po`:

```text
#, fuzzy
msgid "The flag of your headquarters"
msgstr "Idź do kwatery głównej"     # = "GEH ZUM HAUPTQUARTIER"

#, fuzzy
msgid "Open it"
msgstr "Otwarte"                  # = "Offen", das Eigenschaftswort
```

Auf dem Schirm stand davon nichts, weil `msgfmt` ohne `--use-fuzzy` laeuft. Die Falle schnappt
beim naechsten Uebersetzer zu, der die Fuzzies bestaetigt — er bekommt eine HQ-Flagge mit der
Ueberschrift "Geh zum Hauptquartier". Ein Hinweis, der luegt, ist schlimmer als kein Hinweis;
das gilt in jeder Sprache.

Ausser diesem Schalter ist die eigene Fassung ein Abbild des Originals: dieselbe
Argumentpruefung (`FATAL_ERROR` bei fehlendem `DESTINATION`/`FILES` und bei unbekannten
Argumenten), dieselbe Umrechnung eines relativen `DESTINATION` auf `CMAKE_CURRENT_BINARY_DIR`,
dieselben zwei Befehle je Sprache. Beides fehlte in der ersten Fassung und ist nachgetragen —
folgenlos fuer den einen Aufruf, den es gibt, aber eine stille Abweichung von der Funktion, die
sie ersetzt, bemerkt erst der naechste Aufrufer.

Der Schalter **unterlaesst nur das Raten**. Er entfernt keine bestehenden Fuzzy-Eintraege und
beruehrt die Arbeit keines Uebersetzers. Eine neue msgid steht danach unuebersetzt in der `.po`,
und unuebersetzt heisst "faellt auf das englische Original zurueck" — ehrlich und sichtbar.

Die bereits geschriebenen Falschvorschlaege sind einmalig aus den 27 Katalogen entfernt worden
(nur die, deren msgid neu war; jeder aeltere Fuzzy-Eintrag steht unangetastet). Ohne den
Schalter waeren sie beim naechsten `msgmerge --update` sofort wieder da.

Die eigene Fassung steht **hier** und nicht in `external/libutil`: das ist ein Submodul und
gehoert nicht zu diesem Repository — eine Aenderung dort waere beim naechsten
`git submodule update` weg.

## Was NUR auf Deutsch und Englisch dasteht

Die Klartexte des Padpfades (Phase 9 und Phase 12: Gebaeudebloecke, Knotenbloecke, die Saetze zu
den Handlungen des Aktionsfensters, die Tastenhinweisleiste) sind **nur im deutschen Katalog
uebersetzt**. Ein polnischer oder franzoesischer Spieler liest sie auf Englisch, mitten in einer
sonst uebersetzten Oberflaeche.

Das ist kein Fehler des Programms, sondern der Stand der Uebersetzung, und es ist die richtige
Voreinstellung: unuebersetzt heisst englisch, und englisch ist wahr. Falsch waere der
Fuzzy-Vorschlag oben. Wer eine Sprache nachziehen will, findet die Saetze in `rttr.pot` — sie
stehen dort am Ende, in der Reihenfolge, in der `brief::ForBuilding`, `brief::ForNode`,
`brief::ForAction` und `brief::KeyLabel` sie erzeugen.

Die `.mo` werden von dort in das Ausgabeverzeichnis kopiert. Die `.po` und `.pot` selbst
werden **nicht** ausgeliefert und **nicht** installiert; dafuer sorgen die
`PATTERN "languages" EXCLUDE`-Regeln in `copyDepsToBuildDir.cmake` und in der
`install(DIRECTORY "data/RTTR" ...)`-Regel.

## rttr.pot erneuern

Es gibt kein `xgettext` in der mitgelieferten Werkzeugkette; die Vorlage wird mit Poedit
(Version 3 oder neuer) erneuert.

- `rttr.pot` in Poedit oeffnen.
- "Katalog" -> "Aus Quellcode aktualisieren".
- Speichern.
- Bauen und den Diff lesen: `msgmerge` zieht die neuen msgids in alle 28 `.po` nach.

Die dafuer noetigen Angaben stehen im Kopf von `rttr.pot` und sind auf **diesen** Ablageort
abgestimmt:

```text
X-Poedit-Basepath: ../../..
X-Poedit-KeywordsList: _;gettext_noop;__
X-Poedit-SearchPath-0: libs
X-Poedit-SearchPath-1: extras
X-Poedit-SearchPath-2: data/RTTR/gamedata
```

`X-Poedit-Basepath` ist relativ zum Ort von `rttr.pot`. Diese Datei liegt drei Ebenen unter
der Repowurzel, deshalb `../../..`. Wer die Kataloge verschiebt, muss diesen Wert mitziehen.

## Eine Sprache uebersetzen

- Die `.po` der Sprache in Poedit oeffnen.
- "Katalog" -> "Aus POT-Datei aktualisieren" mit `rttr.pot`.
- Uebersetzen, speichern.
- Bauen. `msgmerge` sortiert die Datei; erst danach committen, damit der Diff klein bleibt.

## Upstream-Stand einspielen

Uebersetzungen aus dem Upstream kommen nicht mehr per `git submodule update`. Der Vorgang ist
Handarbeit und muss von einem Menschen geprueft werden — er wird bewusst **nicht** in der CI
verankert.

```sh
git clone --depth 1 https://github.com/Return-To-The-Roots/languages.git /tmp/rttr-languages
cd <repo>/data/RTTR/languages
for f in rttr-*.po; do
    msgcat --use-first "$f" "/tmp/rttr-languages/$f" -o "$f.new" && mv "$f.new" "$f"
done
```

`--use-first` laesst bei einem Zusammentreffen unsere Fassung gewinnen. Danach bauen und den
Diff lesen: msgids, die nur der Upstream kennt, kommen hinzu; unsere Uebersetzungen bleiben
stehen. Jede Stelle, an der der Upstream eine msgid geaendert hat, die wir uebersetzt haben,
muss von Hand entschieden werden.

`msgcat` liegt **nicht** in `external/dev-tools`; unter Windows kommt es aus der Git-Bash oder
aus einer eigenen gettext-Installation.
