/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 * TODO: the code is 100% percent different from the sysrepo example, but it esentially the same thing, should I still
 * include the author from sysrepo?
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/

#include <atomic>
#include <csignal>
#include <experimental/iterator>
#include <iostream>
#include <sysrepo-cpp/Connection.hpp>
#include <sysrepo-cpp/utils/utils.hpp>
#include <unistd.h>


std::atomic<bool> exitApplication = false;

// TODO: maybe libyang-cpp should provide operator<< for all of the types? But that won't work for std types... so maybe
// libyang-cpp should provide this value printer? With possible overrides in case the user wants something else, but
// still wants some of the defaults.
struct ValuePrinter {
    std::string operator()(const libyang::Empty) const
    {
        return "(empty)";
    }

    std::string operator()(const std::vector<libyang::Bit>& val) const
    {
        std::ostringstream oss;
        std::transform(val.begin(), val.end(), std::experimental::make_ostream_joiner(oss, " "), [] (const auto& bit) {
            return bit.name;
        });
        return oss.str();
    }

    std::string operator()(const libyang::Decimal64& val) const
    {
        return std::to_string(double{val});
    }

    std::string operator()(const libyang::Binary& val) const
    {
        return val.base64;
    }

    std::string operator()(const libyang::Enum& val) const
    {
        return val.name;
    }

    std::string operator()(const libyang::IdentityRef& val) const
    {
        return val.module + ":" + val.name;
    }

    std::string operator()(const std::optional<libyang::DataNode>& val) const
    {
        if (!val) {
            return "Leafref{no-instance}";
        }

        return std::visit(*this, val->asTerm().value());
    }

    template <typename ValueType>
    std::string operator()(const ValueType& val) const
    {
        std::ostringstream oss;
        oss << val;

        return oss.str();
    }
};

void printValue(libyang::DataNode node)
{
    std::cout << node.path() << " = ";

    switch (node.schema().nodeType()) {
    case libyang::NodeType::Container:
        std::cout << "(container)";
        break;
    case libyang::NodeType::List:
        std::cout << "(list instance)";
        break;
    case libyang::NodeType::Leaf:
        std::cout << std::visit(ValuePrinter{}, node.asTerm().value());
        break;
    default:
        std::cout << "(unprintable)";
        break;
    }

    if (node.schema().nodeType() == libyang::NodeType::Leaf && node.asTerm().isDefaultValue()) {
        std::cout << " [default]";
    }

    std::cout << "\n";
}

void printChange(const sysrepo::Change& change)
{
    switch (change.operation) {
    case sysrepo::ChangeOperation::Created:
        std::cout << "CREATED: ";
        printValue(change.node);
        break;
    case sysrepo::ChangeOperation::Deleted:
        std::cout << "DELETED: ";
        printValue(change.node);
        break;
    case sysrepo::ChangeOperation::Modified:
        std::cout << "MODIFIED: ";
        printValue(change.node);
        std::cout << " previous value: " << (change.previousValue ? *change.previousValue : "{none}");
        break;
    case sysrepo::ChangeOperation::Moved:
        std::cout << "MOVED: " << change.node.path();
        break;
    }
}

void printCurrentConfig(sysrepo::Session session, std::string moduleName)
{
    auto data = session.getData(("/" + moduleName + ":*//.").c_str());

    if (data) {
        for (const auto& sibling : data->siblings()) {
            for (const auto& node : sibling.childrenDfs()) {
                printValue(node);
            }
        }
    } else {

        std::cerr << "<no data>\n";
    }
}

// TODO: Sysrepo should provide operator<< for this?
const char* eventToString(sysrepo::Event ev)
{
    switch (ev) {
    case sysrepo::Event::Change:
        return "change";
    case sysrepo::Event::Done:
        return "done";
    case sysrepo::Event::Abort:
        return "abort";
    case sysrepo::Event::Enabled:
        return "enabled";
    case sysrepo::Event::RPC:
        return "rpc";
    case sysrepo::Event::Update:
        return "update";
    }

    __builtin_unreachable();
}

sysrepo::ErrorCode moduleChangeCb(
        sysrepo::Session session,
        uint32_t /*subscriptionId*/,
        std::string_view moduleName,
        std::optional<std::string_view> subXPath,
        sysrepo::Event event,
        uint32_t /*requestId*/)
{

    std::cout << "\n\n ========== EVENT " << eventToString(event) << " CHANGES: ====================================\n\n";

    std::string pathForChanges;
    if (subXPath) {
        pathForChanges = std::string{*subXPath} + "//.";
    } else {
        pathForChanges = "/" + std::string{moduleName} + ":*//.";
    }

    for (const auto& change : session.getChanges(pathForChanges.c_str())) {
        printChange(change);
    }

    std::cout << "\n\n ========== END OF CHANGES =======================================";

    if (event == sysrepo::Event::Done) {
        std::cout << "\n\n ========== CONFIG HAS CHANGED, CURRENT RUNNING CONFIG: ==========\n\n";
        printCurrentConfig(session, moduleName.data());
    }

    return sysrepo::ErrorCode::Ok;
}

void sigint_handler(int /*signum*/)
{
}

int main(int argc, char** argv)
{
    const char* moduleName = nullptr;
    const char* xpath = nullptr;

    if ((argc < 2) || (argc > 3)) {
        std::cout << argv[0] << " <module-to-subscribe> [<xpath-to-subscribe>]\n";
        return 1;
    }

    moduleName = argv[1];
    if (argc == 3) {
        xpath = argv[2];
    }

    std::cout << "Application will watch for changes in \"" << ( xpath ? xpath : moduleName) << "\"";

    // turn logging on
    sysrepo::setLogLevelStderr(sysrepo::LogLevel::Warning);

    // connect to sysrepo
    auto connection = sysrepo::Connection{};

    // start session
    auto session = connection.sessionStart();

    // read current config
    std::cout << "\n ========== READING RUNNING CONFIG: ==========\n\n";
    printCurrentConfig(session, moduleName);

    // subscribe for changes in running config
    auto sub = session.onModuleChange(moduleName, moduleChangeCb);

    std::cout << "\n\n ========== LISTENING FOR CHANGES ==========\n\n";

    // loop until ctrl-c is pressed / SIGINT is received
    signal(SIGINT, sigint_handler);
    signal(SIGPIPE, SIG_IGN);
    pause();

    std::cout << "Application exit requested, exiting.\n";

    return 0;
}
