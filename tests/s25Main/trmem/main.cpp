// SPDX-License-Identifier: GPL-2.0-or-later
#define BOOST_TEST_MODULE RTTR_TrMem
#include <rttr/test/Fixture.hpp>
#include <boost/test/unit_test.hpp>

struct TrMemFixture : rttr::test::Fixture
{};

BOOST_GLOBAL_FIXTURE(TrMemFixture);
