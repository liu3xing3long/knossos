#include "GuiConstants.h"
#include "segmentation/segmentation.h"
#include "session.h"
#include "snapshotwidget.h"
#include "stateInfo.h"
#include "viewer.h"

#include <math.h>

#include <QApplication>
#include <QDesktopWidget>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QSettings>

SnapshotWidget::SnapshotWidget(QWidget *parent) : QDialog(parent), saveDir(QDir::homePath()) {
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowTitle("Snapshot Tool");
    sizeCombo.addItem("8192 x 8192");
    sizeCombo.addItem("4096 x 4096");
    sizeCombo.addItem("2048 x 2048");
    sizeCombo.addItem("1024 x 1024");
    sizeCombo.setCurrentIndex(2); // 2048x2048 default

    auto viewportChoiceLayout = new QVBoxLayout();
    auto vpGroup = new QButtonGroup(this);
    vpGroup->addButton(&vpXYRadio);
    vpGroup->addButton(&vpXZRadio);
    vpGroup->addButton(&vpZYRadio);
    vpGroup->addButton(&vp3dRadio);
    viewportChoiceLayout->addWidget(&vpXYRadio);
    viewportChoiceLayout->addWidget(&vpXZRadio);
    viewportChoiceLayout->addWidget(&vpZYRadio);
    viewportChoiceLayout->addWidget(&vp3dRadio);
    QObject::connect(vpGroup, static_cast<void(QButtonGroup::*)(int)>(&QButtonGroup::buttonClicked), [this](const int) { updateOptionVisibility(); });

    auto imageOptionsLayout = new QVBoxLayout();
    withAxesCheck.setChecked(true);
    withOverlayCheck.setChecked(true);
    withScaleCheck.setChecked(true);
    withVpPlanes.setHidden(true);
    imageOptionsLayout->addWidget(&withAxesCheck);
    imageOptionsLayout->addWidget(&withOverlayCheck);
    imageOptionsLayout->addWidget(&withSkeletonCheck);
    imageOptionsLayout->addWidget(&withScaleCheck);
    imageOptionsLayout->addWidget(&withVpPlanes);

    auto settingsLayout = new QHBoxLayout();
    settingsLayout->addLayout(viewportChoiceLayout);
    auto line = new QFrame();
    line->setFrameShape(QFrame::VLine);
    line->setFrameStyle(QFrame::Sunken);
    settingsLayout->addWidget(line);
    settingsLayout->addLayout(imageOptionsLayout);

    mainLayout.addWidget(&sizeCombo);
    mainLayout.addLayout(settingsLayout);
    mainLayout.addWidget(&snapshotButton);

    QObject::connect(&snapshotButton, &QPushButton::clicked, [this]() {
        state->viewerState->renderInterval = SLOW;
        const auto path = QFileDialog::getSaveFileName(this, tr("Save path"), saveDir + defaultFilename(), tr("Images (*.png *.xpm *.xbm *.jpg *.bmp)"));
        state->viewerState->renderInterval = FAST;
        if(path.isEmpty() == false) {
            QFileInfo info(path);
            saveDir = info.absolutePath() + "/";
            const auto vp = vpXYRadio.isChecked() ? VIEWPORT_XY :
                            vpXZRadio.isChecked() ? VIEWPORT_XZ :
                            vpZYRadio.isChecked() ? VIEWPORT_ZY :
                                                    VIEWPORT_SKELETON;

            emit snapshotRequest(path, vp, 8192/pow(2, sizeCombo.currentIndex()), withAxesCheck.isChecked(), withOverlayCheck.isChecked(), withSkeletonCheck.isChecked(), withScaleCheck.isChecked(), withVpPlanes.isChecked());
        }
    });
    setLayout(&mainLayout);
}

uint SnapshotWidget::getCheckedViewport() const {
    return vpXYRadio.isChecked() ? VIEWPORT_XY :
           vpXZRadio.isChecked() ? VIEWPORT_XZ :
           vpZYRadio.isChecked() ? VIEWPORT_ZY :
                                   VIEWPORT_SKELETON;
}

void SnapshotWidget::updateOptionVisibility() {
    withOverlayCheck.setVisible(vp3dRadio.isChecked() == false);
    withSkeletonCheck.setVisible(vp3dRadio.isChecked() == false || !Segmentation::singleton().volume_render_toggle);
    withScaleCheck.setVisible(vp3dRadio.isChecked() == false || !Segmentation::singleton().volume_render_toggle);
    withAxesCheck.setVisible(vp3dRadio.isChecked() && !Segmentation::singleton().volume_render_toggle);
    withVpPlanes.setVisible(vp3dRadio.isChecked() && !Segmentation::singleton().volume_render_toggle);
}

QString SnapshotWidget::defaultFilename() const {
    const QString vp = vpXYRadio.isChecked() ? "XY" :
                   vpXZRadio.isChecked() ? "XZ" :
                   vpZYRadio.isChecked() ? "ZY" :
                                           "3D";
    auto pos = &state->viewerState->currentPosition;
    auto annotationName = Session::singleton().annotationFilename;
    annotationName.remove(0, annotationName.lastIndexOf("/") + 1); // remove directory structure from name
    annotationName.chop(annotationName.endsWith(".k.zip") ? 6 : /* .nml */ 4); // remove file type
    return QString("%1_%2_%3_%4_%5.png").arg(vp).arg(pos->x).arg(pos->y).arg(pos->z).arg(annotationName.isEmpty() ? state->name : annotationName);
}

void SnapshotWidget::saveSettings() {
    QSettings settings;
    settings.beginGroup(SNAPSHOT_WIDGET);
    settings.setValue(GEOMETRY, saveGeometry());
    settings.setValue(VISIBLE, isVisible());

    settings.setValue(VIEWPORT, getCheckedViewport());
    settings.setValue(WITH_AXES, withAxesCheck.isChecked());
    settings.setValue(WITH_OVERLAY, withOverlayCheck.isChecked());
    settings.setValue(WITH_SKELETON, withSkeletonCheck.isChecked());
    settings.setValue(WITH_SCALE, withScaleCheck.isChecked());
    settings.setValue(WITH_VP_PLANES, withVpPlanes.isChecked());
    settings.setValue(SAVE_DIR, saveDir);
    settings.endGroup();
}

void SnapshotWidget::loadSettings() {
    QSettings settings;
    settings.beginGroup(SNAPSHOT_WIDGET);

    restoreGeometry(settings.value(GEOMETRY).toByteArray());
    setVisible(settings.value(VISIBLE, false).toBool());

    const auto vp = settings.value(VIEWPORT, VIEWPORT_XY).toInt();
    switch(vp) {
        case VIEWPORT_XY: vpXYRadio.setChecked(true); break;
        case VIEWPORT_XZ: vpXZRadio.setChecked(true); break;
        case VIEWPORT_ZY: vpZYRadio.setChecked(true); break;
        default: vp3dRadio.setChecked(true); break;
    }
    withAxesCheck.setChecked(settings.value(WITH_AXES, true).toBool());
    withOverlayCheck.setChecked(settings.value(WITH_OVERLAY, true).toBool());
    withSkeletonCheck.setChecked(settings.value(WITH_SKELETON, true).toBool());
    withScaleCheck.setChecked(settings.value(WITH_SCALE, true).toBool());
    withVpPlanes.setChecked(settings.value(WITH_VP_PLANES, false).toBool());
    updateOptionVisibility();
    saveDir = settings.value(SAVE_DIR, QDir::homePath() + "/").toString();

    settings.endGroup();
}
