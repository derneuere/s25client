// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "helpers/EnumArray.h"
#include "gameTypes/BuildingType.h"

/// KURZTEXTE UEBER GEBAEUDE, geschrieben fuer jemanden, der Siedler noch nie gespielt hat.
///
/// Warum es sie ueberhaupt gibt, obwohl BUILDING_HELP_STRINGS (gameData/BuildingConsts.cpp)
/// bereits fuer jedes Gebaeude einen Text hat:
///
///  1. ERREICHBARKEIT. Die Hilfetexte haengen am Fragezeichenknopf eines BESTEHENDEN Gebaeudes
///     (iwBuilding.cpp, iwBuildingSite.cpp, iwBaseWarehouse.cpp). Wer noch nicht gebaut hat,
///     kommt gar nicht an sie heran - also genau der, der sie braucht.
///  2. LAENGE. Sie sind Handbuchprosa, bis 595 Zeichen (Hauptquartier). In eine Zeile unter der
///     Ansicht eines Viertelbildschirms passt das nicht.
///  3. INHALT. Sie beschreiben den Betrieb ("Der Steinmetz verarbeitet den Stein zu Ziegeln"),
///     nicht die ENTSCHEIDUNG. Der Ausloeser dieser Phase war woertlich: "Ich hab den
///     Steinbruch und den Holzfaeller verwechselt." Beide kosten 2 Bretter, beide sind eine
///     Huette, beide Icons sind Pixelgrafik aus 1994. Was sie unterscheidet, ist WOFUER man sie
///     baut und WORAN sie haengen - und genau das steht hier.
///
/// Die Hilfetexte bleiben unangetastet; sie werden weiterhin dort gezeigt, wo sie heute stehen.

/// Ein Satz: wozu baut man das, und warum jetzt. Fuer jeden Gebaeudetyp gefuellt ausser
/// BuildingType::Nothing9 (Platzhalter, hat auch in BUILDING_NAMES keinen Namen).
extern const helpers::EnumArray<const char*, BuildingType> BUILDING_PURPOSE_STRINGS;

/// Ein Satz: woran haengt der Betrieb, ausserhalb der angelieferten Waren. Leer, wo es nichts
/// gibt - Muehle und Baeckerei stehen ueberall gleich gut.
///
/// Diese Auskunft steht NIRGENDWO SONST als Datum. BLD_WORK_DESC kennt nur Beruf, Erzeugnis und
/// ANGELIEFERTE Waren; dass ein Steinbruch Felsen in Reichweite braucht, lebt ausschliesslich in
/// nofStonemason::GetPointQuality. Ein Anfaenger kann es also weder im Spiel nachlesen noch aus
/// den Baukosten ableiten - er baut den Steinbruch in die Wiese und wundert sich.
extern const helpers::EnumArray<const char*, BuildingType> BUILDING_SITE_STRINGS;
