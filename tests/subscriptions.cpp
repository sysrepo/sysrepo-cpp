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

    DOCTEST_SUBCASE("Getting changes")
    {
        Recorder rec;
        sysrepo::ModuleChangeCb moduleChangeCb;

        DOCTEST_SUBCASE("Getting changes")
        {
            moduleChangeCb = [&rec] (sysrepo::Session session, auto, auto, auto, auto, auto) -> sysrepo::ErrorCode {
                TROMPELOEIL_REQUIRE_CALL(rec, record(sysrepo::ChangeOperation::Modified, "/test_module:leafInt32", std::nullopt, "42", false));
                for (const auto& change : session.getChanges("//.")) {
                    rec.record(change.operation, std::string{change.node.path()}, change.previousList, change.previousValue, change.previousDefault);
                }
                return sysrepo::ErrorCode::Ok;
            };
        }

        DOCTEST_SUBCASE("Iterator comparison")
        {
            moduleChangeCb = [] (sysrepo::Session session, auto, auto, auto, auto, auto) -> sysrepo::ErrorCode {
                auto changes = session.getChanges();
                auto it1 = changes.begin();
                auto it2 = changes.begin();
                REQUIRE(it1 == it2);
                it1++;
                REQUIRE(it1 != it2);
                it2++;
                REQUIRE(it1 == it2);
                REQUIRE(it1 == changes.end());
                REQUIRE(it2 == changes.end());
                return sysrepo::ErrorCode::Ok;
            };

        }

        auto sub = sess.onModuleChange("test_module", moduleChangeCb, nullptr, 0, sysrepo::SubscribeOptions::DoneOnly);
        sess.setItem("/test_module:leafInt32", "123");
        sess.applyChanges();
    }

    DOCTEST_SUBCASE("Copy config")
    {
        Recorder rec;
        sysrepo::ModuleChangeCb moduleChangeCb = [&rec] (sysrepo::Session session, auto, auto, auto, auto, auto) -> sysrepo::ErrorCode {
            for (const auto& change : session.getChanges("//.")) {
                rec.record(change.operation, std::string{change.node.path()}, change.previousList, change.previousValue, change.previousDefault);
            }
            return sysrepo::ErrorCode::Ok;
        };


        TROMPELOEIL_REQUIRE_CALL(rec, record(sysrepo::ChangeOperation::Deleted, "/test_module:leafInt32", std::nullopt, std::nullopt, false));
        auto sub = sess.onModuleChange("test_module", moduleChangeCb, nullptr, 0, sysrepo::SubscribeOptions::DoneOnly);
        sess.copyConfig(sysrepo::Datastore::Startup, "test_module");
    }

    DOCTEST_SUBCASE("Operational get items")
    {
        sysrepo::ErrorCode retCode;
        std::optional<libyang::DataNode> toSet;
        sysrepo::OperGetItemsCb operGetItemsCb = [&] (sysrepo::Session, auto, auto, auto, auto, auto, std::optional<libyang::DataNode>& parent) {
            parent = toSet;
            return retCode;
        };

        auto sub = sess.onOperGetItems("test_module", operGetItemsCb, "/test_module:stateLeaf");
        sess.switchDatastore(sysrepo::Datastore::Operational);

        DOCTEST_SUBCASE("ok code")
        {
            retCode = sysrepo::ErrorCode::Ok;

            DOCTEST_SUBCASE("set nullopt")
            {
                toSet = std::nullopt;
                REQUIRE(sess.getData("/test_module:stateLeaf") == std::nullopt);
            }

            DOCTEST_SUBCASE("set a return node")
            {
                toSet = sess.getContext().newPath("/test_module:stateLeaf", "123");
                REQUIRE(sess.getData("/test_module:stateLeaf")->path() == "/test_module:stateLeaf");
            }
        }

        DOCTEST_SUBCASE("error code")
        {
            retCode = sysrepo::ErrorCode::Internal;

            DOCTEST_SUBCASE("set nullopt")
            {
                toSet = std::nullopt;
            }

            DOCTEST_SUBCASE("set a return node")
            {
                toSet = sess.getContext().newPath("/test_module:stateLeaf", "123");
            }

            REQUIRE_THROWS(sess.getData("/test_module:stateLeaf"));
        }
    }
}
