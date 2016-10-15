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
 *  For further information, visit http://www.knossostool.org
 *  or contact knossos-team@mpimf-heidelberg.mpg.de
 */

#include "widgets/viewport.h"

#include "profiler.h"
#include "segmentation/cubeloader.h"
#include "segmentation/segmentation.h"
#include "session.h"
#include "skeleton/node.h"
#include "skeleton/skeletonizer.h"
#include "skeleton/tree.h"
#include "viewer.h"

#include <QMatrix4x4>
#include <QOpenGLPaintDevice>
#include <QOpenGLTimeMonitor>
#include <QPainter>
#include <QQuaternion>
#include <QVector3D>
#include <QOpenGLBuffer>
#include <QOpenGLFramebufferObject>

#ifdef Q_OS_MAC
    #include <glu.h>
#endif
#ifdef Q_OS_LINUX
    #include <GL/gl.h>
    #include <GL/glu.h>
#endif
#ifdef Q_OS_WIN32
    #include <GL/glu.h>
#endif

#include <boost/math/constants/constants.hpp>

#include <cmath>

enum GLNames {
    None,
    Scalebar,
    NodeOffset
};

uint ViewportBase::renderCylinder(Coordinate *base, float baseRadius, Coordinate *top, float topRadius, QColor color, const RenderOptions & options) {
    decltype(state->viewerState->lineVertBuffer.colors)::value_type color4f = {static_cast<GLfloat>(color.redF()), static_cast<GLfloat>(color.greenF()), static_cast<GLfloat>(color.blueF()), static_cast<GLfloat>(color.alphaF())};
    const auto alwaysLinesAndPoints = state->viewerState->cumDistRenderThres > 19.f;
    const auto alwaysTubesAndSpheres = state->viewerState->cumDistRenderThres <= 1.0f;
    const auto switchDynamically =  !alwaysLinesAndPoints && !alwaysTubesAndSpheres;
    const auto dynamicLinesAndPoints = switchDynamically && screenPxXPerDataPx * baseRadius < 1.0f && screenPxXPerDataPx * topRadius < 1.0f;
    const auto linesAndPoitns = options.enableSkeletonDownsampling && (alwaysLinesAndPoints || dynamicLinesAndPoints);
    if (linesAndPoitns) {
        state->viewerState->lineVertBuffer.vertices.emplace_back(*base);
        state->viewerState->lineVertBuffer.vertices.emplace_back(*top);

        state->viewerState->lineVertBuffer.colors.emplace_back(color4f);
        state->viewerState->lineVertBuffer.colors.emplace_back(color4f);
    } else {
        glColor4fv(color4f.data());

        glPushMatrix();
        GLUquadricObj * gluCylObj = gluNewQuadric();
        gluQuadricNormals(gluCylObj, GLU_SMOOTH);
        gluQuadricOrientation(gluCylObj, GLU_OUTSIDE);

        glTranslatef(base->x, base->y, base->z);
        glScalef(1.f, 1.f, state->viewerState->voxelXYtoZRatio);

        //Some calculations for the correct direction of the cylinder.
        const floatCoordinate cylinderDirection{0.0f, 0.0f, 1.0f};
        floatCoordinate segDirection{*top - *base};
        segDirection.z /= state->viewerState->voxelXYtoZRatio;

        floatCoordinate rotationAxis{cylinderDirection.cross(segDirection)};
        const float currentAngle{radToDeg(cylinderDirection.angleRad(segDirection))};
        //we need another reference vector for 180° rotations
        if (rotationAxis == floatCoordinate{0, 0, 0}) {
            rotationAxis = {floatCoordinate(0.0f, 1.0f, 0.0f).cross(segDirection)};
        }
        //some gl implementations have problems with the params occuring for
        //segs in straight directions. we need a fix here.
        glRotatef(currentAngle, rotationAxis.x, rotationAxis.y, rotationAxis.z);

        //tdItem use screenPxXPerDataPx for proper LOD
        //the same values have to be used in rendersegplaneintersections to avoid ugly graphics
        const auto edges = std::max(baseRadius, topRadius) > 100.f ? 10 : std::max(baseRadius, topRadius) > 15.f ? 6 : 3;
        gluCylinder(gluCylObj, baseRadius, topRadius, segDirection.length(), edges, 1);

        gluDeleteQuadric(gluCylObj);
        glPopMatrix();
    }
    return true;
}

uint ViewportBase::renderSphere(const Coordinate & pos, const float & radius, const QColor & color, const RenderOptions & options) {
    GLUquadricObj *gluSphereObj = NULL;

    decltype(state->viewerState->lineVertBuffer.colors)::value_type color4f = {static_cast<GLfloat>(color.redF()), static_cast<GLfloat>(color.greenF()), static_cast<GLfloat>(color.blueF()), static_cast<GLfloat>(color.alphaF())};
    /* Render only a point if the sphere wouldn't be visible anyway */
    if (options.enableSkeletonDownsampling &&
        (((screenPxXPerDataPx * radius > 0.0f) && (screenPxXPerDataPx * radius < 2.0f)) || (state->viewerState->cumDistRenderThres > 19.f))) {
        /* This is cumbersome, but SELECT mode cannot be used with glDrawArray.
        Color buffer picking brings its own issues on the other hand, so we
        stick with SELECT mode for the time being. */
        if(state->viewerState->selectModeFlag) {
            glPointSize(radius * 2.);
            glBegin(GL_POINTS);
                glVertex3f((float)pos.x, (float)pos.y, (float)pos.z);
            glEnd();
            glPointSize(1.);
        } else {
            state->viewerState->pointVertBuffer.vertices.emplace_back(pos);
            state->viewerState->pointVertBuffer.colors.emplace_back(color4f);
        }
    }
    else {
        glColor4fv(color4f.data());
        glPushMatrix();
        glTranslatef((float)pos.x, (float)pos.y, (float)pos.z);
        glScalef(1.f, 1.f, state->viewerState->voxelXYtoZRatio);
        gluSphereObj = gluNewQuadric();
        gluQuadricNormals(gluSphereObj, GLU_SMOOTH);
        gluQuadricOrientation(gluSphereObj, GLU_OUTSIDE);

        if(radius * screenPxXPerDataPx  > 20.) {
            gluSphere(gluSphereObj, radius, 14, 14);
        }
        else if(radius * screenPxXPerDataPx  > 5.) {
            gluSphere(gluSphereObj, radius, 8, 8);
        }
        else {
            gluSphere(gluSphereObj, radius, 5, 5);
        }
        //glScalef(1.f, 1.f, 1.f/state->viewerState->voxelXYtoZRatio);
        gluDeleteQuadric(gluSphereObj);
        glPopMatrix();
    }

    return true;
}

void ViewportBase::renderSegment(const segmentListElement & segment, const QColor & color, const RenderOptions & options) {
    renderCylinder(&(segment.source.position), Skeletonizer::singleton().radius(segment.source) * state->viewerState->segRadiusToNodeRadius,
        &(segment.target.position), Skeletonizer::singleton().radius(segment.target) * state->viewerState->segRadiusToNodeRadius, color, options);
}

void ViewportOrtho::renderSegment(const segmentListElement & segment, const QColor & color, const RenderOptions & options) {
    ViewportBase::renderSegment(segment, color, options);
    if (state->viewerState->showIntersections) {
        renderSegPlaneIntersection(segment);
    }
}

void ViewportBase::renderNode(const nodeListElement & node, const RenderOptions & options) {
    auto color = state->viewer->getNodeColor(node);
    const float radius = Skeletonizer::singleton().radius(node);

    float mp;
    if( radius > 30.0f) {
        mp = 1.1; //halo only 10% bigger then node
    } else {
        //scale the halo size from 10% up to 100% bigger then the node size
        //otherwise, the halo won't be visible on very small node sizes (=1.0)
        //(x - x_0) * (f_1 - f_0)/(x_1 - x_0)  //Linear interpolation
        mp = (radius - 1.0) * (1.1 - 2.0)/(30.0 - 1.0) + 2.0;
    }

    renderSphere(node.position, radius, color, options);

    if(node.selected && options.highlightSelection) { // highlight selected nodes
        auto selectedNodeColor = QColor(Qt::green);
        selectedNodeColor.setAlphaF(0.5f);
        renderSphere(node.position, radius * mp, selectedNodeColor);
    }
    // Highlight active node with a halo
    if(state->skeletonState->activeNode == &node && options.highlightActiveNode) {
        // Color gets changes in case there is a comment & conditional comment highlighting
        auto haloColor = state->viewer->getNodeColor(node);
        haloColor.setAlphaF(0.2);
        renderSphere(node.position, radius * ((mp - 1.0)/2.0 + 1.0), haloColor);
    }
}

void ViewportOrtho::renderNode(const nodeListElement & node, const RenderOptions & options) {
    ViewportBase::renderNode(node, options);
    if(1.5 <  Skeletonizer::singleton().radius(node)) { // draw node center to make large nodes visible and clickable in ortho vps
        renderSphere(node.position, 1.5, state->viewer->getNodeColor(node));
    }
    // Render the node description
    glColor4f(0.f, 0.f, 0.f, 1.f);
    auto nodeID = (state->viewerState->idDisplay.testFlag(IdDisplay::AllNodes) || (state->viewerState->idDisplay.testFlag(IdDisplay::ActiveNode) && state->skeletonState->activeNode == &node))? QString::number(node.nodeID) : "";
    auto comment = node.getComment();
    comment = (ViewportOrtho::showNodeComments && comment.isEmpty() == false)? QString(":%1").arg(comment) : "";
    if(nodeID.isEmpty() == false || comment.isEmpty() == false) {
        renderText(node.position, nodeID.append(comment), options.enableTextScaling);
    }
}

void Viewport3D::renderNode(const nodeListElement & node, const RenderOptions & options) {
    ViewportBase::renderNode(node, options);
    // Render the node description
    if (state->viewerState->idDisplay.testFlag(IdDisplay::AllNodes) || (state->viewerState->idDisplay.testFlag(IdDisplay::ActiveNode) && state->skeletonState->activeNode == &node)) {
        glColor4f(0.f, 0.f, 0.f, 1.f);
        renderText(node.position, QString::number(node.nodeID), options.enableTextScaling);
    }
}

static void backup_gl_state() {
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
}

static void restore_gl_state() {
    glMatrixMode(GL_TEXTURE);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopAttrib();
    glPopClientAttrib();
}

void ViewportBase::renderText(const Coordinate & pos, const QString & str, const bool fontScaling, bool centered) {
    GLdouble x, y, z, model[16], projection[16];
    GLint gl_viewport[4];
    glGetDoublev(GL_MODELVIEW_MATRIX, &model[0]);
    glGetDoublev(GL_PROJECTION_MATRIX, &projection[0]);
    glGetIntegerv(GL_VIEWPORT, gl_viewport);
    //retrieve 2d screen position from coordinate
    backup_gl_state();
    QOpenGLPaintDevice paintDevice(gl_viewport[2], gl_viewport[3]);//create paint device from viewport size and current context
    QPainter painter(&paintDevice);
    painter.setFont(QFont(painter.font().family(), (fontScaling ? std::ceil(0.02*gl_viewport[2]) : defaultFonsSize) * devicePixelRatio()));
    gluProject(pos.x, pos.y - 3, pos.z, &model[0], &projection[0], &gl_viewport[0], &x, &y, &z);
    painter.setPen(Qt::black);
    painter.drawText(centered ? x - QFontMetrics(painter.font()).width(str)/2. : x, gl_viewport[3] - y, str);//inverse y coordinate, extract height from gl viewport
    painter.end();//would otherwise fiddle with the gl state in the dtor
    restore_gl_state();
}


uint ViewportOrtho::renderSegPlaneIntersection(const segmentListElement & segment) {
    float p[2][3], a, currentAngle, length, radius, distSourceInter, sSr_local, sTr_local;
    int i, distToCurrPos;
    floatCoordinate tempVec2, tempVec, tempVec3, segDir, intPoint, sTp_local, sSp_local;
    GLUquadricObj *gluCylObj = NULL;

    sSp_local.x = (float)segment.source.position.x;
    sSp_local.y = (float)segment.source.position.y;
    sSp_local.z = (float)segment.source.position.z;

    sTp_local.x = (float)segment.target.position.x;
    sTp_local.y = (float)segment.target.position.y;
    sTp_local.z = (float)segment.target.position.z;

    sSr_local = (float)segment.source.radius;
    sTr_local = (float)segment.target.radius;

    //n contains the normal vectors of the 3 orthogonal planes
    float n[3][3] = {{1.,0.,0.},
                    {0.,1.,0.},
                    {0.,0.,1.}};

    distToCurrPos = ((state->M / 2) + 1) + 1 * state->cubeEdgeLength;

    //Check if there is an intersection between the given segment and one
    //of the slice planes.
    p[0][0] = sSp_local.x - (float)state->viewerState->currentPosition.x;
    p[0][1] = sSp_local.y - (float)state->viewerState->currentPosition.y;
    p[0][2] = sSp_local.z - (float)state->viewerState->currentPosition.z;

    p[1][0] = sTp_local.x - (float)state->viewerState->currentPosition.x;
    p[1][1] = sTp_local.y - (float)state->viewerState->currentPosition.y;
    p[1][2] = sTp_local.z - (float)state->viewerState->currentPosition.z;


    //i represents the current orthogonal plane
    for(i = 0; i<=2; i++) {
        //There is an intersection and the segment doesn't lie in the plane
        if(sgn(p[0][i])*sgn(p[1][i]) == -1) {
            //Calculate intersection point
            segDir.x = sTp_local.x - sSp_local.x;
            segDir.y = sTp_local.y - sSp_local.y;
            segDir.z = sTp_local.z - sSp_local.z;

            //a is the scaling factor for the straight line equation: g:=segDir*a+v0
            a = (n[i][0] * (((float)state->viewerState->currentPosition.x - sSp_local.x))
                    + n[i][1] * (((float)state->viewerState->currentPosition.y - sSp_local.y))
                    + n[i][2] * (((float)state->viewerState->currentPosition.z - sSp_local.z)))
                / (segDir.x*n[i][0] + segDir.y*n[i][1] + segDir.z*n[i][2]);

            tempVec3.x = segDir.x * a;
            tempVec3.y = segDir.y * a;
            tempVec3.z = segDir.z * a;

            intPoint.x = tempVec3.x + sSp_local.x;
            intPoint.y = tempVec3.y + sSp_local.y;
            intPoint.z = tempVec3.z + sSp_local.z;

            //Check wether the intersection point lies outside the current zoom cube
            if(abs((int)intPoint.x - state->viewerState->currentPosition.x) <= distToCurrPos
                && abs((int)intPoint.y - state->viewerState->currentPosition.y) <= distToCurrPos
                && abs((int)intPoint.z - state->viewerState->currentPosition.z) <= distToCurrPos) {

                //Render a cylinder to highlight the intersection
                glPushMatrix();
                gluCylObj = gluNewQuadric();
                gluQuadricNormals(gluCylObj, GLU_SMOOTH);
                gluQuadricOrientation(gluCylObj, GLU_OUTSIDE);

                length = segDir.length();
                distSourceInter = tempVec3.length();

                if(sSr_local < sTr_local)
                    radius = sTr_local + sSr_local * (1. - distSourceInter / length);
                else if(sSr_local == sTr_local)
                    radius = sSr_local;
                else
                    radius = sSr_local - (sSr_local - sTr_local) * distSourceInter / length;

                segDir = {segDir.x / length, segDir.y / length, segDir.z / length / state->viewerState->voxelXYtoZRatio};

                glTranslatef(intPoint.x, intPoint.y, intPoint.z);
                glScalef(1.f, 1.f, state->viewerState->voxelXYtoZRatio);

                //Some calculations for the correct direction of the cylinder.
                tempVec = {0., 0., 1.};
                //temVec2 defines the rotation axis
                tempVec2 = tempVec.cross(segDir);
                currentAngle = radToDeg(tempVec.angleRad(segDir));
                //we need another reference vector for 180° rotations
                if (tempVec2 == floatCoordinate{0, 0, 0}) {
                    tempVec2 = {floatCoordinate(0.0f, 1.0f, 0.0f).cross(segDir)};
                }
                glRotatef(currentAngle, tempVec2.x, tempVec2.y, tempVec2.z);

                glColor4f(0.,0.,0.,1.);

                if(state->viewerState->overrideNodeRadiusBool)
                    gluCylinder(gluCylObj,
                        state->viewerState->overrideNodeRadiusVal * state->viewerState->segRadiusToNodeRadius*1.2,
                        state->viewerState->overrideNodeRadiusVal * state->viewerState->segRadiusToNodeRadius*1.2,
                        1.5, 3, 1);

                else gluCylinder(gluCylObj,
                        radius * state->viewerState->segRadiusToNodeRadius*1.2,
                        radius * state->viewerState->segRadiusToNodeRadius*1.2,
                        1.5, 3, 1);

                gluDeleteQuadric(gluCylObj);
                glPopMatrix();
            }

        }
    }
    return true;
}

