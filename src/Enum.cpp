/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/
#include <sysrepo-cpp/Enum.hpp>

namespace sysrepo {
std::ostream& operator<<(std::ostream& os, const NotificationType& type)
{
    switch (type) {
    case sysrepo::NotificationType::Realtime:
        return os << "sysrepo::NotificationType::Realtime";
    case sysrepo::NotificationType::Replay:
        return os << "sysrepo::NotificationType::Replay";
    case sysrepo::NotificationType::ReplayComplete:
        return os << "sysrepo::NotificationType::ReplayComplete";
    case sysrepo::NotificationType::Terminated:
        return os << "sysrepo::NotificationType::Terminated";
    case sysrepo::NotificationType::Modified:
        return os << "sysrepo::NotificationType::Modified";
    case sysrepo::NotificationType::Suspended:
        return os << "sysrepo::NotificationType::Suspended";
    case sysrepo::NotificationType::Resumed:
        return os << "sysrepo::NotificationType::Resumed";
    default:
        return os << "[unknown event type]";
    }
}
}
