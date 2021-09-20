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
#include <trompeloeil.hpp>

namespace trompeloeil {
template <>
    struct printer<std::optional<std::string_view>>
    {
        static void print(std::ostream& os, const std::optional<std::string_view>& b)
        {
            if (!b) {
                os << "std::nullopt";
            } else {
                os << *b;
            }
        }
    };
}

class Recorder {
public:
    TROMPELOEIL_MAKE_CONST_MOCK5(record, void(sysrepo::ChangeOperation, std::string, std::optional<std::string_view>, std::optional<std::string_view>, bool));
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

    DOCTEST_SUBCASE("Session's lifetime is prolonged by the subscription")
    {
        auto sub = sysrepo::Connection().sessionStart().onModuleChange("test_module", [] (auto, auto, auto, auto, auto, auto) -> sysrepo::ErrorCode {
            return sysrepo::ErrorCode::Ok;
        });
    }

    DOCTEST_SUBCASE("moving ctor")
    {
        sysrepo::ModuleChangeCb moduleChangeCb = [&called] (auto, auto, auto, auto, auto, auto) -> sysrepo::ErrorCode {
            called++;
            return sysrepo::ErrorCode::Ok;
        };
        auto sub = sess.onModuleChange("test_module", moduleChangeCb, nullptr, 0, sysrepo::SubscribeOptions::DoneOnly);
        auto sub2 = std::move(sub);
        sess.setItem("/test_module:leafInt32", "42");
        sess.applyChanges();

        REQUIRE(called == 1);
    }

    // TODO: needs more test and possibly better way of testing
    DOCTEST_SUBCASE("Getting changes")
    {
        Recorder rec;
        sysrepo::ModuleChangeCb moduleChangeCb = [&rec] (sysrepo::Session session, auto, auto, auto, auto, auto) -> sysrepo::ErrorCode {
            for (const auto& change : session.getChanges("//.")) {
                // FIXME: this sucks, think of something better
                rec.record(change.operation, std::string{change.node.path()}, change.previousList, change.previousValue, change.previousDefault);
            }
            return sysrepo::ErrorCode::Ok;
        };

        auto sub = sess.onModuleChange("test_module", moduleChangeCb, nullptr, 0, sysrepo::SubscribeOptions::DoneOnly);

        DOCTEST_SUBCASE("Simple change")
        {
            sess.setItem("/test_module:leafInt32", "123");

            TROMPELOEIL_REQUIRE_CALL(rec, record(sysrepo::ChangeOperation::Modified, "/test_module:leafInt32", std::nullopt, "42", false));
            sess.applyChanges();

        }

        DOCTEST_SUBCASE("Simple change")
        {
            TROMPELOEIL_REQUIRE_CALL(rec, record(sysrepo::ChangeOperation::Deleted, "/test_module:leafInt32", std::nullopt, std::nullopt, false));

            sess.copyConfig(sysrepo::Datastore::Startup, "test_module");
        }
    }
}