void ViewportBase::setFrontFacePerspective() {
    if(state->viewerState->selectModeFlag == false) { // messes up pick matrix
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
    }
    /* define coordinate system for our viewport: left right bottom top near far */
    glOrtho(0, edgeLength, edgeLength, 0, 25, -25);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void ViewportBase::renderViewportFrontFace() {
    setFrontFacePerspective();
    // render node selection box
    if (state->viewerState->nodeSelectSquareData.first == static_cast<int>(viewportType)) {
        Coordinate leftUpper = state->viewerState->nodeSelectionSquare.first;
        Coordinate rightLower = state->viewerState->nodeSelectionSquare.second;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glLineWidth(1.);
        glBegin(GL_QUADS);
        glColor4f(0, 1., 0, 0.2);
            glVertex3f(leftUpper.x, leftUpper.y, 0.f);
            glVertex3f(leftUpper.x, rightLower.y, 0.f);
            glVertex3f(rightLower.x, rightLower.y, 0.f);
            glVertex3f(rightLower.x, leftUpper.y, 0.f);
        glEnd();
        glBegin(GL_LINE_LOOP);
        glColor4f(0, 1., 0, 1);
            glVertex3f(leftUpper.x, leftUpper.y, 0.f);
            glVertex3f(leftUpper.x, rightLower.y, 0.f);
            glVertex3f(rightLower.x, rightLower.y, 0.f);
            glVertex3f(rightLower.x, leftUpper.y, 0.f);
        glEnd();
        glDisable(GL_BLEND);
    }
}

void ViewportOrtho::renderViewportFrontFace() {
    ViewportBase::renderViewportFrontFace();
    switch(viewportType) {
    case VIEWPORT_XY:
        glColor4f(0.7, 0., 0., 1.);
        break;
    case VIEWPORT_XZ:
        glColor4f(0., 0.7, 0., 1.);
        break;
    case VIEWPORT_ZY:
        glColor4f(0., 0., 0.7, 1.);
        break;
    default:
        glColor4f(n.z, n.y, n.x, 1.);
        break;
    }
    glLineWidth(2.);
    glBegin(GL_LINES);
        glVertex3d(1, 1, -1);
        glVertex3d(edgeLength - 1, 1, -1);
        glVertex3d(edgeLength - 1, 1, -1);
        glVertex3d(edgeLength - 1, edgeLength - 1, -1);
        glVertex3d(edgeLength - 1, edgeLength - 1, -1);
        glVertex3d(1, edgeLength - 1, -1);
        glVertex3d(1, edgeLength - 1, -1);
        glVertex3d(1, 1, -1);
    glEnd();

    if (viewportType == state->viewerState->highlightVp) {
        // Draw an orange border to highlight the viewport.
        glColor4f(1., 0.3, 0., 1.);
        glBegin(GL_LINE_LOOP);
            glVertex3f(3, 3, -1);
            glVertex3f(edgeLength - 3, 3, -1);
            glVertex3f(edgeLength - 3, edgeLength - 3, -1);
            glVertex3f(3, edgeLength - 3, -1);
        glEnd();
    }

    if (state->viewerState->showScalebar) {
        renderScaleBar();
    }
}

void Viewport3D::renderViewportFrontFace() {
    ViewportBase::renderViewportFrontFace();
    glColor4f(0, 0, 0, 1.);
    glLineWidth(2.);
    glBegin(GL_LINES);
        glVertex3d(1, 1, -1);
        glVertex3d(edgeLength - 1, 1, -1);
        glVertex3d(edgeLength - 1, 1, -1);
        glVertex3d(edgeLength - 1, edgeLength - 1, -1);
        glVertex3d(edgeLength - 1, edgeLength - 1, -1);
        glVertex3d(1, edgeLength - 1, -1);
        glVertex3d(1, edgeLength - 1, -1);
        glVertex3d(1, 1, -1);
    glEnd();
    if (Segmentation::singleton().volume_render_toggle == false && state->viewerState->showScalebar) {
        renderScaleBar();
    }
}

void ViewportBase::renderScaleBar() {
    if (state->viewerState->selectModeFlag) {
        glLoadName(GLNames::Scalebar);
    }
    const auto vp_edgelen_um = 0.001 * displayedlengthInNmX;
    auto rounded_scalebar_len_um = std::round(vp_edgelen_um/3 * 2) / 2; // round to next 0.5
    auto sizeLabel = QString("%1 µm").arg(rounded_scalebar_len_um);
    auto divisor = vp_edgelen_um / rounded_scalebar_len_um; // for scalebar size in pixels

    if(rounded_scalebar_len_um == 0) {
        const auto rounded_scalebar_len_nm = std::round(displayedlengthInNmX/3/5)*5; // switch to nanometers rounded to next multiple of 5
        sizeLabel = QString("%1 nm").arg(rounded_scalebar_len_nm);
        divisor = displayedlengthInNmX/rounded_scalebar_len_nm;
    }
    int min_x = 0.02 * edgeLength, max_x = min_x + edgeLength / divisor, min_y = edgeLength - min_x - 2, max_y = min_y + 2, z = -1;
    glColor3f(0., 0., 0.);
    glBegin(GL_POLYGON);
    glVertex3d(min_x, min_y, z);
    glVertex3d(max_x, min_y, z);
    glVertex3d(max_x, max_y, z);
    glVertex3d(min_x, max_y, z);
    glEnd();

    renderText(Coordinate(min_x + edgeLength / divisor / 2, min_y, z), sizeLabel, true, true);

    if (state->viewerState->selectModeFlag) {
        glLoadName(GLNames::None);
    }
}

void ViewportOrtho::renderViewportFast() {
    if (state->viewer->layers.empty()) {
        return;
    }

    QOpenGLTimeMonitor times;
    times.setSampleCount(3);
    times.create();
    times.recordSample();

    glClearColor(1, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const bool xy = viewportType == VIEWPORT_XY;
    const bool xz = viewportType == VIEWPORT_XZ;
    const bool zy = viewportType == VIEWPORT_ZY;
    const bool arb = viewportType == VIEWPORT_ARBITRARY;
    const float gpucubeedge = state->viewer->gpucubeedge;
    const auto fov = (state->M - 1) * state->cubeEdgeLength / (arb ? std::sqrt(2) : 1);//remove cpu overlap
    const auto gpusupercube = fov / gpucubeedge + 1;//add gpu overlap
    floatCoordinate cpos = state->viewerState->currentPosition;
    const auto scale = state->scale.z / state->scale.x;
    if (arb) {
        cpos.z *= scale;
    }

    std::vector<std::array<GLfloat, 3>> triangleVertices;
    triangleVertices.reserve(6);
    triangleVertices.push_back({{0.0f, 0.0f, 0.0f}});
    triangleVertices.push_back({{gpucubeedge, 0.0f, 0.0f}});
    triangleVertices.push_back({{gpucubeedge, gpucubeedge, 0.0f}});
    triangleVertices.push_back({{0.0f, gpucubeedge, 0.0f}});
    std::vector<std::array<GLfloat, 3>> textureVertices;
    textureVertices.reserve(6);
    for (float z = 0.0f; z < (xy ? 1 : gpusupercube); ++z)
    for (float y = 0.0f; y < (xz ? 1 : gpusupercube); ++y)
    for (float x = 0.0f; x < (zy ? 1 : gpusupercube); ++x) {
        const float frame = std::fmod(xy ? cpos.z : xz ? cpos.y : cpos.x, state->viewer->gpucubeedge);
        const auto texR = (0.5f + frame) / gpucubeedge;

        if (xy) {
            textureVertices.push_back({{0.0f, 0.0f, texR}});
            textureVertices.push_back({{1.0f, 0.0f, texR}});
            textureVertices.push_back({{1.0f, 1.0f, texR}});
            textureVertices.push_back({{0.0f, 1.0f, texR}});
        } else if (xz) {
            textureVertices.push_back({{0.0f, texR, 0.0f}});
            textureVertices.push_back({{1.0f, texR, 0.0f}});
            textureVertices.push_back({{1.0f, texR, 1.0f}});
            textureVertices.push_back({{0.0f, texR, 1.0f}});
        } else if (zy) {
            textureVertices.push_back({{texR, 0.0f, 0.0f}});
            textureVertices.push_back({{texR, 0.0f, 1.0f}});
            textureVertices.push_back({{texR, 1.0f, 1.0f}});
            textureVertices.push_back({{texR, 1.0f, 0.0f}});
        }
    }

    QMatrix4x4 viewMatrix;
    QMatrix4x4 projectionMatrix;
    projectionMatrix.ortho(-0.5 * width(), 0.5 * width(), -0.5 * height(), 0.5 * height(), -scale * gpucubeedge, scale * gpucubeedge);

    //z component of vp vectors specifies portion of scale to apply
    const auto zScaleIncrement = !arb ? scale - 1 : 0;
    const float hfov = texture.FOV * fov / (1 + zScaleIncrement * std::abs(v1.z));
    const float vfov = texture.FOV * fov / (1 + zScaleIncrement * std::abs(v2.z));
    viewMatrix.scale(width() / hfov, height() / vfov);
    viewMatrix.scale(1, -1);//invert y because whe want our origin in the top right corner
    viewMatrix.scale(xz || zy ? -1 : 1, 1);//HACK idk
    const auto cameraPos = floatCoordinate{cpos} + n;
    viewMatrix.lookAt(cameraPos, cpos, v2);

    // raw data shader
    raw_data_shader.bind();
    int vertexLocation = raw_data_shader.attributeLocation("vertex");
    int texLocation = raw_data_shader.attributeLocation("texCoordVertex");
    raw_data_shader.enableAttributeArray(vertexLocation);
    raw_data_shader.enableAttributeArray(texLocation);
    raw_data_shader.setAttributeArray(vertexLocation, triangleVertices.data()->data(), 3);
    raw_data_shader.setAttributeArray(texLocation, textureVertices.data()->data(), 3);
    raw_data_shader.setUniformValue("view_matrix", viewMatrix);
    raw_data_shader.setUniformValue("projection_matrix", projectionMatrix);
    raw_data_shader.setUniformValue("texture", 0);

    // overlay data shader
    overlay_data_shader.bind();
    int overtexLocation = overlay_data_shader.attributeLocation("vertex");
    int otexLocation = overlay_data_shader.attributeLocation("texCoordVertex");
    overlay_data_shader.enableAttributeArray(overtexLocation);
    overlay_data_shader.enableAttributeArray(otexLocation);
    overlay_data_shader.setAttributeArray(overtexLocation, triangleVertices.data()->data(), triangleVertices.data()->size());
    overlay_data_shader.setAttributeArray(otexLocation, textureVertices.data()->data(), textureVertices.data()->size());
    overlay_data_shader.setUniformValue("view_matrix", viewMatrix);
    overlay_data_shader.setUniformValue("projection_matrix", projectionMatrix);
    overlay_data_shader.setUniformValue("indexTexture", 0);
    overlay_data_shader.setUniformValue("textureLUT", 1);
    overlay_data_shader.setUniformValue("factor", static_cast<float>(std::numeric_limits<gpu_lut_cube::gpu_index>::max()));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_3D);

    for (auto & layer : state->viewer->layers) {
        if (layer.enabled && layer.opacity >= 0.0f) {
            if (layer.isOverlayData) {
                overlay_data_shader.bind();
                overlay_data_shader.setUniformValue("textureOpacity", Segmentation::singleton().alpha / 256.0f);
            } else {
                raw_data_shader.bind();
                raw_data_shader.setUniformValue("textureOpacity", layer.opacity);
            }

            auto render = [&](auto & cube, const QMatrix4x4 modelMatrix = {}){
                if (layer.isOverlayData) {
                    auto & punned = static_cast<gpu_lut_cube&>(cube);
                    punned.cube.bind(0);
                    punned.lut.bind(1);
                    overlay_data_shader.setUniformValue("model_matrix", modelMatrix);
                    overlay_data_shader.setUniformValue("lutSize", static_cast<float>(punned.lut.width() * punned.lut.height() * punned.lut.depth()));
                } else {
                    raw_data_shader.setUniformValue("model_matrix", modelMatrix);
                    cube.cube.bind(0);
                }
                glDrawArrays(GL_TRIANGLE_FAN, 0, static_cast<int>(triangleVertices.size()));
            };
            if (!arb) {
                const float halfsc = fov * 0.5f / gpucubeedge;
                const float offsetx = cpos.x / gpucubeedge - halfsc * !zy;
                const float offsety = cpos.y / gpucubeedge - halfsc * !xz;
                const float offsetz = cpos.z / gpucubeedge - halfsc * !xy;
                const float startx = 0 * cpos.x / gpucubeedge;
                const float starty = 0 * cpos.y / gpucubeedge;
                const float startz = 0 * cpos.z / gpucubeedge;
                const float endx = startx + (zy ? 1 : gpusupercube);
                const float endy = starty + (xz ? 1 : gpusupercube);
                const float endz = startz + (xy ? 1 : gpusupercube);
                for (float z = startz; z < endz; ++z)
                for (float y = starty; y < endy; ++y)
                for (float x = startx; x < endx; ++x) {
                    const auto pos = CoordOfGPUCube(offsetx + x, offsety + y, offsetz + z);
                    auto it = layer.textures.find(pos);
                    auto & ptr = it != std::end(layer.textures) ? *it->second : *layer.bogusCube;

                    QMatrix4x4 modelMatrix;
                    modelMatrix.translate(pos.x * gpucubeedge, pos.y * gpucubeedge, pos.z * gpucubeedge);
                    modelMatrix.rotate(QQuaternion::fromAxes(v1, v2, -n));//HACK idk why the normal has to be negative

                    render(ptr, modelMatrix);
                }
            } else {
                for (auto & pair : layer.textures) {
                    auto & pos = pair.first;
                    auto & cube = *pair.second;
                    if (!cube.vertices.empty()) {
                        triangleVertices.clear();
                        textureVertices.clear();
                        for (const auto & vertex : cube.vertices) {
                            triangleVertices.push_back({{vertex.x, vertex.y, vertex.z * scale}});
                            const auto depthOffset = static_cast<float>(vertex.z - pos.z * gpucubeedge);
                            const auto texR = (0.5f + depthOffset) / gpucubeedge;
                            textureVertices.push_back({{static_cast<float>(vertex.x - pos.x * gpucubeedge) / gpucubeedge
                                                        , static_cast<float>(vertex.y - pos.y * gpucubeedge) / gpucubeedge
                                                        , texR}});
                        }
                        render(cube);
                    }
                }
            }
        }
    }

    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_TEXTURE_3D);

    raw_data_shader.disableAttributeArray(vertexLocation);
    raw_data_shader.disableAttributeArray(texLocation);
    raw_data_shader.release();
    overlay_data_shader.disableAttributeArray(overtexLocation);
    overlay_data_shader.disableAttributeArray(otexLocation);
    overlay_data_shader.release();

    times.recordSample();

//    qDebug() << "render time: " << times.waitForIntervals();
}

