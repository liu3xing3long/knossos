/*
 *  This file is a part of KNOSSOS.
 *
 *  (C) Copyright 2007-2013
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
 */

/*
 * For further information, visit http://www.knossostool.org or contact
 *     Joergen.Kornfeld@mpimf-heidelberg.mpg.de or
 *     Fabian.Svara@mpimf-heidelberg.mpg.de
 */
#include "skeletonizer.h"

#include "file_io.h"
#include "functions.h"
#include "knossos.h"
#include "segmentation/cubeloader.h"
#include "session.h"
#include "skeleton/node.h"
#include "skeleton/tree.h"
#include "version.h"
#include "viewer.h"
#include "widgets/mainwindow.h"

#include <QMessageBox>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <cstring>
#include <queue>
#include <unordered_set>
#include <vector>

#define CATCH_RADIUS 10

template<typename T, typename Func>
bool connectedComponent(T & node, Func func) {
    std::queue<T*> queue;
    std::unordered_set<const T*> visitedNodes;
    visitedNodes.emplace(&node);
    queue.push(&node);
    while (!queue.empty()) {
        auto * nextNode = queue.front();
        queue.pop();

        if (func(*nextNode)) {
            return true;
        }

        for (auto & segment : nextNode->segments) {
            auto & neighbor = segment.forward ? segment.target : segment.source;
            if (visitedNodes.find(&neighbor) == visitedNodes.end()) {
                visitedNodes.emplace(&neighbor);
                queue.push(&neighbor);
            }
        }
    }
    return false;
}

treeListElement* Skeletonizer::findTreeByTreeID(int treeID) {
    const auto treeIt = state->skeletonState->treesByID.find(treeID);
    return treeIt != std::end(state->skeletonState->treesByID) ? treeIt->second : nullptr;
}

QList<treeListElement *> Skeletonizer::findTrees(const QString & comment) {
    auto tree = state->skeletonState->firstTree.get();
    QList<treeListElement *> hits;
    while(tree) {
        if(QString(tree->comment).contains(comment)) {
            hits.append(tree);
        }
        tree = tree->next.get();
    }
    return hits;
}

boost::optional<nodeListElement &> Skeletonizer::UI_addSkeletonNode(const Coordinate & clickedCoordinate, ViewportType VPtype) {
    color4F treeCol;
    /* -1 causes new color assignment */
    treeCol.r = -1.;
    treeCol.g = -1.;
    treeCol.b = -1.;
    treeCol.a = 1.;

    if(!state->skeletonState->activeTree) {
        addTreeListElement(0, treeCol);
    }

    auto addedNode = addNode(
                          0,
                          state->skeletonState->defaultNodeRadius,
                          state->skeletonState->activeTree->treeID,
                          clickedCoordinate,
                          VPtype,
                          state->magnification,
                          boost::none,
                          true);
    if(!addedNode) {
        qDebug() << "Error: Could not add new node!";
        return boost::none;
    }

    setActiveNode(&addedNode.get());

    if (state->skeletonState->activeTree->nodes.size() == 1) {//First node tree is branch node
        pushBranchNode(addedNode.get());
        addComment("First Node", addedNode.get());
    }
    return addedNode.get();
}

boost::optional<nodeListElement &> Skeletonizer::addSkeletonNodeAndLinkWithActive(const Coordinate & clickedCoordinate, ViewportType VPtype, int makeNodeActive) {
    if(!state->skeletonState->activeNode) {
        qDebug() << "Please create a node before trying to link nodes.";
        return boost::none;
    }

    //Add a new node at the target position first.
    auto targetNode = addNode(
                           0,
                           state->skeletonState->defaultNodeRadius,
                           state->skeletonState->activeTree->treeID,
                           clickedCoordinate,
                           VPtype,
                           state->magnification,
                           boost::none,
                           true);
    if(!targetNode) {
        qDebug() << "Could not add new node while trying to add node and link with active node!";
        return boost::none;
    }

    addSegment(*state->skeletonState->activeNode, targetNode.get());

    if(makeNodeActive) {
        setActiveNode(&targetNode.get());
    }
    if (state->skeletonState->activeTree->nodes.size() == 1) {
        /* First node in this tree */
        pushBranchNode(targetNode.get());
        addComment("First Node", targetNode.get());
    }

    return targetNode.get();
}

bool Skeletonizer::saveXmlSkeleton(QIODevice & file) const {
    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();

    xml.writeStartElement("things");//root node

    xml.writeStartElement("parameters");
    xml.writeStartElement("experiment");
    xml.writeAttribute("name", QString(state->name));
    xml.writeEndElement();

    xml.writeStartElement("lastsavedin");
    xml.writeAttribute("version", QString(KVERSION));
    xml.writeEndElement();

    xml.writeStartElement("createdin");
    xml.writeAttribute("version", state->skeletonState->skeletonCreatedInVersion);
    xml.writeEndElement();

    xml.writeStartElement("dataset");
    xml.writeAttribute("path", state->viewer->window->widgetContainer.datasetLoadWidget.datasetUrl.toString());
    xml.writeEndElement();

    xml.writeStartElement("task");
    xml.writeAttribute("category", Session::singleton().task.first);
    xml.writeAttribute("name", Session::singleton().task.second);
    xml.writeEndElement();

    xml.writeStartElement("MovementArea");
    xml.writeAttribute("min.x", QString::number(Session::singleton().movementAreaMin.x));
    xml.writeAttribute("min.y", QString::number(Session::singleton().movementAreaMin.y));
    xml.writeAttribute("min.z", QString::number(Session::singleton().movementAreaMin.z));
    xml.writeAttribute("max.x", QString::number(Session::singleton().movementAreaMax.x));
    xml.writeAttribute("max.y", QString::number(Session::singleton().movementAreaMax.y));
    xml.writeAttribute("max.z", QString::number(Session::singleton().movementAreaMax.z));
    xml.writeEndElement();

    xml.writeStartElement("scale");
    xml.writeAttribute("x", QString::number(state->scale.x));
    xml.writeAttribute("y", QString::number(state->scale.y));
    xml.writeAttribute("z", QString::number(state->scale.z));
    xml.writeEndElement();

    xml.writeStartElement("RadiusLocking");
    xml.writeAttribute("enableCommentLocking", QString::number(state->skeletonState->lockPositions));
    xml.writeAttribute("lockingRadius", QString::number(state->skeletonState->lockRadius));
    xml.writeAttribute("lockToNodesWithComment", QString(state->skeletonState->onCommentLock));
    xml.writeEndElement();

    xml.writeStartElement("time");
    const auto time = Session::singleton().getAnnotationTime();
    xml.writeAttribute("ms", QString::number(time));
    const auto timeData = QByteArray::fromRawData(reinterpret_cast<const char * const>(&time), sizeof(time));
    const QString timeChecksum = QCryptographicHash::hash(timeData, QCryptographicHash::Sha256).toHex().constData();
    xml.writeAttribute("checksum", timeChecksum);
    xml.writeEndElement();

    if (state->skeletonState->activeNode != nullptr) {
        xml.writeStartElement("activeNode");
        xml.writeAttribute("id", QString::number(state->skeletonState->activeNode->nodeID));
        xml.writeEndElement();
    }

    xml.writeStartElement("segmentation");
    xml.writeAttribute("backgroundId", QString::number(Segmentation::singleton().getBackgroundId()));
    xml.writeEndElement();

    xml.writeStartElement("editPosition");
    xml.writeAttribute("x", QString::number(state->viewerState->currentPosition.x + 1));
    xml.writeAttribute("y", QString::number(state->viewerState->currentPosition.y + 1));
    xml.writeAttribute("z", QString::number(state->viewerState->currentPosition.z + 1));
    xml.writeEndElement();

    xml.writeStartElement("skeletonVPState");
    for (int j = 0; j < 16; ++j) {
        xml.writeAttribute(QString("E%1").arg(j), QString::number(state->skeletonState->skeletonVpModelView[j]));
    }
    xml.writeAttribute("translateX", QString::number(state->skeletonState->translateX));
    xml.writeAttribute("translateY", QString::number(state->skeletonState->translateY));
    xml.writeEndElement();

    xml.writeStartElement("vpSettingsZoom");
    xml.writeAttribute("XYPlane", QString::number(state->viewer->window->viewportXY.get()->texture.zoomLevel));
    xml.writeAttribute("XZPlane", QString::number(state->viewer->window->viewportXZ.get()->texture.zoomLevel));
    xml.writeAttribute("YZPlane", QString::number(state->viewer->window->viewportYZ.get()->texture.zoomLevel));
    xml.writeAttribute("SkelVP", QString::number(state->skeletonState->zoomLevel));
    xml.writeEndElement();

    xml.writeEndElement(); // end parameters

    for (auto currentTree = state->skeletonState->firstTree.get(); currentTree != nullptr; currentTree = currentTree->next.get()) {
        //Every "thing" (tree) has associated nodes and edges.
        xml.writeStartElement("thing");
        xml.writeAttribute("id", QString::number(currentTree->treeID));

        if (currentTree->colorSetManually) {
            xml.writeAttribute("color.r", QString::number(currentTree->color.r));
            xml.writeAttribute("color.g", QString::number(currentTree->color.g));
            xml.writeAttribute("color.b", QString::number(currentTree->color.b));
            xml.writeAttribute("color.a", QString::number(currentTree->color.a));
        } else {
            xml.writeAttribute("color.r", QString("-1."));
            xml.writeAttribute("color.g", QString("-1."));
            xml.writeAttribute("color.b", QString("-1."));
            xml.writeAttribute("color.a", QString("1."));
        }
        if (currentTree->comment[0] != '\0') {
            xml.writeAttribute("comment", QString(currentTree->comment));
        }

        xml.writeStartElement("nodes");
        for (const auto & node : currentTree->nodes) {
            xml.writeStartElement("node");
            xml.writeAttribute("id", QString::number(node.nodeID));
            xml.writeAttribute("radius", QString::number(node.radius));
            xml.writeAttribute("x", QString::number(node.position.x + 1));
            xml.writeAttribute("y", QString::number(node.position.y + 1));
            xml.writeAttribute("z", QString::number(node.position.z + 1));
            xml.writeAttribute("inVp", QString::number(node.createdInVp));
            xml.writeAttribute("inMag", QString::number(node.createdInMag));
            xml.writeAttribute("time", QString::number(node.timestamp));
            for (auto propertyIt = node.properties.constBegin(); propertyIt != node.properties.constEnd(); ++propertyIt) {
                xml.writeAttribute(propertyIt.key(), propertyIt.value().toString());
            }
            xml.writeEndElement(); // end node
        }
        xml.writeEndElement(); // end nodes

        xml.writeStartElement("edges");
        for (const auto & node : currentTree->nodes) {
            for (const auto & currentSegment : node.segments) {
                if (currentSegment.forward) {
                    xml.writeStartElement("edge");
                    xml.writeAttribute("source", QString::number(currentSegment.source.nodeID));
                    xml.writeAttribute("target", QString::number(currentSegment.target.nodeID));
                    xml.writeEndElement();
                }
            }
        }
        xml.writeEndElement(); // end edges

        xml.writeEndElement(); // end tree
    }

    xml.writeStartElement("comments");
    for (auto currentComment = state->skeletonState->currentComment; currentComment != nullptr; currentComment = currentComment->next) {
        xml.writeStartElement("comment");
        xml.writeAttribute("node", QString::number(currentComment->node->nodeID));
        xml.writeAttribute("content", QString(currentComment->content));
        xml.writeEndElement();
        if (currentComment->next == state->skeletonState->currentComment) {//comment list is circular
            break;
        }
    }
    xml.writeEndElement(); // end comments

    xml.writeStartElement("branchpoints");
    for (const auto branchNodeID : state->skeletonState->branchStack) {
        xml.writeStartElement("branchpoint");
        xml.writeAttribute("id", QString::number(branchNodeID));
        xml.writeEndElement();
    }
    xml.writeEndElement(); // end branchpoints

    xml.writeEndElement(); // end things
    xml.writeEndDocument();
    return true;
}

