// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

// Das Aufklappmenue AM PAD, ueber die Treibernaht.
//
// Der Auftraggeber nannte zwei Dinge: die Liste sei nur per Maus bedienbar, und das
// Aufklappmenue sei schwierig zu bedienen. Die MAUSFEHLER sind in
// tests/s25Main/UI/testDropdownAndMapNavigation.cpp eingefroren; hier geht es darum, dass die
// vier Schritte am Pad wirklich beim Control ankommen, wenn nur PadEvents in den Treiber
// geschrieben werden:
//
//   A  = aufklappen, dann bestaetigen
//   Steuerkreuz = blaettern, ohne etwas festzulegen
//   B  = verwerfen (und NICHT den Bildschirm verlassen)
//   Schultertaste = weiter, verwirft ebenfalls
//
// DER EIGENE TESTDESKTOP ist Absicht. dskOptions waere der naheliegende Ort, hat aber eine
// Combobox, die in dieser Testumgebung beim Umstellen abstuerzt (ID_cbCommonPortrait ->
// updatePortraitControls -> LOADER.GetTextureN liefert nullptr -> ctrlImageButton::DrawContent
// dereferenziert ungeprueft). Das trifft den Maus- genauso wie den Padweg und ist keine
// Padregression - aber ein Nachweis fuer ctrlComboBox darf nicht an der Ressourcenlage von
// dskOptions haengen. Ein kurzer zweiter Fall unten belegt, dass es am echten Bildschirm
// ankommt, ohne dort einen Wert anzufassen.

#include "MenuPadFixture.h"
#include "Loader.h"
#include "WindowManager.h"
#include "controls/ctrlComboBox.h"
#include "controls/ctrlList.h"
#include "desktops/dskOptions.h"
#include "input/PadRouter.h"
#include "rttr/test/ConfigOverride.hpp"
#include "rttr/test/TmpFolder.hpp"
#include <boost/test/unit_test.hpp>
#include <helpers/optional_io.h>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr PadButton Activate = PadButton::A;
constexpr PadButton Cancel = PadButton::B;
constexpr PadButton NextCtrl = PadButton::RightShoulder;
constexpr PadButton Down = PadButton::DpadDown;
constexpr PadDeviceId pad = 80;

constexpr unsigned ID_Combo = 1;
constexpr unsigned ID_Button = 2;

/// Ein Bildschirm mit genau einem Aufklappmenue und einem Knopf daneben. Er merkt sich, was
/// die Controls ihm melden - mehr tut er nicht.
struct ComboDesktop : Desktop
{
    std::vector<unsigned> comboSelected;
    std::vector<unsigned> clicks;
    std::vector<PadButton> padCommands;

    ComboDesktop() : Desktop(nullptr)
    {
        auto* combo =
          AddComboBox(ID_Combo, DrawPoint(50, 50), Extent(120, 20), TextureColor::Green1, NormalFont, 200, false);
        for(int i = 0; i < 5; i++)
            combo->AddItem("Wahl " + std::to_string(i));
        combo->SetSelection(1);
        AddTextButton(ID_Button, DrawPoint(50, 300), Extent(120, 20), TextureColor::Green1, "Weiter", NormalFont);
    }

    bool WantsPadInput() const override { return true; }
    void Msg_ComboSelectItem(unsigned, unsigned sel) override { comboSelected.push_back(sel); }
    void Msg_ButtonClick(unsigned id) override { clicks.push_back(id); }
    bool Msg_PadCommand(unsigned, PadButton button) override
    {
        padCommands.push_back(button);
        return true;
    }
};

struct DropdownPadFixture : rttr::test::MenuPadFixture
{
    ComboDesktop* toComboDesktop()
    {
        WINDOWMANAGER.Switch(std::make_unique<ComboDesktop>());
        frame();
        auto* dsk = desktopAs<ComboDesktop>();
        BOOST_TEST_REQUIRE(dsk != nullptr);
        return dsk;
    }

    static ctrlComboBox& combo()
    {
        auto* dsk = desktopAs<ComboDesktop>();
        BOOST_TEST_REQUIRE(dsk != nullptr);
        auto* cb = dsk->GetCtrl<ctrlComboBox>(ID_Combo);
        BOOST_TEST_REQUIRE(cb != nullptr);
        return *cb;
    }
};

} // namespace

