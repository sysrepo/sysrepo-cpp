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
    TROMPELOEIL_MAKE_CONST_MOCK2(recordYangPush, void(uint32_t, std::optional<std::string>));
};

bool pipeHasData(int fd)
{
    int r;
    pollfd fds = {.fd = fd, .events = POLLIN | POLLHUP, .revents = 0};

    r = poll(&fds, 1, 222);
    if (r == -1) {
        throw std::runtime_error("poll failed");
    } else if (!r) {
        throw std::runtime_error("poll timed out");
    } else if (fds.revents & POLLIN) {
        return true;
    } else if (fds.revents & POLLHUP) {
        return false;
    } else if (fds.revents & POLLERR) {
        throw std::runtime_error("poll returned POLLERR");
    }

    return false;
}

TEST_CASE("Yang push subscriptions")
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

        // the notification contains subscription id. This is a hack that enables us to compare string representations
        // the subscription is compared using another parameter in the expectation
        auto idNode = tree->findPath("id");
        const auto subId = std::get<uint32_t>(idNode->asTerm().value());
        idNode->unlink();

        rec.recordYangPush(subId, tree->printStr(libyang::DataFormat::JSON, libyang::PrintFlags::WithSiblings));
    };

    client.setItem("/test_module:values[.='2']", std::nullopt);
    client.setItem("/test_module:values[.='3']", std::nullopt);
    client.applyChanges();

    DOCTEST_SUBCASE("YANG push on change")
    {
        auto sub = std::make_unique<sysrepo::YangPushSubscription>(sess.yangPushOnChange(std::nullopt, std::nullopt, sysrepo::SyncOnStart::Yes));

        TROMPELOEIL_REQUIRE_CALL(rec, recordYangPush(sub->subscriptionId(), R"({
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
            .IN_SEQUENCE(seq);
        REQUIRE(pipeHasData(sub->fd()));
        sub->processEvent(cb);

        TROMPELOEIL_REQUIRE_CALL(rec, recordYangPush(sub->subscriptionId(), R"({
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
)"))
            .IN_SEQUENCE(seq);
        client.setItem("/test_module:leafInt32", "123");
        client.setItem("/test_module:values[.='5']", std::nullopt);
        client.deleteItem("/test_module:values[.='3']");
        client.applyChanges();
        REQUIRE(pipeHasData(sub->fd()));
        sub->processEvent(cb);

        DOCTEST_SUBCASE("Terminate manually without a reason")
        {
            sub->terminate();
            REQUIRE(!pipeHasData(sub->fd()));

            // events can still happen and the FD is not yet closed, so we can still read from it but no new events are expected
            client.setItem("/test_module:values[.='6']", std::nullopt);
            client.applyChanges();
            REQUIRE(!pipeHasData(sub->fd()));
        }

        DOCTEST_SUBCASE("Terminate manually with a reason")
        {
            TROMPELOEIL_REQUIRE_CALL(rec, recordYangPush(sub->subscriptionId(), R"({
  "ietf-subscribed-notifications:subscription-terminated": {
    "reason": "no-such-subscription"
  }
}
)"))
                .IN_SEQUENCE(seq);
            sub->terminate("ietf-subscribed-notifications:no-such-subscription");
            REQUIRE(pipeHasData(sub->fd()));
            sub->processEvent(cb);
            REQUIRE(!pipeHasData(sub->fd()));
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
            int r = poll(pfds, 1, 200); // we are getting data in 100ms interval, so 200ms timeout should be enough
            if (r == -1) {
                REQUIRE(false); // error
            } else if (!r) {
                REQUIRE(false); // timeout
            } else if (pfds[0].revents & POLLIN) {
                REQUIRE(pipeHasData(sub.fd()));
                sub.processEvent(cb);
            } else if (pfds[0].revents & POLLHUP) {
                break;
            } else {
                REQUIRE(false); // error
            }
        }

        REQUIRE(subId == sub.subscriptionId());
        waitForCompletionAndBitMore(seq);
    }
}