bool Skeletonizer::loadXmlSkeleton(QIODevice & file, const QString & treeCmtOnMultiLoad) {
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qErrnoWarning("Document not parsed successfully.");
        return false;
    }

    const bool merge = state->skeletonState->mergeOnLoadFlag;
    if (!merge) {
        clearSkeleton();
        // Default for annotations created in very old versions that don't have an annotationTime.
        // It is okay to set it to 0 in newer versions, because the time will be
        // read from file anyway in case of no merge.
        Session::singleton().setAnnotationTime(0);
    }

    // If "createdin"-node does not exist, skeleton was created in a version before 3.2
    state->skeletonState->skeletonCreatedInVersion = "pre-3.2";
    state->skeletonState->skeletonLastSavedInVersion = "pre-3.2";

    QTime bench;
    QXmlStreamReader xml(&file);

    QString experimentName, taskCategory, taskName;
    uint64_t activeNodeID = 0;
    const uint64_t greatestNodeIDbeforeLoading = state->skeletonState->greatestNodeID;
    const int greatestTreeIDbeforeLoading = state->skeletonState->greatestTreeID;
    treeListElement *currentTree;
    Coordinate loadedPosition;
    std::vector<uint64_t> branchVector;
    std::vector<std::pair<uint64_t, QString>> commentsVector;
    std::vector<std::pair<uint64_t, uint64_t>> edgeVector;

    bench.start();
    const auto blockState = this->signalsBlocked();
    blockSignals(true);
    if (!xml.readNextStartElement() || xml.name() != "things") {
        qDebug() << "invalid xml token: " << xml.name();
        return false;
    }
    while(xml.readNextStartElement()) {
        if(xml.name() == "parameters") {
            while(xml.readNextStartElement()) {
                QXmlStreamAttributes attributes = xml.attributes();

                if (xml.name() == "experiment") {
                    experimentName = attributes.value("name").toString();
                } else if (xml.name() == "createdin") {
                    state->skeletonState->skeletonCreatedInVersion = attributes.value("version").toString();
                } else if(xml.name() == "lastsavedin") {
                    state->skeletonState->skeletonLastSavedInVersion = attributes.value("version").toString();
                } else if(xml.name() == "dataset") {
                    QStringRef attribute = attributes.value("path");
                    QString path = attribute.isNull() ? "" : attribute.toString();
                    if (experimentName != state->name) {
                        state->viewer->window->widgetContainer.datasetLoadWidget.loadDataset(path, true);
                    }
                } else if(xml.name() == "MovementArea") {
                    Coordinate movementAreaMin;//0
                    Coordinate movementAreaMax = state->boundary;

                    for (const auto & attribute : attributes) {
                        const auto & name = attribute.name();
                        const auto & value = attribute.value();
                        if (name == "min.x") {
                            movementAreaMin.x = value.toInt();
                        } else if (name == "min.y") {
                            movementAreaMin.y = value.toInt();
                        } else if (name == "min.z") {
                            movementAreaMin.z = value.toInt();
                        } else if (name == "max.x") {
                            movementAreaMax.x = value.toInt();
                        } else if (name == "max.y") {
                            movementAreaMax.y = value.toInt();
                        } else if (name == "max.z") {
                            movementAreaMax.z = value.toInt();
                        }
                    }

                    Session::singleton().updateMovementArea(movementAreaMin, movementAreaMax);//range checked
                } else if(xml.name() == "time" && merge == false) { // in case of a merge the current annotation's time is kept.
                    const auto ms = attributes.value("ms").toULongLong();
                    Session::singleton().setAnnotationTime(ms);
                } else if(xml.name() == "activeNode" && !merge) {
                    QStringRef attribute = attributes.value("id");
                    if(attribute.isNull() == false) {
                        activeNodeID = attribute.toULongLong();
                    }
                } else if(xml.name() == "segmentation") {
                    Segmentation::singleton().setBackgroundId(attributes.value("backgroundId").toULongLong());
                } else if(xml.name() == "editPosition") {
                    QStringRef attribute = attributes.value("x");
                    if(attribute.isNull() == false)
                        loadedPosition.x = attribute.toLocal8Bit().toInt();
                    attribute = attributes.value("y");
                    if(attribute.isNull() == false)
                        loadedPosition.y = attribute.toLocal8Bit().toInt();
                    attribute = attributes.value("z");
                    if(attribute.isNull() == false)
                        loadedPosition.z = attribute.toLocal8Bit().toInt();

                } else if(xml.name() == "skeletonVPState" && !merge) {
                    int j = 0;
                    char element [8];
                    for (j = 0; j < 16; j++){
                        sprintf (element, "E%d", j);
                        QStringRef attribute = attributes.value(element);
                        state->skeletonState->skeletonVpModelView[j] = attribute.toString().toFloat();
                    }
                    glMatrixMode(GL_MODELVIEW);
                    glLoadMatrixf(state->skeletonState->skeletonVpModelView);

                    QStringRef attribute = attributes.value("translateX");
                    if(attribute.isNull() == false) {
                        state->skeletonState->translateX = attribute.toString().toFloat();
                    }
                    attribute = attributes.value("translateY");
                    if(attribute.isNull() == false) {
                        state->skeletonState->translateY = attribute.toString().toFloat();
                    }
                } else if(xml.name() == "vpSettingsZoom") {
                    QStringRef attribute = attributes.value("XYPlane");
                    if(attribute.isNull() == false) {
                        state->viewer->window->viewportXY.get()->texture.zoomLevel = attribute.toString().toFloat();
                    }
                    attribute = attributes.value("XZPlane");
                    if(attribute.isNull() == false) {
                        state->viewer->window->viewportXZ.get()->texture.zoomLevel = attribute.toString().toFloat();
                    }
                    attribute = attributes.value("YZPlane");
                    if(attribute.isNull() == false) {
                        state->viewer->window->viewportYZ.get()->texture.zoomLevel = attribute.toString().toFloat();
                    }
                    attribute = attributes.value("SkelVP");
                    if(attribute.isNull() == false) {
                        state->skeletonState->zoomLevel = attribute.toString().toFloat();
                    }
                } else if(xml.name() == "RadiusLocking") {
                    QStringRef attribute = attributes.value("enableCommentLocking");
                    if(attribute.isNull() == false) {
                        state->skeletonState->lockPositions = attribute.toString().toInt();
                    }
                    attribute = attributes.value("lockingRadius");
                    if(attribute.isNull() == false) {
                        state->skeletonState->lockRadius = attribute.toString().toInt();
                    }
                    attribute = attributes.value("lockToNodesWithComment");
                    if(attribute.isNull() == false) {
                        state->skeletonState->onCommentLock = attribute.toString();
                    }
                } else if(merge == false && xml.name() == "idleTime") { // in case of a merge the current annotation's idleTime is kept.
                    QStringRef attribute = attributes.value("ms");
                    if (attribute.isNull() == false) {
                        const auto annotationTime = Session::singleton().getAnnotationTime();
                        const auto idleTime = attribute.toString().toInt();
                        //subract from annotationTime
                        Session::singleton().setAnnotationTime(annotationTime - idleTime);
                    }
                } else if(xml.name() == "task") {
                    for (auto && attribute : attributes) {
                        auto key = attribute.name();
                        if (key == "category") {
                            taskCategory = attribute.value().toString();
                        } else if (key == "name") {
                            taskName = attribute.value().toString();
                        }
                    }
                }
                xml.skipCurrentElement();
            }
        } else if(xml.name() == "properties") {
            while(xml.readNextStartElement()) {
                if(xml.name() == "property") {
                    const auto attributes = xml.attributes();
                    const auto name = attributes.value("name").toString();
                    const auto type = attributes.value("type").toString();
                    if (name.isEmpty() == false && type.isEmpty() == false) {
                        if (type == "number") {
                            numberProperties.insert(name);
                        }
                        else {
                            textProperties.insert(name);
                        }
                    }
                }
                xml.skipCurrentElement();
            }
        } else if(xml.name() == "branchpoints") {
            while(xml.readNextStartElement()) {
                if(xml.name() == "branchpoint") {
                    QXmlStreamAttributes attributes = xml.attributes();
                    QStringRef attribute = attributes.value("id");
                    if(attribute.isNull() == false) {
                        uint64_t nodeID = attribute.toULongLong();;
                        if(merge) {
                            nodeID += greatestNodeIDbeforeLoading;
                        }
                        branchVector.emplace_back(nodeID);
                    }
                }
                xml.skipCurrentElement();
            }
        } else if(xml.name() == "comments") {
            // comments must be buffered and can only be set after thing nodes were parsed
            // and the skeleton structure was created. This is necessary, because sometimes the
            // comments node comes before the thing nodes.
            while(xml.readNextStartElement()) {
                if(xml.name() == "comment") {
                    QXmlStreamAttributes attributes = xml.attributes();
                    QStringRef attribute = attributes.value("node");
                    uint64_t nodeID = attribute.toULongLong();
                    if(merge) {
                        nodeID += greatestNodeIDbeforeLoading;
                    }
                    attribute = attributes.value("content");
                    if((!attribute.isNull()) && (!attribute.isEmpty())) {
                        commentsVector.emplace_back(nodeID, attribute.toString());
                    }
                }
                xml.skipCurrentElement();
            }
        } else if(xml.name() == "thing") {
            QXmlStreamAttributes attributes = xml.attributes();

            int treeID = 0;
            QStringRef attribute = attributes.value("id");
            if(attribute.isNull() == false) {
                treeID = attribute.toInt();
            }
            // color: -1 causes default color assignment
            color4F neuronColor;
            attribute = attributes.value("color.r");
            if(attribute.isNull() == false) {
                neuronColor.r = attribute.toLocal8Bit().toFloat();
            } else {
                neuronColor.r = -1;
            }
            attribute = attributes.value("color.g");
            if(attribute.isNull() == false) {
                neuronColor.g = attribute.toLocal8Bit().toFloat();
            } else {
                neuronColor.g = -1;
            }
            attribute = attributes.value("color.b");
            if(attribute.isNull() == false) {
                neuronColor.b = attribute.toLocal8Bit().toFloat();
            } else {
                neuronColor.b = -1;
            }
            attribute = attributes.value("color.a");
            if(attribute.isNull() == false) {
                neuronColor.a = attribute.toLocal8Bit().toFloat();
            } else {
                neuronColor.a = -1;
            }

            if(merge) {
                treeID += greatestTreeIDbeforeLoading;
                currentTree = addTreeListElement(treeID, neuronColor);
                treeID = currentTree->treeID;
            } else {
                currentTree = addTreeListElement(treeID, neuronColor);
            }

            attribute = attributes.value("comment"); // the tree comment
            if(attribute.isNull() == false && attribute.length() > 0) {
                addTreeComment(currentTree->treeID, attribute.toString());
            } else {
                addTreeComment(currentTree->treeID, treeCmtOnMultiLoad);
            }

            while (xml.readNextStartElement()) {
                if(xml.name() == "nodes") {
                    while(xml.readNextStartElement()) {
                        if(xml.name() == "node") {
                            uint64_t nodeID = 0;
                            float radius = state->skeletonState->defaultNodeRadius;
                            Coordinate currentCoordinate;
                            ViewportType inVP = VIEWPORT_UNDEFINED;
                            int inMag = 0;
                            uint64_t ms = 0;
                            QVariantHash properties;
                            for (const auto & attribute : xml.attributes()) {
                                const auto & name = attribute.name();
                                const auto & value = attribute.value();
                                if (name == "id") {
                                    nodeID = {value.toULongLong()};
                                } else if (name == "radius") {
                                    radius = {value.toFloat()};
                                } else if (name == "x") {
                                    currentCoordinate.x = {value.toInt() - 1};
                                } else if (name == "y") {
                                    currentCoordinate.y = {value.toInt() - 1};
                                } else if (name == "z") {
                                    currentCoordinate.z = {value.toInt() - 1};
                                } else if (name == "inVp") {
                                    inVP = static_cast<ViewportType>(value.toInt());
                                } else if (name == "inMag") {
                                    inMag = {value.toInt()};
                                } else if (name == "time") {
                                    ms = {value.toULongLong()};
                                } else {
                                    properties.insert(name.toString(), value.toString());
                                }
                            }

                            if (merge) {
                                nodeID += greatestNodeIDbeforeLoading;
                            }
                            addNode(nodeID, radius, treeID, currentCoordinate, inVP, inMag, ms, false, properties);
                        }
                        xml.skipCurrentElement();
                    } // end while nodes
                }
                if(xml.name() == "edges") {
                    while(xml.readNextStartElement()) {
                        if(xml.name() == "edge") {
                            attributes = xml.attributes();
                            uint64_t sourceNodeId = attributes.value("source").toULongLong();
                            uint64_t targetNodeId = attributes.value("target").toULongLong();
                            if(merge) {
                                sourceNodeId += greatestNodeIDbeforeLoading;
                                targetNodeId += greatestNodeIDbeforeLoading;
                            }
                            edgeVector.emplace_back(sourceNodeId, targetNodeId);
                        }
                        xml.skipCurrentElement();
                    }
                }
            }
        } // end thing
    }
    xml.readNext();//</things>
    if (!xml.isEndDocument()) {
        qDebug() << "unknown content following after line" << xml.lineNumber();
        return false;
    }
    if(xml.hasError()) {
        qDebug() << __FILE__ << ":" << __LINE__ << " xml error: " << xml.errorString() << " at " << xml.lineNumber();
        return false;
    }

    auto msg = tr("");
    const auto mismatchedDataset = !experimentName.isEmpty() && experimentName != state->name;
    if (mismatchedDataset) {
        msg += tr("• The annotation (created in dataset “%1”) does not belong to the currently loaded dataset (“%2”).").arg(experimentName).arg(state->name);
    }
    const auto currentTaskCategory = Session::singleton().task.first;
    const auto currentTaskName = Session::singleton().task.second;
    const auto mismatchedTask = !currentTaskCategory.isEmpty() && !currentTaskName.isEmpty() && (currentTaskCategory != taskCategory || currentTaskName != taskName);
    if (mismatchedDataset && mismatchedTask) {
        msg += "\n\n";
    }
    if (mismatchedTask) {
        msg += tr("• The associated task “%1” (%2) is different from the currently active “%3” (%4).").arg(taskName).arg(taskCategory).arg(currentTaskName).arg(currentTaskCategory);
    }
    if (!msg.isEmpty()) {
        QMessageBox msgBox(state->viewer->window);
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setText("Incompatible Annotation File\nAlthough the file was loaded successfully, working with it is not recommended.");
        msgBox.setInformativeText(msg);
        msgBox.exec();
    }

    for (const auto & elem : edgeVector) {
        auto * sourceNode = findNodeByNodeID(elem.first);
        auto * targetNode = findNodeByNodeID(elem.second);
        if(sourceNode != nullptr && targetNode != nullptr) {
            addSegment(*sourceNode, *targetNode);
        } else {
            qDebug() << "Could not add segment between nodes" << elem.first << "and" << elem.second;
        }
    }

    for (const auto & elem : branchVector) {
        const auto & currentNode = findNodeByNodeID(elem);
        if(currentNode != nullptr)
            pushBranchNode(*currentNode);
    }

    for (const auto & elem : commentsVector) {
        const auto & currentNode = findNodeByNodeID(elem.first);
        if(currentNode != nullptr) {
            addComment(elem.second, *currentNode);
        }
    }

    blockSignals(blockState);
    emit resetData();
    emit propertiesChanged(numberProperties);
    qDebug() << "loading skeleton took: "<< bench.elapsed();

    if (!merge) {
        auto * node = Skeletonizer::singleton().findNodeByNodeID(activeNodeID);
        setActiveNode(node);

        if((loadedPosition.x != 0) &&
           (loadedPosition.y != 0) &&
           (loadedPosition.z != 0)) {
            Coordinate jump = loadedPosition - 1 - state->viewerState->currentPosition;
            state->viewer->userMove(jump, USERMOVE_NEUTRAL);
        }
    }
    if (state->skeletonState->activeNode == nullptr && state->skeletonState->firstTree != nullptr) {
        if (!state->skeletonState->firstTree->nodes.empty()) {
            setActiveNode(&state->skeletonState->firstTree->nodes.front());
        }
    }
    return true;
}