BOOST_AUTO_TEST_SUITE(MenuPadDropdownTests)

/// Aufklappen, blaettern, bestaetigen - drei getrennte Schritte, alle nur aus Padereignissen.
///
/// Vorher gab es nur einen: jeder Steuerkreuzschritt setzte den Wert sofort UND klappte die
/// Liste dabei zu. Die aufgeklappte Liste hatte fuer das Pad ueberhaupt keine Funktion, und in
/// dskOptions hiess ein einziger Schritt in der Sprachliste: Sprache umstellen und den ganzen
/// Bildschirm neu bauen.
BOOST_FIXTURE_TEST_CASE(APadOpensBrowsesAndConfirmsADropdown, DropdownPadFixture)
{
    auto* dsk = toComboDesktop();
    pickUp(pad);
    BOOST_TEST_REQUIRE(router().GetSlot(pad) == 0u);
    // Die Combobox hat die kleinste Id und ist damit der Einstieg.
    BOOST_TEST_REQUIRE(focused(0) == static_cast<Window*>(&combo()));
    BOOST_TEST_REQUIRE(!combo().IsListOpen());
    BOOST_TEST_REQUIRE(combo().GetSelection() == 1u);

    // A klappt auf und aendert KEINEN Wert.
    press(pad, Activate);
    BOOST_TEST(combo().IsListOpen());
    BOOST_TEST(combo().GetSelection() == 1u);
    BOOST_TEST(dsk->comboSelected.empty());

    // Steuerkreuz blaettert: die Liste bleibt offen, der Bildschirm hoert nichts.
    press(pad, Down);
    BOOST_TEST(combo().IsListOpen());
    BOOST_TEST(combo().GetSelection() == 2u);
    BOOST_TEST(dsk->comboSelected.empty());
    press(pad, Down);
    BOOST_TEST(combo().GetSelection() == 3u);
    BOOST_TEST(dsk->comboSelected.empty());
    // Und der Fokus bleibt dabei auf der Combobox - er rutscht nicht unter der offenen Liste
    // weg.
    BOOST_TEST(focused(0) == static_cast<Window*>(&combo()));

    // A bestaetigt: zu, und JETZT genau eine Meldung.
    press(pad, Activate);
    BOOST_TEST(!combo().IsListOpen());
    BOOST_TEST(combo().GetSelection() == 3u);
    BOOST_TEST_REQUIRE(dsk->comboSelected.size() == 1u);
    BOOST_TEST(dsk->comboSelected.back() == 3u);
}

/// B verwirft - und verlaesst den Bildschirm NICHT.
///
/// Das ist die Haelfte, die man leicht vergisst: MenuPadInput leitet B ausdruecklich am Fokus
/// vorbei an den Desktop (Fenster zu bzw. Msg_PadCommand). Eine Umsetzung, die das hier
/// einfach durchreicht, wuerde den halben Optionsbildschirm verlassen, nur weil der Spieler
/// ein Aufklappmenue wieder loswerden wollte.
BOOST_FIXTURE_TEST_CASE(BDiscardsTheOpenListAndKeepsTheScreen, DropdownPadFixture)
{
    auto* dsk = toComboDesktop();
    pickUp(pad);

    press(pad, Activate);
    press(pad, Down);
    BOOST_TEST_REQUIRE(combo().IsListOpen());
    BOOST_TEST_REQUIRE(combo().GetSelection() == 2u);

    press(pad, Cancel);
    BOOST_TEST(!combo().IsListOpen());
    BOOST_TEST(combo().GetSelection() == 1u); // der alte Wert steht wieder da
    BOOST_TEST(dsk->comboSelected.empty());
    // Der Desktop hat von diesem B NICHTS gehoert.
    BOOST_TEST(dsk->padCommands.empty());
    BOOST_TEST(desktopAs<ComboDesktop>() == dsk);

    // Bei GESCHLOSSENER Liste ist B unveraendert die Sache des Desktops - sonst koennte man mit
    // dem Pad aus keinem Bildschirm mehr heraus, auf dem ein Aufklappmenue den Fokus hat.
    press(pad, Cancel);
    BOOST_TEST_REQUIRE(dsk->padCommands.size() == 1u);
    BOOST_TEST((dsk->padCommands.back() == PadButton::B));
}

