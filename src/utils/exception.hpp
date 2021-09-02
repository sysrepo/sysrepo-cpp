/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/
#pragma once

#include <sysrepo-cpp/utils/exception.hpp>

namespace sysrepo {
    void throwIfError(int code, std::string msg);
}
