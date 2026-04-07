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

#ifndef SIMGRIOT_H
#define SIMGRIOT_H

#include <QObject>
#include <QJsonObject>

/**
 * @brief The narrative agent that turns dry mechanical results into immersive prose.
 */
class SimulationGriot : public QObject {
    Q_OBJECT

public:
    explicit SimulationGriot(QObject *parent = nullptr);

    /**
     * @brief Generates a narrative for the given outcome.
     */
    void narrate(const QString &actorName, const QJsonObject &intent, const QString &arbiterLog, const QJsonObject &patch, const QJsonObject &worldState);

Q_SIGNALS:
    void narrativeDecided(const QString &prose);
    void narrationStarted();
    void errorOccurred(const QString &message);
};

#endif // SIMGRIOT_H