void ViewportOrtho::renderViewport(const RenderOptions &options) {
    if(state->viewerState->selectModeFlag) {
        glDisable(GL_DEPTH_TEST);
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

    if(!state->viewerState->selectModeFlag) {
        if(state->viewerState->multisamplingOnOff) {
            glEnable(GL_MULTISAMPLE);
        }
    }

    /* Multiplying by state->magnification increases the area covered
     * by the textured OpenGL quad for downsampled datasets. */
    float dataPxX = texture.displayedEdgeLengthX / texture.texUnitsPerDataPx * 0.5;
    float dataPxY = texture.displayedEdgeLengthY / texture.texUnitsPerDataPx * 0.5;

    const bool xy = viewportType == VIEWPORT_XY;
    const bool xz = viewportType == VIEWPORT_XZ;
    const bool zy = viewportType == VIEWPORT_ZY;
    const bool arb = viewportType == VIEWPORT_ARBITRARY;
    if (!arb) {
        if (!state->viewerState->selectModeFlag) {
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
        }

        auto view = [&](){
            gluLookAt(state->viewerState->currentPosition.x, state->viewerState->currentPosition.y, state->viewerState->currentPosition.z
                    , state->viewerState->currentPosition.x - zy, state->viewerState->currentPosition.y - xz , state->viewerState->currentPosition.z + xy
                    , 0, -(xy || zy), -xz);
        };

        if (zy) {
            glOrtho(-dataPxY, +dataPxY, -dataPxX, +dataPxX, -state->viewerState->depthCutOff, +state->viewerState->depthCutOff);
        } else {
            glOrtho(-dataPxX, +dataPxX, -dataPxY, +dataPxY, -state->viewerState->depthCutOff, +state->viewerState->depthCutOff);
        }

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        view();

        updateFrustumClippingPlanes();

        glTranslatef(state->viewerState->currentPosition.x, state->viewerState->currentPosition.y, state->viewerState->currentPosition.z);
        glRotatef(180, 1, 0, 0);//OGL to K origin
        if (xz) {
            glRotatef(90, 1, 0, 0);
        } else if (zy) {
            glRotatef(90, 0, 1, 0);
        }

        auto swapZY = [&]() {//TODO fix offsets everywhere
            if (zy) {
                std::swap(dataPxX, dataPxY);
            }
            std::swap(state->viewer->window->viewportOrtho(VIEWPORT_ZY)->texture.texRUx, state->viewer->window->viewportOrtho(VIEWPORT_ZY)->texture.texLLx);
            std::swap(state->viewer->window->viewportOrtho(VIEWPORT_ZY)->texture.texRUy, state->viewer->window->viewportOrtho(VIEWPORT_ZY)->texture.texLLy);
        };

        glDisable(GL_DEPTH_TEST);//render first raw slice below everything

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glColor4f(1, 1, 1, 1);

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture.texHandle);

        swapZY();

        auto slice = [&](){
            glBegin(GL_QUADS);
                glNormal3i(0, 0, 1);
                glTexCoord2f(texture.texLUx, texture.texLUy);
                glVertex3f(-dataPxX, dataPxY, 0);
                glTexCoord2f(texture.texRUx, texture.texRUy);
                glVertex3f(dataPxX, dataPxY, 0);
                glTexCoord2f(texture.texRLx, texture.texRLy);
                glVertex3f(dataPxX, -dataPxY, 0);
                glTexCoord2f(texture.texLLx, texture.texLLy);
                glVertex3f(-dataPxX, -dataPxY, 0);
            glEnd();
        };

        slice();

        glBindTexture (GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);

        swapZY();

        if (options.drawSkeleton && state->viewerState->skeletonDisplay.testFlag(SkeletonDisplay::ShowInOrthoVPs)) {
            glPushMatrix();
            glLoadIdentity();
            view();

            glTranslatef((xy || xz) * 0.5, (xy || zy) * 0.5, (xz || zy) * 0.5);//arrange to pixel center
            renderSkeleton(options);

            glPopMatrix();
        }

        swapZY();

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glColor4f(1, 1, 1, 0.6);//second raw slice is semi transparent, with one direction of the skeleton showing through and the other above rendered above

        glEnable(GL_TEXTURE_2D);

        glBindTexture(GL_TEXTURE_2D, texture.texHandle);
        slice();

        /* Draw the overlay textures */
        if(options.drawOverlay) {
            glBindTexture(GL_TEXTURE_2D, texture.overlayHandle);
            slice();
        }

        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_DEPTH_TEST);
        if (options.drawCrosshairs) {
            glLineWidth(1);
            glBegin(GL_LINES);
                glColor4f(xz || zy, xy, 0, 0.3);
                glVertex3f(-dataPxX, -0.5, 0);//why negative 0.5 here?
                glVertex3f( dataPxX, -0.5, 0);

                glColor4f(0, zy, xy || xz , 0.3);
                glVertex3f(0.5, -dataPxY, 0);
                glVertex3f(0.5,  dataPxY, 0);
            glEnd();
        }

        swapZY();

        if (Session::singleton().annotationMode.testFlag(AnnotationMode::Brush)) {
            glPushMatrix();
            glLoadIdentity();
            view();

            renderBrush(getMouseCoordinate());

            glPopMatrix();
        }

        glDepthFunc(GL_LESS);//reset depth func to default value
    } else {
        if(!state->viewerState->selectModeFlag) {
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
        }
        // left, right, bottom, top, near, far clipping planes
        glOrtho(-((float)(state->boundary.x)/ 2.) + (float)state->viewerState->currentPosition.x - dataPxX,
            -((float)(state->boundary.x) / 2.) + (float)state->viewerState->currentPosition.x + dataPxX,
            -((float)(state->boundary.y) / 2.) + (float)state->viewerState->currentPosition.y - dataPxY,
            -((float)(state->boundary.y) / 2.) + (float)state->viewerState->currentPosition.y + dataPxY,
            ((float)(state->boundary.z) / 2.) - state->viewerState->depthCutOff - (float)state->viewerState->currentPosition.z,
            ((float)(state->boundary.z) / 2.) + state->viewerState->depthCutOff - (float)state->viewerState->currentPosition.z);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        // optimize that! TDitem

        glTranslatef(-((float)state->boundary.x / 2.),
                    -((float)state->boundary.y / 2.),
                    -((float)state->boundary.z / 2.));

        updateFrustumClippingPlanes();

        glTranslatef((float)state->viewerState->currentPosition.x,
                    (float)state->viewerState->currentPosition.y,
                    (float)state->viewerState->currentPosition.z);

        glRotatef(180., 1.,0.,0.);
        QVector3D vec1 = {1, 0, 0};
        auto normQuat = QQuaternion::rotationTo({0, 0, 1}, {n.x, n.y, n.z});
        float angle;
        QVector3D axis;
        normQuat.getAxisAndAngle(&axis, &angle);
        glRotatef(-angle, axis.x(), axis.y(), axis.z());
        vec1 = normQuat.rotatedVector(vec1);
        normQuat = QQuaternion::rotationTo(vec1, {v1.x, v1.y, v1.z});
        normQuat.getAxisAndAngle(&axis, &angle);
        glRotatef(-angle, axis.x(), axis.y(), axis.z());

        glEnable(GL_TEXTURE_2D);
        glDisable(GL_DEPTH_TEST);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glColor4f(1., 1., 1., 1.);

        glBindTexture(GL_TEXTURE_2D, texture.texHandle);
        glBegin(GL_QUADS);
            glNormal3i(n.x, n.y, n.z);
            glTexCoord2f(texture.texLUx, texture.texLUy);
            glVertex3f(-dataPxX * v1.x - dataPxY * v2.x, -dataPxX * v1.y - dataPxY * v2.y, -dataPxX * v1.z - dataPxY * v2.z);
            glTexCoord2f(texture.texRUx, texture.texRUy);
            glVertex3f(dataPxX * v1.x - dataPxY * v2.x, dataPxX * v1.y - dataPxY * v2.y, dataPxX * v1.z - dataPxY * v2.z);
            glTexCoord2f(texture.texRLx, texture.texRLy);
            glVertex3f(dataPxX * v1.x + dataPxY * v2.x, dataPxX * v1.y + dataPxY * v2.y, dataPxX * v1.z + dataPxY * v2.z);
            glTexCoord2f(texture.texLLx, texture.texLLy);
            glVertex3f(-dataPxX * v1.x + dataPxY * v2.x, -dataPxX * v1.y + dataPxY * v2.y, -dataPxX * v1.z + dataPxY * v2.z);
        glEnd();
        glBindTexture (GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_DEPTH_TEST);

        if (options.drawSkeleton && state->viewerState->skeletonDisplay.testFlag(SkeletonDisplay::ShowInOrthoVPs)) {
            glPushMatrix();
            glTranslatef(-state->viewerState->currentPosition.x, -state->viewerState->currentPosition.y, -state->viewerState->currentPosition.z);
            glTranslatef(0.5, 0.5, 0.5);//arrange to pixel center, this is never correct, TODO angle adjustments
            renderSkeleton(options);
            glPopMatrix();
        }

        glEnable(GL_TEXTURE_2D);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glColor4f(1., 1., 1., 0.6);

        glBindTexture(GL_TEXTURE_2D, texture.texHandle);
        glBegin(GL_QUADS);
            glNormal3i(n.x, n.y, n.z);
            const auto offset = n;
            glTexCoord2f(texture.texLUx, texture.texLUy);
            glVertex3f(-dataPxX * v1.x - dataPxY * v2.x + offset.x, -dataPxX * v1.y - dataPxY * v2.y + offset.y, -dataPxX * v1.z - dataPxY * v2.z + offset.z);
            glTexCoord2f(texture.texRUx, texture.texRUy);
            glVertex3f(dataPxX * v1.x - dataPxY * v2.x + offset.x, dataPxX * v1.y - dataPxY * v2.y + offset.y, dataPxX * v1.z - dataPxY * v2.z + offset.z);
            glTexCoord2f(texture.texRLx, texture.texRLy);

            glVertex3f(dataPxX * v1.x + dataPxY * v2.x + offset.x, dataPxX * v1.y + dataPxY * v2.y + offset.y, dataPxX * v1.z + dataPxY * v2.z + offset.z);
            glTexCoord2f(texture.texLLx, texture.texLLy);

            glVertex3f(-dataPxX * v1.x + dataPxY * v2.x + offset.x, -dataPxX * v1.y + dataPxY * v2.y + offset.y, -dataPxX * v1.z + dataPxY * v2.z + offset.z);
        glEnd();

        // Draw the overlay textures
        if(options.drawOverlay) {
            glBindTexture(GL_TEXTURE_2D, texture.overlayHandle);
            glBegin(GL_QUADS);
                glNormal3i(n.x, n.y, n.z);
                const auto offset = n * -0.1;
                glTexCoord2f(texture.texLUx, texture.texLUy);
                glVertex3f(-dataPxX * v1.x - dataPxY * v2.x + offset.x,
                           -dataPxX * v1.y - dataPxY * v2.y + offset.y,
                           -dataPxX * v1.z - dataPxY * v2.z + offset.z);
                glTexCoord2f(texture.texRUx, texture.texRUy);
                glVertex3f(dataPxX * v1.x - dataPxY * v2.x + offset.x,
                           dataPxX * v1.y - dataPxY * v2.y + offset.y,
                           dataPxX * v1.z - dataPxY * v2.z + offset.z);
                glTexCoord2f(texture.texRLx, texture.texRLy);
                glVertex3f(dataPxX * v1.x + dataPxY * v2.x + offset.x,
                           dataPxX * v1.y + dataPxY * v2.y + offset.y,
                           dataPxX * v1.z + dataPxY * v2.z + offset.z);
                glTexCoord2f(texture.texLLx, texture.texLLy);
                glVertex3f(-dataPxX * v1.x + dataPxY * v2.x + offset.x,
                           -dataPxX * v1.y + dataPxY * v2.y + offset.y,
                           -dataPxX * v1.z + dataPxY * v2.z + offset.z);
            glEnd();
        }

        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_DEPTH_TEST);
        if (options.drawCrosshairs) {
            glLineWidth(1.);
            glBegin(GL_LINES);
                glColor4f(v2.z, v2.y, v2.x, 0.3);
                glVertex3f(-dataPxX * v1.x + 0.5 * v2.x - 0.0001 * n.x,
                           -dataPxX * v1.y + 0.5 * v2.y - 0.0001 * n.y,
                           -dataPxX * v1.z + 0.5 * v2.z - 0.0001 * n.z);

                glVertex3f(dataPxX * v1.x + 0.5 * v2.x - 0.0001 * n.x,
                           dataPxX * v1.y + 0.5 * v2.y - 0.0001 * n.y,
                           dataPxX * v1.z + 0.5 * v2.z - 0.0001 * n.z);

                glColor4f(v1.z, v1.y, v1.x, 0.3);
                glVertex3f(0.5 * v1.x - dataPxY * v2.x - 0.0001 * n.x,
                           0.5 * v1.y - dataPxY * v2.y - 0.0001 * n.y,
                           0.5 * v1.z - dataPxY * v2.z - 0.0001 * n.z);

                glVertex3f(0.5 * v1.x + dataPxY * v2.x - 0.0001 * n.x,
                           0.5 * v1.y + dataPxY * v2.y - 0.0001 * n.y,
                           0.5 * v1.z + dataPxY * v2.z - 0.0001 * n.z);
            glEnd();
        }
    }

    glDisable(GL_BLEND);
}

