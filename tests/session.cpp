/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/

#include <doctest/doctest.h>
#include <sysrepo-cpp/Connection.hpp>

TEST_CASE("session")
{
    sysrepo::Connection conn;
    auto sess = conn.sessionStart();
}
