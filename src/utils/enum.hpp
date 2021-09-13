/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/

#include <type_traits>
extern "C" {
#include <sysrepo.h>
}
#include <sysrepo-cpp/Enum.hpp>

namespace sysrepo {
static_assert(std::is_same_v<std::underlying_type_t<sr_datastore_t>, std::underlying_type_t<Datastore>>);

constexpr sr_datastore_t toDatastore(const Datastore options)
{
    return static_cast<sr_datastore_t>(options);
}

static_assert(toDatastore(Datastore::Running) == SR_DS_RUNNING);
static_assert(toDatastore(Datastore::Candidate) == SR_DS_CANDIDATE);
static_assert(toDatastore(Datastore::Operational) == SR_DS_OPERATIONAL);
static_assert(toDatastore(Datastore::Startup) == SR_DS_STARTUP);

constexpr Event toEvent(const sr_event_t event)
{
    return static_cast<Event>(event);
}

static_assert(std::is_same_v<std::underlying_type_t<sr_event_t>, std::underlying_type_t<Event>>);
static_assert(toEvent(SR_EV_UPDATE) == Event::Update);
static_assert(toEvent(SR_EV_CHANGE) == Event::Change);
static_assert(toEvent(SR_EV_DONE) == Event::Done);
static_assert(toEvent(SR_EV_ABORT) == Event::Abort);
static_assert(toEvent(SR_EV_ENABLED) == Event::Enabled);
static_assert(toEvent(SR_EV_RPC) == Event::RPC);

constexpr uint32_t toSubOptions(const SubOptions opts)
{
    return static_cast<uint32_t>(opts);
}

template <typename Enum>
constexpr Enum implEnumBitOr(const Enum a, const Enum b)
{
    using Type = std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<Type>(a) | static_cast<Type>(b));
}

constexpr SubOptions operator|(const SubOptions a, const SubOptions b)
{
    return implEnumBitOr(a, b);
}

static_assert(toSubOptions(SubOptions::Default) == SR_SUBSCR_DEFAULT);
static_assert(toSubOptions(SubOptions::NoThread) == SR_SUBSCR_NO_THREAD);
static_assert(toSubOptions(SubOptions::Passive) == SR_SUBSCR_PASSIVE);
static_assert(toSubOptions(SubOptions::DoneOnly) == SR_SUBSCR_DONE_ONLY);
static_assert(toSubOptions(SubOptions::Enabled) == SR_SUBSCR_ENABLED);
static_assert(toSubOptions(SubOptions::Update) == SR_SUBSCR_UPDATE);
static_assert(toSubOptions(SubOptions::OperMerge) == SR_SUBSCR_OPER_MERGE);
static_assert(toSubOptions(SubOptions::ThreadSuspend) == SR_SUBSCR_THREAD_SUSPEND);

constexpr ChangeOperation toChangeOper(const sr_change_oper_t oper)
{
    return static_cast<ChangeOperation>(oper);
}

static_assert(toChangeOper(SR_OP_CREATED) == ChangeOperation::Created);
static_assert(toChangeOper(SR_OP_MODIFIED) == ChangeOperation::Modified);
static_assert(toChangeOper(SR_OP_DELETED) == ChangeOperation::Deleted);
static_assert(toChangeOper(SR_OP_MOVED) == ChangeOperation::Moved);
}