bool Skeletonizer::delSegment(std::list<segmentListElement>::iterator segToDelIt) {
    // Delete the segment out of the segment list and out of the visualization structure!
    /* A bit cumbersome, but we cannot delete the segment and then find its source node.. */
    segToDelIt->length = 0.f;
    updateCircRadius(&segToDelIt->source);

    segToDelIt->target.segments.erase(segToDelIt->sisterSegment);
    segToDelIt->source.segments.erase(segToDelIt);

    Session::singleton().unsavedChanges = true;
    return true;
}

/*
 * We have to delete the node from 2 structures: the skeleton's nested linked list structure
 * and the skeleton visualization structure (hashtable with skeletonDCs).
 */
bool Skeletonizer::delNode(uint nodeID, nodeListElement *nodeToDel) {
    if (!nodeToDel) {
        nodeToDel = findNodeByNodeID(nodeID);
    }
    if (!nodeToDel) {
        qDebug("The given node %u doesn't exist. Unable to delete it.", nodeID);
        return false;
    }
    nodeID = nodeToDel->nodeID;
    if (nodeToDel->comment) {
        delComment(nodeToDel->comment, 0);
    }
    if (nodeToDel->isBranchNode) {
        auto foundIt = std::find(std::begin(state->skeletonState->branchStack), std::end(state->skeletonState->branchStack), nodeToDel->nodeID);
        state->skeletonState->branchStack.erase(foundIt);
    }

    for (auto segmentIt = std::begin(nodeToDel->segments); segmentIt != std::end(nodeToDel->segments); segmentIt = std::begin(nodeToDel->segments)) {
        delSegment(segmentIt);
    }

    state->skeletonState->nodesByNodeID.erase(nodeToDel->nodeID);

    if (nodeToDel->selected) {
        auto & selectedNodes = state->skeletonState->selectedNodes;
        const auto eraseit = std::find(std::begin(selectedNodes), std::end(selectedNodes), nodeToDel);
        if (eraseit != std::end(selectedNodes)) {
            selectedNodes.erase(eraseit);
        }
    }

    bool resetActiveNode = state->skeletonState->activeNode == nodeToDel;
    auto tree = nodeToDel->correspondingTree;
    const auto pos = nodeToDel->position;

    unsetSubobjectOfHybridNode(*nodeToDel);
    nodeToDel->correspondingTree->nodes.erase(nodeToDel->iterator);

    emit nodeRemovedSignal(nodeID);

    if (resetActiveNode) {
        auto * newActiveNode = findNearbyNode(tree, pos);
        state->viewer->skeletonizer->setActiveNode(newActiveNode);
    }

    state->skeletonState->totalNodeElements--;

    Session::singleton().unsavedChanges = true;

    return true;
}

