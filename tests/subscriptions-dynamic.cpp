/*
 * Copyright (C) 2024 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <doctest/doctest.h>
#include <poll.h>
#include <pretty_printers.hpp>
#include <sysrepo-cpp/Connection.hpp>
#include <sysrepo-cpp/utils/exception.hpp>
#include <sysrepo-cpp/utils/utils.hpp>
#include <thread>
#include <trompeloeil.hpp>
#include "sysrepo-cpp/Subscription.hpp"
#include "utils.hpp"

#define REQUIRE_YANG_PUSH_ONCHANGE(SUBSCRIPTION, JSON_DATA)                                                    \
    TROMPELOEIL_REQUIRE_CALL(rec, recordYangPush(SUBSCRIPTION->subscriptionId(), JSON_DATA)).IN_SEQUENCE(seq); \
    REQUIRE(pipeStatus(SUBSCRIPTION->fd()) == PipeStatus::DataReady);                                          \
    SUBSCRIPTION->processEvent(cb);

#define _READ_NOTIFICATION(SUBSCRIPTION)                               \
    REQUIRE(pipeStatus((SUBSCRIPTION).fd()) == PipeStatus::DataReady); \
    (SUBSCRIPTION).processEvent(cbNotif);

#define REQUIRE_NOTIFICATION(SUBSCRIPTION, NOTIFICATION)                              \
    TROMPELOEIL_REQUIRE_CALL(rec, recordNotification(NOTIFICATION)).IN_SEQUENCE(seq); \
    _READ_NOTIFICATION(SUBSCRIPTION);

#define SEND_NOTIFICATION(SUBSCRIPTION, NOTIFICATION)                                                                                \
    {                                                                                                                                \
        auto notif = client.getContext().parseOp(NOTIFICATION, libyang::DataFormat::JSON, libyang::OperationType::NotificationYang); \
        client.sendNotification(*notif.tree, sysrepo::Wait::No);                                                                     \
    }

#define SEND_AND_READ_NOTIFICATION(SUBSCRIPTION, NOTIFICATION)                        \
    TROMPELOEIL_REQUIRE_CALL(rec, recordNotification(NOTIFICATION)).IN_SEQUENCE(seq); \
    SEND_NOTIFICATION(SUBSCRIPTION, NOTIFICATION);                                    \
    _READ_NOTIFICATION(SUBSCRIPTION);

using namespace std::chrono_literals;

namespace trompeloeil {
template <>
struct printer<std::optional<std::string>> {
    static void print(std::ostream& os, const std::optional<std::string>& b)
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
    TROMPELOEIL_MAKE_CONST_MOCK1(recordNotification, void(std::optional<std::string>));
    TROMPELOEIL_MAKE_CONST_MOCK2(recordYangPush, void(uint32_t, std::optional<std::string>));
};

enum class PipeStatus {
    NoData,
    DataReady,
    Hangup,
    Other, // represents any other poll() result, we are not interested in details
};

PipeStatus pipeStatus(int fd)
{
    pollfd fds = {.fd = fd, .events = POLLIN | POLLHUP, .revents = 0};

    int r = poll(&fds, 1, 250 /* timeout ms */);
    INFO("poll returned " << r << ", revents: " << fds.revents);
    if (r == 0) {
        return PipeStatus::NoData;
    } else if (r == 1 && fds.revents & POLLIN) {
        return PipeStatus::DataReady;
    } else if (r == 1 && fds.revents & POLLHUP) {
        return PipeStatus::Hangup;
    } else {
        return PipeStatus::Other;
    }
}