bool Viewport3D::renderVolumeVP() {
    auto& seg = Segmentation::singleton();

    std::array<double, 3> background_color;
    seg.volume_background_color.getRgbF(&background_color[0], &background_color[1], &background_color[2]);
    glClearColor(background_color[0], background_color[1], background_color[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if(seg.volume_tex_id != 0) {
        static float volumeClippingAdjust = 1.73f;
        static float translationSpeedAdjust = 1.0 / 500.0f;
        auto cubeLen = state->cubeEdgeLength;
        int texLen = seg.volume_tex_len;
        GLuint volTexId = seg.volume_tex_id;

        static Profiler render_profiler;

        render_profiler.start(); // ----------------------------------------------------------- profiling

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(-1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f);

        // volume viewport rotation
        static QMatrix4x4 volRotMatrix;
        float rotdx = state->skeletonState->rotdx;
        float rotdy = state->skeletonState->rotdy;
        state->skeletonState->rotdx = 0;
        state->skeletonState->rotdy = 0;

        if(rotdx || rotdy) {
            QVector3D xRotAxis{0.0f, 1.0f, 0.0f};
            QVector3D yRotAxis{1.0f, 0.0f, 0.0f};

            volRotMatrix.rotate(-rotdx, xRotAxis);
            volRotMatrix.rotate( rotdy, yRotAxis);
        }

        // volume viewport translation
        static float transx = 0.0f;
        static float transy = 0.0f;
        transx += seg.volume_mouse_move_x * translationSpeedAdjust;
        transy += seg.volume_mouse_move_y * translationSpeedAdjust;
        seg.volume_mouse_move_x = 0;
        seg.volume_mouse_move_y = 0;

        // volume viewport zoom
        static float zoom = seg.volume_mouse_zoom;
        zoom = seg.volume_mouse_zoom;

        // dataset scaling adjustment
        auto datascale = state->scale;
        float biggestScale = 0.0f;
        if(datascale.x > datascale.y) {
            biggestScale = datascale.x;
        } else {
            biggestScale = datascale.y;
        }
        if(datascale.z > biggestScale) {
            biggestScale = datascale.z;
        }

        float smallestScale = 0.0f;
        if(datascale.x < datascale.y) {
            smallestScale = datascale.x;
        } else {
            smallestScale = datascale.y;
        }
        if(datascale.z < smallestScale) {
            smallestScale = datascale.z;
        }
        float maxScaleRatio = biggestScale / smallestScale;
        float scalex = 1.0f / (datascale.x / biggestScale);
        float scaley = 1.0f / (datascale.y / biggestScale);
        float scalez = 1.0f / (datascale.z / biggestScale);

        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();

        // dataset translation adjustment
        glTranslatef((static_cast<float>(state->viewerState->currentPosition.x % cubeLen) / cubeLen - 0.5f) / state->M,
                     (static_cast<float>(state->viewerState->currentPosition.y % cubeLen) / cubeLen - 0.5f) / state->M,
                     (static_cast<float>(state->viewerState->currentPosition.z % cubeLen) / cubeLen - 0.5f) / state->M);

        glTranslatef(0.5f, 0.5f, 0.5f);
        glScalef(volumeClippingAdjust, volumeClippingAdjust, volumeClippingAdjust); // scale to remove cube corner clipping
        glScalef(scalex, scaley, scalez); // dataset scaling adjustment
        glMultMatrixf(volRotMatrix.data()); // volume viewport rotation
        glScalef(1.0f/zoom, 1.0f/zoom, 1.0f/zoom*2.0f); // volume viewport zoom
        glTranslatef(-0.5f, -0.5f, -0.5f);
        glTranslatef(transx, transy, 0.0f); // volume viewport translation

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_TEXTURE_3D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glBindTexture(GL_TEXTURE_3D, volTexId);
        float volume_opacity = seg.volume_opacity / 255.0f;
        for(int i = 0; i < texLen * volumeClippingAdjust * maxScaleRatio; ++i) {
            float depth = i/(texLen * volumeClippingAdjust * maxScaleRatio);
            glColor4f(depth, depth, depth, volume_opacity);
            glBegin(GL_QUADS);
                glTexCoord3f(0.0f, 1.0f, depth);
                glVertex3f(-1.0f, -1.0f,  1.0f-depth*2.0f);
                glTexCoord3f(1.0f, 1.0f, depth);
                glVertex3f( 1.0f, -1.0f,  1.0f-depth*2.0f);
                glTexCoord3f(1.0f, 0.0f, depth);
                glVertex3f( 1.0f,  1.0f,  1.0f-depth*2.0f);
                glTexCoord3f(0.0f, 0.0f, depth);
                glVertex3f(-1.0f,  1.0f,  1.0f-depth*2.0f);
            glEnd();
        }

        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);

        // Reset previously changed OGL parameters
        glDisable(GL_TEXTURE_3D);
        glEnable(GL_TEXTURE_2D);
        glDisable(GL_BLEND);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDisable(GL_LIGHTING);
        glDisable(GL_DEPTH_TEST);

        render_profiler.end(); // ----------------------------------------------------------- profiling

        // --------------------- display some profiling information ------------------------
        // static auto timer = std::chrono::steady_clock::now();
        // std::chrono::duration<double> duration = std::chrono::steady_clock::now() - timer;
        // if(duration.count() > 1.0) {
        //     qDebug() << "render  avg time: " <<  render_profiler.average_time()*1000 << "ms";
        //     qDebug() << "---------------------------------------------";

        //     timer = std::chrono::steady_clock::now();
        // }
    }

    return true;
}

void Viewport3D::renderViewport(const RenderOptions &options) {
    auto& seg = Segmentation::singleton();
    if (seg.volume_render_toggle) {
        if(seg.volume_update_required) {
            seg.volume_update_required = false;
            updateVolumeTexture();
        }
        renderVolumeVP();
    } else {
        renderSkeletonVP(options);
    }
}

void Viewport3D::renderPointCloudBuffer(PointCloud & buf) {
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glTranslatef(-state->boundary.x / 2., -state->boundary.y / 2., -state->boundary.z / 2.);//reset to origin of projection
    glTranslatef(0.5, 0.5, 0.5);

    // get modelview and projection matrices
    GLfloat modelview_mat[4][4];
    glGetFloatv(GL_MODELVIEW_MATRIX, &modelview_mat[0][0]);
    GLfloat projection_mat[4][4];
    glGetFloatv(GL_PROJECTION_MATRIX, &projection_mat[0][0]);

    pointcloudShader.bind();
    pointcloudShader.setUniformValue("modelview_matrix", modelview_mat);
    pointcloudShader.setUniformValue("projection_matrix", projection_mat);
    pointcloudShader.setUniformValue("use_tree_color", buf.useTreeColor);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    buf.position_buf.bind();
    int vertexLocation = pointcloudShader.attributeLocation("vertex");
    pointcloudShader.enableAttributeArray(vertexLocation);
    pointcloudShader.setAttributeBuffer(vertexLocation, GL_FLOAT, 0, 3);
    buf.position_buf.release();

    buf.normal_buf.bind();
    int normalLocation = pointcloudShader.attributeLocation("normal");
    pointcloudShader.enableAttributeArray(normalLocation);
    pointcloudShader.setAttributeBuffer(normalLocation, GL_FLOAT, 0, 3);
    buf.normal_buf.release();

    int colorLocation = pointcloudShader.attributeLocation("color");
    buf.color_buf.bind();
    pointcloudShader.enableAttributeArray(colorLocation);
    pointcloudShader.setAttributeBuffer(colorLocation, GL_FLOAT, 0, 4);
    buf.color_buf.release();
    pointcloudShader.setUniformValue("tree_color", buf.correspondingTree->color);

    if(buf.index_count != 0) {
        buf.index_buf.bind();
        glDrawElements(buf.render_mode, buf.index_count, GL_UNSIGNED_INT, 0);
        buf.index_buf.release();
    } else {
        glDrawArrays(buf.render_mode, 0, buf.vertex_count);
    }
    pointcloudShader.disableAttributeArray(colorLocation);
    pointcloudShader.disableAttributeArray(normalLocation);
    pointcloudShader.disableAttributeArray(vertexLocation);

    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);

    pointcloudShader.release();
    glPopMatrix();
}

void Viewport3D::renderPointCloudBufferIds(PointCloud & buf) {
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glTranslatef(-state->boundary.x / 2., -state->boundary.y / 2., -state->boundary.z / 2.);//reset to origin of projection
    glTranslatef(0.5, 0.5, 0.5);

    // get modelview and projection matrices
    GLfloat modelview_mat[4][4];
    glGetFloatv(GL_MODELVIEW_MATRIX, &modelview_mat[0][0]);
    GLfloat projection_mat[4][4];
    glGetFloatv(GL_PROJECTION_MATRIX, &projection_mat[0][0]);

    pointcloudIdShader.bind();
    pointcloudIdShader.setUniformValue("modelview_matrix", modelview_mat);
    pointcloudIdShader.setUniformValue("projection_matrix", projection_mat);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    buf.position_buf.bind();
    int vertexLocation = pointcloudIdShader.attributeLocation("vertex");
    pointcloudIdShader.enableAttributeArray(vertexLocation);
    pointcloudIdShader.setAttributeBuffer(vertexLocation, GL_FLOAT, 0, 3);
    buf.position_buf.release();

    buf.color_buf.bind();
    int colorLocation = pointcloudIdShader.attributeLocation("color");
    pointcloudIdShader.enableAttributeArray(colorLocation);
    pointcloudIdShader.setAttributeBuffer(colorLocation, GL_UNSIGNED_BYTE, 0, 4);
    buf.color_buf.release();

    if(buf.index_count != 0) {
        buf.index_buf.bind();
        glDrawElements(buf.render_mode, buf.index_count, GL_UNSIGNED_INT, 0);
        buf.index_buf.release();
    } else {
        glDrawArrays(buf.render_mode, 0, buf.vertex_count);
    }

    pointcloudIdShader.disableAttributeArray(colorLocation);
    pointcloudIdShader.disableAttributeArray(vertexLocation);

    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);

    pointcloudIdShader.release();
    glPopMatrix();
}

void Viewport3D::renderPointCloud() {
    static bool pointcloud_init = true;
    if(pointcloud_init) {
        pointcloudShader.addShaderFromSourceCode(QOpenGLShader::Vertex, R"shaderSource(
            #version 110
            attribute vec3 vertex;
            attribute vec3 normal;
            attribute vec4 color;

            uniform mat4 modelview_matrix;
            uniform mat4 projection_matrix;

            varying vec4 frag_color;
            varying vec3 frag_normal;
            varying mat4 mvp_matrix;

            void main() {
                mvp_matrix = projection_matrix * modelview_matrix;
                gl_Position = mvp_matrix * vec4(vertex, 1.0);
                frag_color = color;
                frag_normal = normal;
            }
        )shaderSource");

        pointcloudShader.addShaderFromSourceCode(QOpenGLShader::Fragment, R"shaderSource(
            #version 110

            uniform mat4 modelview_matrix;
            uniform mat4 projection_matrix;
            uniform bool use_tree_color;
            uniform vec4 tree_color;

            varying vec4 frag_color;
            varying vec3 frag_normal;
            varying mat4 mvp_matrix;

            void main() {
                vec3 specular_color = vec3(1.0, 1.0, 1.0);
                float specular_exp = 3.0;
                vec3 view_dir = vec3(0.0, 0.0, 1.0);

                // diffuse lighting
                vec3 main_light_dir = normalize((/*modelview_matrix **/ vec4(0.0, 1.0, 0.0, 0.0)).xyz);
                float main_light_power = max(0.0, dot(-main_light_dir, frag_normal));
                vec3 sub_light_dir = vec3(0.0, -1.0, 0.0);
                float sub_light_power = max(0.0, dot(-sub_light_dir, frag_normal));

                // pseudo ambient lighting
                vec3 pseudo_ambient_dir = view_dir;
                float pseudo_ambient_power = pow(abs(max(0.0, dot(pseudo_ambient_dir, frag_normal)) - 1.0), 3.0);

                // specular
                float specular_power = 0.0;
                if (dot(frag_normal, -main_light_dir) >= 0.0) {
                    specular_power = pow(max(0.0, dot(reflect(-main_light_dir, frag_normal), view_dir)), specular_exp);
                }

                vec3 fcolor = use_tree_color ? tree_color.rgb : frag_color.rgb;
                gl_FragColor = vec4((0.1 * fcolor                                 // ambient
                            + 0.9 * fcolor * main_light_power                     // diffuse(main)
                            + 0.4 * vec3(1.0, 1.0, 1.0) * sub_light_power         // diffuse(sub)
                            // + 0.3 * vec3(1.0, 1.0, 1.0) * pseudo_ambient_power // pseudo ambient lighting
                            // + specular_color * specular_power                  // specular
                            ) //* ambient_occlusion_power
                            , 1.0);

                // gl_FragColor = //vec4((frag_normal+1.0)/2.0, 1.0); // display normals
            }
        )shaderSource");

        pointcloudShader.link();
        pointcloud_init = false;
    }

    float screenPxXPerDataPx = (float)edgeLength / (state->skeletonState->volBoundary - 2.f * state->skeletonState->volBoundary * state->skeletonState->zoomLevel);
    float point_size = std::max(screenPxXPerDataPx * 100.0f, 1.0f);
    glPointSize(point_size);

    for(auto & tree : state->skeletonState->trees) {
        if (tree.pointCloud != nullptr && tree.pointCloud->vertex_count > 0) {
            renderPointCloudBuffer(*(tree.pointCloud));
        }
    }
}