bool Skeletonizer::delTree(int treeID) {
    auto * const treeToDel = findTreeByTreeID(treeID);
    if (treeToDel == nullptr) {
        qDebug("There exists no tree with ID %d. Unable to delete it.", treeID);
        return false;
    }

    const auto blockState = this->signalsBlocked();
    blockSignals(true);//chunk operation
    for (auto nodeIt = std::begin(treeToDel->nodes); nodeIt != std::end(treeToDel->nodes); nodeIt = std::begin(treeToDel->nodes)) {
        delNode(0, &*nodeIt);
    }
    blockSignals(blockState);

    state->skeletonState->treesByID.erase(treeToDel->treeID);

    if (treeToDel->selected) {
        auto & selectedTrees = state->skeletonState->selectedTrees;
        const auto eraseit = std::find(std::begin(selectedTrees), std::end(selectedTrees), treeToDel);
        if (eraseit != std::end(selectedTrees)) {
            selectedTrees.erase(eraseit);
        }
    }

    //remove if active tree
    if (treeToDel == state->skeletonState->activeTree) {
        state->skeletonState->activeTree = nullptr;
    }

    if (treeToDel->next != nullptr) {
        treeToDel->next->previous = treeToDel->previous;
    }
    if (treeToDel->previous != nullptr) {
        treeToDel->previous->next = std::move(treeToDel->next);
    } else if (treeToDel == state->skeletonState->firstTree.get()) {
        state->skeletonState->firstTree = std::move(treeToDel->next);
    }

    --state->skeletonState->treeElements;

    //no references to tree left
    emit treeRemovedSignal(treeID);//updates tools

    //if the active tree was not set through the active node but another tree exists
    if (state->skeletonState->activeTree == nullptr && state->skeletonState->firstTree != nullptr) {
        setActiveTreeByID(state->skeletonState->firstTree->treeID);//updates tools too
    }

    Session::singleton().unsavedChanges = true;
    return true;
}

nodeListElement* Skeletonizer::findNearbyNode(treeListElement *nearbyTree, Coordinate searchPosition) {
    nodeListElement *nodeWithCurrentlySmallestDistance = NULL;
    treeListElement *currentTree = NULL;
    float smallestDistance = 0;


    //  If available, search for a node within nearbyTree first.


    if(nearbyTree) {
        for (auto & currentNode : nearbyTree->nodes) {
            // We make the nearest node the next active node
            const floatCoordinate distanceVector = searchPosition - currentNode.position;
            //set nearest distance to distance to first node found, then to distance of any nearer node found.
            if (smallestDistance == 0 || euclidicNorm(distanceVector) < smallestDistance) {
                smallestDistance = euclidicNorm(distanceVector);
                nodeWithCurrentlySmallestDistance = &currentNode;
            }
        }

        if(nodeWithCurrentlySmallestDistance) {
            return nodeWithCurrentlySmallestDistance;
        }
    }

     // Ok, we didn't find any node in nearbyTree.
     // Now we take the nearest node, independent of the tree it belongs to.


    currentTree = state->skeletonState->firstTree.get();
    smallestDistance = 0;
    while(currentTree) {
        for (auto & currentNode : currentTree->nodes) {
            // We make the nearest node the next active node
            const floatCoordinate distanceVector = searchPosition - currentNode.position;
            if (smallestDistance == 0 || euclidicNorm(distanceVector) < smallestDistance) {
                smallestDistance = euclidicNorm(distanceVector);
                nodeWithCurrentlySmallestDistance = &currentNode;
            }
        }
       currentTree = currentTree->next.get();
    }

    if(nodeWithCurrentlySmallestDistance) {
        return nodeWithCurrentlySmallestDistance;
    }

    return NULL;
}

nodeListElement* Skeletonizer::findNodeInRadius(Coordinate searchPosition) {
    nodeListElement *nodeWithCurrentlySmallestDistance = NULL;
    treeListElement *currentTree = NULL;
    float smallestDistance = CATCH_RADIUS;

    currentTree = state->skeletonState->firstTree.get();
    while(currentTree) {
        for (auto & currentNode : currentTree->nodes) {
            //We make the nearest node the next active node
            const floatCoordinate distanceVector = searchPosition - currentNode.position;
            if(euclidicNorm(distanceVector) < smallestDistance) {
                smallestDistance = euclidicNorm(distanceVector);
                nodeWithCurrentlySmallestDistance = &currentNode;
            }
        }
       currentTree = currentTree->next.get();
    }

    if(nodeWithCurrentlySmallestDistance) {
        return nodeWithCurrentlySmallestDistance;
    }

    return NULL;
}

bool Skeletonizer::setActiveTreeByID(int treeID) {
    treeListElement *currentTree;
    currentTree = findTreeByTreeID(treeID);
    if (currentTree == nullptr) {
        qDebug("There exists no tree with ID %d!", treeID);
        return false;
    } else if (currentTree == state->skeletonState->activeTree) {
        return true;
    }

    state->skeletonState->activeTree = currentTree;

    selectTrees({currentTree});

    //switch to nearby node of new tree
    if (state->skeletonState->activeNode != nullptr && state->skeletonState->activeNode->correspondingTree != currentTree) {
        //prevent ping pong if tree was activated from setActiveNode
        auto * node = findNearbyNode(currentTree, state->skeletonState->activeNode->position);
        if (node->correspondingTree == currentTree) {
            setActiveNode(node);
        } else if (!currentTree->nodes.empty()) {
            setActiveNode(&currentTree->nodes.front());
        }
    } else if (state->skeletonState->activeNode == nullptr && !currentTree->nodes.empty()) {
        setActiveNode(&currentTree->nodes.front());
    }

    Session::singleton().unsavedChanges = true;

    return true;
}

bool Skeletonizer::setActiveNode(nodeListElement *node) {
    if (node == state->skeletonState->activeNode) {
        return true;
    }

    state->skeletonState->activeNode = node;

    if (node == nullptr) {
        selectNodes({});
        Segmentation::singleton().clearObjectSelection();
    } else {
        if (!node->selected) {
            selectNodes({node});
        }
        selectObjectForNode(*node);

        setActiveTreeByID(node->correspondingTree->treeID);

        state->skeletonState->commentBuffer.clear();
        if (node->comment) {
            state->skeletonState->currentComment = node->comment;
            state->skeletonState->commentBuffer = state->skeletonState->currentComment->content;
        }
    }

    Session::singleton().unsavedChanges = true;

    return true;
}

uint64_t Skeletonizer::findAvailableNodeID() {
    return state->skeletonState->greatestNodeID + 1;
}