TEST_CASE("Dynamic subscriptions")
{
    trompeloeil::sequence seq;
    Recorder rec;

    sysrepo::setLogLevelStderr(sysrepo::LogLevel::Information);
    sysrepo::Connection conn{};
    auto sess = conn.sessionStart();
    auto client = conn.sessionStart();
    sess.sendRPC(sess.getContext().newPath("/ietf-factory-default:factory-reset"));

    auto cb = [&](const std::optional<libyang::DataNode>& tree, auto) {
        REQUIRE(tree);

        // the yang-push notification contains subscription id. This is a hack that enables us to compare string representations
        // the subscription is compared using another parameter in the expectation
        auto idNode = tree->findPath("id");
        const auto subId = std::get<uint32_t>(idNode->asTerm().value());
        idNode->unlink();

        rec.recordYangPush(subId, tree->printStr(libyang::DataFormat::JSON, libyang::PrintFlags::WithSiblings));
    };

    auto cbNotif = [&](const std::optional<libyang::DataNode>& tree, auto) {
        REQUIRE(tree);
        rec.recordNotification(tree->printStr(libyang::DataFormat::JSON, libyang::PrintFlags::WithSiblings));
    };

    // write some initial data
    client.setItem("/test_module:values[.='2']", std::nullopt);
    client.setItem("/test_module:values[.='3']", std::nullopt);
    client.applyChanges();

    DOCTEST_SUBCASE("Subscribed notifications")
    {
        auto stopTime = sysrepo::NotificationTimeStamp{std::chrono::system_clock::now() + 222ms};

        DOCTEST_SUBCASE("Subscribe to everything from test_module")
        {
            auto sub = sess.subscribeNotifications("/test_module:*", std::nullopt, stopTime);

            SEND_AND_READ_NOTIFICATION(sub, R"({
  "test_module:ping": {
    "myLeaf": 1
  }
}
)");

            SEND_AND_READ_NOTIFICATION(sub, R"({
  "test_module:silent-ping": {}
}
)");

            SEND_AND_READ_NOTIFICATION(sub, R"({
  "test_module:ping": {
    "myLeaf": 2
  }
}
)");
            REQUIRE_NOTIFICATION(sub, R"({
  "ietf-subscribed-notifications:subscription-terminated": {
    "id": 1,
    "reason": "no-such-subscription"
  }
}
)");

            waitForCompletionAndBitMore(seq);
            REQUIRE(pipeStatus(sub.fd()) == PipeStatus::Hangup);
        }

        DOCTEST_SUBCASE("Subscribe to notification termination and terminate the subscription")
        {
            auto sub = sess.subscribeNotifications("/ietf-subscribed-notifications:subscription-terminated", std::nullopt, stopTime);

            SEND_NOTIFICATION(sub, R"({
  "test_module:ping": {
    "myLeaf": 1
  }
}
)");
            REQUIRE_NOTIFICATION(sub, R"({
  "ietf-subscribed-notifications:subscription-terminated": {
    "id": 2,
    "reason": "no-such-subscription"
  }
}
)");
            waitForCompletionAndBitMore(seq);
            REQUIRE(pipeStatus(sub.fd()) == PipeStatus::Hangup);
        }
    }

    DOCTEST_SUBCASE("YANG push on change")
    {
        auto sub = std::make_unique<sysrepo::DynamicSubscription>(sess.yangPushOnChange(std::nullopt, std::nullopt, sysrepo::SyncOnStart::Yes));
        auto subFiltered = std::make_unique<sysrepo::DynamicSubscription>(sess.yangPushOnChange("/test_module:leafInt32", std::nullopt, sysrepo::SyncOnStart::Yes));

        REQUIRE_YANG_PUSH_ONCHANGE(sub, R"({
  "ietf-yang-push:push-update": {
    "datastore-contents": {
      "test_module:values": [
        2,
        3
      ]
    }
  }
}
)");

        REQUIRE_YANG_PUSH_ONCHANGE(subFiltered, R"({
  "ietf-yang-push:push-update": {
    "datastore-contents": {

    }
  }
}
)")

        client.setItem("/test_module:leafInt32", "123");
        client.setItem("/test_module:values[.='5']", std::nullopt);
        client.deleteItem("/test_module:values[.='3']");
        client.applyChanges();

        REQUIRE_YANG_PUSH_ONCHANGE(sub, R"({
  "ietf-yang-push:push-change-update": {
    "datastore-changes": {
      "yang-patch": {
        "patch-id": "patch-1",
        "edit": [
          {
            "edit-id": "edit-1",
            "operation": "create",
            "target": "/test_module:leafInt32",
            "value": {
              "test_module:leafInt32": 123
            }
          },
          {
            "edit-id": "edit-2",
            "operation": "insert",
            "target": "/test_module:values[.='5']",
            "point": "/test_module:values[.='5'][.='3']",
            "where": "after",
            "value": {
              "test_module:values": [
                5
              ]
            }
          },
          {
            "edit-id": "edit-3",
            "operation": "delete",
            "target": "/test_module:values[.='3']"
          }
        ]
      }
    }
  }
}
)");

        REQUIRE_YANG_PUSH_ONCHANGE(subFiltered, R"({
  "ietf-yang-push:push-change-update": {
    "datastore-changes": {
      "yang-patch": {
        "patch-id": "patch-1",
        "edit": [
          {
            "edit-id": "edit-1",
            "operation": "create",
            "target": "/test_module:leafInt32",
            "value": {
              "test_module:leafInt32": 123
            }
          }
        ]
      }
    }
  }
}
)")

        DOCTEST_SUBCASE("Terminate manually without a reason")
        {
            sub->terminate();

            // events can still happen and the FD is not yet closed, so we can still read from it but no new events are expected
            REQUIRE(pipeStatus(sub->fd()) == PipeStatus::Hangup);
            client.setItem("/test_module:values[.='6']", std::nullopt);
            client.applyChanges();
            REQUIRE(pipeStatus(sub->fd()) == PipeStatus::Hangup);
        }

        DOCTEST_SUBCASE("Terminate manually with a reason")
        {
            sub->terminate("ietf-subscribed-notifications:no-such-subscription");
            REQUIRE_YANG_PUSH_ONCHANGE(sub, R"({
  "ietf-subscribed-notifications:subscription-terminated": {
    "reason": "no-such-subscription"
  }
}
)");
        }

        DOCTEST_SUBCASE("Terminate by destruction")
        {
            sub.reset();

            // new events can happen but sysrepo is not supposed to send them to a terminated subscription
            // also the FD is closed no point in reading from it
            client.setItem("/test_module:values[.='6']", std::nullopt);
            client.applyChanges();
        }

        DOCTEST_SUBCASE("Terminate called twice")
        {
            sub->terminate();

            // if the message is inlined into the macro, doctest<=2.4.8 fails with "REQUIRE_THROWS_WITH_AS( sub->terminate(), "<garbage>", sysrepo::ErrorWithCode )",
            // it does work after 8de4cf7e759c55a89761c25900d80e01ae7ac3fd
            // TODO: Can be simplified after we bump minimal doctest version to doctest>=2.4.9
            const auto excMessage = "Couldn't terminate yang-push subscription with id " + std::to_string(sub->subscriptionId()) + ": SR_ERR_NOT_FOUND";
            REQUIRE_THROWS_WITH_AS(sub->terminate(), excMessage.c_str(), sysrepo::ErrorWithCode);
        }

        waitForCompletionAndBitMore(seq);
    }

    DOCTEST_SUBCASE("YANG push periodic")
    {
        uint32_t subId;
        TROMPELOEIL_REQUIRE_CALL(rec, recordYangPush(ANY(uint32_t), R"({
  "ietf-yang-push:push-update": {
    "datastore-contents": {
      "test_module:values": [
        2,
        3
      ]
    }
  }
}
)"))
            .IN_SEQUENCE(seq)
            .TIMES(AT_LEAST(1))
            .LR_SIDE_EFFECT(subId = _1);

        TROMPELOEIL_REQUIRE_CALL(rec, recordYangPush(ANY(uint32_t), R"({
  "ietf-yang-push:push-update": {
    "datastore-contents": {
      "test_module:leafInt32": 123,
      "test_module:values": [
        2,
        5
      ]
    }
  }
}
)"))
            .IN_SEQUENCE(seq)
            .TIMES(AT_LEAST(1))
            .LR_WITH(subId == _1);

        TROMPELOEIL_REQUIRE_CALL(rec, recordYangPush(ANY(uint32_t), R"({
  "ietf-yang-push:push-update": {
    "datastore-contents": {
      "test_module:leafInt32": 123,
      "test_module:values": [
        2,
        5,
        6
      ]
    }
  }
}
)"))
            .IN_SEQUENCE(seq)
            .TIMES(AT_LEAST(1))
            .LR_WITH(subId == _1);

        TROMPELOEIL_REQUIRE_CALL(rec, recordYangPush(ANY(uint32_t), R"({
  "ietf-yang-push:push-update": {
    "datastore-contents": {
      "test_module:leafInt32": 123,
      "test_module:values": [
        2,
        5,
        7
      ]
    }
  }
}
)"))
            .IN_SEQUENCE(seq)
            .TIMES(AT_LEAST(1))
            .LR_WITH(subId == _1);

        TROMPELOEIL_REQUIRE_CALL(rec, recordYangPush(ANY(uint32_t), R"({
  "ietf-subscribed-notifications:subscription-terminated": {
    "reason": "no-such-subscription"
  }
}
)"))
            .IN_SEQUENCE(seq)
            .LR_WITH(_1 = subId);

        auto sub = sess.yangPushPeriodic(std::nullopt, std::chrono::milliseconds{100}, std::nullopt, std::chrono::system_clock::now() + 1333ms);

        std::jthread srDataEditor([&] {
            std::this_thread::sleep_for(150ms);

            client.setItem("/test_module:leafInt32", "123");
            client.setItem("/test_module:values[.='5']", std::nullopt);
            client.deleteItem("/test_module:values[.='3']");
            client.applyChanges();
            std::this_thread::sleep_for(350ms);

            client.setItem("/test_module:values[.='6']", std::nullopt);
            client.applyChanges();
            std::this_thread::sleep_for(150ms);

            client.setItem("/test_module:values[.='7']", std::nullopt);
            client.deleteItem("/test_module:values[.='6']");
            client.applyChanges();
        });

        struct pollfd pfds[1];
        memset(pfds, 0, sizeof(pfds));
        pfds[0].fd = sub.fd();
        pfds[0].events = POLLIN | POLLHUP;

        while (true) {
            auto status = pipeStatus(sub.fd());
            if (status == PipeStatus::Hangup) {
                break;
            } else if (status == PipeStatus::DataReady) {
                sub.processEvent(cb);
            } else {
                FAIL("Unexpected pipe status");
            }
        }

        REQUIRE(subId == sub.subscriptionId());
        waitForCompletionAndBitMore(seq);
    }
}