uint32_t pointcloudColorToId(std::array<unsigned char, 4> color) {
    return color[0] + (color[1] << 8) + (color[2] << 16) + (color[3] << 24);
}

std::array<unsigned char, 4> pointcloudIdToColor(uint32_t id) {
    return {{static_cast<unsigned char>(id),
             static_cast<unsigned char>(id >> 8),
             static_cast<unsigned char>(id >> 16),
             static_cast<unsigned char>(id >> 24)}};
}

boost::optional<BufferSelection> Viewport3D::pickPointCloud(const QPoint pos) {
    makeCurrent();
    RenderOptions options;
    options.pointCloudPicking = true;
    renderSkeletonVP(options);
    // read color and translate to id
    const auto yinverse = height() - pos.y() - 1;
    std::array<GLubyte, 4> buffer;
    glReadPixels(pos.x(), yinverse, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glFlush();
    const auto triangleID = pointcloudColorToId(buffer);
    auto it = selection_ids.find(triangleID);
    return (it != std::end(selection_ids)) ? boost::optional<BufferSelection>{it->second} : boost::none;
}


void Viewport3D::pickPointCloudIdAtPosition() {
    static bool pointcloud_id_init = true;
    if(pointcloud_id_init) {
        pointcloudIdShader.addShaderFromSourceCode(QOpenGLShader::Vertex, R"shaderSource(
            #version 110
            attribute vec3 vertex;
            attribute vec4 color;

            uniform mat4 modelview_matrix;
            uniform mat4 projection_matrix;

            varying vec4 frag_color;
            varying mat4 mvp_matrix;

            void main() {
                mvp_matrix = projection_matrix * modelview_matrix;
                gl_Position = mvp_matrix * vec4(vertex, 1.0);
                frag_color = color;
            }
        )shaderSource");

        pointcloudIdShader.addShaderFromSourceCode(QOpenGLShader::Fragment, R"shaderSource(
            #version 110

            uniform mat4 modelview_matrix;
            uniform mat4 projection_matrix;

            varying vec4 frag_color;
            varying mat4 mvp_matrix;

            void main() {
                gl_FragColor = frag_color;
            }
        )shaderSource");

        pointcloudIdShader.link();
        pointcloud_id_init = false;
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);//the depth thing buffer clear is the important part

    // create FBO
    // QOpenGLFramebufferObject pickFBO{width(), height()};

    // create id map
    std::uint32_t id_counter = 1;
    for(auto & tree : state->skeletonState->trees) {
        if (tree.pointCloud == nullptr || tree.pointCloud->vertex_count == 0) {
            continue;
        }
        std::vector<std::array<unsigned char, 4>> colors;
        colors.resize(tree.pointCloud->vertex_count);
        std::vector<std::array<float, 3>> flat_verts;
        std::vector<std::array<GLubyte, 4>> flat_colors;

        for(std::size_t i = 0; i < tree.pointCloud->index_count - 3; i += 3) { // for each face
            auto id_color = pointcloudIdToColor(id_counter);
            std::array<unsigned int, 3> v_ids{{tree.pointCloud->indices[i], tree.pointCloud->indices[i+1], tree.pointCloud->indices[i+2]}};
            floatCoordinate centerOfMass;
            for(std::size_t j = 0; j < 3; ++j) {
                flat_verts.emplace_back(std::array<float, 3>{{
                    tree.pointCloud->vertex_coords[v_ids[j]*3],
                    tree.pointCloud->vertex_coords[v_ids[j]*3+1],
                    tree.pointCloud->vertex_coords[v_ids[j]*3+2]}});
                centerOfMass += floatCoordinate{flat_verts.back()[0], flat_verts.back()[1], flat_verts.back()[2]};
                flat_colors.emplace_back(id_color);
            }
            selection_ids.emplace(id_counter, BufferSelection{tree.treeID, centerOfMass / 3});
            ++id_counter;
        }
        PointCloud id_buf{nullptr, false, GL_TRIANGLES};
        id_buf.vertex_count = flat_verts.size();
        id_buf.position_buf.bind();
        id_buf.position_buf.allocate(flat_verts.data(), flat_verts.size() * 3 * sizeof(GLfloat));

        id_buf.color_buf.bind();
        id_buf.color_buf.allocate(flat_colors.data(), flat_colors.size() * 4 * sizeof(GLubyte));

        renderPointCloudBufferIds(id_buf);
    }
}


bool Viewport3D::renderSkeletonVP(const RenderOptions &options) {
    if(!state->viewerState->selectModeFlag) {
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
    }
    // left, right, bottom, top, near, far clipping planes; substitute arbitrary vals to something more sensible. TDitem
    glScalef(1, -1, 1);// flip ogl y
    const auto halfBoundary = state->skeletonState->volBoundary / 2;
    const auto zoomedBoundary = state->skeletonState->volBoundary * state->skeletonState->zoomLevel;
    glOrtho(-halfBoundary + zoomedBoundary + state->skeletonState->translateX
        , halfBoundary - zoomedBoundary + state->skeletonState->translateX
        , -halfBoundary + zoomedBoundary + state->skeletonState->translateY
        , halfBoundary - zoomedBoundary + state->skeletonState->translateY
        , -10 * state->skeletonState->volBoundary, 10 * state->skeletonState->volBoundary);

    screenPxXPerDataPx = (float)edgeLength / (state->skeletonState->volBoundary - 2.f * state->skeletonState->volBoundary * state->skeletonState->zoomLevel);
    displayedlengthInNmX = edgeLength / screenPxXPerDataPx * state->scale.x;

    if(state->viewerState->multisamplingOnOff) {
        glEnable(GL_MULTISAMPLE);
    }
     // Now we set up the view on the skeleton and draw some very basic VP stuff like the gray background
    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

     // Now we draw the  background of our skeleton VP
    glPushMatrix();
        glTranslatef(0., 0., -10. * ((float)state->skeletonState->volBoundary - 2.));

        glShadeModel(GL_SMOOTH);
        glDisable(GL_TEXTURE_2D);

        glColor4f(1., 1., 1., 1.); // HERE
        // The * 10 should prevent, that the user translates into space with gray background - dirty solution. TDitem
        glBegin(GL_QUADS);
            glVertex3i(-state->skeletonState->volBoundary * 10, -state->skeletonState->volBoundary * 10, 0);
            glVertex3i(state->skeletonState->volBoundary  * 10, -state->skeletonState->volBoundary * 10, 0);
            glVertex3i(state->skeletonState->volBoundary  * 10, state->skeletonState->volBoundary  * 10, 0);
            glVertex3i(-state->skeletonState->volBoundary * 10, state->skeletonState->volBoundary  * 10, 0);
        glEnd();
    glPopMatrix();

    // load model view matrix that stores rotation state!
    glLoadMatrixf(state->skeletonState->skeletonVpModelView);

    auto rotateMe = [this](auto x, auto y){
        floatCoordinate datasetCenter{state->boundary / 2};
        floatCoordinate rotationCenter{datasetCenter};
        if (state->viewerState->rotationCenter == RotationCenter::ActiveNode && state->skeletonState->activeNode != nullptr) {
            rotationCenter = state->skeletonState->activeNode->position;
        } else if (state->viewerState->rotationCenter == RotationCenter::CurrentPosition) {
            rotationCenter = state->viewerState->currentPosition;
        }
        // invert rotation
        const auto rotation = QMatrix4x4{state->skeletonState->rotationState};
        float inverseRotation[16];
        rotation.inverted().copyDataTo(inverseRotation);
        // add new rotation
        QMatrix4x4 singleRotation;
        singleRotation.rotate(y, {0, 1, 0});
        singleRotation.rotate(x, {1, 0, 0});
        (rotation * singleRotation).copyDataTo(state->skeletonState->rotationState);
        // apply complete rotation
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glLoadMatrixf(state->skeletonState->skeletonVpModelView);
        glTranslatef(-datasetCenter.x, -datasetCenter.y, -datasetCenter.z);
        glTranslatef(rotationCenter.x, rotationCenter.y, rotationCenter.z);
        glScalef(1, 1, state->viewerState->voxelXYtoZRatio);
        glMultMatrixf(inverseRotation);
        glMultMatrixf(state->skeletonState->rotationState);
        glScalef(1, 1, 1/state->viewerState->voxelXYtoZRatio);
        glTranslatef(-rotationCenter.x, -rotationCenter.y, -rotationCenter.z);
        glTranslatef(datasetCenter.x, datasetCenter.y, datasetCenter.z);
        // save the modified basic model view matrix
        glGetFloatv(GL_MODELVIEW_MATRIX, state->skeletonState->skeletonVpModelView);
    };

    // perform user defined coordinate system rotations. use single matrix multiplication as opt.! TDitem
    if (state->skeletonState->rotdx != 0 || state->skeletonState->rotdy != 0) {
        rotateMe(state->skeletonState->rotdy, -state->skeletonState->rotdx);// moving the cursor horizontally rotates the y axis
        state->skeletonState->rotdx = state->skeletonState->rotdy = 0;
    }

    const auto xy = state->skeletonState->definedSkeletonVpView == SKELVP_XY_VIEW;
    const auto xz = state->skeletonState->definedSkeletonVpView == SKELVP_XZ_VIEW;
    const auto zy = state->skeletonState->definedSkeletonVpView == SKELVP_ZY_VIEW;
    if (xy || xz || zy) {
        state->skeletonState->definedSkeletonVpView = -1;

        QMatrix4x4{}.copyDataTo(state->skeletonState->rotationState);
        QMatrix4x4{}.copyDataTo(state->skeletonState->skeletonVpModelView);
        rotateMe(90 * xz, -90 * zy);// updates rotationState and skeletonVpModelView, must therefore also be called for xy

        const auto translate = state->viewerState->currentPosition - state->boundary / 2;
        state->skeletonState->translateX = zy ? translate.z : translate.x;
        state->skeletonState->translateY = xz ? translate.z : translate.y;
    } else if (state->skeletonState->definedSkeletonVpView == SKELVP_R90 || state->skeletonState->definedSkeletonVpView == SKELVP_R180) {
        state->skeletonState->rotdx = 10;
        state->skeletonState->rotationcounter++;
        if (state->skeletonState->rotationcounter > (state->skeletonState->definedSkeletonVpView == SKELVP_R90 ? 9 : 18)) {
            state->skeletonState->rotdx = 0;
            state->skeletonState->definedSkeletonVpView = -1;
            state->skeletonState->rotationcounter = 0;
        }

    } else if (state->skeletonState->definedSkeletonVpView == SKELVP_RESET) {
        state->skeletonState->definedSkeletonVpView = -1;
        state->skeletonState->translateX = 0;
        state->skeletonState->translateY = 0;
        state->skeletonState->zoomLevel = SKELZOOMMIN;

        QMatrix4x4{}.copyDataTo(state->skeletonState->rotationState);
        QMatrix4x4{}.copyDataTo(state->skeletonState->skeletonVpModelView);
        rotateMe(25, 25);
    }

    if(options.drawViewportPlanes) { // Draw the slice planes for orientation inside the data stack
        glPushMatrix();

        // single operation! TDitem
        glTranslatef(-((float)state->boundary.x / 2.), -((float)state->boundary.y / 2.), -((float)state->boundary.z / 2.));
        glTranslatef(0.5, 0.5, 0.5);

        updateFrustumClippingPlanes();
        glTranslatef((float)state->viewerState->currentPosition.x, (float)state->viewerState->currentPosition.y, (float)state->viewerState->currentPosition.z);

        glEnable(GL_TEXTURE_2D);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glColor4f(1., 1., 1., 1.);
        state->viewer->window->forEachOrthoVPDo([this](ViewportOrtho & orthoVP) {
            // Used for calculation of slice pane length inside the 3d view
            float dataPxX = orthoVP.texture.displayedEdgeLengthX / orthoVP.texture.texUnitsPerDataPx * 0.5;
            float dataPxY = orthoVP.texture.displayedEdgeLengthY / orthoVP.texture.texUnitsPerDataPx * 0.5;

            switch(orthoVP.viewportType) {
            case VIEWPORT_XY:
                if (!state->viewerState->showXYplane) break;
                glBindTexture(GL_TEXTURE_2D, orthoVP.texture.texHandle);
                glBegin(GL_QUADS);
                    glNormal3i(0,0,1);
                    glTexCoord2f(orthoVP.texture.texLUx, orthoVP.texture.texLUy);
                    glVertex3f(-dataPxX, -dataPxY, 0.);
                    glTexCoord2f(orthoVP.texture.texRUx, orthoVP.texture.texRUy);
                    glVertex3f(dataPxX, -dataPxY, 0.);
                    glTexCoord2f(orthoVP.texture.texRLx, orthoVP.texture.texRLy);
                    glVertex3f(dataPxX, dataPxY, 0.);
                    glTexCoord2f(orthoVP.texture.texLLx, orthoVP.texture.texLLy);
                    glVertex3f(-dataPxX, dataPxY, 0.);
                glEnd();
                glBindTexture (GL_TEXTURE_2D, 0);
                break;
            case VIEWPORT_XZ:
                if (!state->viewerState->showXZplane) break;
                glBindTexture(GL_TEXTURE_2D, orthoVP.texture.texHandle);
                glBegin(GL_QUADS);
                    glNormal3i(0,1,0);
                    glTexCoord2f(orthoVP.texture.texLUx, orthoVP.texture.texLUy);
                    glVertex3f(-dataPxX, 0., -dataPxY);
                    glTexCoord2f(orthoVP.texture.texRUx, orthoVP.texture.texRUy);
                    glVertex3f(dataPxX, 0., -dataPxY);
                    glTexCoord2f(orthoVP.texture.texRLx, orthoVP.texture.texRLy);
                    glVertex3f(dataPxX, 0., dataPxY);
                    glTexCoord2f(orthoVP.texture.texLLx, orthoVP.texture.texLLy);
                    glVertex3f(-dataPxX, 0., dataPxY);
                glEnd();
                glBindTexture (GL_TEXTURE_2D, 0);
                break;
            case VIEWPORT_ZY:
                if (!state->viewerState->showZYplane) break;
                glBindTexture(GL_TEXTURE_2D, orthoVP.texture.texHandle);
                glBegin(GL_QUADS);
                    glNormal3i(1,0,0);
                    glTexCoord2f(orthoVP.texture.texLUx, orthoVP.texture.texLUy);
                    glVertex3f(0., -dataPxX, -dataPxY);
                    glTexCoord2f(orthoVP.texture.texRUx, orthoVP.texture.texRUy);
                    glVertex3f(0., dataPxX, -dataPxY);
                    glTexCoord2f(orthoVP.texture.texRLx, orthoVP.texture.texRLy);
                    glVertex3f(0., dataPxX, dataPxY);
                    glTexCoord2f(orthoVP.texture.texLLx, orthoVP.texture.texLLy);
                    glVertex3f(0., -dataPxX, dataPxY);
                glEnd();
                glBindTexture (GL_TEXTURE_2D, 0);
                break;
            case VIEWPORT_ARBITRARY:
                if (!state->viewerState->enableArbVP || !state->viewerState->showArbplane) break;
                renderArbitrarySlicePane(orthoVP);
                break;
            default:
                throw std::runtime_error("Viewport3D::renderSkeletonVP unknown vp");
            }
        });

        glDisable(GL_TEXTURE_2D);

        state->viewer->window->forEachOrthoVPDo([this](ViewportOrtho & orthoVP) {
            GLUquadricObj * gluCylObj;
            float dataPxX = orthoVP.texture.displayedEdgeLengthX / orthoVP.texture.texUnitsPerDataPx * 0.5;
            float dataPxY = orthoVP.texture.displayedEdgeLengthY / orthoVP.texture.texUnitsPerDataPx * 0.5;
            switch(orthoVP.viewportType) {
            case VIEWPORT_XY:
                glColor4f(0.7, 0., 0., 1.);
                glBegin(GL_LINE_LOOP);
                    glVertex3f(-dataPxX, -dataPxY, 0.);
                    glVertex3f(dataPxX, -dataPxY, 0.);
                    glVertex3f(dataPxX, dataPxY, 0.);
                    glVertex3f(-dataPxX, dataPxY, 0.);
                glEnd();

                glColor4f(0., 0., 0., 1.);
                glPushMatrix();
                glTranslatef(-dataPxX, 0., 0.);
                glRotatef(90., 0., 1., 0.);
                gluCylObj = gluNewQuadric();
                gluQuadricNormals(gluCylObj, GLU_SMOOTH);
                gluQuadricOrientation(gluCylObj, GLU_OUTSIDE);
                gluCylinder(gluCylObj, 0.4, 0.4, dataPxX * 2, 5, 5);
                gluDeleteQuadric(gluCylObj);
                glPopMatrix();

                glPushMatrix();
                glTranslatef(0., dataPxY, 0.);
                glRotatef(90., 1., 0., 0.);
                gluCylObj = gluNewQuadric();
                gluQuadricNormals(gluCylObj, GLU_SMOOTH);
                gluQuadricOrientation(gluCylObj, GLU_OUTSIDE);
                gluCylinder(gluCylObj, 0.4, 0.4, dataPxY * 2, 5, 5);
                gluDeleteQuadric(gluCylObj);
                glPopMatrix();

                break;
            case VIEWPORT_XZ:
                glColor4f(0., 0.7, 0., 1.);
                glBegin(GL_LINE_LOOP);
                    glVertex3f(-dataPxX, 0., -dataPxY);
                    glVertex3f(dataPxX, 0., -dataPxY);
                    glVertex3f(dataPxX, 0., dataPxY);
                    glVertex3f(-dataPxX, 0., dataPxY);
                glEnd();

                glColor4f(0., 0., 0., 1.);
                glPushMatrix();
                glTranslatef(0., 0., -dataPxY);
                gluCylObj = gluNewQuadric();
                gluQuadricNormals(gluCylObj, GLU_SMOOTH);
                gluQuadricOrientation(gluCylObj, GLU_OUTSIDE);
                gluCylinder(gluCylObj, 0.4, 0.4, dataPxY * 2, 5, 5);
                gluDeleteQuadric(gluCylObj);
                glPopMatrix();

                break;
            case VIEWPORT_ZY:
                glColor4f(0., 0., 0.7, 1.);
                glBegin(GL_LINE_LOOP);
                    glVertex3f(0., -dataPxX, -dataPxY);
                    glVertex3f(0., dataPxX, -dataPxY);
                    glVertex3f(0., dataPxX, dataPxY);
                    glVertex3f(0., -dataPxX, dataPxY);
                glEnd();
                break;
            case VIEWPORT_ARBITRARY:
                if (!state->viewerState->enableArbVP) break;
                glColor4f(orthoVP.n.z, orthoVP.n.y, orthoVP.n.x, 1.);

                glBegin(GL_LINE_LOOP);
                    glVertex3f(-dataPxX * orthoVP.v1.x - dataPxY * orthoVP.v2.x,
                               -dataPxX * orthoVP.v1.y - dataPxY * orthoVP.v2.y,
                               -dataPxX * orthoVP.v1.z - dataPxY * orthoVP.v2.z);
                    glVertex3f(dataPxX * orthoVP.v1.x - dataPxY * orthoVP.v2.x,
                               dataPxX * orthoVP.v1.y - dataPxY * orthoVP.v2.y,
                               dataPxX * orthoVP.v1.z - dataPxY * orthoVP.v2.z);
                    glVertex3f(dataPxX * orthoVP.v1.x + dataPxY * orthoVP.v2.x,
                               dataPxX * orthoVP.v1.y + dataPxY * orthoVP.v2.y,
                               dataPxX * orthoVP.v1.z + dataPxY * orthoVP.v2.z);
                    glVertex3f(-dataPxX * orthoVP.v1.x + dataPxY * orthoVP.v2.x,
                               -dataPxX * orthoVP.v1.y + dataPxY * orthoVP.v2.y,
                               -dataPxX * orthoVP.v1.z + dataPxY * orthoVP.v2.z);
                glEnd();
                break;
            default:
                throw std::runtime_error("Viewport3D::renderSkeletonVP unknown vp");
            }
        });

        glPopMatrix();
        glEnable(GL_TEXTURE_2D);
    }

    if(options.drawBoundaryBox || options.drawBoundaryAxes) {
        // Now we draw the dataset corresponding stuff (volume box of right size, axis descriptions...)
        glEnable(GL_BLEND);

        if(options.drawBoundaryBox) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

            glColor4f(0.8, 0.8, 0.8, 1.0);
            glBegin(GL_QUADS);
                glNormal3i(0,0,1);
                glVertex3i(-(state->boundary.x / 2), -(state->boundary.y / 2), -(state->boundary.z / 2));
                glVertex3i(state->boundary.x / 2, -(state->boundary.y / 2), -(state->boundary.z / 2));

                glVertex3i(state->boundary.x / 2, (state->boundary.y / 2), -(state->boundary.z / 2));
                glVertex3i(-(state->boundary.x / 2), (state->boundary.y / 2), -(state->boundary.z / 2));

                glNormal3i(0,0,1);
                glVertex3i(-(state->boundary.x / 2), -(state->boundary.y / 2), (state->boundary.z / 2));
                glVertex3i(state->boundary.x / 2, -(state->boundary.y / 2), (state->boundary.z / 2));

                glVertex3i(state->boundary.x / 2, (state->boundary.y / 2), (state->boundary.z / 2));
                glVertex3i(-(state->boundary.x / 2), (state->boundary.y / 2), (state->boundary.z / 2));

                glNormal3i(0,1,0);
                glVertex3i(-(state->boundary.x / 2), -(state->boundary.y / 2), -(state->boundary.z / 2));
                glVertex3i(-(state->boundary.x / 2), -(state->boundary.y / 2), (state->boundary.z / 2));

                glVertex3i(state->boundary.x / 2, -(state->boundary.y / 2), (state->boundary.z / 2));
                glVertex3i(state->boundary.x / 2, -(state->boundary.y / 2), -(state->boundary.z / 2));

                glNormal3i(0,1,0);
                glVertex3i(-(state->boundary.x / 2), (state->boundary.y / 2), -(state->boundary.z / 2));
                glVertex3i(-(state->boundary.x / 2), (state->boundary.y / 2), (state->boundary.z / 2));

                glVertex3i(state->boundary.x / 2, (state->boundary.y / 2), (state->boundary.z / 2));
                glVertex3i(state->boundary.x / 2, (state->boundary.y / 2), -(state->boundary.z / 2));

                glNormal3i(1,0,0);
                glVertex3i(-(state->boundary.x / 2), -(state->boundary.y / 2), -(state->boundary.z / 2));
                glVertex3i(-(state->boundary.x / 2), -(state->boundary.y / 2), (state->boundary.z / 2));

                glVertex3i(-(state->boundary.x / 2), (state->boundary.y / 2), (state->boundary.z / 2));
                glVertex3i(-(state->boundary.x / 2), (state->boundary.y / 2), -(state->boundary.z / 2));

                glNormal3i(1,0,0);
                glVertex3i(state->boundary.x / 2, -(state->boundary.y / 2), -(state->boundary.z / 2));
                glVertex3i(state->boundary.x / 2, -(state->boundary.y / 2), (state->boundary.z / 2));

                glVertex3i(state->boundary.x / 2, (state->boundary.y / 2), (state->boundary.z / 2));
                glVertex3i(state->boundary.x / 2, (state->boundary.y / 2), -(state->boundary.z / 2));
            glEnd();
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }

        // draw axes
        GLdouble origin[3], conePos_x[3], conePos_y[3], conePos_z[3], model[16], projection[16];
        GLint gl_viewport[4];
        glGetDoublev(GL_MODELVIEW_MATRIX, &model[0]);
        glGetDoublev(GL_PROJECTION_MATRIX, &projection[0]);
        glGetIntegerv(GL_VIEWPORT, gl_viewport);
        gluProject(-state->boundary.x/2, -state->boundary.y/2, -state->boundary.z/2, &model[0], &projection[0], &gl_viewport[0], &origin[0], &origin[1], &origin[2]);
        gluProject(state->boundary.x/2, -state->boundary.y/2, -state->boundary.z/2, &model[0], &projection[0], &gl_viewport[0], &conePos_x[0], &conePos_x[1], &conePos_x[2]);
        gluProject(-state->boundary.x/2, state->boundary.y/2, -state->boundary.z/2, &model[0], &projection[0], &gl_viewport[0], &conePos_y[0], &conePos_y[1], &conePos_y[2]);
        gluProject(-state->boundary.x/2, -state->boundary.y/2, state->boundary.z/2, &model[0], &projection[0], &gl_viewport[0], &conePos_z[0], &conePos_z[1], &conePos_z[2]);
        floatCoordinate axis_x{static_cast<float>(conePos_x[0] - origin[0]), static_cast<float>(origin[1] - conePos_x[1]), static_cast<float>(conePos_x[2] - origin[2])};
        floatCoordinate axis_y{static_cast<float>(conePos_y[0] - origin[0]), static_cast<float>(origin[1] - conePos_y[1]), static_cast<float>(conePos_y[2] - origin[2])};
        floatCoordinate axis_z{static_cast<float>(conePos_z[0] - origin[0]), static_cast<float>(origin[1] - conePos_z[1]), static_cast<float>(conePos_z[2] - origin[2])};
        glDisable(GL_DEPTH_TEST);
        auto renderAxis = [this, gl_viewport, options](floatCoordinate & targetView, const QString label) {
            glPushMatrix();
            floatCoordinate currentView = {0, 0, -1};
            const auto angle = acosf(currentView.dot(targetView));
            auto axis = currentView.cross(targetView);
            if (axis.normalize()) {
                glRotatef(-(angle*180/boost::math::constants::pi<float>()), axis.x, axis.y, axis.z);
            }
            // axis
            const auto diameter = std::ceil(0.005*gl_viewport[2]);
            GLUquadricObj * gluCylObj = gluNewQuadric();
            gluQuadricNormals(gluCylObj, GLU_SMOOTH);
            gluQuadricOrientation(gluCylObj, GLU_OUTSIDE);
            gluCylinder(gluCylObj, diameter, diameter , targetView.length(), 10, 1);
            gluDeleteQuadric(gluCylObj);
            // cone z
            glTranslatef(0, 0, targetView.length());
            gluCylObj = gluNewQuadric();
            gluQuadricNormals(gluCylObj, GLU_SMOOTH);
            gluQuadricOrientation(gluCylObj, GLU_OUTSIDE);
            gluCylinder(gluCylObj, std::ceil(0.014*gl_viewport[2]), 0., std::ceil(0.028*gl_viewport[2]), 10, 5);
            gluDeleteQuadric(gluCylObj);
            const int offset = std::ceil(0.043*gl_viewport[2]);
            renderText({offset, offset, offset}, label, options.enableTextScaling);
            glPopMatrix();
        };
        // remember world coordinate system
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        // switch to viewport coordinate system
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        const auto lenLongestAxis = std::max(axis_x.length(), std::max(axis_y.length(), axis_z.length()));
        glOrtho(0, gl_viewport[2], gl_viewport[3], 0, lenLongestAxis, -lenLongestAxis);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(origin[0], gl_viewport[3] - origin[1], origin[2]);
        glColor4f(0, 0, 0, 1);
        if(Viewport3D::showBoundariesInUm) {
            renderAxis(axis_x, QString("x: %1 µm").arg(state->boundary.x * state->scale.x * 0.001));
            renderAxis(axis_y, QString("y: %1 µm").arg(state->boundary.y * state->scale.y * 0.001));
            renderAxis(axis_z, QString("z: %1 µm").arg(state->boundary.z * state->scale.z * 0.001));
        }
        else {
            renderAxis(axis_x, QString("x: %1 px").arg(state->boundary.x + 1));
            renderAxis(axis_y, QString("y: %1 px").arg(state->boundary.y + 1));
            renderAxis(axis_z, QString("z: %1 px").arg(state->boundary.z + 1));
        }
        // restore world coordinate system
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);

        glEnable(GL_TEXTURE_2D);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
    }
    if (state->viewerState->selectModeFlag == false) {
        if (options.pointCloudPicking) {
            pickPointCloudIdAtPosition();
        } else {
            renderPointCloud();
        }
    }

    if(options.drawSkeleton && state->viewerState->skeletonDisplay.testFlag(SkeletonDisplay::ShowIn3DVP)) {
        glPushMatrix();
        glTranslatef(-state->boundary.x / 2., -state->boundary.y / 2., -state->boundary.z / 2.);//reset to origin of projection
        glTranslatef(0.5, 0.5, 0.5);//arrange to pixel center
        renderSkeleton(options);
        glPopMatrix();
    }

    // Reset previously changed OGL parameters
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_DEPTH_TEST);
    glLoadIdentity();
    return true;
}