boost::optional<nodeListElement &> Skeletonizer::addNode(uint64_t nodeID, const float radius, const int treeID, const Coordinate & position
        , const ViewportType VPtype, const int inMag, boost::optional<uint64_t> time, const bool respectLocks, const QHash<QString, QVariant> & properties) {
    state->skeletonState->branchpointUnresolved = false;

     // respectLocks refers to locking the position to a specific coordinate such as to
     // disallow tracing areas further than a certain distance away from a specific point in the
     // dataset.
    if (respectLocks) {
        if(state->skeletonState->positionLocked) {
            if (state->viewerState->lockComment == QString(state->skeletonState->onCommentLock)) {
                unlockPosition();
                return boost::none;
            }

            const floatCoordinate lockVector = {(float)(position.x - state->skeletonState->lockedPosition.x),
                                                (float)(position.y - state->skeletonState->lockedPosition.y),
                                                (float)(position.z - state->skeletonState->lockedPosition.z)};
            float lockDistance = euclidicNorm(lockVector);
            if (lockDistance > state->skeletonState->lockRadius) {
                qDebug() << tr("Node is too far away from lock point (%1), not adding.").arg(lockDistance);
                return boost::none;
            }
        }
    }

    if (nodeID == 0 && findNodeByNodeID(nodeID)) {
        qDebug() << tr("Node with ID %1 already exists, no node added.").arg(nodeID);
        return boost::none;
    }
    auto * const tempTree = findTreeByTreeID(treeID);

    if (!tempTree) {
        qDebug() << tr("There exists no tree with the provided ID %1!").arg(treeID);
        return boost::none;
    }

    // One node more in all trees
    ++state->skeletonState->totalNodeElements;

    if (nodeID == 0) {
        nodeID = findAvailableNodeID();
    }

    if (!time) {//time was not provided
        time = Session::singleton().getAnnotationTime() + Session::singleton().currentTimeSliceMs();
    }

    tempTree->nodes.emplace_back(nodeID, radius, position, inMag, VPtype, time.get(), properties, *tempTree);
    auto & tempNode = tempTree->nodes.back();
    tempNode.iterator = std::prev(std::end(tempTree->nodes));
    updateCircRadius(&tempNode);
    updateSubobjectCountFromProperty(tempNode);

    state->skeletonState->nodesByNodeID.emplace(nodeID, &tempNode);

    if (nodeID > state->skeletonState->greatestNodeID) {
        state->skeletonState->greatestNodeID = nodeID;
    }
    Session::singleton().unsavedChanges = true;

    emit nodeAddedSignal(tempNode);

    return tempNode;
}

bool Skeletonizer::addSegment(nodeListElement & sourceNode, nodeListElement & targetNode) {
    if (findSegmentBetween(sourceNode, targetNode) != std::end(sourceNode.segments)) {
        qDebug() << "Segment between nodes" << sourceNode.nodeID << "and" << targetNode.nodeID << "exists already.";
        return false;
    }
    if (sourceNode == targetNode) {
        qDebug() << "Cannot link node with itself!";
        return false;
    }

     // Add the segment to the tree structure
    sourceNode.segments.emplace_back(sourceNode, targetNode);
    targetNode.segments.emplace_back(sourceNode, targetNode, false);//although it’s the ›reverse‹ segment, its direction doesn’t change
    auto sourceSegIt = std::prev(std::end(sourceNode.segments));
    auto targetSegIt = std::prev(std::end(targetNode.segments));
    sourceSegIt->sisterSegment = targetSegIt;
    sourceSegIt->sisterSegment->sisterSegment = sourceSegIt;

    /* Do we really skip this node? Test cum dist. to last rendered node! */
    sourceSegIt->length = sourceSegIt->sisterSegment->length = euclidicNorm(targetNode.position - sourceNode.position);

    updateCircRadius(&sourceNode);

    Session::singleton().unsavedChanges = true;

    return true;
}

void Skeletonizer::toggleLink(nodeListElement & lhs, nodeListElement & rhs) {
    auto segmentIt = findSegmentBetween(lhs, rhs);
    if (segmentIt != std::end(lhs.segments)) {
        delSegment(segmentIt);
    } else if ((segmentIt = findSegmentBetween(rhs, lhs)) != std::end(rhs.segments)) {
        delSegment(segmentIt);
    } else {
        addSegment(lhs, rhs);
    }
}

void Skeletonizer::clearSkeleton() {
    treeListElement *currentTree, *treeToDel;
    auto skeletonState = state->skeletonState;

    const auto blockState = this->signalsBlocked();
    blockSignals(true);
    currentTree = skeletonState->firstTree.get();
    while(currentTree) {
        treeToDel = currentTree;
        currentTree = treeToDel->next.get();
        delTree(treeToDel->treeID);
    }
    skeletonState->firstTree = nullptr;
    blockSignals(blockState);
    emit resetData();

    skeletonState->activeNode = NULL;
    skeletonState->activeTree = NULL;

    skeletonState->nodesByNodeID.clear();
    skeletonState->treesByID.clear();
    skeletonState->branchStack.clear();

    //Generate empty tree structures
    skeletonState->firstTree = NULL;
    skeletonState->totalNodeElements = 0;
    skeletonState->treeElements = 0;
    skeletonState->activeTree = NULL;
    skeletonState->activeNode = NULL;
    skeletonState->greatestNodeID = 0;
    skeletonState->greatestTreeID = 0;

    Session::singleton().resetMovementArea();
    Session::singleton().setAnnotationTime(0);
    state->skeletonState->skeletonCreatedInVersion = KVERSION;
    state->skeletonState->skeletonLastSavedInVersion.clear();
}

bool Skeletonizer::mergeTrees(int treeID1, int treeID2) {
    if(treeID1 == treeID2) {
        qDebug() << "Could not merge trees. Provided IDs are the same!";
        return false;
    }

    treeListElement * const tree1 = findTreeByTreeID(treeID1);
    treeListElement * const tree2 = findTreeByTreeID(treeID2);

    if(!(tree1) || !(tree2)) {
        qDebug() << "Could not merge trees, provided IDs are not valid!";
        return false;
    }

    for (auto & node : tree2->nodes) {
        node.correspondingTree = tree1;
    }
    tree1->nodes.splice(std::end(tree1->nodes), tree2->nodes);
    delTree(tree2->treeID);
    setActiveTreeByID(tree1->treeID);
    emit treesMerged(treeID1, treeID2);//update nodes
    return true;
}

nodeListElement* Skeletonizer::getNodeWithPrevID(nodeListElement *currentNode, bool sameTree) {
    nodeListElement *prevNode = NULL;
    nodeListElement *highestNode = NULL;
    unsigned int minDistance = UINT_MAX;
    unsigned int tempMinDistance = minDistance;
    unsigned int maxID = 0;

    if(currentNode == NULL) {
        // no current node, return active node
        if(state->skeletonState->activeNode) {
            return state->skeletonState->activeNode;
        }
        // no active node, simply return first node found
        treeListElement *tree = state->skeletonState->firstTree.get();
        while(tree) {
            if (!tree->nodes.empty()) {
                return &tree->nodes.front();
            }
            tree = tree->next.get();
        }
        qDebug() << "no nodes to move to";
        return NULL;
    }
    treeListElement *tree;
    if(sameTree) {
         tree = currentNode->correspondingTree;
    }
    else {
        tree = state->skeletonState->firstTree.get();
    }
    while(tree) {
        for (auto & node : tree->nodes) {
            if(node.nodeID > maxID) {
                highestNode = &node;
                maxID = node.nodeID;
            }
            if(node.nodeID < currentNode->nodeID) {
                tempMinDistance = currentNode->nodeID - node.nodeID;

                if(tempMinDistance == 1) { //smallest distance possible
                    return &node;
                }
                if(tempMinDistance < minDistance) {
                    minDistance = tempMinDistance;
                    prevNode = &node;
                }
            }
        }
        if(sameTree) {
            break;
        }
        tree = tree->next.get();
    }

    if(!prevNode && sameTree) {
        prevNode = highestNode;
    }
    return prevNode;
}

nodeListElement* Skeletonizer::getNodeWithNextID(nodeListElement *currentNode, bool sameTree) {
    nodeListElement *nextNode = NULL;
    nodeListElement *lowestNode = NULL;
    unsigned int minDistance = UINT_MAX;
    unsigned int tempMinDistance = minDistance;
    unsigned int minID = UINT_MAX;

    if(currentNode == NULL) {
        // no current node, return active node
        if(state->skeletonState->activeNode) {
            return state->skeletonState->activeNode;
        }
        // no active node, simply return first node found
        treeListElement *tree = state->skeletonState->firstTree.get();
        while(tree) {
            if (!tree->nodes.empty()) {
                return &tree->nodes.front();
            }
            tree = tree->next.get();
        }
        qDebug() << "no nodes to move to";
        return NULL;
    }
    treeListElement *tree;
    if(sameTree) {
         tree = currentNode->correspondingTree;
    }
    else {
        tree = state->skeletonState->firstTree.get();
    }
    while(tree) {
        for (auto & node : tree->nodes) {
            if(node.nodeID < minID) {
                lowestNode = &node;
                minID = node.nodeID;
            }
            if(node.nodeID > currentNode->nodeID) {
                tempMinDistance = node.nodeID - currentNode->nodeID;

                if(tempMinDistance == 1) { //smallest distance possible
                    return &node;
                }
                if(tempMinDistance < minDistance) {
                    minDistance = tempMinDistance;
                    nextNode = &node;
                }
            }
        }
        if(sameTree) {
            break;
        }
        tree = tree->next.get();
    }

    if(!nextNode && sameTree) {
        nextNode = lowestNode;
    }
    return nextNode;
}

