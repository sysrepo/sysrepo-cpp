/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
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
        std::cout << node.asTerm().valueStr();
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
    std::cout << change.operation << ": ";
    switch (change.operation) {
    case sysrepo::ChangeOperation::Created:
    case sysrepo::ChangeOperation::Deleted:
        printValue(change.node);
        break;
    case sysrepo::ChangeOperation::Modified:
        printValue(change.node);
        std::cout << " previous value: " << (change.previousValue ? *change.previousValue : "{none}");
        break;
    case sysrepo::ChangeOperation::Moved:
        std::cout << change.node.path();
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

sysrepo::ErrorCode moduleChangeCb(
        sysrepo::Session session,
        uint32_t /*subscriptionId*/,
        std::string_view moduleName,
        std::optional<std::string_view> subXPath,
        sysrepo::Event event,
        uint32_t /*requestId*/)
{

    std::cout << "\n\n ========== EVENT " << event << " CHANGES: ====================================\n\n";

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
    std::string moduleName;
    std::optional<std::string> xpath;

    if ((argc < 2) || (argc > 3)) {
        std::cout << argv[0] << " <module-to-subscribe> [<xpath-to-subscribe>]\n";
        return 1;
    }

    moduleName = argv[1];
    if (argc == 3) {
        xpath = argv[2];
    }

    std::cout << "Application will watch for changes in \"" << ( xpath ? *xpath : moduleName) << "\"";

    sysrepo::setLogLevelStderr(sysrepo::LogLevel::Warning);

    auto session = sysrepo::Connection{}.sessionStart();

    std::cout << "\n ========== READING RUNNING CONFIG: ==========\n\n";
    printCurrentConfig(session, moduleName);

    // subscribe for changes in running config
    auto sub = session.onModuleChange(moduleName, moduleChangeCb, xpath);

    std::cout << "\n\n ========== LISTENING FOR CHANGES ==========\n\n";

    // loop until ctrl-c is pressed / SIGINT is received
    signal(SIGINT, sigint_handler);
    signal(SIGPIPE, SIG_IGN);
    pause();

    std::cout << "Application exit requested, exiting.\n";

    return 0;
}