void ViewportOrtho::renderBrush(const Coordinate coord) {
    glLineWidth(2.0f);

    auto & seg = Segmentation::singleton();
    auto drawCursor = [this, &seg, coord](const float r, const float g, const float b) {
        const auto bradius = seg.brush.getRadius();
        const auto bview = seg.brush.getView();
        const auto xsize = bradius / state->scale.x;
        const auto ysize = bradius / state->scale.y;
        const auto zsize = bradius / state->scale.z;

        //move from center to cursor
        glTranslatef(coord.x, coord.y, coord.z);
        if (viewportType == VIEWPORT_XZ && bview == brush_t::view_t::xz) {
            glTranslatef(0, 0, 1);//move origin to other corner of voxel, idrk why that’s necessary
            glRotatef(-90, 1, 0, 0);
        } else if(viewportType == VIEWPORT_ZY && bview == brush_t::view_t::zy) {
            glTranslatef(0, 0, 1);//move origin to other corner of voxel, idrk why that’s necessary
            glRotatef(90, 0, 1, 0);
        } else if (viewportType != VIEWPORT_XY || bview != brush_t::view_t::xy) {
            return;
        }

        const bool xy = viewportType == VIEWPORT_XY;
        const bool xz = viewportType == VIEWPORT_XZ;
        const int z = 0;
        std::vector<floatCoordinate> vertices;
        if(seg.brush.getShape() == brush_t::shape_t::angular) {
            const auto x = xy || xz ? xsize : zsize;
            const auto y = xz ? zsize : ysize;
            //integer coordinates to round to voxel boundaries
            vertices.push_back({-x    , -y    , z});
            vertices.push_back({ x + 1, -y    , z});
            vertices.push_back({ x + 1,  y + 1, z});
            vertices.push_back({-x    ,  y + 1, z});
        } else if(seg.brush.getShape() == brush_t::shape_t::round) {
            const int xmax = xy ? xsize : xz ? xsize : zsize;
            const int ymax = xy ? ysize : xz ? zsize : ysize;
            int y = 0;
            int x = xmax;
            auto addVerticalPixelBorder = [&vertices, this](float x, float y, float z) {
                vertices.push_back({x, y    , z});
                vertices.push_back({x, y + 1, z});
            };
            auto addHorizontalPixelBorder = [&vertices, this](float x, float y, float z) {
                vertices.push_back({x    , y, z});
                vertices.push_back({x + 1, y, z});
            };
            while (x >= y) { //first part of the ellipse (circle with anisotropic pixels), y dominant movement
                auto val = isInsideSphere(xy ? x : xz ? x : z, xy ? y : xz ? z : y, xy ? z : xz ? y : x, bradius);
                if (val) {
                    addVerticalPixelBorder( x + 1,  y, z);
                    addVerticalPixelBorder(-x    ,  y, z);
                    addVerticalPixelBorder(-x    , -y, z);
                    addVerticalPixelBorder( x + 1, -y, z);
                } else if (x != xmax || y != 0) {
                    addHorizontalPixelBorder( x,  y    , z);
                    addHorizontalPixelBorder(-x,  y    , z);
                    addHorizontalPixelBorder(-x, -y + 1, z);
                    addHorizontalPixelBorder( x, -y + 1, z);
                }
                if (val) {
                    ++y;
                } else {
                    --x;
                }
            }

            x = 0;
            y = ymax;
            while (y >= x) { //second part of the ellipse, x dominant movement
                auto val = isInsideSphere(xy ? x : xz ? x : z, xy ? y : xz ? z : y, xy ? z : xz ? y : x, bradius);
                if (val) {
                    addHorizontalPixelBorder( x,  y + 1, z);
                    addHorizontalPixelBorder(-x,  y + 1, z);
                    addHorizontalPixelBorder(-x, -y    , z);
                    addHorizontalPixelBorder( x, -y    , z);
                } else if (y != ymax || x != 0) {
                    addVerticalPixelBorder( x    ,  y, z);
                    addVerticalPixelBorder(-x + 1,  y, z);
                    addVerticalPixelBorder(-x + 1, -y, z);
                    addVerticalPixelBorder( x    , -y, z);
                }
                if (val) {
                    ++x;
                } else {
                    --y;
                }
            }
        }
        // sort by angle
        const auto center = std::accumulate(std::begin(vertices), std::end(vertices), floatCoordinate(0, 0, 0)) / vertices.size();
        const auto start = vertices.front() - center;
        std::sort(std::begin(vertices), std::end(vertices), [&center, &start](const floatCoordinate & lhs, const floatCoordinate & rhs) {
            const auto lineLhs = lhs - center;
            const auto lineRhs = rhs - center;
            return std::atan2(start.x * lineLhs.y - start.y * lineLhs.x, start.dot(lineLhs)) < std::atan2(start.x * lineRhs.y - start.y * lineRhs.x, start.dot(lineRhs));
        });
        glBegin(GL_LINE_LOOP);
        glColor4f(r, g, b, 1.);
        for (const auto & point : vertices) {
            glVertex3f(point.x, point.y, point.z);
        }
        glEnd();
        if (Session::singleton().annotationMode.testFlag(AnnotationMode::Mode_Paint)) { // fill brush with object color
            glBegin(GL_TRIANGLE_FAN);
            glColor4f(r, g, b, .25);
            glVertex3f(center.x, center.y, center.z);
            for (const auto & point : vertices) {
                glVertex3f(point.x, point.y, point.z);
            }
            glVertex3f(vertices.front().x, vertices.front().y, vertices.front().z); // close triangle fan
            glEnd();
        }

    };
    const auto objColor = seg.colorOfSelectedObject();
    if (seg.brush.isInverse()) {
        drawCursor(1.f, 0.f, 0.f);
    }
    else {
        drawCursor(std::get<0>(objColor)/255., std::get<1>(objColor)/255., std::get<2>(objColor)/255.);
    }
}