nodeListElement* Skeletonizer::findNodeByNodeID(uint nodeID) {
    const auto nodeIt = state->skeletonState->nodesByNodeID.find(nodeID);
    return nodeIt != std::end(state->skeletonState->nodesByNodeID) ? nodeIt->second : nullptr;
}

QList<nodeListElement*> Skeletonizer::findNodesInTree(treeListElement & tree, const QString & comment) {
    QList<nodeListElement *> hits;
    for (auto & node : tree.nodes) {
        if (node.comment != nullptr && QString(node.comment->content).contains(comment)) {
            hits.append(&node);
        }
    }
    return hits;
}

int Skeletonizer::findAvailableTreeID() {
    return state->skeletonState->greatestTreeID + 1;
}

treeListElement* Skeletonizer::addTreeListElement(int treeID, color4F color) {
     // The variable sync is a workaround for the problem that this function
     // will sometimes be called by other syncable functions that themselves hold
     // the lock and handle synchronization. If set to false, all synchro
     // routines will be skipped.

    treeListElement *newElement = NULL;

    newElement = findTreeByTreeID(treeID);
    if(newElement) {
        qDebug("Tree with ID %d already exists!", treeID);
        return newElement;
    }

    newElement = new treeListElement();

    state->skeletonState->treeElements++;

    //Tree ID is unique in tree list
    //Take the provided tree ID if there is one.
    newElement->treeID = treeID != 0 ? treeID : findAvailableTreeID();
    newElement->render = true;
    newElement->selected = false;

    // calling function sets values < 0 when no color was specified
    if(color.r < 0) {//Set a tree color
        const auto blockState = this->signalsBlocked();
        blockSignals(true);
        restoreDefaultTreeColor(*newElement);
        blockSignals(blockState);
    } else {
        newElement->color = color;
    }
    newElement->colorSetManually = false;

    memset(newElement->comment, '\0', 8192);

    //Insert the new tree at the beginning of the tree list
    newElement->next = std::move(state->skeletonState->firstTree);
    newElement->previous = NULL;
    //The old first element should have the new first element as previous element
    if (newElement->next) {
        newElement->next->previous = newElement;
    }
    //We change the old and new first elements
    state->skeletonState->firstTree.reset(newElement);

    state->skeletonState->treesByID.emplace(newElement->treeID, newElement);

    if(newElement->treeID > state->skeletonState->greatestTreeID) {
        state->skeletonState->greatestTreeID = newElement->treeID;
    }

    Session::singleton().unsavedChanges = true;

    emit treeAddedSignal(*newElement);

    setActiveTreeByID(newElement->treeID);

    return newElement;
}

treeListElement *Skeletonizer::getTreeWithPrevID(treeListElement *currentTree) {
    treeListElement *tree = state->skeletonState->firstTree.get();
    treeListElement *prevTree = NULL;
    uint idDistance = state->skeletonState->treeElements;
    uint tempDistance = idDistance;

    if(currentTree == NULL) {
        // no current tree, return active tree
        if(state->skeletonState->activeTree) {
            return state->skeletonState->activeTree;
        }
        // no active tree, simply return first tree found
        if(tree) {
            return tree;
        }
        qDebug() << "no tree to move to";
        return NULL;
    }
    while(tree) {
        if(tree->treeID < currentTree->treeID) {
            tempDistance = currentTree->treeID - tree->treeID;
            if(tempDistance == 1) {//smallest distance possible
                return tree;
            }
            if(tempDistance < idDistance) {
                idDistance = tempDistance;
                prevTree = tree;
            }
        }
        tree = tree->next.get();
    }
    return prevTree;
}

treeListElement* Skeletonizer::getTreeWithNextID(treeListElement *currentTree) {
    treeListElement *tree = state->skeletonState->firstTree.get();
    treeListElement *nextTree = NULL;
    uint idDistance = state->skeletonState->treeElements;
    uint tempDistance = idDistance;

    if(currentTree == NULL) {
        // no current tree, return active tree
        if(state->skeletonState->activeTree) {
            return state->skeletonState->activeTree;
        }
        // no active tree, simply return first tree found
        if(tree) {
            return tree;
        }
        qDebug() << "no tree to move to";
        return NULL;
    }
    while(tree) {
        if(tree->treeID > currentTree->treeID) {
            tempDistance = tree->treeID - currentTree->treeID;

            if(tempDistance == 1) { //smallest distance possible
                return tree;
            }
            if(tempDistance < idDistance) {
                idDistance = tempDistance;
                nextTree = tree;
            }
        }
        tree = tree->next.get();
    }
    return nextTree;
}

bool Skeletonizer::addTreeCommentToSelectedTrees(QString comment) {
    const auto blockState = signalsBlocked();
    blockSignals(true);

    for(const auto &tree : state->skeletonState->selectedTrees) {
        addTreeComment(tree->treeID, comment);
    }

    blockSignals(blockState);

    emit resetData();

    return true;
}

bool Skeletonizer::addTreeComment(int treeID, QString comment) {
    treeListElement *tree = NULL;

    tree = findTreeByTreeID(treeID);

    if((!comment.isNull()) && tree) {
        strncpy(tree->comment, comment.toStdString().c_str(), sizeof(tree->comment) - 1);//space for '\0'
    }

    Session::singleton().unsavedChanges = true;
    emit treeChangedSignal(*tree);

    return true;
}

std::list<segmentListElement>::iterator Skeletonizer::findSegmentBetween(nodeListElement & sourceNode, const nodeListElement & targetNode) {
    for (auto segmentIt = std::begin(sourceNode.segments); segmentIt != std::end(sourceNode.segments); ++segmentIt) {
        if (!segmentIt->forward) {
            continue;
        }
        if (segmentIt->target == targetNode) {
            return segmentIt;
        }
    }
    return std::end(sourceNode.segments);
}

bool Skeletonizer::editNode(uint nodeID, nodeListElement *node, float newRadius, const Coordinate & newPos, int inMag) {
    if(!node) {
        node = findNodeByNodeID(nodeID);
    }
    if(!node) {
        qDebug("Cannot edit: node id %u invalid.", nodeID);
        return false;
    }

    nodeID = node->nodeID;

    auto oldPos = node->position;
    node->position = newPos.capped({0, 0, 0}, state->boundary);

    if(newRadius != 0.) {
        node->radius = newRadius;
    }
    node->createdInMag = inMag;

    updateCircRadius(node);
    Session::singleton().unsavedChanges = true;

    const quint64 newSubobjectId = readVoxel(newPos);
    Skeletonizer::singleton().movedHybridNode(*node, newSubobjectId, oldPos);

    emit nodeChangedSignal(*node);

    return true;
}

bool Skeletonizer::extractConnectedComponent(int nodeID) {
    //  This function takes a node and splits the connected component
    //  containing that node into a new tree, unless the connected component
    //  is equivalent to exactly one entire tree.

    //  It uses breadth-first-search. Depth-first-search would be the same with
    //  a stack instead of a queue for storing pending nodes. There is no
    //  practical difference between the two algorithms for this task.

    auto * node = findNodeByNodeID(nodeID);
    if (!node) {
        return false;
    }

    std::unordered_set<treeListElement*> treesSeen; // Connected component might consist of multiple trees.
    std::unordered_set<nodeListElement*> visitedNodes;
    visitedNodes.insert(node);
    connectedComponent(*node, [&treesSeen, &visitedNodes](nodeListElement & node){
        visitedNodes.emplace(&node);
        treesSeen.emplace(node.correspondingTree);
        return false;
    });

    //  If the total number of nodes visited is smaller than the sum of the
    //  number of nodes in all trees we've seen, the connected component is a
    //  strict subgraph of the graph containing all trees we've seen and we
    //  should split it.

    // We want this function to not do anything when
    // there are no disconnected components in the same tree, but create a new
    // tree when the connected component spans several trees. This is a useful
    // feature when performing skeleton consolidation and allows one to merge
    // many trees at once.
    std::size_t nodeCountSeenTrees = 0;
    for(auto * tree : treesSeen) {
        nodeCountSeenTrees += tree->nodes.size();
    }
    if (visitedNodes.size() == nodeCountSeenTrees && treesSeen.size() == 1) {
        return false;
    }

    color4F treeCol;
    treeCol.r = -1.;
    auto newTree = addTreeListElement(0, treeCol);
    // Splitting the connected component.
    for (auto * nodeIt : visitedNodes) {
        if (nodeIt->correspondingTree->nodes.empty()) {//remove empty trees
            delTree(nodeIt->correspondingTree->treeID);
        }
        // Removing node list element from its old position
        // Inserting node list element into new list.
        newTree->nodes.splice(std::end(newTree->nodes), node->correspondingTree->nodes, node->iterator);
        nodeIt->correspondingTree = newTree;
    }
    Session::singleton().unsavedChanges = true;
    setActiveTreeByID(newTree->treeID);//the empty tree had no active node

    return true;
}

