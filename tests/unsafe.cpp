/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/

#include <doctest/doctest.h>
#include <optional>
#include <sysrepo-cpp/Connection.hpp>
#include <sysrepo-cpp/utils/utils.hpp>
#include <sysrepo.h>
#include <trompeloeil.hpp>

class Recorder {
public:
    TROMPELOEIL_MAKE_CONST_MOCK5(record, void(sysrepo::ChangeOperation, std::string, std::optional<std::string_view>, std::optional<std::string_view>, bool));
};

TEST_CASE("unsafe")
{
    DOCTEST_SUBCASE("wrapUnmanagedSession")
    {
        sr_conn_ctx_t* conn;
        sr_connect(0, &conn);
        sr_session_ctx_t* sess;
        sr_session_start(conn, SR_DS_RUNNING, &sess);
        // This management is just for the sake of this test, the usecase for wrapUnmanagedSession is that you get SOME
        // session and you want to use C++ operations on it.
        auto conn_guard = std::unique_ptr<sr_conn_ctx_t, decltype([](auto conn) constexpr { sr_disconnect(conn); })>(conn);
        auto sess_guard = std::unique_ptr<sr_session_ctx_t, decltype([](auto sess) constexpr { sr_session_stop(sess); })>(sess);

        auto wrapped = sysrepo::wrapUnmanagedSession(sess);

        DOCTEST_SUBCASE("You can create subscriptions")
        {
            Recorder rec;

            sysrepo::ModuleChangeCb moduleChangeCb = [&rec] (auto session, auto, auto, auto, auto, auto) -> sysrepo::ErrorCode {
                for (const auto& change : session.getChanges("//.")) {
                    rec.record(change.operation, std::string{change.node.path()}, change.previousList, change.previousValue, change.previousDefault);
                }
                return sysrepo::ErrorCode::Ok;
            };

            auto sub = wrapped.onModuleChange("test_module", moduleChangeCb, std::nullopt, 0, sysrepo::SubscribeOptions::DoneOnly);

            TROMPELOEIL_REQUIRE_CALL(rec, record(sysrepo::ChangeOperation::Created, "/test_module:leafInt32", std::nullopt, std::nullopt, false));
            wrapped.setItem("/test_module:leafInt32", "123");
            wrapped.applyChanges();
        }
    }
}
