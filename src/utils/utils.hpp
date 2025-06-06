/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/
#pragma once
#include <chrono>
#include <sysrepo-cpp/Session.hpp>

namespace sysrepo {
std::timespec toTimespec(std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>);
std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds> toTimePoint(std::timespec ts);
void checkNoThreadFlag(const SubscribeOptions opts, const std::optional<FDHandling>& callbacks);
}
