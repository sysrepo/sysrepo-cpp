/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/

#include <doctest/doctest.h>
#include <optional>
#include <sysrepo-cpp/Connection.hpp>

TEST_CASE("connection")
{
    std::optional<sysrepo::Connection> conn{std::in_place};
    auto sess = conn->sessionStart();

    // The Session should be still valid even after the Connection class gets freed.
    conn = std::nullopt;

    REQUIRE(sess.activeDatastore() == sysrepo::Datastore::Running);
}
