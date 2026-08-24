// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

// BEFUND 4: dskOptions ist fuer das Pad freigeschaltet (dskOptions.h: WantsPadInput), war aber
// bisher der EINZIGE freigeschaltete Bildschirm ohne einen einzigen Nachweis - er liess sich in
// dieser Testumgebung nicht einmal konstruieren.
//
// DIE URSACHE, gefunden statt vermutet: dskOptions zeigt eine Sprachliste, und die kommt aus
// Languages::loadLanguages. Diese Stelle dereferenziert das Suchergebnis ihres Archivs
// UNGEPRUEFT (languages.cpp: dynamic_cast<...&>(*LOADER.GetArchive("languages").find(...))).
// Im Test gibt es das Archiv "languages" nicht - die Ressource liegt in den Spieldaten, die der
// Testlauf nicht aufloest -, also lief der Konstruktor in einen Nullzeigerzugriff.
//
// Behoben mit einem Ersatzarchiv in derselben Bauform wie LoadDummyGUIFiles und
// LoadDummyMapFiles: Loader::LoadDummyLanguageFiles. Damit ist der Bildschirm hier baubar und
// die Luecke geschlossen.
//
// Wie ueberall in diesen Nachweisen liegt die einzige Naht beim Treiber: der Test schreibt
// Padereignisse in MockupVideoDriver::padEvents_ und ruft WINDOWMANAGER.Draw().

#include "MenuPadFixture.h"
#include "Loader.h"
#include "Settings.h"
#include "WindowManager.h"
#include "controls/ctrlButton.h"
#include "controls/ctrlOptionGroup.h"
#include "desktops/dskMainMenu.h"
#include "desktops/dskOptions.h"
#include "helpers/containerUtils.h"
#include "input/PadRouter.h"
#include "rttr/test/ConfigOverride.hpp"
#include "rttr/test/TmpFolder.hpp"
#include <boost/test/unit_test.hpp>
#include <memory>
#include <vector>

namespace {

constexpr PadButton Activate = PadButton::A;
constexpr PadButton NextCtrl = PadButton::RightShoulder;
constexpr PadDeviceId pad = 70;

struct OptionsPadFixture : rttr::test::MenuPadFixture
{
    /// dskOptions schreibt beim Verlassen SETTINGS.Save() - das darf NICHT in das echte
    /// Benutzerverzeichnis gehen. Dieselbe Absicherung wie in LocalGameFixture.
    rttr::test::TmpFolder userData_;
    rttr::test::ConfigOverride userDataOverride_{"USERDATA", userData_};

    OptionsPadFixture() { LOADER.LoadDummyLanguageFiles(); }

    void toOptions()
    {
        WINDOWMANAGER.Switch(std::make_unique<dskOptions>());
        frame();
        BOOST_TEST_REQUIRE(desktopAs<dskOptions>() != nullptr);
    }

    /// Die Reiterleiste "Allgemein / Grafik / Sound". Gesucht wird sie ueber ihren TYP und nicht
    /// ueber eine Id - die Aufzaehlung in dskOptions.cpp ist privat, und ein Test, der ihre
    /// Zahlen abschreibt, prueft am Ende die Abschrift.
    static ctrlOptionGroup& tabs()
    {
        auto* dsk = desktopAs<dskOptions>();
        BOOST_TEST_REQUIRE(dsk != nullptr);
        const auto groups = dsk->GetCtrls<ctrlOptionGroup>();
        BOOST_TEST_REQUIRE(groups.size() == 1u);
        return *groups.front();
    }

    /// Faehrt den Fokus mit dem Schulterknopf weiter, bis er auf `target` steht.
    bool focusUntil(const Window* target, const unsigned maxSteps = 40)
    {
        for(unsigned i = 0; i < maxSteps; ++i)
        {
            if(focused(0) == target)
                return true;
            press(pad, NextCtrl);
        }
        return focused(0) == target;
    }
};

} // namespace

BOOST_AUTO_TEST_SUITE(MenuPadOptionsTests)

/// Der Bildschirm laesst sich ueberhaupt bauen und nimmt ein Pad an. Genau das war bisher
/// unbewiesen - freigeschaltet, aber nie ausprobiert.
BOOST_FIXTURE_TEST_CASE(TheOptionsScreenAcceptsAPad, OptionsPadFixture)
{
    toOptions();
    pickUp(pad);
    BOOST_TEST(router().GetSlot(pad) == 0u);
    BOOST_TEST_REQUIRE(focused(0) != nullptr);

    // Und die Navigation bewegt sich wirklich - ein Bildschirm mit genau einer Fokusstation
    // waere mit dem Pad nicht bedienbar, obwohl er "bedienbar" hiesse.
    const Window* first = focused(0);
    press(pad, NextCtrl);
    BOOST_TEST(focused(0) != first);
}

/// Die drei Reiter sind mit dem Pad erreichbar UND umschaltbar. Ohne sie waere der halbe
/// Bildschirm (Grafik, Sound) vom Sofa aus unsichtbar.
BOOST_FIXTURE_TEST_CASE(TheOptionsTabsCanBeSwitchedWithThePad, OptionsPadFixture)
{
    toOptions();
    pickUp(pad);

    ctrlOptionGroup& group = tabs();
    const std::vector<ctrlButton*> tabButtons = group.GetCtrls<ctrlButton>();
    BOOST_TEST_REQUIRE(tabButtons.size() == 3u); // Allgemein, Grafik, Sound
    const unsigned before = group.GetSelection();

    // Ein Reiter, der NICHT der aktuelle ist.
    ctrlButton* target = nullptr;
    for(ctrlButton* bt : tabButtons)
    {
        if(bt->GetID() != before)
        {
            target = bt;
            break;
        }
    }
    BOOST_TEST_REQUIRE(target != nullptr);

    BOOST_TEST_REQUIRE(focusUntil(target), "the pad to reach another options tab");
    press(pad, Activate);
    frame();
    BOOST_TEST(group.GetSelection() == target->GetID());
    BOOST_TEST(group.GetSelection() != before);
}

/// Und der Weg zurueck ins Hauptmenue ist ebenfalls rein mit dem Pad begehbar - sonst waere der
/// Optionsbildschirm eine Sackgasse, aus der nur die Maus wieder herausfuehrt.
BOOST_FIXTURE_TEST_CASE(ThePadFindsTheWayBackFromTheOptions, OptionsPadFixture)
{
    toOptions();
    pickUp(pad);

    // "Zurueck" ist der erste fokussierbare Knopf des Bildschirms (dskOptions.cpp: ID_btBack ist
    // die erste freie Id nach dskMenuBase, und die Texte davor koennen keinen Fokus annehmen).
    BOOST_TEST_REQUIRE(focused(0) != nullptr);
    press(pad, Activate);
    for(int i = 0; i < 5; ++i)
        frame();
    BOOST_TEST(desktopAs<dskMainMenu>() != nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
