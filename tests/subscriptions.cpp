/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/

#include <atomic>
#include <doctest/doctest.h>
#include <pretty_printers.hpp>
#include <sysrepo-cpp/Connection.hpp>
#include <trompeloeil.hpp>

using namespace std::string_view_literals;
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
    TROMPELOEIL_MAKE_CONST_MOCK1(recordRPC, void(std::string_view));
};

TEST_CASE("subscriptions")
{
    sysrepo::Connection conn;
    auto sess = conn.sessionStart();
    sess.copyConfig(sysrepo::Datastore::Startup, "test_module");
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

    DOCTEST_SUBCASE("Moving items")
    {
        auto getNumberOrder = [&] {
            std::vector<int32_t> res;
            auto data = sess.getData("/test_module:values");
            auto siblings = data->firstSibling().siblings();
            for (const auto& sibling : siblings)
            {
                REQUIRE(sibling.schema().path() == "/test_module:values");
                res.emplace_back(std::get<int32_t>(sibling.asTerm().value()));

            }
            return res;
        };

        sess.setItem("/test_module:values[.='10']", nullptr);
        sess.setItem("/test_module:values[.='20']", nullptr);
        sess.setItem("/test_module:values[.='30']", nullptr);
        sess.setItem("/test_module:values[.='40']", nullptr);
        sess.applyChanges();
        Recorder rec;
        sysrepo::ModuleChangeCb moduleChangeCb = [&rec] (sysrepo::Session session, auto, auto, auto, auto, auto) -> sysrepo::ErrorCode {
            for (const auto& change : session.getChanges()) {
                // FIXME: nothing appears here for some reason? no moves, nothing. But the callback IS called.
                rec.record(change.operation, std::string{change.node.path()}, change.previousList, change.previousValue, change.previousDefault);
            }
            return sysrepo::ErrorCode::Ok;
        };

        auto sub = sess.onModuleChange("test_module", moduleChangeCb, nullptr, 0, sysrepo::SubscribeOptions::DoneOnly);

        std::vector<int32_t> expected;

        DOCTEST_SUBCASE("don't move")
        {
            expected = {10, 20, 30, 40};
        }

        DOCTEST_SUBCASE("first")
        {
            sess.moveItem("/test_module:values[.='40']", sysrepo::MovePosition::First, nullptr);
            expected = {40, 10, 20, 30};
            sess.applyChanges();
        }

        DOCTEST_SUBCASE("last")
        {
            sess.moveItem("/test_module:values[.='20']", sysrepo::MovePosition::Last, nullptr);
            expected = {10, 30, 40, 20};
            sess.applyChanges();
        }

        DOCTEST_SUBCASE("after")
        {
            sess.moveItem("/test_module:values[.='20']", sysrepo::MovePosition::After, "30");
            expected = {10, 30, 20, 40};
            sess.applyChanges();
        }

        DOCTEST_SUBCASE("before")
        {
            sess.moveItem("/test_module:values[.='30']", sysrepo::MovePosition::Before, "20");
            expected = {10, 30, 20, 40};
            sess.applyChanges();
        }

        REQUIRE(getNumberOrder() == expected);
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

        // Add something to the datastore, so that the copyConfig call can delete it.
        sess.setItem("/test_module:leafInt32", "123");
        sess.applyChanges();

        TROMPELOEIL_REQUIRE_CALL(rec, record(sysrepo::ChangeOperation::Deleted, "/test_module:leafInt32", std::nullopt, std::nullopt, false));
        auto sub = sess.onModuleChange("test_module", moduleChangeCb, nullptr, 0, sysrepo::SubscribeOptions::DoneOnly);
        sess.copyConfig(sysrepo::Datastore::Startup, "test_module");
    }

    DOCTEST_SUBCASE("Operational get items")
    {
        sysrepo::ErrorCode retCode;
        std::optional<libyang::DataNode> toSet;
        std::atomic<bool> shouldThrow = false;
        sysrepo::OperGetItemsCb operGetItemsCb = [&] (sysrepo::Session, auto, auto, auto, auto, auto, std::optional<libyang::DataNode>& parent) {
            parent = toSet;
            if (shouldThrow) {
                throw std::runtime_error("Test callback throw");
            }
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

        DOCTEST_SUBCASE("exception")
        {
            retCode = sysrepo::ErrorCode::Ok;

            shouldThrow = true;
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

    DOCTEST_SUBCASE("RPC/action")
    {
        Recorder rec;
        const char* rpcPath;
        sysrepo::ErrorCode ret;
        std::atomic<bool> shouldThrow = false;
        std::function<void(libyang::DataNode&)> setFunction;
        sysrepo::RpcActionCb rpcActionCb = [&] (sysrepo::Session, auto, auto path, auto, auto, auto, libyang::DataNode output) {
            rec.recordRPC(path);
            if (setFunction) {
                setFunction(output);
            }

            if (shouldThrow) {
                throw std::runtime_error("Test callback throw");
            }
            return ret;
        };

        DOCTEST_SUBCASE("ok return / test_module:noop / no output")
        {
            rpcPath = "/test_module:noop";
            ret = sysrepo::ErrorCode::Ok;
            setFunction = nullptr;
        }
        DOCTEST_SUBCASE("ok return / test_module:shutdown / no output")
        {
            rpcPath = "/test_module:shutdown";
            ret = sysrepo::ErrorCode::Ok;
            setFunction = nullptr;
        }
        DOCTEST_SUBCASE("ok return / test_module:shutdown / some output")
        {
            rpcPath = "/test_module:shutdown";
            ret = sysrepo::ErrorCode::Ok;
            setFunction = [] (auto node) {
                node.newPath("/test_module:shutdown/success", "true", libyang::CreationOptions::Output);
            };
        }
        DOCTEST_SUBCASE("error return / test_module:noop / no output")
        {
            rpcPath = "/test_module:noop";
            ret = sysrepo::ErrorCode::Internal;
            setFunction = nullptr;
        }
        DOCTEST_SUBCASE("error return / test_module:shutdown / no output")
        {
            rpcPath = "/test_module:shutdown";
            ret = sysrepo::ErrorCode::Internal;
            setFunction = nullptr;
        }
        DOCTEST_SUBCASE("error return / test_module:shutdown / some output")
        {
            rpcPath = "/test_module:shutdown";
            ret = sysrepo::ErrorCode::Internal;
            setFunction = [] (auto node) {
                node.newPath("/test_module:shutdown/success", "true", libyang::CreationOptions::Output);
            };
        }
        DOCTEST_SUBCASE("exception / test_module:noop / no output")
        {
            rpcPath = "/test_module:noop";
            ret = sysrepo::ErrorCode::Internal;
            setFunction = nullptr;
            shouldThrow = true;
        }
        DOCTEST_SUBCASE("exception / test_module:shutdown / no output")
        {
            rpcPath = "/test_module:shutdown";
            ret = sysrepo::ErrorCode::Internal;
            setFunction = nullptr;
            shouldThrow = true;
        }
        DOCTEST_SUBCASE("exception / test_module:shutdown / some output")
        {
            rpcPath = "/test_module:shutdown";
            ret = sysrepo::ErrorCode::Internal;
            setFunction = [] (auto node) {
                node.newPath("/test_module:shutdown/success", "true", libyang::CreationOptions::Output);
            };
            shouldThrow = true;
        }

        auto sub = sess.onRPCAction(rpcPath, rpcActionCb);
        REQUIRE_CALL(rec, recordRPC(rpcPath));
        if (ret == sysrepo::ErrorCode::Ok) {
            auto output = sess.sendRPC(sess.getContext().newPath(rpcPath));
            if (setFunction) {
                REQUIRE(output.findPath("/test_module:shutdown/success", libyang::OutputNodes::Yes));
            } else {
                REQUIRE(!output.findPath("/test_module:shutdown/success", libyang::OutputNodes::Yes).has_value());
            }
        } else {
            REQUIRE_THROWS(sess.sendRPC(sess.getContext().newPath(rpcPath)));
        }
    }
}
