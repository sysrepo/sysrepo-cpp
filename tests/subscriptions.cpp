/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/

#include <atomic>
#include <doctest/doctest.h>
#include <sysrepo-cpp/Connection.hpp>

struct GetNode {
    template <typename Type>
    const libyang::DataNode operator()(const Type& x) const
    {
        return x.node;
    }

};

TEST_CASE("subscriptions")
{
    sysrepo::Connection conn;
    auto sess = conn.sessionStart();
    std::atomic<int> called = 0;

    DOCTEST_SUBCASE("simple case")
    {
        sysrepo::ModuleChangeCb moduleChangeCb = [&called] (auto, auto, auto, auto, auto, auto) -> sysrepo::ErrorCode {
            called++;
            return sysrepo::ErrorCode::Ok;
        };

        auto sub = sess.onModuleChange("test_module", moduleChangeCb);
        // This creates the same subscription as above, but I want to test that Subscription::onModuleChange works fine.
        sub.onModuleChange("test_module", moduleChangeCb);
        sess.setItem("/test_module:leafInt32", "123");
        sess.applyChanges();
        // Called four times twice for Event::Change and twice for Event::Done
        REQUIRE(called == 4);

    }

    DOCTEST_SUBCASE("moving ctor")
    {
        sysrepo::ModuleChangeCb moduleChangeCb = [&called] (auto, auto, auto, auto, auto, auto) -> sysrepo::ErrorCode {
            called++;
            return sysrepo::ErrorCode::Ok;
        };
        auto sub = sess.onModuleChange("test_module", moduleChangeCb, nullptr, 0, sysrepo::SubOptions::DoneOnly);
        auto sub2 = std::move(sub);
        sess.setItem("/test_module:leafInt32", "42");
        sess.applyChanges();

        REQUIRE(called == 1);
    }

    // TODO: needs more test and possibly better way of testing
    DOCTEST_SUBCASE("Getting changes")
    {
        sysrepo::ModuleChangeCb moduleChangeCb = [&called] (sysrepo::Session session, auto, auto, auto, auto, auto) -> sysrepo::ErrorCode {
            for (const auto& change : session.getChanges("//.")) {
                // FIXME: this sucks, think of something better
                REQUIRE(change.operation == sysrepo::ChangeOperation::Modified);
                REQUIRE(change.node.path() == "/test_module:leafInt32");
                REQUIRE(change.previousDefault == false);
                REQUIRE(change.previousList == std::nullopt);
                REQUIRE(change.previousValue == "42");
            }
            called++;
            return sysrepo::ErrorCode::Ok;
        };

        auto sub = sess.onModuleChange("test_module", moduleChangeCb, nullptr, 0, sysrepo::SubOptions::DoneOnly);

        sess.setItem("/test_module:leafInt32", "123");
        sess.applyChanges();

        REQUIRE(called == 1);
    }
}
