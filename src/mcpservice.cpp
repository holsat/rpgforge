/*
    RPG Forge
    Copyright (C) 2026  Sheldon L.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "mcpservice.h"
#include "knowledgebase.h"
#include "diceengine.h"
#include "simulationmanager.h"
#include "projectmanager.h"
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QJsonDocument>
#include <QSocketNotifier>
#include <QDebug>
#include <iostream>

MCPService::MCPService(QObject *parent)
    : QObject(parent)
{
}

void MCPService::start()
{
    // Use a notifier to listen for data on stdin
    auto *notifier = new QSocketNotifier(fileno(stdin), QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, &MCPService::onReadyRead);
}

void MCPService::onReadyRead()
{
    // MCP messages are usually single-line or length-prefixed. 
    // For simplicity in this implementation, we read until newline or EOF.
    char buffer[4096];
    if (fgets(buffer, sizeof(buffer), stdin)) {
        processMessage(QByteArray(buffer));
    }
}

void MCPService::processMessage(const QByteArray &data)
{
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (doc.isNull() || !doc.isObject()) return;

    QJsonObject msg = doc.object();
    QJsonValue id = msg.value(QStringLiteral("id"));
    QString method = msg.value(QStringLiteral("method")).toString();
    QJsonObject params = msg.value(QStringLiteral("params")).toObject();

    if (method == QLatin1String("initialize")) {
        handleInitialize(id, params);
    } else if (method == QLatin1String("list_tools")) {
        handleListTools(id);
    } else if (method == QLatin1String("call_tool")) {
        handleCallTool(id, params);
    } else if (method == QLatin1String("list_resources")) {
        handleListResources(id);
    } else {
        sendError(id, -32601, QStringLiteral("Method not found"));
    }
}

void MCPService::sendResponse(const QJsonValue &id, const QJsonObject &result)
{
    QJsonObject response;
    response[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    response[QStringLiteral("id")] = id;
    response[QStringLiteral("result")] = result;

    std::cout << QJsonDocument(response).toJson(QJsonDocument::Compact).constData() << std::endl;
}

void MCPService::sendError(const QJsonValue &id, int code, const QString &message)
{
    QJsonObject response;
    response[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    response[QStringLiteral("id")] = id;
    
    QJsonObject error;
    error[QStringLiteral("code")] = code;
    error[QStringLiteral("message")] = message;
    response[QStringLiteral("error")] = error;

    std::cout << QJsonDocument(response).toJson(QJsonDocument::Compact).constData() << std::endl;
}

void MCPService::handleInitialize(const QJsonValue &id, const QJsonObject &params)
{
    Q_UNUSED(params);
    QJsonObject result;
    result[QStringLiteral("protocolVersion")] = QStringLiteral("2024-11-05");
    result[QStringLiteral("capabilities")] = QJsonObject();
    
    QJsonObject serverInfo;
    serverInfo[QStringLiteral("name")] = QStringLiteral("RPG Forge Sim Engine");
    serverInfo[QStringLiteral("version")] = QStringLiteral("0.1.0");
    result[QStringLiteral("serverInfo")] = serverInfo;

    sendResponse(id, result);
}

void MCPService::handleListTools(const QJsonValue &id)
{
    QJsonArray tools;

    // Tool: lookup_rule
    QJsonObject lookup;
    lookup[QStringLiteral("name")] = QStringLiteral("lookup_rule");
    lookup[QStringLiteral("description")] = QStringLiteral("Searches the RPG project documentation for specific rules or lore.");
    QJsonObject lookupInput;
    lookupInput[QStringLiteral("type")] = QStringLiteral("object");
    QJsonObject lookupProps;
    lookupProps[QStringLiteral("query")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("The rule to search for.")}};
    lookupInput[QStringLiteral("properties")] = lookupProps;
    lookupInput[QStringLiteral("required")] = QJsonArray{QStringLiteral("query")};
    lookup[QStringLiteral("inputSchema")] = lookupInput;
    tools.append(lookup);

    // Tool: roll_dice
    QJsonObject roll;
    roll[QStringLiteral("name")] = QStringLiteral("roll_dice");
    roll[QStringLiteral("description")] = QStringLiteral("Rolls RPG dice using secure C++ logic.");
    QJsonObject rollInput;
    rollInput[QStringLiteral("type")] = QStringLiteral("object");
    QJsonObject rollProps;
    rollProps[QStringLiteral("formula")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Dice formula like 1d20+5.")}};
    rollInput[QStringLiteral("properties")] = rollProps;
    rollInput[QStringLiteral("required")] = QJsonArray{QStringLiteral("formula")};
    roll[QStringLiteral("inputSchema")] = rollInput;
    tools.append(roll);

    // Tool: get_sim_state
    QJsonObject state;
    state[QStringLiteral("name")] = QStringLiteral("get_sim_state");
    state[QStringLiteral("description")] = QStringLiteral("Returns the current live JSON state of the RPG simulation.");
    state[QStringLiteral("inputSchema")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}};
    tools.append(state);

    // Tool: update_sim_state
    QJsonObject updateSim;
    updateSim[QStringLiteral("name")] = QStringLiteral("update_sim_state");
    updateSim[QStringLiteral("description")] = QStringLiteral("Applies a JSON patch to the live simulation state.");
    QJsonObject updateSimInput;
    updateSimInput[QStringLiteral("type")] = QStringLiteral("object");
    QJsonObject updateSimProps;
    updateSimProps[QStringLiteral("patch")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("description"), QStringLiteral("JSON object with dot-separated paths to update.")}};
    updateSimInput[QStringLiteral("properties")] = updateSimProps;
    updateSimInput[QStringLiteral("required")] = QJsonArray{QStringLiteral("patch")};
    updateSim[QStringLiteral("inputSchema")] = updateSimInput;
    tools.append(updateSim);

    // Tool: write_file
    QJsonObject write;
    write[QStringLiteral("name")] = QStringLiteral("write_file");
    write[QStringLiteral("description")] = QStringLiteral("Writes content to a file within the project directory.");
    QJsonObject writeInput;
    writeInput[QStringLiteral("type")] = QStringLiteral("object");
    QJsonObject writeProps;
    writeProps[QStringLiteral("path")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Relative path within the project.")}};
    writeProps[QStringLiteral("content")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("The text content to write.")}};
    writeInput[QStringLiteral("properties")] = writeProps;
    writeInput[QStringLiteral("required")] = QJsonArray{QStringLiteral("path"), QStringLiteral("content")};
    write[QStringLiteral("inputSchema")] = writeInput;
    tools.append(write);

    // Tool: append_to_file
    QJsonObject append;
    append[QStringLiteral("name")] = QStringLiteral("append_to_file");
    append[QStringLiteral("description")] = QStringLiteral("Appends content to an existing file within the project directory.");
    QJsonObject appendInput;
    appendInput[QStringLiteral("type")] = QStringLiteral("object");
    QJsonObject appendProps;
    appendProps[QStringLiteral("path")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Relative path within the project.")}};
    appendProps[QStringLiteral("content")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("The text content to append.")}};
    appendInput[QStringLiteral("properties")] = appendProps;
    appendInput[QStringLiteral("required")] = QJsonArray{QStringLiteral("path"), QStringLiteral("content")};
    append[QStringLiteral("inputSchema")] = appendInput;
    tools.append(append);

    QJsonObject result;
    result[QStringLiteral("tools")] = tools;
    sendResponse(id, result);
}

void MCPService::handleCallTool(const QJsonValue &id, const QJsonObject &params)
{
    QString name = params.value(QStringLiteral("name")).toString();
    QJsonObject args = params.value(QStringLiteral("arguments")).toObject();

    if (name == QLatin1String("lookup_rule")) {
        toolLookupRule(id, args);
    } else if (name == QLatin1String("roll_dice")) {
        toolRollDice(id, args);
    } else if (name == QLatin1String("get_sim_state")) {
        toolGetSimState(id);
    } else if (name == QLatin1String("update_sim_state")) {
        toolUpdateSimState(id, args);
    } else if (name == QLatin1String("write_file")) {
        toolWriteFile(id, args);
    } else if (name == QLatin1String("append_to_file")) {
        toolAppendToFile(id, args);
    } else {
        sendError(id, -32602, QStringLiteral("Tool not found"));
    }
}

void MCPService::handleListResources(const QJsonValue &id)
{
    QJsonObject result;
    result[QStringLiteral("resources")] = QJsonArray(); // Not implemented yet
    sendResponse(id, result);
}

void MCPService::toolLookupRule(const QJsonValue &id, const QJsonObject &args)
{
    QString query = args.value(QStringLiteral("query")).toString();
    KnowledgeBase::instance().search(query, 3, QString(), [this, id](const QList<SearchResult> &results) {
        QJsonArray content;
        for (const auto &res : results) {
            QJsonObject item;
            item[QStringLiteral("type")] = QStringLiteral("text");
            item[QStringLiteral("text")] = QStringLiteral("Source: %1\n%2").arg(res.heading, res.content);
            content.append(item);
        }
        
        QJsonObject result;
        result[QStringLiteral("content")] = content;
        sendResponse(id, result);
    });
}

void MCPService::toolRollDice(const QJsonValue &id, const QJsonObject &args)
{
    QString formula = args.value(QStringLiteral("formula")).toString();
    DiceResult dr = DiceEngine::roll(formula);
    
    QJsonObject result;
    QJsonArray content;
    QJsonObject item;
    item[QStringLiteral("type")] = QStringLiteral("text");
    item[QStringLiteral("text")] = dr.explanation;
    content.append(item);
    
    result[QStringLiteral("content")] = content;
    sendResponse(id, result);
}

void MCPService::toolGetSimState(const QJsonValue &id)
{
    QJsonObject result;
    QJsonArray content;
    QJsonObject item;
    item[QStringLiteral("type")] = QStringLiteral("text");
    item[QStringLiteral("text")] = QString::fromUtf8(QJsonDocument(SimulationManager::instance().state()->state()).toJson());
    content.append(item);
    
    result[QStringLiteral("content")] = content;
    sendResponse(id, result);
}

void MCPService::toolUpdateSimState(const QJsonValue &id, const QJsonObject &args)
{
    QJsonObject patch = args.value(QStringLiteral("patch")).toObject();
    SimulationState *state = SimulationManager::instance().state();
    
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        state->setValue(it.key(), it.value());
    }

    QJsonObject result;
    QJsonArray content;
    QJsonObject item;
    item[QStringLiteral("type")] = QStringLiteral("text");
    item[QStringLiteral("text")] = QStringLiteral("Simulation state updated successfully.");
    content.append(item);
    result[QStringLiteral("content")] = content;
    sendResponse(id, result);
}

void MCPService::toolWriteFile(const QJsonValue &id, const QJsonObject &args)
{
    QString relPath = args.value(QStringLiteral("path")).toString();
    QString content = args.value(QStringLiteral("content")).toString();
    QString absPath = QDir(ProjectManager::instance().projectPath()).absoluteFilePath(relPath);

    if (!isPathSafe(absPath)) {
        sendError(id, -32602, QStringLiteral("Access denied: Path outside project directory."));
        return;
    }

    QFile file(absPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(content.toUtf8());
        file.close();
        
        QJsonObject result;
        QJsonArray resContent;
        QJsonObject item;
        item[QStringLiteral("type")] = QStringLiteral("text");
        item[QStringLiteral("text")] = QStringLiteral("File written successfully: %1").arg(relPath);
        resContent.append(item);
        result[QStringLiteral("content")] = resContent;
        sendResponse(id, result);
    } else {
        sendError(id, -32000, QStringLiteral("Failed to open file for writing."));
    }
}

void MCPService::toolAppendToFile(const QJsonValue &id, const QJsonObject &args)
{
    QString relPath = args.value(QStringLiteral("path")).toString();
    QString content = args.value(QStringLiteral("content")).toString();
    QString absPath = QDir(ProjectManager::instance().projectPath()).absoluteFilePath(relPath);

    if (!isPathSafe(absPath)) {
        sendError(id, -32602, QStringLiteral("Access denied: Path outside project directory."));
        return;
    }

    QFile file(absPath);
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        file.write(content.toUtf8());
        file.close();
        
        QJsonObject result;
        QJsonArray resContent;
        QJsonObject item;
        item[QStringLiteral("type")] = QStringLiteral("text");
        item[QStringLiteral("text")] = QStringLiteral("Content appended successfully: %1").arg(relPath);
        resContent.append(item);
        result[QStringLiteral("content")] = resContent;
        sendResponse(id, result);
    } else {
        sendError(id, -32000, QStringLiteral("Failed to open file for appending."));
    }
}

bool MCPService::isPathSafe(const QString &path) const
{
    QString projectPath = ProjectManager::instance().projectPath();
    if (projectPath.isEmpty()) return false;

    QString projectRoot = QDir(projectPath).absolutePath();
    QString targetPath = QFileInfo(path).absoluteFilePath();
    
    return targetPath.startsWith(projectRoot);
}
