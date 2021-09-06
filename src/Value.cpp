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
#include <sysrepo-cpp/Value.hpp>

namespace sysrepo {
Value::Value(sr_val_s* val)
    : m_val(val, sr_free_val)
{
}

std::string_view Value::xpath() const
{
    return m_val->xpath;
}
}
