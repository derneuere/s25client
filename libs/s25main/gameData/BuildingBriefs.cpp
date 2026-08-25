// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "BuildingBriefs.h"
#include "mygettext/mygettext.h"

// Reihenfolge ist die von BuildingType (gameTypes/BuildingType.h). Der Kommentar vor jedem
// Eintrag ist der Enumname - ohne ihn ist eine 40er-Liste nicht pruefbar.
//
// Regel fuer den Text: EIN Satz, in der zweiten Person, ohne Fachwort, und er beantwortet
// "brauche ich das JETZT?". Nicht "produziert Bretter", sondern wozu Bretter gut sind.
//
// VIER HARTE REGELN, die ein Nachweis festhaelt (testPadBrief.cpp,
// EverySentenceAboutAWareUsesTheWordTheSupplyLineUses). Wer hier einen Satz aendert oder
// uebersetzt, muss sie kennen, sonst faellt der Bau:
//
//  R1  Der Satz nennt die Ware, die das Gebaeude HERSTELLT (BLD_WORK_DESC::producedWare), mit
//      genau dem Wort aus WARE_NAMES - als ganzes Wort. Also "Digs gold", nicht "Digs gold ore";
//      die Ware heisst "Gold".
//
//  R2  Der Satz gibt KEINER Ware einen Namen, die das Gebaeude ANGELIEFERT bekommt
//      (BLD_WORK_DESC::waresNeeded). Diese Waren stehen bereits in der Nachschubzeile, die
//      PlayerBrief.cpp aus denselben Daten baut - ein zweites Mal von Hand geschrieben sind sie
//      genau die Stelle, an der sich ein Block widersprechen kann. Gemessen war das:
//      "Macht aus dem Schinken der Schweinezucht Fleisch" ueber "Nachschub: Fleisch", weil der
//      deutsche Katalog die beiden Fleischwaren umgekehrt benennt. Was nur einmal und nur aus
//      den Daten kommt, kann sich nicht widersprechen.
//
//      "Keinen Namen geben" heisst BEIDES: nicht das Wort der Ware, und auch kein Ersatzwort
//      dafuer. Nur die erste Haelfte kann eine Maschine nachrechnen; die zweite ist eine
//      Pflicht des Schreibenden. Gefunden und ausgeraeumt wurden drei Umschreibungen -
//      "Cuts logs into boards" fuer Wood, "from what the pig farm raises" fuer Ham und
//      "Hurls rocks" fuer Stones. Ein Ersatzwort ist genau so schaedlich wie das doppelte:
//      der Anfaenger sucht danach in seinem Lager und findet es dort nicht.
//
//  R2b Der Satz nennt AUCH NICHT das Gebaeude, das eine seiner Eingangswaren herstellt. Das ist
//      dieselbe Regel, eine Ebene hoeher: die Lieferbeziehung steht schon in der Nachschubzeile,
//      und was hier von Hand danebengeschrieben wird, kann veralten, sobald sich BLD_WORK_DESC
//      aendert. Diese Haelfte IST nachrechenbar, und sie faengt genau den Weg, den die beiden
//      Umschreibungen oben genommen haben (Saegewerk -> Holzfaeller, Abdecker -> Schweinezucht).
//      Ein Gebaeude als ABNEHMER zu nennen bleibt erlaubt und erwuenscht ("Grinds flour for the
//      bakery") - das ist keine Aussage ueber den eigenen Nachschub.
//
//  R3  Nennt der englische Satz ein anderes Gebaeude, muss die Uebersetzung dessen Namen aus
//      BUILDING_NAMES benutzen - das Wort, das im Baumenue steht ("Goldbergwerk", nicht
//      "Goldmine").
//
// WO DIESE REGELN GEPRUEFT WERDEN: am englischen Quelltext (den msgids) und an JEDEM
// ausgelieferten Katalog, der diese Saetze uebersetzt - je Satz einzeln, damit eine halb
// fertige Uebersetzung nicht den ganzen Nachweis blockiert. Heute ist der deutsche Katalog der
// einzige mit uebersetzten Saetzen; dass sein Durchgang laeuft, ist im Nachweis hart gefordert
// und haengt NICHT mehr an der Sprache des Rechners.
//
// OFFEN und ausdruecklich sichtbar gelassen: WARE_NAMES[Skins], [Leather] und [Armor] haben in
// KEINEM Katalog eine Uebersetzung (sie stehen nicht einmal in rttr.pot). Der deutsche Spieler
// liest in seiner Nachschubzeile also "Skins" und "Leather", und nach R1 tun diese Saetze es
// auch. Das ist haesslich, aber es ist das Wort, nach dem er im Lager suchen muss; es hier
// einzudeutschen, waere ein Widerspruch zur Anzeige. Die saubere Loesung liegt im Katalog,
// nicht hier.