void Viewport3D::renderArbitrarySlicePane(const ViewportOrtho & vp) {
    // Used for calculation of slice pane length inside the 3d view
    const auto dataPxX = vp.texture.displayedEdgeLengthX / vp.texture.texUnitsPerDataPx * 0.5;
    const auto dataPxY = vp.texture.displayedEdgeLengthY / vp.texture.texUnitsPerDataPx * 0.5;

    glBindTexture(GL_TEXTURE_2D, vp.texture.texHandle);

    glBegin(GL_QUADS);
        glNormal3i(vp.n.x, vp.n.y, vp.n.z);
        glTexCoord2f(vp.texture.texLUx, vp.texture.texLUy);
        glVertex3f(-dataPxX * vp.v1.x - dataPxY * vp.v2.x, -dataPxX * vp.v1.y - dataPxY * vp.v2.y, -dataPxX * vp.v1.z - dataPxY * vp.v2.z);
        glTexCoord2f(vp.texture.texRUx, vp.texture.texRUy);
        glVertex3f(dataPxX * vp.v1.x - dataPxY * vp.v2.x, dataPxX * vp.v1.y - dataPxY * vp.v2.y, dataPxX * vp.v1.z - dataPxY * vp.v2.z);
        glTexCoord2f(vp.texture.texRLx, vp.texture.texRLy);
        glVertex3f(dataPxX * vp.v1.x + dataPxY * vp.v2.x, dataPxX * vp.v1.y + dataPxY * vp.v2.y, dataPxX * vp.v1.z + dataPxY * vp.v2.z);
        glTexCoord2f(vp.texture.texLLx, vp.texture.texLLy);
        glVertex3f(-dataPxX * vp.v1.x + dataPxY * vp.v2.x, -dataPxX * vp.v1.y + dataPxY * vp.v2.y, -dataPxX * vp.v1.z + dataPxY * vp.v2.z);
    glEnd();
    glBindTexture (GL_TEXTURE_2D, 0);
}

boost::optional<nodeListElement &> ViewportBase::pickNode(uint x, uint y, uint width) {
    const auto & nodes = pickNodes(x, y, width, width);
    if (nodes.size() != 0) {
        return *(*std::begin(nodes));
    }
    return boost::none;
}

QSet<nodeListElement *> ViewportBase::pickNodes(uint centerX, uint centerY, uint width, uint height) {
    RenderOptions options;
    const auto renderFunc = [&options, this]() { renderViewport(options); };
    options.selectionPass = RenderOptions::SelectionPass::NodeIDLowerBits;
    const auto lowerBitSelection = pickingBox(renderFunc, centerX, centerY, width, height);
    options.selectionPass = RenderOptions::SelectionPass::NodeIDHigherBits;
    const auto higherBitSelection = pickingBox(renderFunc, centerX, centerY, width, height);
    QSet<nodeListElement *> foundNodes;
    for (size_t i = 0; i < lowerBitSelection.size(); ++i) {
        const std::uint64_t name = static_cast<std::uint64_t>(higherBitSelection[i]) << 32 | lowerBitSelection[i];
        nodeListElement * const foundNode = Skeletonizer::findNodeByNodeID(name - GLNames::NodeOffset);
        if (foundNode != nullptr) {
            foundNodes.insert(foundNode);
        }
    }
    return foundNodes;
}

bool ViewportBase::pickedScalebar(uint centerX, uint centerY, uint width) {
    const auto selection = pickingBox([this]() { renderViewportFrontFace(); }, centerX, centerY, width, width);
    return std::find(std::begin(selection), std::end(selection), GLNames::Scalebar) != std::end(selection);
}

template<typename F>
std::vector<GLuint> ViewportBase::pickingBox(F renderFunc, uint centerX, uint centerY, uint selectionWidth, uint selectionHeight) {
    makeCurrent();

    //4 elems per node: hit_count(always 1), min, max and 1 name
    //generous amount of addional space for non-node-glloadname-calls
    std::vector<GLuint> selectionBuffer(state->skeletonState->nodesByNodeID.size() * 4 + 2048);
    glSelectBuffer(selectionBuffer.size(), selectionBuffer.data());

    state->viewerState->selectModeFlag = true;
    glRenderMode(GL_SELECT);

    glInitNames();
    glPushName(GLNames::None);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    GLdouble vp_height = height();

    GLint openGLviewport[4]{0, 0, width(), height()};
    gluPickMatrix(centerX, vp_height - centerY, selectionWidth, selectionHeight, openGLviewport);

    renderFunc();

    GLint hits = glRenderMode(GL_RENDER);
    glLoadIdentity();

    qDebug() << "selection hits: " << hits;
    state->viewerState->selectModeFlag = false;
    std::vector<GLuint> result;
    for (std::size_t i = 0; i < selectionBuffer.size();) {
        if (hits == 0) { //if hits was positive and reaches 0
            //if overflow bit was set hits is negative and we only use the buffer-end-condition
            break;
        }
        --hits;
        const GLuint hit_count = selectionBuffer[i];
        if (hit_count > 0) {
            result.push_back(selectionBuffer[i+3]); //the first name on the stack is the 4th element of the hit record
        }
        i = i + 3 + hit_count;
    }
    return result;
}

/*
 * Fast and simplified tree rendering that uses frustum culling and
 * a heuristic level-of-detail implementation that exploits the implicit
 * sorting of the tree node list to avoid a depth first search for the compilation
 * of a spatial graph that is similar to the true skeleton, but without nodes /
 * vertices that would not be visible anyway. It uses large vertex batches for
 * line and point geometry (most data) drawn with vertex arrays, since the geometry is highly
 * dynamic (can change each frame). VBOs would make a lot of sense if we had a
 * smart spatial organization of the skeleton.
 * Ugly code, not nice to read, should be simplified...
 */

