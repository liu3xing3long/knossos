/*
 *  This file is a part of KNOSSOS.
 *
 *  (C) Copyright 2007-2016
 *  Max-Planck-Gesellschaft zur Foerderung der Wissenschaften e.V.
 *
 *  KNOSSOS is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 of
 *  the License as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 *  For further information, visit https://knossostool.org
 *  or contact knossos-team@mpimf-heidelberg.mpg.de
 */

#ifndef NAVIGATIONTAB_H
#define NAVIGATIONTAB_H

#include <QButtonGroup>
#include <QCheckBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QWidget>

enum class Recentering {
    OnNode
    , Off
    , AheadOfNode
};

class QSettings;
class NavigationTab : public QWidget {
    Q_OBJECT
    QVBoxLayout mainLayout;
    QHBoxLayout upperLayout;
    QVBoxLayout rightupperLayout;

    QGroupBox movementAreaGroup{"Movement area"};
    QGridLayout movementAreaLayout;
    QLabel minLabel{tr("Min")};
    QLabel maxLabel{tr("Max")};
    QSpinBox xMinField, yMinField, zMinField, xMaxField, yMaxField, zMaxField;
    QLabel outVisibilityLabel{tr("Visibility of outside area")};
    QSlider outVisibilitySlider;
    QSpinBox outVisibilitySpin;
    QPushButton resetMovementAreaButton{"Reset to dataset boundaries"};
    QFrame separator;
    QGroupBox keyboardMovementGroup{"Keyboard movement"};
    QGroupBox mouseBehaviourGroup{"Mouse behaviour"};
    QFormLayout mouseBehaviourLayout;
    QCheckBox penModeCheckBox;
    QFormLayout keyboardMovementLayout;
    QSpinBox movementSpeedSpinBox;
    QSpinBox jumpFramesSpinBox;
    QSpinBox walkFramesSpinBox;

    QGroupBox advancedGroup{"Recentering behaviour"};
    QFormLayout advancedFormLayout;
    QButtonGroup recenteringButtonGroup;
    QRadioButton recenteringButton{"Recenter on new node (default)"};
    QRadioButton noRecenteringButton{"No recentering"};
    QRadioButton additionalTracingDirectionMoveButton{"Recenter ahead of tracing direction"};
    QSpinBox delayTimePerStepSpinBox;
    QSpinBox numberOfStepsSpinBox;

    void updateMovementArea();
public:
    explicit NavigationTab(QWidget *parent = 0);
    void loadSettings(const QSettings &settings);
    void saveSettings(QSettings &settings);
};

#endif//NAVIGATIONWIDGET_H
