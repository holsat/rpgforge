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

#include "toggleswitch.h"
#include <QPainter>
#include <QPaintEvent>

ToggleSwitch::ToggleSwitch(QWidget *parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    // No text/icon — the switch is a pure visual control; the identity of
    // the thing being toggled is conveyed by surrounding UI (e.g. the
    // provider group box title in the LLM settings tab).
}

void ToggleSwitch::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Colours chosen to match the app's existing "Connected" green status
    // in the settings dialog so enabled = green reads as "this provider
    // is active", disabled = neutral grey reads as "paused".
    const QColor trackOff(95, 95, 95);
    const QColor trackOn(39, 174, 96);
    const QColor thumb(240, 240, 240);

    const QRect r = rect().adjusted(0, 0, -1, -1);
    const int radius = r.height() / 2;

    p.setPen(Qt::NoPen);
    p.setBrush(isChecked() ? trackOn : trackOff);
    p.drawRoundedRect(r, radius, radius);

    const int thumbDiameter = r.height() - 4;
    const int thumbX = isChecked()
        ? r.right() - thumbDiameter - 1
        : r.left() + 2;
    p.setBrush(thumb);
    p.drawEllipse(QRect(thumbX, r.top() + 2, thumbDiameter, thumbDiameter));
}
