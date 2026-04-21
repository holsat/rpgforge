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

#ifndef TOGGLESWITCH_H
#define TOGGLESWITCH_H

#include <QAbstractButton>

/**
 * @brief Minimal iOS-style slide toggle. Functionally a QAbstractButton
 * in checkable mode; visually a pill-shaped track with a thumb that
 * snaps between two positions. Used in the LLM settings tab to enable
 * or disable a provider's participation in the fallback chain.
 */
class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget *parent = nullptr);

    QSize sizeHint() const override { return QSize(46, 22); }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *event) override;
};

#endif // TOGGLESWITCH_H
