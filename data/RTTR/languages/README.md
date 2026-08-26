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
`gettext_create_translations`. Pro Sprache laufen dann zwei Schritte:

1. `msgmerge --sort-output --no-wrap --quiet --update --backup=none <po> <pot>` — dieser
   Schritt schreibt **in die Quelldatei zurueck**. Er ist idempotent: solange sich `rttr.pot`
   nicht aendert, bleiben die `.po` byte-gleich. Nach einer Aenderung an `rttr.pot` sind
   einmalig alle `.po` geaendert; diese Aenderung ist erwuenscht und gehoert committet.
2. `msgfmt` erzeugt `<build>/gen/languages/rttr-<locale>.mo`.

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
