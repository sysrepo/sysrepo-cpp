/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/

#include <doctest/doctest.h>
#include <sysrepo-cpp/Connection.hpp>

TEST_CASE("session")
{
    sysrepo::Connection conn;
    auto sess = conn.sessionStart();
    auto called = 0;

    sysrepo::ModuleChangeCb moduleChangeCb = [&called] (auto, auto, auto, auto, auto, auto) -> sysrepo::ErrorCode {
        called++;
        return sysrepo::ErrorCode::Ok;
    };

    auto sub = sess.subModuleChange("test_module", moduleChangeCb);
    sub.subModuleChange("test_module", moduleChangeCb);
    sess.setItem("/test_module:leafInt32", "123");
    sess.applyChanges();
    // Called four times twice for Event::Change and twice for Event::Done
    REQUIRE(called == 4);

}
