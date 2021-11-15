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
#include <sysrepo-cpp/utils/utils.hpp>
#include <sysrepo-cpp/utils/exception.hpp>

TEST_CASE("session")
{
    sysrepo::setLogLevelStderr(sysrepo::LogLevel::Information);
    std::optional<sysrepo::Connection> conn{std::in_place};
    auto sess = conn->sessionStart();
    sess.copyConfig(sysrepo::Datastore::Startup);

    DOCTEST_SUBCASE("Session should be still valid even after the Connection class gets freed")
    {
        conn = std::nullopt;
        REQUIRE(sess.activeDatastore() == sysrepo::Datastore::Running);
    }

    DOCTEST_SUBCASE("Session lifetime is prolonged with the sysrepo::Data class")
    {
        sess.setItem("/test_module:leafInt32", "123");
        sess.applyChanges();
        {
            auto data = sysrepo::Connection{}.sessionStart().getData("/test_module:leafInt32");
            REQUIRE(data->tree().asTerm().valueStr() == "123");
        }

        {
            auto tree = sysrepo::Connection{}.sessionStart().getData("/test_module:leafInt32")->tree();
            REQUIRE(tree.asTerm().valueStr() == "123");
        }
    }

    DOCTEST_SUBCASE("basic data manipulation")
    {
        auto data = sess.getData("/test_module:leafInt32");
        REQUIRE(!data);

        sess.setItem("/test_module:leafInt32", "123");
        sess.applyChanges();
        data = sess.getData("/test_module:leafInt32");
        REQUIRE(data->tree().asTerm().valueStr() == "123");

        sess.setItem("/test_module:leafInt32", "420");
        sess.applyChanges();
        data = sess.getData("/test_module:leafInt32");
        REQUIRE(data->tree().asTerm().valueStr() == "420");

        sess.deleteItem("/test_module:leafInt32");
        sess.applyChanges();
        data = sess.getData("/test_module:leafInt32");
        REQUIRE(!data);

        sess.setItem("/test_module:leafInt32", "123");
        sess.discardChanges();
        data = sess.getData("/test_module:leafInt32");
        REQUIRE(!data);

        REQUIRE_THROWS_WITH_AS(sess.setItem("/test_module:non-existent", nullptr),
                "Session::setItem: Couldn't set '/test_module:non-existent (1)",
                sysrepo::ErrorWithCode);;
    }

    DOCTEST_SUBCASE("edit batch")
    {
        auto data = sess.getData("/test_module:leafInt32");
        REQUIRE(!data);

        auto batch = sess.getContext().newPath("/test_module:leafInt32", "1230");
        sess.editBatch(batch, sysrepo::DefaultOperation::Merge);
        sess.applyChanges();
        data = sess.getData("/test_module:leafInt32");
        REQUIRE(data->tree().asTerm().valueStr() == "1230");
    }

    DOCTEST_SUBCASE("switching datastore")
    {
        sess.switchDatastore(sysrepo::Datastore::Startup);
        REQUIRE(sess.activeDatastore() == sysrepo::Datastore::Startup);
        sess.switchDatastore(sysrepo::Datastore::Candidate);
        REQUIRE(sess.activeDatastore() == sysrepo::Datastore::Candidate);
        sess.switchDatastore(sysrepo::Datastore::Operational);
        REQUIRE(sess.activeDatastore() == sysrepo::Datastore::Operational);
        sess.switchDatastore(sysrepo::Datastore::Running);
        REQUIRE(sess.activeDatastore() == sysrepo::Datastore::Running);
    }
}
