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

    DOCTEST_SUBCASE("Session lifetime is prolonged with data from getData")
    {
        sess.setItem("/test_module:leafInt32", "123");
        sess.applyChanges();
        auto data = sysrepo::Connection{}.sessionStart().getData("/test_module:leafInt32");
        REQUIRE(data->asTerm().valueStr() == "123");
    }

    DOCTEST_SUBCASE("basic data manipulation")
    {
        auto data = sess.getData("/test_module:leafInt32");
        REQUIRE(!data);

        sess.setItem("/test_module:leafInt32", "123");
        sess.applyChanges();
        data = sess.getData("/test_module:leafInt32");
        REQUIRE(data->asTerm().valueStr() == "123");

        sess.setItem("/test_module:leafInt32", "420");
        sess.applyChanges();
        data = sess.getData("/test_module:leafInt32");
        REQUIRE(data->asTerm().valueStr() == "420");

        sess.deleteItem("/test_module:leafInt32");
        sess.applyChanges();
        data = sess.getData("/test_module:leafInt32");
        REQUIRE(!data);

        sess.setItem("/test_module:leafInt32", "123");
        sess.discardChanges();
        data = sess.getData("/test_module:leafInt32");
        REQUIRE(!data);

        REQUIRE_THROWS_WITH_AS(sess.setItem("/test_module:non-existent", std::nullopt),
                "Session::setItem: Couldn't set '/test_module:non-existent': SR_ERR_INVAL_ARG",
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
        REQUIRE(data->asTerm().valueStr() == "1230");
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

    DOCTEST_SUBCASE("Session::getConnection")
    {
        auto connection = sess.getConnection();
    }

    DOCTEST_SUBCASE("NACM")
    {
        // Before turning NACM on, we can set the value of the default-deny-all leaf.
        sess.setItem("/test_module:denyAllLeaf", "AHOJ");
        sess.applyChanges();
        // And also retrieve the value.
        auto data = sess.getData("/test_module:denyAllLeaf");
        REQUIRE(data.value().findPath("/test_module:denyAllLeaf").value().asTerm().valueStr() == "AHOJ");

        auto nacmSub = sess.initNacm();
        sess.setNacmUser("nobody");
        data = sess.getData("/test_module:denyAllLeaf");
        // After turning on NACM, we can't access the leaf.
        REQUIRE(!data);

        // And we can't set its value.
        sess.setItem("/test_module:denyAllLeaf", "someValue");
        REQUIRE_THROWS_WITH_AS(sess.applyChanges(), "Session::applyChanges: Couldn't apply changes: SR_ERR_UNAUTHORIZED", sysrepo::ErrorWithCode);
    }
}