void ViewportBase::renderSkeleton(const RenderOptions &options) {
    if(state->viewerState->lightOnOff && state->viewerState->selectModeFlag == false) {
        // Configure light
        glEnable(GL_LIGHTING);
        GLfloat ambientLight[] = {0.5, 0.5, 0.5, 0.8};
        GLfloat diffuseLight[] = {1., 1., 1., 1.};
        GLfloat lightPos[] = {0., 0., 1., 1.};

        glLightfv(GL_LIGHT0,GL_AMBIENT,ambientLight);
        glLightfv(GL_LIGHT0,GL_DIFFUSE,diffuseLight);
        glLightfv(GL_LIGHT0,GL_POSITION,lightPos);
        glEnable(GL_LIGHT0);

        GLfloat global_ambient[] = { 0.0f, 0.0f, 0.0f, 1.0f };
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, global_ambient);

        // Enable materials with automatic color tracking
        glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
        glEnable(GL_COLOR_MATERIAL);
    }
    state->viewerState->lineVertBuffer.vertices.clear();
    state->viewerState->lineVertBuffer.colors.clear();
    state->viewerState->pointVertBuffer.vertices.clear();
    state->viewerState->pointVertBuffer.colors.clear();

    //tdItem: test culling under different conditions!
    //if(viewportType == VIEWPORT_SKELETON) glEnable(GL_CULL_FACE);

    /* Enable blending just once, since we never disable it? */
    glEnable(GL_BLEND);

    glPushMatrix();

    const auto * activeTree = state->skeletonState->activeTree;
    const auto * activeNode = state->skeletonState->activeNode;
    const auto * activeSynapse = (activeNode && activeNode->isSynapticNode) ? activeNode->correspondingSynapse :
                                 (activeTree && activeTree->isSynapticCleft) ? activeTree->correspondingSynapse :
                                                                               nullptr;
    const bool synapseBuilding = Synapse::state != Synapse::State::PreSynapse;
    const bool onlySelected = state->viewerState->skeletonDisplay.testFlag(SkeletonDisplay::OnlySelected);
    for (auto & currentTree : Skeletonizer::singleton().skeletonState.trees) {
        // focus on synapses, darken rest of skeleton
        const bool darken = (synapseBuilding && currentTree.correspondingSynapse != &Synapse::temporarySynapse)
                || (activeSynapse && activeSynapse->getCleft() != &currentTree);
        const bool hideSynapses = !darken && !synapseBuilding && !activeSynapse && currentTree.isSynapticCleft;
        const bool selectionFilter = onlySelected && !currentTree.selected;
        if (hideSynapses || selectionFilter || !currentTree.render) {
            continue;
        }
        nodeListElement * previousNode = nullptr;
        nodeListElement * lastRenderedNode = nullptr;
        float cumDistToLastRenderedNode = 0.f;

        for (auto nodeIt = std::begin(currentTree.nodes); nodeIt != std::end(currentTree.nodes); ++nodeIt) {
            /* We start with frustum culling:
             * all nodes that are not in the current viewing frustum for the
             * currently rendered viewports are discarded. This is very fast. */

            /* For frustum culling. These values should be stored, mem <-> cpu tradeoff  */
            floatCoordinate currNodePos = nodeIt->position;

            /* Every node is tested based on a precomputed circumsphere
            that includes its segments. */

            if (!sphereInFrustum(currNodePos, nodeIt->circRadius)) {
                previousNode = lastRenderedNode = nullptr;
                continue;
            }

            bool virtualSegRendered = false;
            bool nodeVisible = true;

            /* First test whether this node is actually connected to the next,
            i.e. whether the implicit sorting is not broken here. */
            bool allowHeuristic = false;
            if (std::next(nodeIt) != std::end(currentTree.nodes) && nodeIt->segments.size() <= 2) {
                for (const auto & currentSegment : std::next(nodeIt)->segments) {
                    if (currentSegment.target == *nodeIt || currentSegment.source == *nodeIt) {
                        /* Connected, heuristic is allowed */
                        allowHeuristic = true;
                        break;
                    }
                }
            }

            if (previousNode != nullptr && allowHeuristic && !state->viewerState->selectModeFlag) {
                for (auto & currentSegment : nodeIt->segments) {
                    //isBranchNode tells you only whether the node is on the branch point stack,
                    //not whether it is actually a node connected to more than two other nodes!
                    const bool mustBeRendered = nodeIt->getComment().isEmpty() == false || nodeIt->isBranchNode || nodeIt->segments.size() > 2 || nodeIt->radius * screenPxXPerDataPx > 5.f;
                    const bool cullingCandidate = currentSegment.target == *previousNode || (currentSegment.source == *previousNode && !mustBeRendered);
                    if (cullingCandidate) {
                        //Node is a candidate for LOD culling
                        //Do we really skip this node? Test cum dist. to last rendered node!
                        cumDistToLastRenderedNode += currentSegment.length * screenPxXPerDataPx;
                        if (cumDistToLastRenderedNode <= state->viewerState->cumDistRenderThres) {
                            nodeVisible = false;
                        }
                        break;
                    }
                }
            }

            if (nodeVisible) {
                //This sets the current color for the segment rendering
                QColor currentColor = currentTree.color;
                if((currentTree.treeID == state->skeletonState->activeTree->treeID)
                    && (state->viewerState->highlightActiveTree)) {
                    currentColor = Qt::red;
                }
                if (darken) {
                    currentColor.setAlpha(Synapse::darkenedAlpha);
                }

                cumDistToLastRenderedNode = 0.f;

                if (previousNode != lastRenderedNode) {
                    virtualSegRendered = true;
                    // We need a "virtual" segment now
                    segmentListElement virtualSegment(*lastRenderedNode, *nodeIt, false);
                    renderSegment(virtualSegment, currentColor, options);
                }

                /* Second pass over segments needed... But only if node is actually rendered! */
                for (const auto & currentSegment : nodeIt->segments) {
                    if (currentSegment.forward || (virtualSegRendered && (currentSegment.source == *previousNode || currentSegment.target == *previousNode))) {
                        continue;
                    }
                    renderSegment(currentSegment, currentColor, options);
                }

                if (state->viewerState->selectModeFlag) {
                    const auto name = GLNames::NodeOffset + nodeIt->nodeID;
                    GLuint pickedBits = (options.selectionPass == RenderOptions::SelectionPass::NodeIDLowerBits) ? static_cast<GLuint>(name) : name >> 32;
                    glLoadName(pickedBits);
                }
                renderNode(*nodeIt, options);
                if (state->viewerState->selectModeFlag) {
                    glLoadName(GLNames::None);
                }
                lastRenderedNode = &*nodeIt;
            }
            previousNode = &*nodeIt;
        }
    }

    /* Connect all synapses */
    for (auto & synapse : state->skeletonState->synapses) {
        const auto * activeTree = state->skeletonState->activeTree;
        const auto * activeNode = state->skeletonState->activeNode;
        const auto synapseCreated = synapse.getPostSynapse() != nullptr && synapse.getPreSynapse() != nullptr;
        const auto synapseHidden = !synapse.getPreSynapse()->correspondingTree->render && !synapse.getPostSynapse()->correspondingTree->render;
        const auto synapseSelected = synapse.getCleft() == activeTree || synapse.getPostSynapse() == activeNode || synapse.getPreSynapse() == activeNode;

        if (synapseCreated && synapseHidden == false && (state->viewerState->skeletonDisplay.testFlag(SkeletonDisplay::OnlySelected) == false || synapseSelected)) {
            segmentListElement virtualSegment(*synapse.getPostSynapse(), *synapse.getPreSynapse());
            QColor color = Qt::black;
            if (synapseSelected == false) {
                color.setAlpha(Synapse::darkenedAlpha);
            }
            renderSegment(virtualSegment, color, options);

            auto post = synapse.getPostSynapse()->position;
            auto pre = synapse.getPreSynapse()->position;
            const auto offset = (post - pre)/10;
            Coordinate arrowbase = post - offset;

            renderCylinder(&arrowbase, Skeletonizer::singleton().radius(*synapse.getPreSynapse()) * 3.0f
                , &synapse.getPostSynapse()->position
                , Skeletonizer::singleton().radius(*synapse.getPostSynapse()) * state->viewerState->segRadiusToNodeRadius, color, options);
        }
    }

    /* Render line geometry batch if it contains data */
    if (!state->viewerState->lineVertBuffer.vertices.empty()) {
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);

        /* draw all segments */
        glVertexPointer(3, GL_FLOAT, 0, state->viewerState->lineVertBuffer.vertices.data());
        glColorPointer(4, GL_FLOAT, 0, state->viewerState->lineVertBuffer.colors.data());

        glDrawArrays(GL_LINES, 0, state->viewerState->lineVertBuffer.vertices.size());

        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
    }

    if(state->viewerState->overrideNodeRadiusBool)
        glPointSize(state->viewerState->overrideNodeRadiusVal);
    else
        glPointSize(3.f);

    /* Render point geometry batch if it contains data */
    if (!state->viewerState->pointVertBuffer.vertices.empty()) {
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);

        /* draw all segments */
        glVertexPointer(3, GL_FLOAT, 0, state->viewerState->pointVertBuffer.vertices.data());
        glColorPointer(4, GL_FLOAT, 0, state->viewerState->pointVertBuffer.colors.data());

        glDrawArrays(GL_POINTS, 0, state->viewerState->pointVertBuffer.vertices.size());

        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);

    }
    glPointSize(1.f);

    glPopMatrix(); // Restore modelview matrix
    glEnable(GL_BLEND);
    glDisable(GL_LIGHTING);
    glDisable(GL_COLOR_MATERIAL);
}

bool ViewportBase::updateFrustumClippingPlanes() {
   float   tmpFrustum[6][4];
   float   proj[16];
   float   modl[16];
   float   clip[16];

   /* Get the current PROJECTION matrix from OpenGL */
   glGetFloatv( GL_PROJECTION_MATRIX, proj );

   /* Get the current MODELVIEW matrix from OpenGL */
   glGetFloatv( GL_MODELVIEW_MATRIX, modl );

   /* Combine the two matrices (multiply projection by modelview) */
   clip[ 0] = modl[ 0] * proj[ 0] + modl[ 1] * proj[ 4] + modl[ 2] * proj[ 8] + modl[ 3] * proj[12];
   clip[ 1] = modl[ 0] * proj[ 1] + modl[ 1] * proj[ 5] + modl[ 2] * proj[ 9] + modl[ 3] * proj[13];
   clip[ 2] = modl[ 0] * proj[ 2] + modl[ 1] * proj[ 6] + modl[ 2] * proj[10] + modl[ 3] * proj[14];
   clip[ 3] = modl[ 0] * proj[ 3] + modl[ 1] * proj[ 7] + modl[ 2] * proj[11] + modl[ 3] * proj[15];

   clip[ 4] = modl[ 4] * proj[ 0] + modl[ 5] * proj[ 4] + modl[ 6] * proj[ 8] + modl[ 7] * proj[12];
   clip[ 5] = modl[ 4] * proj[ 1] + modl[ 5] * proj[ 5] + modl[ 6] * proj[ 9] + modl[ 7] * proj[13];
   clip[ 6] = modl[ 4] * proj[ 2] + modl[ 5] * proj[ 6] + modl[ 6] * proj[10] + modl[ 7] * proj[14];
   clip[ 7] = modl[ 4] * proj[ 3] + modl[ 5] * proj[ 7] + modl[ 6] * proj[11] + modl[ 7] * proj[15];

   clip[ 8] = modl[ 8] * proj[ 0] + modl[ 9] * proj[ 4] + modl[10] * proj[ 8] + modl[11] * proj[12];
   clip[ 9] = modl[ 8] * proj[ 1] + modl[ 9] * proj[ 5] + modl[10] * proj[ 9] + modl[11] * proj[13];
   clip[10] = modl[ 8] * proj[ 2] + modl[ 9] * proj[ 6] + modl[10] * proj[10] + modl[11] * proj[14];
   clip[11] = modl[ 8] * proj[ 3] + modl[ 9] * proj[ 7] + modl[10] * proj[11] + modl[11] * proj[15];

   clip[12] = modl[12] * proj[ 0] + modl[13] * proj[ 4] + modl[14] * proj[ 8] + modl[15] * proj[12];
   clip[13] = modl[12] * proj[ 1] + modl[13] * proj[ 5] + modl[14] * proj[ 9] + modl[15] * proj[13];
   clip[14] = modl[12] * proj[ 2] + modl[13] * proj[ 6] + modl[14] * proj[10] + modl[15] * proj[14];
   clip[15] = modl[12] * proj[ 3] + modl[13] * proj[ 7] + modl[14] * proj[11] + modl[15] * proj[15];

   /* Extract the numbers for the RIGHT plane */
   tmpFrustum[0][0] = clip[ 3] - clip[ 0];
   tmpFrustum[0][1] = clip[ 7] - clip[ 4];
   tmpFrustum[0][2] = clip[11] - clip[ 8];
   tmpFrustum[0][3] = clip[15] - clip[12];

   /* Normalize the result */
   float t = sqrt( tmpFrustum[0][0] * tmpFrustum[0][0] + tmpFrustum[0][1] * tmpFrustum[0][1] + tmpFrustum[0][2] * tmpFrustum[0][2] );
   tmpFrustum[0][0] /= t;
   tmpFrustum[0][1] /= t;
   tmpFrustum[0][2] /= t;
   tmpFrustum[0][3] /= t;

   /* Extract the numbers for the LEFT plane */
   tmpFrustum[1][0] = clip[ 3] + clip[ 0];
   tmpFrustum[1][1] = clip[ 7] + clip[ 4];
   tmpFrustum[1][2] = clip[11] + clip[ 8];
   tmpFrustum[1][3] = clip[15] + clip[12];

   /* Normalize the result */
   t = sqrt( tmpFrustum[1][0] * tmpFrustum[1][0] + tmpFrustum[1][1] * tmpFrustum[1][1] + tmpFrustum[1][2] * tmpFrustum[1][2] );
   tmpFrustum[1][0] /= t;
   tmpFrustum[1][1] /= t;
   tmpFrustum[1][2] /= t;
   tmpFrustum[1][3] /= t;

   /* Extract the BOTTOM plane */
   tmpFrustum[2][0] = clip[ 3] + clip[ 1];
   tmpFrustum[2][1] = clip[ 7] + clip[ 5];
   tmpFrustum[2][2] = clip[11] + clip[ 9];
   tmpFrustum[2][3] = clip[15] + clip[13];

   /* Normalize the result */
   t = sqrt( tmpFrustum[2][0] * tmpFrustum[2][0] + tmpFrustum[2][1] * tmpFrustum[2][1] + tmpFrustum[2][2] * tmpFrustum[2][2] );
   tmpFrustum[2][0] /= t;
   tmpFrustum[2][1] /= t;
   tmpFrustum[2][2] /= t;
   tmpFrustum[2][3] /= t;

   /* Extract the TOP plane */
   tmpFrustum[3][0] = clip[ 3] - clip[ 1];
   tmpFrustum[3][1] = clip[ 7] - clip[ 5];
   tmpFrustum[3][2] = clip[11] - clip[ 9];
   tmpFrustum[3][3] = clip[15] - clip[13];

   /* Normalize the result */
   t = sqrt( tmpFrustum[3][0] * tmpFrustum[3][0] + tmpFrustum[3][1] * tmpFrustum[3][1] + tmpFrustum[3][2] * tmpFrustum[3][2] );
   tmpFrustum[3][0] /= t;
   tmpFrustum[3][1] /= t;
   tmpFrustum[3][2] /= t;
   tmpFrustum[3][3] /= t;

   /* Extract the FAR plane */
   tmpFrustum[4][0] = clip[ 3] - clip[ 2];
   tmpFrustum[4][1] = clip[ 7] - clip[ 6];
   tmpFrustum[4][2] = clip[11] - clip[10];
   tmpFrustum[4][3] = clip[15] - clip[14];

   /* Normalize the result */
   t = sqrt( tmpFrustum[4][0] * tmpFrustum[4][0] + tmpFrustum[4][1] * tmpFrustum[4][1] + tmpFrustum[4][2] * tmpFrustum[4][2] );
   tmpFrustum[4][0] /= t;
   tmpFrustum[4][1] /= t;
   tmpFrustum[4][2] /= t;
   tmpFrustum[4][3] /= t;

   /* Extract the NEAR plane */
   tmpFrustum[5][0] = clip[ 3] + clip[ 2];
   tmpFrustum[5][1] = clip[ 7] + clip[ 6];
   tmpFrustum[5][2] = clip[11] + clip[10];
   tmpFrustum[5][3] = clip[15] + clip[14];

   /* Normalize the result */
   t = sqrt( tmpFrustum[5][0] * tmpFrustum[5][0] + tmpFrustum[5][1] * tmpFrustum[5][1] + tmpFrustum[5][2] * tmpFrustum[5][2] );
   tmpFrustum[5][0] /= t;
   tmpFrustum[5][1] /= t;
   tmpFrustum[5][2] /= t;
   tmpFrustum[5][3] /= t;

   memcpy(frustum, tmpFrustum, sizeof(tmpFrustum));
    return true;
}

/* modified public domain code from: http://www.crownandcutlass.com/features/technicaldetails/frustum.html */
bool ViewportBase::sphereInFrustum(floatCoordinate pos, float radius) {
    for(int p = 0; p < 6; ++p) {
        if(frustum[p][0] * pos.x + frustum[p][1] * pos.y + frustum[p][2] * pos.z + frustum[p][3] <= -radius) {
           return false;
        }
    }
    return true;
}
