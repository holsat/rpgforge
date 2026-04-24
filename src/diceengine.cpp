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

#include "diceengine.h"
#include <QRegularExpression>
#include <algorithm>
#include <numeric>

std::mt19937& DiceEngine::getGenerator() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    return gen;
}

int DiceEngine::getRandom(int min, int max) {
    if (min > max) std::swap(min, max);
    std::uniform_int_distribution<> dis(min, max);
    return dis(getGenerator());
}

DiceResult DiceEngine::roll(const QString &formula) {
    DiceResult result;
    result.formula = formula.trimmed().toLower();

    // Regex: (\d+)d(\d+)(?:([dk][hl])(\d+))?(?:([+-])(\d+))?
    // 1: count, 2: sides, 3: op (kh, dl, etc), 4: op_count, 5: mod_sign, 6: mod_val
    static QRegularExpression re(QStringLiteral(R"(^(\d+)d(\d+)(?:([dk][hl])(\d+))?(?:([+-])(\d+))?$)"));
    auto match = re.match(result.formula);

    if (!match.hasMatch()) {
        result.explanation = QStringLiteral("Invalid formula: %1").arg(formula);
        return result;
    }

    int count = match.captured(1).toInt();
    int sides = match.captured(2).toInt();
    QString op = match.captured(3);
    int opCount = match.captured(4).toInt();
    QString modSign = match.captured(5);
    int modVal = match.captured(6).toInt();

    if (count <= 0 || sides <= 0) return result;

    // Perform raw rolls
    QVector<int> rawRolls;
    for (int i = 0; i < count; ++i) {
        rawRolls.append(getRandom(1, sides));
    }

    QVector<int> kept = rawRolls;
    QVector<int> dropped;

    // Handle Keep/Drop
    if (!op.isEmpty() && opCount > 0 && opCount < count) {
        std::sort(kept.begin(), kept.end());
        
        int toDrop = 0;
        bool fromStart = false;

        if (op == QLatin1String("dl")) { // Drop Lowest
            toDrop = opCount;
            fromStart = true;
        } else if (op == QLatin1String("dh")) { // Drop Highest
            toDrop = opCount;
            fromStart = false;
        } else if (op == QLatin1String("kh")) { // Keep Highest
            toDrop = count - opCount;
            fromStart = true;
        } else if (op == QLatin1String("kl")) { // Keep Lowest
            toDrop = count - opCount;
            fromStart = false;
        }

        for (int i = 0; i < toDrop; ++i) {
            int idx = fromStart ? 0 : kept.size() - 1;
            dropped.append(kept.takeAt(idx));
        }
    }

    result.rolls = kept;
    result.dropped = dropped;
    result.modifier = (modSign == QLatin1String("-")) ? -modVal : modVal;
    
    int sum = std::accumulate(kept.begin(), kept.end(), 0);
    result.total = sum + result.modifier;

    // Generate explanation
    QStringList parts;
    for (int r : kept) parts << QString::number(r);
    
    QString rollsStr = QStringLiteral("[%1]").arg(parts.join(QLatin1String(", ")));
    if (!dropped.isEmpty()) {
        QStringList dParts;
        for (int d : dropped) dParts << QString::number(d);
        rollsStr += QStringLiteral(" (Dropped: %1)").arg(dParts.join(QLatin1String(", ")));
    }

    if (result.modifier != 0) {
        rollsStr += QStringLiteral(" %1 %2").arg(modSign.isEmpty() ? QStringLiteral("+") : modSign, QString::number(modVal));
    }

    result.explanation = QStringLiteral("%1 = %2").arg(rollsStr, QString::number(result.total));

    return result;
}