/// Die Schultertaste traegt den Fokus weiter. Eine offene Liste muss dabei mitgehen - sonst
/// stuende sie ueber allem und ihre Sperre laege auf dem ganzen Bildschirm, waehrend der
/// Spieler laengst woanders ist.
BOOST_FIXTURE_TEST_CASE(LeavingWithTheShoulderButtonClosesTheList, DropdownPadFixture)
{
    auto* dsk = toComboDesktop();
    pickUp(pad);

    press(pad, Activate);
    press(pad, Down);
    BOOST_TEST_REQUIRE(combo().IsListOpen());

    press(pad, NextCtrl);
    BOOST_TEST(focused(0) != static_cast<Window*>(&combo()));
    BOOST_TEST(!combo().IsListOpen());
    BOOST_TEST(combo().GetSelection() == 1u); // verworfen
    BOOST_TEST(dsk->comboSelected.empty());
    // Der Knopf daneben ist wieder erreichbar: die Sperre der Liste ist weg.
    BOOST_TEST(!dsk->IsInLockedRegion(Position(50, 300)));

    press(pad, Activate);
    BOOST_TEST_REQUIRE(dsk->clicks.size() == 1u);
    BOOST_TEST(dsk->clicks.back() == ID_Button);
}

BOOST_AUTO_TEST_SUITE_END()

namespace {

struct OptionsDropdownFixture : rttr::test::MenuPadFixture
{
    /// dskOptions schreibt beim Verlassen SETTINGS.Save() - das darf NICHT in das echte
    /// Benutzerverzeichnis gehen.
    rttr::test::TmpFolder userData_;
    rttr::test::ConfigOverride userDataOverride_{"USERDATA", userData_};

    OptionsDropdownFixture() { LOADER.LoadDummyLanguageFiles(); }
};

} // namespace

BOOST_AUTO_TEST_SUITE(MenuPadOptionsDropdownTests)

/// Und es kommt am ECHTEN Bildschirm an: auf dskOptions laesst sich ein Aufklappmenue mit dem
/// Pad erreichen, aufklappen und wieder verwerfen.
///
/// Bewusst OHNE eine Wertaenderung. Der Bildschirm haengt an Ressourcen, die dieser Testlauf
/// nicht aufloest (siehe Kopf dieser Datei); geprueft wird hier nur, dass Fokus und die beiden
/// Knoepfe ankommen - und dass der Bildschirm dabei stehen bleibt.
BOOST_FIXTURE_TEST_CASE(ADropdownOnTheOptionsScreenOpensAndCancels, OptionsDropdownFixture)
{
    WINDOWMANAGER.Switch(std::make_unique<dskOptions>());
    frame();
    auto* dsk = desktopAs<dskOptions>();
    BOOST_TEST_REQUIRE(dsk != nullptr);

    pickUp(pad);
    BOOST_TEST_REQUIRE(focused(0) != nullptr);

    // Die erste erreichbare Combobox suchen - ueber ihren TYP und nicht ueber eine Id, denn die
    // Aufzaehlung in dskOptions.cpp ist privat.
    ctrlComboBox* target = nullptr;
    for(unsigned i = 0; i < 60 && !target; ++i)
    {
        target = dynamic_cast<ctrlComboBox*>(focused(0));
        if(!target)
            press(pad, NextCtrl);
    }
    BOOST_TEST_REQUIRE(target != nullptr, "the pad to reach a dropdown on the options screen");

    const auto before = target->GetSelection();
    press(pad, Activate);
    BOOST_TEST(target->IsListOpen());
    BOOST_TEST((target->GetSelection() == before));

    press(pad, Cancel);
    BOOST_TEST(!target->IsListOpen());
    BOOST_TEST((target->GetSelection() == before));
    // Der Optionsbildschirm steht noch - B hat ihn nicht verlassen.
    BOOST_TEST(desktopAs<dskOptions>() == dsk);
}

BOOST_AUTO_TEST_SUITE_END()
