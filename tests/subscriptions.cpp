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
#include <sysrepo-cpp/utils/utils.hpp>
#include <sysrepo-cpp/utils/exception.hpp>
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
    TROMPELOEIL_MAKE_CONST_MOCK1(recordException, void(std::string));
    TROMPELOEIL_MAKE_CONST_MOCK2(recordNotification, void(sysrepo::NotificationType, std::optional<std::string_view>));
};

TEST_CASE("subscriptions")
{
    sysrepo::setLogLevelStderr(sysrepo::LogLevel::Information);
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

    DOCTEST_SUBCASE("getting libyang ctx from subscription")
    {
        sysrepo::OperGetCb operGetCb = [&] (sysrepo::Session session, auto, auto, auto, auto, auto, std::optional<libyang::DataNode>& parent) {
            parent = session.getContext().newPath("/test_module:stateLeaf", "1");
            return sysrepo::ErrorCode::Ok;
        };

        auto sub = sess.onOperGet("test_module", operGetCb, "/test_module:stateLeaf");
        sess.switchDatastore(sysrepo::Datastore::Operational);
        REQUIRE(sess.getData("/test_module:stateLeaf")->path() == "/test_module:stateLeaf");
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
                TROMPELOEIL_REQUIRE_CALL(rec, record(sysrepo::ChangeOperation::Created, "/test_module:leafInt32", std::nullopt, std::nullopt, false));
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
            TROMPELOEIL_REQUIRE_CALL(rec, record(sysrepo::ChangeOperation::Moved, "/test_module:values[.='40']", std::nullopt, "", false));
            sess.moveItem("/test_module:values[.='40']", sysrepo::MovePosition::First, nullptr);
            expected = {40, 10, 20, 30};
            sess.applyChanges();
        }

        DOCTEST_SUBCASE("last")
        {
            TROMPELOEIL_REQUIRE_CALL(rec, record(sysrepo::ChangeOperation::Moved, "/test_module:values[.='20']", std::nullopt, "40", false));
            sess.moveItem("/test_module:values[.='20']", sysrepo::MovePosition::Last, nullptr);
            expected = {10, 30, 40, 20};
            sess.applyChanges();
        }

        DOCTEST_SUBCASE("after")
        {
            TROMPELOEIL_REQUIRE_CALL(rec, record(sysrepo::ChangeOperation::Moved, "/test_module:values[.='20']", std::nullopt, "30", false));
            sess.moveItem("/test_module:values[.='20']", sysrepo::MovePosition::After, "30");
            expected = {10, 30, 20, 40};
            sess.applyChanges();
        }

        DOCTEST_SUBCASE("before")
        {
            TROMPELOEIL_REQUIRE_CALL(rec, record(sysrepo::ChangeOperation::Moved, "/test_module:values[.='30']", std::nullopt, "10", false));
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
        sysrepo::OperGetCb operGetCb = [&] (sysrepo::Session, auto, auto, auto, auto, auto, std::optional<libyang::DataNode>& parent) {
            parent = toSet;
            if (shouldThrow) {
                throw std::runtime_error("Test callback throw");
            }
            return retCode;
        };

        std::optional<sysrepo::Subscription> sub;

        DOCTEST_SUBCASE("ok code")
        {
            sub = sess.onOperGet("test_module", operGetCb, "/test_module:stateLeaf");
            retCode = sysrepo::ErrorCode::Ok;
            sess.switchDatastore(sysrepo::Datastore::Operational);

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
            sub = sess.onOperGet("test_module", operGetCb, "/test_module:stateLeaf");
            retCode = sysrepo::ErrorCode::Internal;
            sess.switchDatastore(sysrepo::Datastore::Operational);

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
            Recorder rec;
            retCode = sysrepo::ErrorCode::Ok;
            shouldThrow = true;
            sub = sess.onOperGet(
                    "test_module",
                    operGetCb,
                    "/test_module:stateLeaf",
                    sysrepo::SubscribeOptions::Default, [&rec] (std::exception& ex) { rec.recordException(ex.what()); });
            sess.switchDatastore(sysrepo::Datastore::Operational);

            REQUIRE_CALL(rec, recordException("Test callback throw"));

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

        auto sub = sess.onRPCAction(
                rpcPath,
                rpcActionCb,
                0,
                sysrepo::SubscribeOptions::Default,
                shouldThrow ? std::function{[&rec] (std::exception& ex) { rec.recordException(ex.what()); }} : nullptr);

        auto throwExpectation =
            shouldThrow ? NAMED_REQUIRE_CALL(rec, recordException("Test callback throw")) : nullptr;
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

    DOCTEST_SUBCASE("notifications")
    {
        Recorder rec;
        sysrepo::NotifCb cb = [&rec] (auto, auto, auto type, auto notification, auto) {
            if (notification) {
                for (const auto& node : notification->childrenDfs()) {
                    rec.recordNotification(type, std::string{node.path()});
                }
            } else {
                rec.recordNotification(type, std::nullopt);
            }
        };

        auto sub = std::optional{sess.onNotification("test_module", cb)};

        REQUIRE_CALL(rec, recordNotification(sysrepo::NotificationType::Realtime, "/test_module:ping"));
        REQUIRE_CALL(rec, recordNotification(sysrepo::NotificationType::Realtime, "/test_module:ping/myLeaf"));
        REQUIRE_CALL(rec, recordNotification(sysrepo::NotificationType::Terminated, std::nullopt));
        auto notification = sess.getContext().newPath("/test_module:ping");
        notification.newPath("myLeaf", "132");
        sess.sendNotification(notification, sysrepo::Wait::Yes);
        sub = std::nullopt;
    }

    DOCTEST_SUBCASE("Session::setErrorMessage")
    {
        const char* message = nullptr;

        DOCTEST_SUBCASE("No printf placeholders in message")
        {
            message = "Test error message.";
        }

        DOCTEST_SUBCASE("%s in error message")
        {
            message = "%s";
        }

        sysrepo::ModuleChangeCb moduleChangeCb = [message, fail = true] (auto session, auto, auto, auto, auto, auto) mutable -> sysrepo::ErrorCode {
            if (fail) {
                session.setErrorMessage(message);
                fail = false;
                return sysrepo::ErrorCode::OperationFailed;
            }

            return sysrepo::ErrorCode::Ok;
        };

        auto sub = sess.onModuleChange("test_module", moduleChangeCb);
        sess.setItem("/test_module:leafInt32", "123");
        try {
            sess.applyChanges();
        } catch (sysrepo::ErrorWithCode&) {
            auto errors = sess.getErrors();
            REQUIRE(errors.size() == 2);
            REQUIRE(*errors.at(0).errorMessage == message);
            REQUIRE(errors.at(0).code == sysrepo::ErrorCode::OperationFailed);
            REQUIRE(*errors.at(1).errorMessage == "User callback failed.");
            REQUIRE(errors.at(1).code == sysrepo::ErrorCode::CallbackFailed);
        }

        // The callback does not fail the second time.
        sess.applyChanges();
        auto errors = sess.getErrors();
        REQUIRE(errors.size() == 0);
    }

    DOCTEST_SUBCASE("Session::setNetconfError")
    {
        sysrepo::NetconfErrorInfo errToSet;

        DOCTEST_SUBCASE("without info elements")
        {
            errToSet = {
                .type = "application",
                .tag = "operation-failed",
                .appTag = std::nullopt,
                .path = std::nullopt,
                .message = "Test callback failure.",
                .infoElements = {}
            };
        }

        DOCTEST_SUBCASE("with info elements")
        {
            errToSet = {
                .type = "application",
                .tag = "operation-failed",
                .appTag = std::nullopt,
                .path = std::nullopt,
                .message = "Test callback failure.",
                .infoElements = {{"MyElement", "MyValue"}, {"AnotherElement", "AnotherValue"}}
            };
        }
        sysrepo::ModuleChangeCb moduleChangeCb = [&errToSet, fail = true] (auto session, auto, auto, auto, auto, auto) mutable -> sysrepo::ErrorCode {
            if (fail) {
                session.setNetconfError(errToSet);
                fail = false;
                return sysrepo::ErrorCode::OperationFailed;
            }

            return sysrepo::ErrorCode::Ok;
        };

        auto sub = sess.onModuleChange("test_module", moduleChangeCb);
        sess.setItem("/test_module:leafInt32", "123");
        try {
            sess.applyChanges();
        } catch (sysrepo::ErrorWithCode&) {
            auto errors = sess.getNetconfErrors();
            REQUIRE(errors.size() == 1);
            REQUIRE(errors.front() == errToSet);
        }
    }

    DOCTEST_SUBCASE("Originator name")
    {
        std::string_view originatorName;

        DOCTEST_SUBCASE("Originator name set")
        {
            originatorName = "Test originator";
            sess.setOriginatorName(originatorName.data());
        }

        DOCTEST_SUBCASE("Originator name not set")
        {
            originatorName = "";
        }

        sysrepo::ModuleChangeCb moduleChangeCb = [&] (sysrepo::Session session, auto, auto, auto, auto, auto) mutable -> sysrepo::ErrorCode {
            REQUIRE(session.getOriginatorName() == originatorName);
            return sysrepo::ErrorCode::Ok;
        };

        auto sub = sess.onModuleChange("test_module", moduleChangeCb);
        sess.setItem("/test_module:leafInt32", "123");
        sess.applyChanges();
    }
}
