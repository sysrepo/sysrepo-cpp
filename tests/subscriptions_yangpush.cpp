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
#include <trompeloeil.hpp>
#include "sysrepo-cpp/Subscription.hpp"
#include "utils.hpp"

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
    TROMPELOEIL_MAKE_CONST_MOCK1(recordYangPush, void(std::optional<std::string>));
};

bool pipeHasData(int fd)
{
    int r;
    pollfd fds = {.fd = fd, .events = POLLIN, .revents = 0};

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

    auto cb = [&](std::optional<libyang::DataNode> tree, auto) {
        REQUIRE(tree);

        // the notification contains subscription id. This is a hack that enables us to compare string representations
        tree->findPath("id")->unlink();
        rec.recordYangPush(tree->printStr(libyang::DataFormat::JSON, libyang::PrintFlags::WithSiblings));
    };

    DOCTEST_SUBCASE("push on change")
    {
        client.setItem("/test_module:values[.='2']", std::nullopt);
        client.setItem("/test_module:values[.='3']", std::nullopt);
        client.applyChanges();

        TROMPELOEIL_REQUIRE_CALL(rec, recordYangPush(R"({
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

        auto sub = std::make_unique<sysrepo::YangPushSubscription>(sess.yangPushOnChange(sysrepo::Datastore::Running, std::nullopt, true));
        REQUIRE(sub->fd() > 0);
        REQUIRE(pipeHasData(sub->fd()));
        sub->processEvents(cb);

        TROMPELOEIL_REQUIRE_CALL(rec, recordYangPush(R"({
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
        sub->processEvents(cb);

        waitForCompletionAndBitMore(seq);

        DOCTEST_SUBCASE("Terminate right away")
        {
            sub->terminate();
            REQUIRE(!pipeHasData(sub->fd()));
        }

        DOCTEST_SUBCASE("Terminate with a reason")
        {
            TROMPELOEIL_REQUIRE_CALL(rec, recordYangPush(R"({
  "ietf-subscribed-notifications:subscription-terminated": {
    "reason": "no-such-subscription"
  }
}
)"))
                .IN_SEQUENCE(seq);
            sub->terminate("ietf-subscribed-notifications:no-such-subscription");
            REQUIRE(pipeHasData(sub->fd()));
            sub->processEvents(cb);
            REQUIRE(!pipeHasData(sub->fd()));
        }

        DOCTEST_SUBCASE("Terminate by dtor")
        {
            sub.reset();
        }
    }
}
