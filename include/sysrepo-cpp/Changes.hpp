/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/
#pragma once
#include <libyang-cpp/DataNode.hpp>
#include <memory>
#include <optional>
#include <sysrepo-cpp/Enum.hpp>
#include <string>

struct sr_session_ctx_s;
struct sr_change_iter_s;

namespace sysrepo {
class ChangeCollection;
class Session;

/**
 * @brief Contains info about a change in datastore.
 *
 * The user isn't supposed to instantiate this class, instead, Session::getChanges should be used to retrieve a change.
 */
struct Change {
    /**
     * @brief Type of the operation made on #node.
     */
    ChangeOperation operation;
    /**
     * @brief The affected node.
     */
    const libyang::DataNode node;
    /**
     * @brief Contains the previous value of a node or new preceding leaf-list instance.
     *
     * This depends on #operation and the node type of #node:
     *
     * If #operation is \link sysrepo::ChangeOperation::Created Created\endlink.
     *  - if #node is a user-ordered leaf-list, #previousValue contains the value of the preceding instance of the
     *    leaf-list. In case the created instance is first in the leaf-list, #previousValue contains an empty string.
     *  - otherwise it's `std::nullopt` (if the node is created it does not have a previous value)
     *
     * If #operation is \link sysrepo::ChangeOperation::Modified Modified\endlink.
     *  - #previousValue is the previous value of #node.
     *
     * If #operation is \link sysrepo::ChangeOperation::Deleted Deleted\endlink.
     *  - #previousValue is `std::nullopt` (value of deleted node can be retrieved from #node).
     *
     * If #operation is \link sysrepo::ChangeOperation::Moved Moved\endlink.
     *  - if #node is a user-ordered leaf-list, #previousValue is the value of the new preceding instance of #node.
     *    If #node became the first instance in the leaf-list, #previousValue contains an empty string.
     */
    std::optional<std::string> previousValue;
    /**
     * @brief Contains the list keys predicate for the new preceding list instance.
     *
     * This depends on #operation and the node type of #node.
     *
     * If #operation is \link sysrepo::ChangeOperation::Created Created\endlink or \link sysrepo::ChangeOperation::Moved Moved\endlink:
     *  - if #node is a user-ordered list, #previousList is list keys predicate of the new preceding instance of #node.
     *    If #node became the first instance in the list, #previousValue contains an empty string.
     *
     * Otherwise #previousList is `std::nullopt`.
     */
    std::optional<std::string> previousList;
    /**
     * @brief Signifies whether #previousValue was a default value.
     *
     * The value depends on #operation and the node type of #node.
     *
     * If #operation is \link sysrepo::ChangeOperation::Modified Modified\endlink and #node is a leaf:
     *  - #previousDefault is `true`, if #previousValue was the default for the leaf.
     *  - #previousDefault is `false`, if #previousValue was NOT the default for the leaf.
     *
     * Otherwise #previousDefault `false`.
     */
    bool previousDefault;
};

/**
 * @brief An iterator pointing to a single change associated with a ChangeCollection.
 */
class ChangeIterator {
public:
    ChangeIterator& operator++();
    ChangeIterator operator++(int);
    const Change& operator*() const;
    const Change& operator->() const;
    bool operator==(const ChangeIterator& other) const;

private:
    /**
     * A tag used for creating an `end` iterator.
     * Internal use only.
     */
    struct iterator_end_tag{
    };

    ChangeIterator(sr_change_iter_s* iter, std::shared_ptr<sr_session_ctx_s> sess);
    ChangeIterator(const iterator_end_tag);
    friend ChangeCollection;

    std::optional<Change> m_current;

    std::shared_ptr<sr_change_iter_s> m_iter;
    std::shared_ptr<sr_session_ctx_s> m_sess;
};

/**
 * @brief An iterable collection containing changes to a datastore.
 *
 * This collection can be retrieved via Session::getChanges. It is compatible with range-based for-loops. Typical usage
 * of this class looks like this:
 * ```
 * // Inside a module change callback
 * for (const auto& change : session.getChanges()) {
 *     std::cerr << "Path of changed node: " << change.node.path() << "\n";
 *     // react to the changes...
 * }
 * ```
 */
class ChangeCollection {
public:
    ChangeIterator begin() const;
    ChangeIterator end() const;

private:
    ChangeCollection(const std::string& xpath, std::shared_ptr<sr_session_ctx_s> sess);
    friend Session;
    std::string m_xpath;
    std::shared_ptr<sr_session_ctx_s> m_sess;
};
}