bool Skeletonizer::addComment(QString content, nodeListElement & node) {
    commentListElement *newComment;

    std::string content_stdstr = content.toStdString();
    const char *content_cstr = content_stdstr.c_str();

    newComment = (commentListElement*)malloc(sizeof(commentListElement));
    memset(newComment, '\0', sizeof(commentListElement));

    newComment->content = (char*)malloc(strlen(content_cstr) * sizeof(char) + 1);
    memset(newComment->content, '\0', strlen(content_cstr) * sizeof(char) + 1);

    newComment->node = &node;
    node.comment = newComment;

    if(content_cstr) {
        strncpy(newComment->content, content_cstr, strlen(content_cstr));
    }

    if(!state->skeletonState->currentComment) {
        state->skeletonState->currentComment = newComment;
        //We build a circular linked list
        newComment->next = newComment;
        newComment->previous = newComment;
    }
    else {
        //We insert into a circular linked list
        state->skeletonState->currentComment->previous->next = newComment;
        newComment->next = state->skeletonState->currentComment;
        newComment->previous = state->skeletonState->currentComment->previous;
        state->skeletonState->currentComment->previous = newComment;

        state->skeletonState->currentComment = newComment;
    }


    //write into commentBuffer, so that comment appears in comment text field when added via Shortcut
    state->skeletonState->commentBuffer.clear();
    state->skeletonState->commentBuffer = state->skeletonState->currentComment->content;

    Session::singleton().unsavedChanges = true;
    state->skeletonState->totalComments++;

    emit nodeChangedSignal(node);
    return true;
}

bool Skeletonizer::setComment(QString newContent, nodeListElement *commentNode, uint commentNodeID) {
    if(commentNodeID) {
        commentNode = findNodeByNodeID(commentNodeID);
    }
    if (!commentNode) {
        qDebug() << "Please provide a valid comment node to set!";
        return false;
    }
    if(commentNode->comment) {
        return editComment(commentNode->comment, 0, newContent, NULL, 0);
    }
    return addComment(newContent, *commentNode);
}

bool Skeletonizer::delComment(commentListElement *currentComment, uint commentNodeID) {
    nodeListElement *commentNode = NULL;

    if(commentNodeID) {
        commentNode = findNodeByNodeID(commentNodeID);
        if(commentNode) {
            currentComment = commentNode->comment;
        }
    }

    if(!currentComment) {
        qDebug() << "Please provide a valid comment node to delete!";
        return false;
    }

    if(currentComment->content) {
        free(currentComment->content);
    }
    if(currentComment->node) {
        currentComment->node->comment = NULL;
    }

    if (state->skeletonState->currentComment == currentComment) {
        state->skeletonState->commentBuffer.clear();
    }

    if(currentComment->next == currentComment) {
        state->skeletonState->currentComment = NULL;
    }
    else {
        currentComment->next->previous = currentComment->previous;
        currentComment->previous->next = currentComment->next;

        if(state->skeletonState->currentComment == currentComment) {
            state->skeletonState->currentComment = currentComment->next;
        }
    }

    auto & node = *currentComment->node;
    free(currentComment);

    Session::singleton().unsavedChanges = true;

    state->skeletonState->totalComments--;

    emit nodeChangedSignal(node);

    return true;
}

bool Skeletonizer::editComment(commentListElement *currentComment, uint nodeID, QString newContent,
                               nodeListElement *newNode, uint newNodeID) {
    // this function also seems to be kind of useless as you could do just the same
    // thing with addComment() with minimal changes ....?

    std::string newContent_strstd = newContent.toStdString();
    const char *newContent_cstr = newContent_strstd.c_str();

    if(nodeID) {
        currentComment = findNodeByNodeID(nodeID)->comment;
    }
    if(!currentComment) {
        qDebug() << "Please provide a valid comment element to edit!";
        return false;
    }

    nodeID = currentComment->node->nodeID;

    if(newContent_cstr) {
        if(currentComment->content) {
            free(currentComment->content);
        }
        currentComment->content = (char*)malloc(strlen(newContent_cstr) * sizeof(char) + 1);
        memset(currentComment->content, '\0', strlen(newContent_cstr) * sizeof(char) + 1);
        strncpy(currentComment->content, newContent_cstr, strlen(newContent_cstr));
    }

    if(newNodeID) {
        newNode = findNodeByNodeID(newNodeID);
    }
    if(newNode) {
        if(currentComment->node) {
            currentComment->node->comment = NULL;
        }
        currentComment->node = newNode;
        newNode->comment = currentComment;
    }

    Session::singleton().unsavedChanges = true;

    emit nodeChangedSignal(*currentComment->node);

    return true;
}

commentListElement* Skeletonizer::nextComment(QString searchString) {
   commentListElement *firstComment, *currentComment;

   std::string searchString_stdstr = searchString.toStdString();
   const char *searchString_cstr = searchString_stdstr.c_str();

    if(!strlen(searchString_cstr)) {
        //->previous here because it would be unintuitive for the user otherwise.
        //(we insert new comments always as first elements)
        if(state->skeletonState->currentComment) {
            state->skeletonState->currentComment = state->skeletonState->currentComment->previous;
            setActiveNode(state->skeletonState->currentComment->node);
            jumpToNode(*state->skeletonState->currentComment->node);
        }
    }
    else {
        if(state->skeletonState->currentComment) {
            firstComment = state->skeletonState->currentComment->previous;
            currentComment = firstComment;
            do {
                if(strstr(currentComment->content, searchString_cstr) != NULL) {
                    state->skeletonState->currentComment = currentComment;
                    setActiveNode(state->skeletonState->currentComment->node);
                    jumpToNode(*state->skeletonState->currentComment->node);
                    break;
                }
                currentComment = currentComment->previous;

            } while (firstComment != currentComment);
        }
    }

    state->skeletonState->commentBuffer.clear();

    if (state->skeletonState->currentComment != nullptr) {
        state->skeletonState->commentBuffer = state->skeletonState->currentComment->content;
        if (state->skeletonState->lockPositions) {
            if (state->skeletonState->commentBuffer == state->skeletonState->onCommentLock) {
                lockPosition(state->skeletonState->currentComment->node->position);
            } else {
                unlockPosition();
            }
        }
    }
    return state->skeletonState->currentComment;
}

commentListElement* Skeletonizer::previousComment(QString searchString) {
    commentListElement *firstComment, *currentComment;
    // ->next here because it would be unintuitive for the user otherwise.
    // (we insert new comments always as first elements)

    std::string searchString_stdstr = searchString.toStdString();
    const char *searchString_cstr = searchString_stdstr.c_str();

    if(!strlen(searchString_cstr)) {
        if(state->skeletonState->currentComment) {
            state->skeletonState->currentComment = state->skeletonState->currentComment->next;
            setActiveNode(state->skeletonState->currentComment->node);
            jumpToNode(*state->skeletonState->currentComment->node);
        }
    }
    else {
        if(state->skeletonState->currentComment) {
            firstComment = state->skeletonState->currentComment->next;
            currentComment = firstComment;
            do {
                if(strstr(currentComment->content, searchString_cstr) != NULL) {
                    state->skeletonState->currentComment = currentComment;
                    setActiveNode(state->skeletonState->currentComment->node);
                    jumpToNode(*state->skeletonState->currentComment->node);
                    break;
                }
                currentComment = currentComment->next;

            } while (firstComment != currentComment);

        }
    }

    state->skeletonState->commentBuffer.clear();

    if (state->skeletonState->currentComment != nullptr) {
        state->skeletonState->commentBuffer = state->skeletonState->currentComment->content;
        if (state->skeletonState->lockPositions) {
            if (state->skeletonState->commentBuffer == state->skeletonState->onCommentLock) {
                lockPosition(state->skeletonState->currentComment->node->position);
            } else {
                unlockPosition();
            }
        }
    }
    return state->skeletonState->currentComment;
}

bool Skeletonizer::searchInComment(char */*searchString*/, commentListElement */*comment*/) {
    // BUG unimplemented method!
    return true;
}

bool Skeletonizer::unlockPosition() {
    if(state->skeletonState->positionLocked) {
        qDebug() << "Spatial locking disabled.";
    }
    state->skeletonState->positionLocked = false;

    return true;
}

bool Skeletonizer::lockPosition(Coordinate lockCoordinate) {
    qDebug("locking to (%d, %d, %d).", lockCoordinate.x, lockCoordinate.y, lockCoordinate.z);
    state->skeletonState->positionLocked = true;
    state->skeletonState->lockedPosition = lockCoordinate;

    return true;
}

nodeListElement * Skeletonizer::popBranchNode() {
    if (state->skeletonState->branchStack.empty()) {
        return nullptr;
    }
    std::size_t branchNodeID = state->skeletonState->branchStack.back();
    state->skeletonState->branchStack.pop_back();

    auto * branchNode = findNodeByNodeID(branchNodeID);
    assert(branchNode->isBranchNode);
    branchNode->isBranchNode = false;
    state->skeletonState->branchpointUnresolved = true;
    qDebug() << "Branch point" << branchNodeID << "deleted.";

    setActiveNode(branchNode);
    state->viewer->userMove(branchNode->position - state->viewerState->currentPosition, USERMOVE_NEUTRAL);

    emit branchPoppedSignal();
    Session::singleton().unsavedChanges = true;
    return branchNode;
}

void Skeletonizer::pushBranchNode(nodeListElement & branchNode) {
    if (!branchNode.isBranchNode) {
        state->skeletonState->branchStack.emplace_back(branchNode.nodeID);
        branchNode.isBranchNode = true;
        qDebug() << "Branch point" << branchNode.nodeID << "added.";
        emit branchPushedSignal();
        Session::singleton().unsavedChanges = true;
    } else {
        qDebug() << "Active node is already a branch point";
    }
}

void Skeletonizer::jumpToNode(const nodeListElement & node) {
    state->viewer->userMove(node.position - state->viewerState->currentPosition, USERMOVE_NEUTRAL);
}

