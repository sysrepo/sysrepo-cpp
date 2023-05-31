/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/
#pragma once

#include <vector>
#include <sysrepo-cpp/utils/exception.hpp>

struct sr_session_ctx_s;

namespace sysrepo {
    void throwIfError(int code, const std::string& msg, sr_session_ctx_s *c_session = nullptr);

    template <typename ErrType>
    std::vector<ErrType> impl_getErrors(sr_session_ctx_s* sess);
}
