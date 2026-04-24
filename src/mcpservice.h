/*
    RPG Forge
    Copyright (C) 2026  Sheldon Lee Wen

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

#ifndef MCPSERVICE_H
#define MCPSERVICE_H

#include <QObject>
#include <QByteArray>
#include <QJsonObject>
#include <QJsonArray>

/**
 * @brief Implements the Model Context Protocol (MCP) over stdio.
 * Allows external LLMs to query project rules and simulation state.
 */
class MCPService : public QObject {
    Q_OBJECT

public:
    explicit MCPService(QObject *parent = nullptr);

    /**
     * @brief Starts the MCP event loop on stdin/stdout.
     */
    void start();

private Q_SLOTS:
    void onReadyRead();

private:
    void processMessage(const QByteArray &data);
    void sendResponse(const QJsonValue &id, const QJsonObject &result);
    void sendError(const QJsonValue &id, int code, const QString &message);

    // MCP Handlers
    void handleInitialize(const QJsonValue &id, const QJsonObject &params);
    void handleListTools(const QJsonValue &id);
    void handleCallTool(const QJsonValue &id, const QJsonObject &params);
    void handleListResources(const QJsonValue &id);

    // Tool Implementations
    void toolLookupRule(const QJsonValue &id, const QJsonObject &args);
    void toolRollDice(const QJsonValue &id, const QJsonObject &args);
    void toolGetSimState(const QJsonValue &id);
    void toolWriteFile(const QJsonValue &id, const QJsonObject &args);
    void toolAppendToFile(const QJsonValue &id, const QJsonObject &args);
    void toolUpdateSimState(const QJsonValue &id, const QJsonObject &args);

    bool isPathSafe(const QString &path) const;

    // Accumulates stdin bytes across readyRead() calls until a complete
    // newline-terminated JSON message is available.
    QByteArray m_stdinBuffer;
};

#endif // MCPSERVICE_H