nodeListElement* Skeletonizer::popBranchNodeAfterConfirmation(QWidget * const parent) {
    if (state->skeletonState->branchStack.empty()) {
        QMessageBox box(parent);
        box.setIcon(QMessageBox::Information);
        box.setText("No branch points remain.");
        box.exec();
        qDebug() << "No branch points remain.";
    } else if (state->skeletonState->branchpointUnresolved) {
        QMessageBox prompt(parent);
        prompt.setIcon(QMessageBox::Question);
        prompt.setText("Unresolved branch point. \nDo you really want to jump to the next one?");
        prompt.setInformativeText("No node has been added after jumping to the last branch point.");
        prompt.addButton("Jump anyway", QMessageBox::AcceptRole);
        auto * cancel = prompt.addButton("Cancel", QMessageBox::RejectRole);

        prompt.exec();
        if (prompt.clickedButton() == cancel) {
            return nullptr;
        }
    }

    return popBranchNode();
}

void Skeletonizer::restoreDefaultTreeColor(treeListElement & tree) {
    const auto index = (tree.treeID - 1) % state->viewerState->treeColors.size();
    tree.color.r = std::get<0>(state->viewerState->treeColors[index]) / MAX_COLORVAL;
    tree.color.g = std::get<1>(state->viewerState->treeColors[index]) / MAX_COLORVAL;
    tree.color.b = std::get<2>(state->viewerState->treeColors[index]) / MAX_COLORVAL;
    tree.color.a = 1.;

    tree.colorSetManually = false;
    Session::singleton().unsavedChanges = true;
    emit treeChangedSignal(tree);
}

void Skeletonizer::updateTreeColors() {
    for (auto * tree = state->skeletonState->firstTree.get(); tree != nullptr; tree = tree->next.get()) {
        restoreDefaultTreeColor(*tree);
    }
}

/**
 * @brief Skeletonizer::updateCircRadius if both the source and target node of a segment are outside the viewport,
 *      the segment would be culled away, too, regardless of wether it is in the viewing frustum.
 *      This function solves this by making the circ radius of one node as large as the segment,
 *      so that it cuts the viewport and its segment is rendered.
 * @return
 */
bool Skeletonizer::updateCircRadius(nodeListElement *node) {
    node->circRadius = singleton().radius(*node);
    /* Any segment longer than the current circ radius?*/
    for (const auto & currentSegment : node->segments) {
        if (currentSegment.length > node->circRadius) {
            node->circRadius = currentSegment.length;
        }
    }
    return true;
}

float Skeletonizer::radius(const nodeListElement & node) const {
    const auto propertyName = state->viewerState->highlightedNodePropertyByRadius;
    if(!propertyName.isEmpty() && node.properties.contains(propertyName)) {
        return state->viewerState->nodePropertyRadiusScale * node.properties.value(propertyName).toDouble();
    }
    else if(node.comment && CommentSetting::useCommentNodeRadius) {
        float newRadius = CommentSetting::getRadius(QString(node.comment->content));
        if(newRadius != 0) {
            return newRadius;
        }
    }
    return state->viewerState->overrideNodeRadiusBool ? state->viewerState->overrideNodeRadiusVal : node.radius;
}

float Skeletonizer::segmentSizeAt(const nodeListElement & node) const {
    float radius = state->viewerState->overrideNodeRadiusBool ? state->viewerState->overrideNodeRadiusVal : node.radius;
    if(node.comment && CommentSetting::useCommentNodeRadius) {
        float newRadius = CommentSetting::getRadius(QString(node.comment->content));
        if(newRadius != 0 && newRadius < radius) {
            return newRadius;
        }
    }
    return radius;
}

bool Skeletonizer::moveToPrevTree() {
    treeListElement *prevTree = getTreeWithPrevID(state->skeletonState->activeTree);
    if(state->skeletonState->activeTree == nullptr) {
        return false;
    }
    if(prevTree) {
        setActiveTreeByID(prevTree->treeID);
        //set tree's first node to active node if existent
        if (state->skeletonState->activeTree->nodes.empty()) {
            return true;
        } else {
            auto & node = state->skeletonState->activeTree->nodes.front();
            setActiveNode(&node);
            emit setRecenteringPositionSignal(node.position);
            Knossos::sendRemoteSignal();
        }
        return true;
    }
    QMessageBox info;
    info.setIcon(QMessageBox::Information);
    info.setWindowFlags(Qt::WindowStaysOnTopHint);
    info.setWindowTitle("Information");
    info.setText("You reached the first tree.");
    info.exec();

    return false;
}


bool Skeletonizer::moveToNextTree() {
    treeListElement *nextTree = getTreeWithNextID(state->skeletonState->activeTree);
    if(state->skeletonState->activeTree == nullptr) {
        return false;
    }
    if(nextTree) {
        setActiveTreeByID(nextTree->treeID);
        //set tree's first node to active node if existent

        if (state->skeletonState->activeTree->nodes.empty()) {
            return true;
        } else {
            auto & node = state->skeletonState->activeTree->nodes.front();
            setActiveNode(&node);
            emit setRecenteringPositionSignal(node.position);
            Knossos::sendRemoteSignal();
        }
        return true;
    }
    QMessageBox info;
    info.setIcon(QMessageBox::Information);
    info.setWindowFlags(Qt::WindowStaysOnTopHint);
    info.setWindowTitle("Information");
    info.setText("You reached the last tree.");
    info.exec();

    return false;
}

bool Skeletonizer::moveToPrevNode() {
    nodeListElement *prevNode = getNodeWithPrevID(state->skeletonState->activeNode, true);
    if(prevNode) {
        setActiveNode(prevNode);
        emit setRecenteringPositionSignal(prevNode->position);
        Knossos::sendRemoteSignal();
        return true;
    }
    return false;
}

bool Skeletonizer::moveToNextNode() {
    nodeListElement *nextNode = getNodeWithNextID(state->skeletonState->activeNode, true);
    if(nextNode) {
        setActiveNode(nextNode);
        emit setRecenteringPositionSignal(nextNode->position);
        Knossos::sendRemoteSignal();
        return true;
    }
    return false;
}

bool Skeletonizer::moveSelectedNodesToTree(int treeID) {
    auto * newTree = findTreeByTreeID(treeID);
    for (auto * const node : state->skeletonState->selectedNodes) {
        if (node == nullptr || newTree == nullptr || node->correspondingTree->treeID == newTree->treeID) {
            return false;
        }
        newTree->nodes.splice(std::end(newTree->nodes), node->correspondingTree->nodes, node->iterator);
        node->correspondingTree = newTree;
    }

    emit resetData();
    emit setActiveTreeByID(treeID);

    return true;
}

void Skeletonizer::selectNodes(QSet<nodeListElement*> nodes) {
    for (auto & elem : state->skeletonState->selectedNodes) {
        if (nodes.contains(elem)) {//if selected node is already selected ignore
            nodes.remove(elem);
        } else {//unselect everything else
            nodes.insert(elem);
        }
    }
    toggleNodeSelection(nodes);
}

void Skeletonizer::toggleNodeSelection(const QSet<nodeListElement*> & nodes) {
    auto & selectedNodes = state->skeletonState->selectedNodes;

    std::unordered_set<decltype(nodeListElement::nodeID)> removeNodes;
    for (auto & elem : nodes) {
        elem->selected = !elem->selected;//toggle
        if (elem->selected) {
            selectedNodes.emplace_back(elem);
        } else {
            removeNodes.emplace(elem->nodeID);
        }
    }
    auto eraseIt = std::remove_if(std::begin(selectedNodes), std::end(selectedNodes), [&removeNodes](nodeListElement const * const node){
        return removeNodes.find(node->nodeID) != std::end(removeNodes);
    });
    if (eraseIt != std::end(selectedNodes)) {
        selectedNodes.erase(eraseIt, std::end(selectedNodes));
    }

    if (selectedNodes.size() == 1) {
        setActiveNode(selectedNodes.front());
    } else if (selectedNodes.empty() && state->skeletonState->activeNode != nullptr) {
        selectNodes({state->skeletonState->activeNode});// at least one must always be selected
        return;
    }
    emit nodeSelectionChangedSignal();
}

void Skeletonizer::selectTrees(const std::vector<treeListElement*> & trees) {
    auto & selectedTrees = state->skeletonState->selectedTrees;
    for (auto &selectedTree : selectedTrees) {
        selectedTree->selected = false;
    }
    for (auto & elem : trees) {
        elem->selected = true;
    }
    selectedTrees = trees;

    if (selectedTrees.size() == 1) {
        setActiveTreeByID(selectedTrees.front()->treeID);
    } else if (selectedTrees.empty() && state->skeletonState->activeTree != nullptr) {
        selectTrees({state->skeletonState->activeTree});// at least one must always be selected
        return;
    }
    emit treeSelectionChangedSignal();
}

void Skeletonizer::deleteSelectedTrees() {
    //make a copy because the selected objects can change
    const auto treesToDelete = state->skeletonState->selectedTrees;
    const auto blockstate = signalsBlocked();
    blockSignals(true);
    for (const auto & elem : treesToDelete) {
        delTree(elem->treeID);
    }
    blockSignals(blockstate);
    emit resetData();
}

void Skeletonizer::deleteSelectedNodes() {
    //make a copy because the selected objects can change
    const auto nodesToDelete = state->skeletonState->selectedNodes;
    const auto blockstate = signalsBlocked();
    blockSignals(true);
    for (auto & elem : nodesToDelete) {
        delNode(0, elem);
    }
    blockSignals(blockstate);
    emit resetData();
}

bool Skeletonizer::areConnected(const nodeListElement & lhs,const nodeListElement & rhs) const {
    return connectedComponent(lhs, [&rhs](const nodeListElement & node){
        return node == rhs;
    });
}
