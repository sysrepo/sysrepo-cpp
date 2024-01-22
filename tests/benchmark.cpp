/*
 * Copyright (C) 2024 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/

#include <doctest/doctest.h>
#include <iostream>
#include <latch>
#include <sysrepo-cpp/Connection.hpp>
#include <sysrepo-cpp/utils/utils.hpp>
#include <thread>
#include <trompeloeil.hpp>

TEST_CASE("benchmark")
{
    sysrepo::setLogLevelStderr(sysrepo::LogLevel::Warning);
    auto sess = sysrepo::Connection{}.sessionStart();
    sess.sendRPC(sess.getContext().newPath("/ietf-factory-default:factory-reset"));

    DOCTEST_SUBCASE("RPCs")
    {
        constexpr auto NUM_RPC = 10'000;
        const auto rpc = sess.getContext().newPath("/test_module:noop");

        std::latch started{1}, terminate{1};
        std::jthread server([&started, &terminate]() {
            auto sub = sysrepo::Connection{}.sessionStart().onRPCAction(
                "/test_module:noop",
                [&](auto, auto, auto, auto, auto, auto, auto) {
                    return sysrepo::ErrorCode::Ok;
                });
            started.count_down();
            const auto start = std::chrono::system_clock::now();
            terminate.wait();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count();
            std::cerr << "Receiving " << NUM_RPC << " RPCs: " << ms << "ms\n";
        });
        started.wait();

        const auto start = std::chrono::system_clock::now();
        for (int i = 0; i < NUM_RPC; ++i) {
            sess.sendRPC(rpc);
        }
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count();
        std::cerr << "Sending " << NUM_RPC << " RPCs: " << ms << "ms\n";

        terminate.count_down();
    }

    DOCTEST_SUBCASE("notifications") {
        constexpr auto NUM_NOTIF = 10'000;

        std::latch done{1};

        auto sub = sess.onNotification(
            "test_module",
            [](auto, auto, const auto, const auto, const auto) {},
            "/test_module:ping");

        const auto start = std::chrono::system_clock::now();
        std::jthread server([&done]() {
            auto sess = sysrepo::Connection{}.sessionStart();
            auto ctx = sess.getContext();
            const auto start = std::chrono::system_clock::now();
            for (int i = 0; i < NUM_NOTIF; ++i) {
                auto notification = ctx.newPath("/test_module:ping");
                notification.newPath("myLeaf", std::to_string(i));
                sess.sendNotification(notification, sysrepo::Wait::No);
            }
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count();
            std::cerr << "Sending " << NUM_NOTIF << " notifications: " << ms << "ms\n";
            done.count_down();
        });
        done.wait();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count();
        std::cerr << "Receiving " << NUM_NOTIF << " notifications: " << ms << "ms\n";
    }
}
