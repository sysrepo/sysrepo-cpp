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

#define CLIENT_SEND_NOTIFICATION(NOTIFICATION)                                                                                       \
    {                                                                                                                                \
        auto notif = client.getContext().parseOp(NOTIFICATION, libyang::DataFormat::JSON, libyang::OperationType::NotificationYang); \
        client.sendNotification(*notif.tree, sysrepo::Wait::Yes);                                                                    \
    }

#define REQUIRE_PIPE_HANGUP(SUBSCRIPTION) \
    REQUIRE(pipeStatus((SUBSCRIPTION).fd()) == PipeStatus::Hangup)

#define REQUIRE_NOTIFICATION(SUBSCRIPTION, NOTIFICATION) \
    TROMPELOEIL_REQUIRE_CALL(rec, recordNotification(NOTIFICATION)).IN_SEQUENCE(seq);

#define REQUIRE_NAMED_NOTIFICATION(SUBSCRIPTION, NOTIFICATION) \
    expectations.emplace_back(TROMPELOEIL_NAMED_REQUIRE_CALL(rec, recordNotification(NOTIFICATION)).IN_SEQUENCE(seq));

#define READ_NOTIFICATION(SUBSCRIPTION)                                \
    REQUIRE(pipeStatus((SUBSCRIPTION).fd()) == PipeStatus::DataReady); \
    (SUBSCRIPTION).processEvent(cbNotif);

#define READ_NOTIFICATION_BLOCKING(SUBSCRIPTION)                           \
    REQUIRE(pipeStatus((SUBSCRIPTION).fd(), -1) == PipeStatus::DataReady); \
    (SUBSCRIPTION).processEvent(cbNotif);

auto SUBSCRIPTION_TERMINATED(const auto& SUBSCRIPTION)
{
    return R"({
  "ietf-subscribed-notifications:subscription-terminated": {
    "id": )"
        + std::to_string((SUBSCRIPTION).subscriptionId()) + R"(,
    "reason": "no-such-subscription"
  }
}
)";
};

auto REPLAY_COMPLETED(const auto& SUBSCRIPTION)
{
    return R"({
  "ietf-subscribed-notifications:replay-completed": {
    "id": )"
        + std::to_string((SUBSCRIPTION).subscriptionId()) + R"(
  }
}
)";
};

#define REQUIRE_YANG_PUSH_UPDATE(SUBSCRIPTION, NOTIFICATION) \
    TROMPELOEIL_REQUIRE_CALL(rec, recordYangPushUpdate((SUBSCRIPTION).subscriptionId(), NOTIFICATION)).IN_SEQUENCE(seq);

#define READ_YANG_PUSH_UPDATE(SUBSCRIPTION)                            \
    REQUIRE(pipeStatus((SUBSCRIPTION).fd()) == PipeStatus::DataReady); \
    (SUBSCRIPTION).processEvent(cbYangPushUpdate);


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
    TROMPELOEIL_MAKE_CONST_MOCK2(recordYangPushUpdate, void(uint32_t, std::optional<std::string>));
};

enum class PipeStatus {
    NoData,
    DataReady,
    Hangup,
    Other, // represents any other poll() result, we are not interested in details
};

/** Check the status of the pipe for reading.
 *
 * @param timeout Timeout in milliseconds, 0 means check the status right now, -1 means blocking wait for an event, see poll(2)
 */