const helpers::EnumArray<const char*, BuildingType> BUILDING_PURPOSE_STRINGS = {
  // Headquarters
  gettext_noop("Your starting building: everything you own is stored here, and every new worker "
               "comes out of it."),
  // Barracks
  gettext_noop("The smallest guard post. Soldiers move in and your border grows a little further "
               "out around it."),
  // Guardhouse
  gettext_noop("A guard post for three soldiers. It pushes your border further out than a "
               "barracks."),
  // Skinner
  gettext_noop("Delivers skins for the tannery - the first of three buildings that make your soldiers "
               "tougher."),
  // Watchtower
  gettext_noop("A guard post for six soldiers that claims a wide ring of new land."),
  // Vineyard
  gettext_noop("Grows grapes. You only need them if you intend to build a temple."),
  // Winery
  gettext_noop("Presses wine, which only the temple uses."),
  // Temple
  gettext_noop("Makes gold without a mountain - the second source of gold besides the gold mine."),
  // Tannery
  gettext_noop("Makes leather for the leatherworks - the second of the three buildings that make your soldiers "
               "tougher."),
  // Fortress
  gettext_noop("The strongest guard post: nine soldiers, and it claims the most land of all."),
  // Granite mine
  gettext_noop("Digs stones out of a mountain when your quarries have nothing left to break."),
  // Coal mine
  gettext_noop("Digs coal. The iron smelter, the armory and the mint all burn it."),
  // Iron mine
  gettext_noop("Digs iron ore. Without it there is no iron, and without iron no weapons and no "
               "tools."),
  // Gold mine
  gettext_noop("Digs gold for the mint. The coins struck from it make your soldiers stronger."),
  // Lookout tower
  gettext_noop("Lifts the fog from a wide circle. It produces nothing - build it to see, not to "
               "earn."),
  // Leatherworks
  gettext_noop("Sews armor, so your soldiers survive longer - the last of the three buildings that make them tougher."),
  // Catapult
  gettext_noop("Fires at enemy military buildings in range. Useless until you share a border with an enemy."),
  // Woodcutter
  gettext_noop("Fells trees and sends the wood to the sawmill. Almost always your very first building."),
  // Fishery
  gettext_noop("Catches fish - the quickest food for your miners, wherever there is water."),
  // Quarry
  //
  // NICHT "jedes Gebaeude, das groesser ist als eine Huette": das ist als Faustregel brauchbar
  // und woertlich falsch. Die Wachstube ist eine Huette und kostet 3 Steine (BUILDING_COSTS[2]),
  // die vier Bergwerke sind groesser als eine Huette und kosten keinen einzigen
  // (BUILDING_COSTS[10..13] = {4,0}). Nachgezaehlt kosten 25 der 38 baubaren Gebaeude Steine,
  // und JEDES davon kostet auch Bretter - genau das steht jetzt da.
  gettext_noop("Breaks stones out of the rocks nearby. Most buildings need stones on top of boards, so you need one "
               "early."),
  // Forester
  gettext_noop("Plants new trees, so your woodcutters never run out. Build one next to every pair "
               "of woodcutters."),
  // Slaughterhouse
  gettext_noop("Makes meat, one of the three foods your miners eat."),
  // Hunter
  gettext_noop("Shoots wild animals for meat. Quick to set up, but the animals do not come back."),
  // Brewery
  gettext_noop("Brews beer. Beer plus a sword plus a shield makes one new soldier."),
  // Armory
  gettext_noop("Forges sword and shield. Without it you never get a new soldier."),
  // Metalworks
  gettext_noop("Makes the tools your people need to take up a trade - tongs, a saw, an axe."),
  // Iron smelter
  gettext_noop("Melts iron for the armory and the metalworks."),
  // Charburner
  gettext_noop("Makes coal. The way out when your mountains have none."),
  // Pig farm
  gettext_noop("Raises pigs into ham for the slaughterhouse."),
  // Storehouse
  gettext_noop("A second store far from home: wares and workers wait here instead of walking all "
               "the way back to the headquarters."),
  // Nothing9 - Platzhalter ohne Namen und ohne Icon
  "",
  // Mill
  gettext_noop("Grinds flour for the bakery."),
  // Bakery
  gettext_noop("Bakes bread - the food your miners like best."),
  // Sawmill
  gettext_noop("Makes boards. Every single building needs them, so put one up early and keep it "
               "supplied."),
  // Mint
  gettext_noop("Strikes coins. Coins sent to a guard post promote the soldiers inside."),
  // Well
  gettext_noop("Draws water for the bakery, the brewery, the pig farm and the donkey breeder."),
  // Shipyard
  gettext_noop("Builds a boat for waterways, and ships once you own a harbour."),
  // Farm
  gettext_noop("Grows grain for the mill, the brewery, the pig farm and the donkey breeder."),
  // Donkey breeding
  gettext_noop("Raises donkeys. A donkey carries wares along a road much faster than a man."),
  // Harbor building
  gettext_noop("Loads ships with wares and settlers, so you can settle another island."),
};

