/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 * Written by Tomas Pecka <tomas.pecka@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/

#include <chrono>
#include <doctest/doctest.h>
#include <thread>
#include <trompeloeil.hpp>

/** @short Wait until a given sequence of expectation is matched, and then a bit more to ensure that there's silence afterwards */
void waitForCompletionAndBitMore(const trompeloeil::sequence& seq)
{
    using namespace std::literals;
    using clock = std::chrono::steady_clock;

    // We're busy-waiting a bit
    const auto waitingStep = 30ms;
    // Timeout after this much
    const auto completionTimeout = 5000ms;
    // When checking for silence afterwards, wait at least this long.
    // We'll also wait as long as it originally took to process everything.
    const auto minExtraWait = 100ms;

    auto start = clock::now();
    while (true) {
        {
            auto lock = trompeloeil::get_lock();
            if (seq.is_completed()) {
                break;
            }
        }
        std::this_thread::sleep_for(waitingStep);
        if (clock::now() - start > completionTimeout) {
            break;
        }
    }
    {
        auto lock = trompeloeil::get_lock();
        REQUIRE(seq.is_completed());
    }
    auto duration = std::chrono::duration<double>(clock::now() - start);
    std::this_thread::sleep_for(std::max(duration, decltype(duration)(minExtraWait)));
}