PipeStatus pipeStatus(int fd, int timeout = 0)
{
    pollfd fds = {.fd = fd, .events = POLLIN | POLLHUP, .revents = 0};

    int r = poll(&fds, 1, timeout);
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

    auto cbYangPushUpdate = [&](const std::optional<libyang::DataNode>& tree, auto) {
        REQUIRE(tree);

        // the yang-push notification contains subscription id. This is a hack that enables us to compare string representations
        // the subscription is compared using another parameter in the expectation
        auto idNode = tree->findPath("id");
        const auto subId = std::get<uint32_t>(idNode->asTerm().value());
        idNode->unlink();

        rec.recordYangPushUpdate(subId, tree->printStr(libyang::DataFormat::JSON, libyang::PrintFlags::WithSiblings));
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
        /* notification JSONs are very verbose, let's give them names even if they are not very descriptive
         * but still better than polluting the tests below with repeated long R-strings */
        const std::vector<std::string> notifications = {
            R"({
  "test_module:ping": {
    "myLeaf": 1
  }
}
)",
            R"({
  "test_module:silent-ping": {}
}
)",
            R"({
  "test_module:ping": {
    "myLeaf": 2
  }
}
)"};

        DOCTEST_SUBCASE("Subscribe to everything from test_module")
        {
            auto sub = sess.subscribeNotifications("/test_module:*", std::nullopt);
            REQUIRE(!sub.replayStartTime());

            CLIENT_SEND_NOTIFICATION(notifications[0]);
            CLIENT_SEND_NOTIFICATION(notifications[1]);

            REQUIRE_NOTIFICATION(sub, notifications[0]);
            READ_NOTIFICATION(sub);

            CLIENT_SEND_NOTIFICATION(notifications[2]);

            REQUIRE_NOTIFICATION(sub, notifications[1]);
            READ_NOTIFICATION(sub);

            REQUIRE_NOTIFICATION(sub, notifications[2]);
            READ_NOTIFICATION(sub);

            sub.terminate();

            // Notification was sent after the subscription was terminated, so it should not be received
            CLIENT_SEND_NOTIFICATION(notifications[0]);

            REQUIRE_PIPE_HANGUP(sub);
        }

        DOCTEST_SUBCASE("Subscribe to the notification about stop-time reached")
        {
            auto stopTime = std::chrono::system_clock::now() + 500ms /* let's hope 500 ms should be enough to create subscription and notification */;
            auto sub = sess.subscribeNotifications("/ietf-subscribed-notifications:subscription-terminated", std::nullopt, stopTime);
            REQUIRE(!sub.replayStartTime());

            // this notification is not subscribed, sysrepo should filter it
            CLIENT_SEND_NOTIFICATION(notifications[0]);

            // wait until stop time and bit more
            std::this_thread::sleep_until(stopTime + 500ms);

            REQUIRE_NOTIFICATION(sub, SUBSCRIPTION_TERMINATED(sub));
            READ_NOTIFICATION(sub);

            REQUIRE_PIPE_HANGUP(sub);
        }

        DOCTEST_SUBCASE("Replays revise replay start time value")
        {
            conn.setModuleReplaySupport("test_module", true);

            // one notification for replay
            auto before = std::chrono::system_clock::now();
            CLIENT_SEND_NOTIFICATION(notifications[0]);
            auto after = std::chrono::system_clock::now();

            auto sub = sess.subscribeNotifications("/test_module:*", std::nullopt, std::nullopt, std::chrono::system_clock::now() - 666s /* replay everything that happened at most 666s ago */);

            // replay start time is revised according to the first notification
            REQUIRE(sub.replayStartTime());
            REQUIRE(sub.replayStartTime() >= before);
            REQUIRE(sub.replayStartTime() <= after);

            // wait for the replayed notification and replay-completed notification
            REQUIRE_NOTIFICATION(sub, notifications[0]);
            READ_NOTIFICATION_BLOCKING(sub);
            REQUIRE_NOTIFICATION(sub, REPLAY_COMPLETED(sub));
            READ_NOTIFICATION_BLOCKING(sub);

            sub.terminate();

            REQUIRE_PIPE_HANGUP(sub);
        }

        DOCTEST_SUBCASE("Terminate manually with a reason")
        {
            auto sub = sess.subscribeNotifications("/test_module:*", std::nullopt);

            REQUIRE_NOTIFICATION(sub, R"({
  "ietf-subscribed-notifications:subscription-terminated": {
    "id": )" + std::to_string(sub.subscriptionId())
                                     + R"(,
    "reason": "filter-unavailable"
  }
}
)");
            sub.terminate("ietf-subscribed-notifications:filter-unavailable");
            READ_NOTIFICATION(sub);

            REQUIRE_PIPE_HANGUP(sub);
        }

        DOCTEST_SUBCASE("Terminate by destruction")
        {
            auto sub = std::make_unique<sysrepo::DynamicSubscription>(sess.subscribeNotifications("/test_module:*", std::nullopt));
            sub.reset();

            // new events can happen but sysrepo is not supposed to send them to a terminated subscription
            // also the FD is closed so no point in reading from it
            client.setItem("/test_module:values[.='6']", std::nullopt);
            client.applyChanges();
        }

        DOCTEST_SUBCASE("Terminate called twice")
        {
            auto sub = std::make_unique<sysrepo::DynamicSubscription>(sess.subscribeNotifications("/test_module:*", std::nullopt));
            sub->terminate();

            // if the message is inlined into the macro, doctest<=2.4.8 fails with "REQUIRE_THROWS_WITH_AS( sub->terminate(), "<garbage>", sysrepo::ErrorWithCode )",
            // it does work after 8de4cf7e759c55a89761c25900d80e01ae7ac3fd
            // TODO: Can be simplified after we bump minimal doctest version to doctest>=2.4.9
            const auto excMessage = "Couldn't terminate yang-push subscription with id " + std::to_string(sub->subscriptionId()) + ": SR_ERR_NOT_FOUND";
            REQUIRE_THROWS_WITH_AS(sub->terminate(), excMessage.c_str(), sysrepo::ErrorWithCode);
        }

        DOCTEST_SUBCASE("Filtering")
        {
            std::optional<sysrepo::DynamicSubscription> sub;
            std::vector<std::unique_ptr<trompeloeil::expectation>> expectations;

            DOCTEST_SUBCASE("xpath filter")
            {
                sub = sess.subscribeNotifications("/test_module:ping");

                REQUIRE_NAMED_NOTIFICATION(sub, notifications[0]);
            }

            DOCTEST_SUBCASE("subtree filter")
            {
                libyang::CreatedNodes createdNodes;

                DOCTEST_SUBCASE("filter a node")
                {
                    DOCTEST_SUBCASE("XML")
                    {
                        createdNodes = sess.getContext().newPath2(
                            "/ietf-subscribed-notifications:establish-subscription/stream-subtree-filter",
                            libyang::XML{"<ping xmlns='urn:ietf:params:xml:ns:yang:test_module' />"});
                    }

                    DOCTEST_SUBCASE("JSON")
                    {
                        createdNodes = sess.getContext().newPath2(
                            "/ietf-subscribed-notifications:establish-subscription/stream-subtree-filter",
                            libyang::JSON{R"({"test_module:ping": {}})"});
                    }

                    REQUIRE_NAMED_NOTIFICATION(sub, notifications[0]);
                }

                DOCTEST_SUBCASE("filter more top level nodes")
                {
                    DOCTEST_SUBCASE("XML")
                    {
                        createdNodes = sess.getContext().newPath2(
                            "/ietf-subscribed-notifications:establish-subscription/stream-subtree-filter",
                            libyang::XML{"<ping xmlns='urn:ietf:params:xml:ns:yang:test_module' />"
                                         "<silent-ping xmlns='urn:ietf:params:xml:ns:yang:test_module' />"});
                    }

                    DOCTEST_SUBCASE("JSON")
                    {
                        createdNodes = sess.getContext().newPath2(
                            "/ietf-subscribed-notifications:establish-subscription/stream-subtree-filter",
                            libyang::JSON{R"({
                                "test_module:ping": {},
                                "test_module:silent-ping": {}
                            })"});
                    }

                    REQUIRE_NAMED_NOTIFICATION(sub, notifications[0]);
                    REQUIRE_NAMED_NOTIFICATION(sub, notifications[1]);
                }

                DOCTEST_SUBCASE("empty filter selects nothing")
                {
                    createdNodes = sess.getContext().newPath2(
                        "/ietf-subscribed-notifications:establish-subscription/stream-subtree-filter",
                        std::nullopt);
                }

                sub = sess.subscribeNotifications(createdNodes.createdNode->asAny());
            }

            CLIENT_SEND_NOTIFICATION(notifications[0]);
            CLIENT_SEND_NOTIFICATION(notifications[1]);

            // read as many notifications as we expect
            for (size_t i = 0; i < expectations.size(); ++i) {
                READ_NOTIFICATION_BLOCKING(*sub);
            }

            sub->terminate();

            // ensure no more notifications were sent
            REQUIRE_PIPE_HANGUP(*sub);
        }
    }

    DOCTEST_SUBCASE("YANG Push on change")
    {
        /* YANG Push on change sends the whole data tree every time the data tree changes.
         * We read the notifications from the associated fd manually, so there should be no races
         * between writing to sysrepo and reading the notifications.
         */

        DOCTEST_SUBCASE("Filters")
        {
            std::optional<sysrepo::DynamicSubscription> sub;

            DOCTEST_SUBCASE("XPath filter")
            {
                sub = sess.yangPushOnChange("/test_module:leafInt32 | /test_module:popelnice/content/trash[name='asd']");
            }

            DOCTEST_SUBCASE("Subtree filter")
            {
                auto createdNodes = sess.getContext().newPath2(
                    "/ietf-subscribed-notifications:establish-subscription/ietf-yang-push:datastore-subtree-filter",
                    libyang::XML{"<leafInt32 xmlns='http://example.com/' />"
                                 "<popelnice xmlns='http://example.com/'><content><trash><name>asd</name></trash></content></popelnice>"});
                sub = sess.yangPushOnChange(createdNodes.createdNode->asAny());
            }

            client.setItem("/test_module:leafInt32", "42");
            client.setItem("/test_module:popelnice/s", "asd");
            client.setItem("/test_module:popelnice/content/trash[name='asd']", std::nullopt);
            client.applyChanges();

            client.deleteItem("/test_module:popelnice/s");
            client.applyChanges();

            REQUIRE_YANG_PUSH_UPDATE(*sub, R"({
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
              "test_module:leafInt32": 42
            }
          },
          {
            "edit-id": "edit-2",
            "operation": "create",
            "target": "/test_module:popelnice/content/trash[name='asd']",
            "value": {
              "test_module:trash": {
                "name": "asd"
              }
            }
          }
        ]
      }
    }
  }
}
)");
            READ_YANG_PUSH_UPDATE(*sub);

            sub->terminate();
            REQUIRE_PIPE_HANGUP(*sub);
        }

        DOCTEST_SUBCASE("Sync on start")
        {
            auto sub = sess.yangPushOnChange(std::nullopt, std::nullopt, sysrepo::SyncOnStart::Yes);

            REQUIRE_YANG_PUSH_UPDATE(sub, R"({
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
            READ_YANG_PUSH_UPDATE(sub);

            client.setItem("/test_module:leafInt32", "123");
            client.setItem("/test_module:values[.='5']", std::nullopt);
            client.deleteItem("/test_module:values[.='3']");
            client.applyChanges();

            REQUIRE_YANG_PUSH_UPDATE(sub, R"({
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
            READ_YANG_PUSH_UPDATE(sub);

            sub.terminate();
            REQUIRE_PIPE_HANGUP(sub);
        }

        DOCTEST_SUBCASE("Excluded changes")
        {
            auto sub = sess.yangPushOnChange(std::nullopt, std::nullopt, sysrepo::SyncOnStart::No, {sysrepo::YangPushChange::Create});

            client.setItem("/test_module:leafInt32", "123");
            client.applyChanges(); // excluded (create)
            client.setItem("/test_module:leafInt32", "124");
            client.applyChanges();
            client.setItem("/test_module:leafInt32", "125");
            client.applyChanges();

            REQUIRE_YANG_PUSH_UPDATE(sub, R"({
  "ietf-yang-push:push-change-update": {
    "datastore-changes": {
      "yang-patch": {
        "patch-id": "patch-1",
        "edit": [
          {
            "edit-id": "edit-1",
            "operation": "replace",
            "target": "/test_module:leafInt32",
            "value": {
              "test_module:leafInt32": 124
            }
          }
        ]
      }
    }
  }
}
)");

            READ_YANG_PUSH_UPDATE(sub);

            REQUIRE_YANG_PUSH_UPDATE(sub, R"({
  "ietf-yang-push:push-change-update": {
    "datastore-changes": {
      "yang-patch": {
        "patch-id": "patch-2",
        "edit": [
          {
            "edit-id": "edit-1",
            "operation": "replace",
            "target": "/test_module:leafInt32",
            "value": {
              "test_module:leafInt32": 125
            }
          }
        ]
      }
    }
  }
}
)");
            READ_YANG_PUSH_UPDATE(sub);

            sub.terminate();
            REQUIRE_PIPE_HANGUP(sub);
        }
    }

    DOCTEST_SUBCASE("YANG Push periodic")
    {
        /*
         * Periodic yang push sends the whole data tree every interval, we can't read manually, so
         * we create another thread that modifies the data tree and this thread will read the notifications.
         *
         * Also, we don't know the subscription id in advance, so we have to read it from the first notification
         * and use it in the following notifications. That requires some trompeloeil magic with LR_SIDE_EFFECT and LR_WITH.
         */

        std::invoke_result_t<decltype(&sysrepo::DynamicSubscription::subscriptionId), sysrepo::DynamicSubscription> subId;

        TROMPELOEIL_REQUIRE_CALL(rec, recordYangPushUpdate(ANY(uint32_t), R"({
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

        TROMPELOEIL_REQUIRE_CALL(rec, recordYangPushUpdate(ANY(uint32_t), R"({
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

        TROMPELOEIL_REQUIRE_CALL(rec, recordYangPushUpdate(ANY(uint32_t), R"({
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

        TROMPELOEIL_REQUIRE_CALL(rec, recordYangPushUpdate(ANY(uint32_t), R"({
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

        TROMPELOEIL_REQUIRE_CALL(rec, recordYangPushUpdate(ANY(uint32_t), R"({
  "ietf-subscribed-notifications:subscription-terminated": {
    "reason": "no-such-subscription"
  }
}
)"))
            .IN_SEQUENCE(seq)
            .LR_WITH(_1 = subId);

        auto sub = sess.yangPushPeriodic(std::nullopt, std::chrono::milliseconds{66}, std::nullopt, std::chrono::system_clock::now() + 6666ms);

        std::jthread srDataEditor([&] {
            auto sess = conn.sessionStart();

            std::this_thread::sleep_for(500ms);

            sess.setItem("/test_module:leafInt32", "123");
            sess.setItem("/test_module:values[.='5']", std::nullopt);
            sess.deleteItem("/test_module:values[.='3']");
            sess.applyChanges();
            std::this_thread::sleep_for(500ms);

            sess.setItem("/test_module:values[.='6']", std::nullopt);
            sess.applyChanges();
            std::this_thread::sleep_for(500ms);

            sess.setItem("/test_module:values[.='7']", std::nullopt);
            sess.deleteItem("/test_module:values[.='6']");
            sess.applyChanges();
        });

        while (true) {
            auto status = pipeStatus(sub.fd(), -1 /* block */);
            if (status == PipeStatus::Hangup) {
                break;
            } else if (status == PipeStatus::DataReady) {
                sub.processEvent(cbYangPushUpdate);
            } else if (status == PipeStatus::Other) {
                FAIL("PipeStatus::Other before the subscription was terminated");
            } else {
                FAIL("PipeStatus::NoData but poll() should block until an event is available");
            }
        }

        REQUIRE(subId == sub.subscriptionId());
        REQUIRE_PIPE_HANGUP(sub);
    }
}