const helpers::EnumArray<const char*, BuildingType> BUILDING_SITE_STRINGS = {
  // Headquarters
  "",
  // Barracks
  gettext_noop("Needs distance from your other guard posts - two of them cannot stand side by "
               "side."),
  // Guardhouse
  gettext_noop("Needs distance from your other guard posts - two of them cannot stand side by "
               "side."),
  // Skinner
  "",
  // Watchtower
  gettext_noop("Needs distance from your other guard posts - two of them cannot stand side by "
               "side."),
  // Vineyard
  gettext_noop("Needs open, flat land around it. The grower plants his vines on it."),
  // Winery
  "",
  // Temple
  "",
  // Tannery
  "",
  // Fortress
  gettext_noop("Needs distance from your other guard posts - two of them cannot stand side by "
               "side."),
  // Granite mine
  gettext_noop("Only in a mountain, and only where a geologist has found stone. The mountain runs "
               "out one day."),
  // Coal mine
  gettext_noop("Only in a mountain, and only where a geologist has found coal. The mountain runs "
               "out one day."),
  // Iron mine
  gettext_noop("Only in a mountain, and only where a geologist has found iron. The mountain runs "
               "out one day."),
  // Gold mine
  gettext_noop("Only in a mountain, and only where a geologist has found gold. The mountain runs "
               "out one day."),
  // Lookout tower
  "",
  // Leatherworks
  "",
  // Catapult
  "",
  // Woodcutter
  gettext_noop("Needs trees within a short walk. When they are all felled he stands still until a "
               "forester replants."),
  // Fishery
  gettext_noop("Needs water within a short walk, and the fish in it run out for good."),
  // Quarry
  gettext_noop("Needs rocks within a short walk. When they are gone the quarry falls silent - "
               "rocks do not grow back."),
  // Forester
  gettext_noop("Needs open ground within a short walk, otherwise he has nowhere to plant."),
  // Slaughterhouse
  "",
  // Hunter
  gettext_noop("Needs wild animals within a short walk, and they do not come back."),
  // Brewery
  "",
  // Armory
  "",
  // Metalworks
  "",
  // Iron smelter
  "",
  // Charburner
  "",
  // Pig farm
  "",
  // Storehouse
  "",
  // Nothing9
  "",
  // Mill
  "",
  // Bakery
  "",
  // Sawmill
  "",
  // Mint
  "",
  // Well
  gettext_noop("Only where there is water under the ground. Send a geologist first, or the well "
               "stays dry."),
  // Shipyard
  gettext_noop("Must stand at the shore, otherwise the boats never reach the water."),
  // Farm
  gettext_noop("Needs a lot of open, flat land around it - the farmer sows his fields on it."),
  // Donkey breeding
  "",
  // Harbor building
  gettext_noop("Only on a harbour site: a marked spot on the coast, and there are only a few on "
               "any map."),
};
