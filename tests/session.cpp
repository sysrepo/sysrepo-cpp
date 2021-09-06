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

TEST_CASE("session")
{
    std::optional<sysrepo::Connection> conn{std::in_place};
    auto sess = conn->sessionStart();

    DOCTEST_SUBCASE("Session should be still valid even after the Connection class gets freed")
    {
        conn = std::nullopt;
        REQUIRE(sess.activeDatastore() == sysrepo::Datastore::Running);
    }

    DOCTEST_SUBCASE("basic data manipulation")
    {
        sess.setItem("/test_module:leafInt32", "123");
        sess.applyChanges();
        REQUIRE(sess.getItem("/test_module:leafInt32").xpath() == "/test_module:leafInt32");
    }
}
