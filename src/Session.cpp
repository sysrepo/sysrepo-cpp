/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/

extern "C" {
#include <sysrepo.h>
}
#include <sysrepo-cpp/Session.hpp>
#include "utils/exception.hpp"

using namespace std::string_literals;
namespace sysrepo {

Value::Value(sr_val_s* val)
    : m_val(val, sr_free_val)
{
}

Session::Session(sr_session_ctx_s* sess, std::shared_ptr<sr_conn_ctx_s> conn)
    : m_conn(conn)
    , m_sess(sess, sr_session_stop)
{
}

void Session::setItem(const char* path, const char* value)
{
    auto res = sr_set_item_str(m_sess.get(), path, value, nullptr, 0);

    throwIfError(res, "Session::setItem: Couldn't set '"s + path + "' to '" + value);
}

Value Session::getItem(const char* path, uint32_t timeoutMs)
{
    sr_val_t* val;
    auto res = sr_get_item(m_sess.get(), path, timeoutMs, &val);

    throwIfError(res, "Session::getItem: Couldn't get '"s + path + "'");

    return Value{val};
}
}
