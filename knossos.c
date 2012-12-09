/*
 *  This file is a part of KNOSSOS.
 *
 *  (C) Copyright 2007-2012
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

/*
 *      This file contains the main function. It should
 *      read the configuration and parameters, compute some important figures,
 *      put them all together into a state structure, pass it to the threads
 *      and wait for everything to finish gracefully.
 *
 */
#define GLUT_DISABLE_ATEXIT_HACK

#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>
#include <SDL/SDL_Clipboard.h>

#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glut.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#ifdef LINUX
#include <signal.h>
#endif

#include "knossos-global.h"
#include "knossos.h"
#include "treeLUT_fallback.h"
#include "y.tab.h"
#include "lex.yy.h"

#ifdef main
#undef main
#endif

struct stateInfo *tempConfig = NULL;
struct stateInfo *state = NULL;

int main(int argc, char *argv[]) {
    SDL_Thread *loadingThread = NULL, *viewingThread = NULL, *remoteThread = NULL, *clientThread = NULL, *refreshTimeThread = NULL;
    char consoleInput[1024];

    memset(consoleInput, '\0', 1024);

#ifdef LINUX
    glutInit(&argc, argv);
    signal(SIGSEGV, (sig_t)catchSegfault);
#endif

    //FILE *temporaryStorage = stdout;
    //stdout = fopen("stdout.txt","w");

    //fclose(stdout);
   // stdout = temporaryStorage;



    SDL_Init(SDL_INIT_TIMER);

    // The idea behind all this is that we have four sources of
    // configuration data:
    //
    //  * Arguments passed by KUKU
    //  * Local knossos.conf
    //  * knossos.conf that comes with the data
    //  * Default parameters
    //
    // All this config data should be used. Command line overrides
    // local knossos.conf and local knossos.conf overrides knossos.conf
    // from data and knossos.conf from data overrides defaults.

    state = emptyState();

    state->loadSignal = FALSE;
    state->remoteSignal = FALSE;
    state->quitSignal = FALSE;
    state->clientSignal = FALSE;
    state->conditionLoadSignal = SDL_CreateCond();
    state->conditionRemoteSignal = SDL_CreateCond();
    state->conditionClientSignal = SDL_CreateCond();
    state->protectSkeleton = SDL_CreateMutex();
    state->protectLoadSignal = SDL_CreateMutex();
    state->protectRemoteSignal = SDL_CreateMutex();
    state->protectClientSignal = SDL_CreateMutex();
    state->protectCube2Pointer = SDL_CreateMutex();
    state->protectPeerList = SDL_CreateMutex();
    state->protectOutBuffer = SDL_CreateMutex();

    if(tempConfigDefaults() != TRUE) {
        LOG("Error loading default parameters.");
        _Exit(FALSE);
    }

    if(argc >= 2)
        configFromCli(argc, argv);

    if(tempConfig->path[0] != '\0') {
        // Got a path from cli.
        readDataConfAndLocalConf();
        // We need to read the specified config file again because it should
        // override all parameters from other config files.
        configFromCli(argc, argv);
    }
    else
        readConfigFile("knossos.conf");


    state->viewerState->voxelDimX = tempConfig->scale.x;
    state->viewerState->voxelDimY = tempConfig->scale.y;
    state->viewerState->voxelDimZ = tempConfig->scale.z;

    if(argc >= 2) {
        if(configFromCli(argc, argv) == FALSE) {
            LOG("Error reading configuration from command line.");
        }
    }

    if(initStates() != TRUE) {
        LOG("Error during initialization of the state struct.");
        _Exit(FALSE);
    }

    printConfigValues();

    viewingThread = SDL_CreateThread(viewer, NULL);
    loadingThread = SDL_CreateThread(loader, NULL);
    remoteThread = SDL_CreateThread(remote, NULL);
    clientThread = SDL_CreateThread(client, NULL);
    refreshTimeThread = SDL_CreateThread(refreshTime, NULL);

    SDL_WaitThread(loadingThread, NULL);
    SDL_WaitThread(remoteThread, NULL);
    SDL_WaitThread(viewingThread, NULL);
    SDL_WaitThread(clientThread, NULL);
    SDL_WaitThread(refreshTimeThread, NULL);
    SDL_Quit();

    cleanUpMain();

    return 0;
}

static uint32_t cleanUpMain() {
    //conditions?
    free(tempConfig->viewerState);
    free(tempConfig->remoteState);
    free(tempConfig->clientState);
    free(tempConfig->loaderState);
    free(tempConfig);
    free(state->viewerState);
    free(state->remoteState);
    free(state->clientState);
    free(state->loaderState);
    free(state);

    return TRUE;
}

static int32_t tempConfigDefaults() {
    uint32_t i = 0;

    if(tempConfig == NULL) {
        tempConfig = emptyState();
        if(tempConfig == NULL)
            return FALSE;
    }

    /* General stuff */
    tempConfig->boergens = FALSE;
    tempConfig->boundary.x = 1000;
    tempConfig->boundary.y = 1000;
    tempConfig->boundary.z = 1000;
    tempConfig->scale.x = 1.;
    tempConfig->scale.y = 1.;
    tempConfig->scale.z = 1.;
    tempConfig->offset.x = 0;
    tempConfig->offset.y = 0;
    tempConfig->offset.z = 0;
    tempConfig->cubeEdgeLength = 128;
    tempConfig->M = 3;
    tempConfig->magnification = 1;
    tempConfig->overlay = FALSE;

    /* For the viewer */
    tempConfig->viewerState->highlightVp = VIEWPORT_UNDEFINED;
    tempConfig->viewerState->vpKeyDirection[VIEWPORT_XY] = 1;
    tempConfig->viewerState->vpKeyDirection[VIEWPORT_XZ] = 1;
    tempConfig->viewerState->vpKeyDirection[VIEWPORT_YZ] = 1;
    tempConfig->viewerState->overlayVisible = FALSE;
    tempConfig->viewerState->datasetColortableOn = FALSE;
    tempConfig->viewerState->datasetAdjustmentOn = FALSE;
    tempConfig->viewerState->treeColortableOn = FALSE;
    tempConfig->viewerState->viewerReady = FALSE;
    tempConfig->viewerState->drawVPCrosshairs = TRUE;
    tempConfig->viewerState->showVPLabels = FALSE;
    tempConfig->viewerState->stepsPerSec = 40;
    tempConfig->viewerState->numberViewPorts = 4;
    tempConfig->viewerState->inputmap = NULL;
    tempConfig->viewerState->dropFrames = 1;
    tempConfig->viewerState->userMove = FALSE;
    tempConfig->viewerState->screenSizeX = 1024;
    tempConfig->viewerState->screenSizeY = 740;
    tempConfig->viewerState->filterType = GL_LINEAR;
    tempConfig->viewerState->currentPosition.x = 0;
    tempConfig->viewerState->currentPosition.y = 0;
    tempConfig->viewerState->currentPosition.z = 0;
    tempConfig->viewerState->voxelDimX = tempConfig->scale.x;
    tempConfig->viewerState->voxelDimY = tempConfig->scale.y;
    tempConfig->viewerState->voxelDimZ = tempConfig->scale.z;
    tempConfig->viewerState->voxelXYRatio = tempConfig->viewerState->voxelDimX / tempConfig->viewerState->voxelDimY;
    tempConfig->viewerState->voxelXYtoZRatio = tempConfig->viewerState->voxelDimX / tempConfig->viewerState->voxelDimZ;
    tempConfig->viewerState->depthCutOff = 5.;
    tempConfig->viewerState->luminanceBias = 0;
    tempConfig->viewerState->luminanceRangeDelta = 255;
    tempConfig->viewerState->autoTracingDelay = 50;
    tempConfig->viewerState->autoTracingSteps = 10;
    tempConfig->viewerState->recenteringTimeOrth = 500;
    tempConfig->viewerState->walkOrth = FALSE;
    tempConfig->viewerState->changeViewportPosSiz = 0;

    tempConfig->viewerState->viewPorts = malloc(tempConfig->viewerState->numberViewPorts * sizeof(struct viewPort));
    if(tempConfig->viewerState->viewPorts == NULL) {
        LOG("Out of memory.");
        return FALSE;
    }

    memset(tempConfig->viewerState->viewPorts, '\0', tempConfig->viewerState->numberViewPorts * sizeof(struct viewPort));

    for(i = 0; i < tempConfig->viewerState->numberViewPorts; i++) {
        switch(i) {
        case VIEWPORT_XY:
            SET_COORDINATE(tempConfig->viewerState->viewPorts[i].upperLeftCorner, 5, 30, 0);
            tempConfig->viewerState->viewPorts[i].type = VIEWPORT_XY;
            break;
        case VIEWPORT_XZ:
            SET_COORDINATE(tempConfig->viewerState->viewPorts[i].upperLeftCorner, 5, 385, 0);
            tempConfig->viewerState->viewPorts[i].type = VIEWPORT_XZ;
            break;
        case VIEWPORT_YZ:
            SET_COORDINATE(tempConfig->viewerState->viewPorts[i].upperLeftCorner, 360, 30, 0);
            tempConfig->viewerState->viewPorts[i].type = VIEWPORT_YZ;
            break;
        case VIEWPORT_SKELETON:
            SET_COORDINATE(tempConfig->viewerState->viewPorts[i].upperLeftCorner, 360, 385, 0);
            tempConfig->viewerState->viewPorts[i].type = VIEWPORT_SKELETON;
            break;
        }

        tempConfig->viewerState->viewPorts[i].draggedNode = NULL;
        tempConfig->viewerState->viewPorts[i].userMouseSlideX = 0.;
        tempConfig->viewerState->viewPorts[i].userMouseSlideY = 0.;
        tempConfig->viewerState->viewPorts[i].edgeLength = 350;
        tempConfig->viewerState->viewPorts[i].texture.texUnitsPerDataPx = 1. / TEXTURE_EDGE_LEN;
        tempConfig->viewerState->viewPorts[i].texture.edgeLengthPx = TEXTURE_EDGE_LEN;
        tempConfig->viewerState->viewPorts[i].texture.edgeLengthDc = TEXTURE_EDGE_LEN / tempConfig->cubeEdgeLength;

        //This variable indicates the current zoom value for a viewport.
        //Zooming is continous, 1: max zoom out, 0.1: max zoom in (adjust values..)
        tempConfig->viewerState->viewPorts[i].texture.zoomLevel = VPZOOMMIN;
    }

    /* For the GUI */
    snprintf(tempConfig->viewerState->ag->settingsFile, 2048, "defaultSettings.xml");

    /* For the client */
    tempConfig->clientState->connectAsap = FALSE;
    tempConfig->clientState->connectionTimeout = 3000;
    tempConfig->clientState->remotePort = 7890;
    strncpy(tempConfig->clientState->serverAddress, "localhost", 1024);
    tempConfig->clientState->connected = FALSE;
    tempConfig->clientState->synchronizeSkeleton = TRUE;
    tempConfig->clientState->synchronizePosition = TRUE;
    tempConfig->clientState->saveMaster = FALSE;

    /* For the remote */
    tempConfig->remoteState->activeTrajectory = 0;
    tempConfig->remoteState->maxTrajectories = 16;
    tempConfig->remoteState->type = FALSE;

    /* For the skeletonizer */
    tempConfig->skeletonState->lockRadius = 100;
    tempConfig->skeletonState->lockPositions = FALSE;
    strncpy(tempConfig->skeletonState->onCommentLock, "seed", 1024);
    tempConfig->skeletonState->branchpointUnresolved = FALSE;
    tempConfig->skeletonState->autoFilenameIncrementBool = TRUE;
    tempConfig->skeletonState->autoSaveBool = TRUE;
    tempConfig->skeletonState->autoSaveInterval = 5;
    tempConfig->skeletonState->skeletonTime = 0;
    tempConfig->skeletonState->skeletonTimeCorrection = SDL_GetTicks();
    tempConfig->skeletonState->definedSkeletonVpView = 0;

    //This number is currently arbitrary, but high values ensure a good performance
    tempConfig->skeletonState->skeletonDCnumber = 8000;
    tempConfig->skeletonState->workMode = ON_CLICK_DRAG;

    return TRUE;
}

static int32_t configFromCli(int argCount, char *arguments[]) {
    #define NUM_PARAMS 15

    char *lval = NULL, *rval = NULL;
    char *equals = NULL;
    int32_t i = 0, j = 0, llen = 0;
    char *lvals[NUM_PARAMS] = {
                                "--data-path",       // 0  Do not forget
                                "--connect-asap",    // 1  to also modify
                                "--scale-x",         // 2  NUM_PARAMS
                                "--scale-y",         // 3  above when adding
                                "--scale-z",         // 4  switches!
                                "--boundary-x",      // 5
                                "--boundary-y",      // 6
                                "--boundary-z",      // 7
                                "--experiment-name", // 8
                                "--supercube-edge",  // 9
                                "--color-table",     // 10
                                "--config-file",     // 11
                                "--magnification",   // 12
                                "--overlay",         // 13
                                "--profile"          // 14
                              };

    for(i = 1; i < argCount; i++) {
        /*
         * Everything right of the equals sign is the rvalue, everythin left of
         * it the lvalue, or, if there is no equals sign, everything is lvalue.
         */

        llen = strlen(arguments[i]);
        equals = strchr(arguments[i], '=');

        if(equals) {
            if(equals[1] != '\0') {
                llen = strlen(arguments[i]) - strlen(equals);
                equals++;
                rval = malloc((strlen(equals) + 1) * sizeof(char));
                if(rval == NULL) {
                    LOG("Out of memory.");
                    _Exit(FALSE);
                }
                memset(rval, '\0', strlen(equals) + 1);

                strncpy(rval, equals, strlen(equals));
            }
            else
                llen = strlen(arguments[i]) - 1;
        }
        lval = malloc((llen + 1) * sizeof(char));
        if(lval == NULL) {
            LOG("Out of memory.");
            _Exit(FALSE);
        }
        memset(lval, '\0', llen + 1);

        strncpy(lval, arguments[i], llen);

        for(j = 0; j < NUM_PARAMS; j++) {
            if(strcmp(lval, lvals[j]) == 0) {
                switch(j) {
                    case 0:
                        strncpy(tempConfig->path, rval, 1023);
                        break;
                    case 1:
                        tempConfig->clientState->connectAsap = TRUE;
                        break;
                    case 2:
                        tempConfig->scale.x = (float)strtod(rval, NULL);
                        break;
                    case 3:
                        tempConfig->scale.y = (float)strtod(rval, NULL);
                        break;
                    case 4:
                        tempConfig->scale.z = (float)strtod(rval, NULL);
                        break;
                    case 5:
                        tempConfig->boundary.x = (int32_t)atoi(rval);
                        break;
                    case 6:
                        tempConfig->boundary.y = (int32_t)atoi(rval);
                        break;
                    case 7:
                        tempConfig->boundary.z = (int32_t)atoi(rval);
                        break;
                    case 8:
                        strncpy(tempConfig->name, rval, 1023);
                        break;
                    case 9:
                        tempConfig->M = (int32_t)atoi(rval);
                        break;
                    case 10:
                        loadDatasetColorTable(rval, &(state->viewerState->datasetColortable[0][0]), GL_RGB);
                        break;
                    case 11:
                        readConfigFile(rval);
                        break;
                    case 12:
                        tempConfig->magnification = (int32_t)atoi(rval);
                        break;
                    case 13:
                        tempConfig->overlay = TRUE;
                        tempConfig->viewerState->overlayVisible = TRUE;
                        break;
                    case 14:
                        strncpy(tempConfig->viewerState->ag->settingsFile, rval, 2000);
                        strcpy(tempConfig->viewerState->ag->settingsFile + strlen(rval), ".xml");
                        break;
                }
            }
        }
    }

    return TRUE;
}

static int32_t initStates() {
    uint32_t i;

    /* General stuff */
    state->boergens = tempConfig->boergens;
    strncpy(state->path, tempConfig->path, 1024);
    strncpy(state->name, tempConfig->name, 1024);
    state->boundary.x = tempConfig->boundary.x;
    state->boundary.y = tempConfig->boundary.y;
    state->boundary.z = tempConfig->boundary.z;
    state->scale.x = tempConfig->scale.x;
    state->scale.y = tempConfig->scale.y;
    state->scale.z = tempConfig->scale.z;
    state->offset.x = tempConfig->offset.x;
    state->offset.y = tempConfig->offset.y;
    state->offset.z = tempConfig->offset.z;
    state->cubeEdgeLength = tempConfig->cubeEdgeLength;

    if(tempConfig->M % 2 == 0) {
        state->M = tempConfig->M - 1;
        tempConfig->M = state->M;
    }
    else {
        state->M = tempConfig->M;
    }

    state->magnification = tempConfig->magnification;
    state->overlay = tempConfig->overlay;


    /* For the viewer */
    state->viewerState->highlightVp = tempConfig->viewerState->highlightVp;
    state->viewerState->vpKeyDirection[VIEWPORT_XY] = tempConfig->viewerState->vpKeyDirection[VIEWPORT_XY];
    state->viewerState->vpKeyDirection[VIEWPORT_XZ] = tempConfig->viewerState->vpKeyDirection[VIEWPORT_XZ];
    state->viewerState->vpKeyDirection[VIEWPORT_YZ] = tempConfig->viewerState->vpKeyDirection[VIEWPORT_YZ];
    state->viewerState->overlayVisible = tempConfig->viewerState->overlayVisible;
    state->viewerState->datasetColortableOn = tempConfig->viewerState->datasetColortableOn;
    state->viewerState->datasetAdjustmentOn = tempConfig->viewerState->datasetAdjustmentOn;
    state->viewerState->treeColortableOn = tempConfig->viewerState->treeColortableOn;
    state->viewerState->drawVPCrosshairs = tempConfig->viewerState->drawVPCrosshairs;
    state->viewerState->showVPLabels = tempConfig->viewerState->showVPLabels;
    state->viewerState->viewerReady = tempConfig->viewerState->viewerReady;
    state->viewerState->stepsPerSec = tempConfig->viewerState->stepsPerSec;
    state->viewerState->numberViewPorts = tempConfig->viewerState->numberViewPorts;
    state->viewerState->inputmap = tempConfig->viewerState->inputmap;
    state->viewerState->dropFrames = tempConfig->viewerState->dropFrames;
    state->viewerState->userMove = tempConfig->viewerState->userMove;
    state->viewerState->screenSizeX = tempConfig->viewerState->screenSizeX;
    state->viewerState->screenSizeY = tempConfig->viewerState->screenSizeY;
    state->viewerState->filterType = tempConfig->viewerState->filterType;
    state->viewerState->currentPosition.x = tempConfig->viewerState->currentPosition.x;
    state->viewerState->currentPosition.y = tempConfig->viewerState->currentPosition.y;
    state->viewerState->currentPosition.z = tempConfig->viewerState->currentPosition.z;
    state->viewerState->recenteringTimeOrth = tempConfig->viewerState->recenteringTimeOrth;
    state->viewerState->walkOrth = tempConfig->viewerState->walkOrth;
    state->viewerState->autoTracingMode = 0;
    state->viewerState->autoTracingDelay = 50;
    state->viewerState->autoTracingSteps = 10;
    state->skeletonState->idleTimeSession = 0;
    state->viewerState->changeViewportPosSiz = tempConfig->viewerState->changeViewportPosSiz;
    /* the voxel dim stuff needs an cleanup. this is such a mess. fuck */
    state->viewerState->voxelDimX = state->scale.x;
    state->viewerState->voxelDimY = state->scale.y;
    state->viewerState->voxelDimZ = state->scale.z;
    state->viewerState->voxelXYRatio = state->scale.x / state->scale.y;
    state->viewerState->voxelXYtoZRatio = state->scale.x / state->scale.z;
    state->viewerState->depthCutOff = tempConfig->viewerState->depthCutOff;
    state->viewerState->luminanceBias = tempConfig->viewerState->luminanceBias;
    state->viewerState->luminanceRangeDelta = tempConfig->viewerState->luminanceRangeDelta;
    loadNeutralDatasetLUT(&(state->viewerState->neutralDatasetTable[0][0]));
    loadDefaultTreeLUT();
    state->viewerState->treeLutSet = FALSE;

    state->viewerState->viewPorts = malloc(state->viewerState->numberViewPorts * sizeof(struct viewPort));
    if(state->viewerState->viewPorts == NULL)
        return FALSE;
    memset(state->viewerState->viewPorts, '\0', state->viewerState->numberViewPorts * sizeof(struct viewPort));

    for(i = 0; i < state->viewerState->numberViewPorts; i++) {
        state->viewerState->viewPorts[i].upperLeftCorner = tempConfig->viewerState->viewPorts[i].upperLeftCorner;
        state->viewerState->viewPorts[i].type = tempConfig->viewerState->viewPorts[i].type;
        state->viewerState->viewPorts[i].draggedNode = tempConfig->viewerState->viewPorts[i].draggedNode;
        state->viewerState->viewPorts[i].userMouseSlideX = tempConfig->viewerState->viewPorts[i].userMouseSlideX;
        state->viewerState->viewPorts[i].userMouseSlideY = tempConfig->viewerState->viewPorts[i].userMouseSlideY;
        state->viewerState->viewPorts[i].edgeLength = tempConfig->viewerState->viewPorts[i].edgeLength;
        state->viewerState->viewPorts[i].texture.texUnitsPerDataPx =
            tempConfig->viewerState->viewPorts[i].texture.texUnitsPerDataPx
            / (float)state->magnification;

        state->viewerState->viewPorts[i].texture.edgeLengthPx = tempConfig->viewerState->viewPorts[i].texture.edgeLengthPx;
        state->viewerState->viewPorts[i].texture.edgeLengthDc = tempConfig->viewerState->viewPorts[i].texture.edgeLengthDc;
        state->viewerState->viewPorts[i].texture.zoomLevel = tempConfig->viewerState->viewPorts[i].texture.zoomLevel;
        state->viewerState->viewPorts[i].texture.usedTexLengthPx = tempConfig->M * tempConfig->cubeEdgeLength;
        state->viewerState->viewPorts[i].texture.usedTexLengthDc = tempConfig->M;
/* old version, smaller buffer
        state->viewerState->viewPorts[i].texture.displayedEdgeLengthX = tempConfig->viewerState->viewPorts[i].texture.displayedEdgeLengthY =
                    (((float)(tempConfig->M / 2) - 0.5) * (float)tempConfig->cubeEdgeLength)
                    / (float) tempConfig->viewerState->viewPorts[i].texture.edgeLengthPx
                    * 2.;
*/
        /* make the buffer a bit smaller to increase the FOV.. this might make M=3 actually useful for the small price of less buffering! */
/*        state->viewerState->viewPorts[i].texture.displayedEdgeLengthX = tempConfig->viewerState->viewPorts[i].texture.displayedEdgeLengthY =
                    ((((float)(tempConfig->M / 2) - 0.5) * (float)tempConfig->cubeEdgeLength) * 1.5)
                    / (float) tempConfig->viewerState->viewPorts[i].texture.edgeLengthPx
                    * 2.;
*/
/* latest version */

/*
        state->viewerState->viewPorts[i].texture.displayedEdgeLengthX =
            tempConfig->viewerState->viewPorts[i].texture.displayedEdgeLengthY =
                    ((((float)(tempConfig->M / 2) - 0.5)  * 1.5) * 2.
                    / (float) tempConfig->viewerState->viewPorts[i].texture.edgeLengthPx
                    * (float) tempConfig->cubeEdgeLength);
        state->viewerState->viewPorts[i].texture.displayedEdgeLengthX =
            tempConfig->viewerState->viewPorts[i].texture.displayedEdgeLengthY =

            * (float) tempConfig->cubeEdgeLength)
            / (float) tempConfig->viewerState->viewPorts[i].texture.edgeLengthPx;

*/
    }

    if(state->M * state->cubeEdgeLength >= TEXTURE_EDGE_LEN) {
        LOG("Please choose smaller values for M or N. Your choice exceeds the KNOSSOS texture size!");
        return FALSE;
    }

    calcDisplayedEdgeLength();

    /* For the GUI */
    strncpy(state->viewerState->ag->settingsFile,
            tempConfig->viewerState->ag->settingsFile,
            2048);

    /* For the client */

    state->clientState->connectAsap = tempConfig->clientState->connectAsap;
    state->clientState->connectionTimeout = tempConfig->clientState->connectionTimeout;
    state->clientState->remotePort = tempConfig->clientState->remotePort;
    strncpy(state->clientState->serverAddress, tempConfig->clientState->serverAddress, 1024);
    state->clientState->connected = tempConfig->clientState->connected;

    state->clientState->inBuffer = malloc(sizeof(struct IOBuffer));
    if(state->clientState->inBuffer == NULL) {
        LOG("Out of memory.");
        return FALSE;
    }
    memset(state->clientState->inBuffer, '\0', sizeof(struct IOBuffer));

    state->clientState->inBuffer->data = malloc(128);
    if(state->clientState->inBuffer->data == NULL) {
        LOG("Out of memory.");
        return FALSE;
    }
    memset(state->clientState->inBuffer->data, '\0', 128);

    state->clientState->inBuffer->size = 128;
    state->clientState->inBuffer->length = 0;

    state->clientState->outBuffer = malloc(sizeof(struct IOBuffer));
    if(state->clientState->outBuffer == NULL) {
        LOG("Out of memory.");
        return FALSE;
    }
    memset(state->clientState->outBuffer, '\0', sizeof(struct IOBuffer));

    state->clientState->outBuffer->data = malloc(128);
    if(state->clientState->outBuffer->data == NULL) {
        LOG("Out of memory.");
        return FALSE;
    }
    memset(state->clientState->outBuffer->data, '\0', 128);

    state->clientState->outBuffer->size = 128;
    state->clientState->outBuffer->length = 0;
    state->clientState->synchronizeSkeleton = tempConfig->clientState->synchronizeSkeleton;
    state->clientState->synchronizePosition = tempConfig->clientState->synchronizePosition;
    state->clientState->saveMaster = tempConfig->clientState->saveMaster;

    /* For the skeletonizer */
    state->skeletonState->lockPositions = tempConfig->skeletonState->lockPositions;
    state->skeletonState->positionLocked = tempConfig->skeletonState->positionLocked;
    state->skeletonState->lockRadius = tempConfig->skeletonState->lockRadius;
    SET_COORDINATE(state->skeletonState->lockedPosition,
                   tempConfig->skeletonState->lockedPosition.x,
                   tempConfig->skeletonState->lockedPosition.y,
                   tempConfig->skeletonState->lockedPosition.y);
    strcpy(state->skeletonState->onCommentLock, tempConfig->skeletonState->onCommentLock);
    state->skeletonState->branchpointUnresolved = tempConfig->skeletonState->branchpointUnresolved;
    state->skeletonState->autoFilenameIncrementBool = tempConfig->skeletonState->autoFilenameIncrementBool;
    state->skeletonState->autoSaveBool = tempConfig->skeletonState->autoSaveBool;
    state->skeletonState->autoSaveInterval = tempConfig->skeletonState->autoSaveInterval;
    state->skeletonState->skeletonTime = tempConfig->skeletonState->skeletonTime;
    state->skeletonState->skeletonTimeCorrection = tempConfig->skeletonState->skeletonTimeCorrection;
    state->skeletonState->definedSkeletonVpView = tempConfig->skeletonState->definedSkeletonVpView;
    strcpy(state->skeletonState->skeletonCreatedInVersion, "3.2");
    state->skeletonState->idleTime = 0;
    state->skeletonState->idleTimeNow = 0;
    state->skeletonState->idleTimeLast = 0;

    /* For the remote */
    state->remoteState->activeTrajectory = tempConfig->remoteState->activeTrajectory;
    state->remoteState->maxTrajectories = tempConfig->remoteState->maxTrajectories;
    state->remoteState->type = tempConfig->remoteState->type;

    /* Those values can be calculated from given parameters */
    state->cubeSliceArea = state->cubeEdgeLength * state->cubeEdgeLength;
    state->cubeBytes = state->cubeEdgeLength * state->cubeEdgeLength * state->cubeEdgeLength;
    state->cubeSetElements = state->M * state->M * state->M;
    state->cubeSetBytes = state->cubeSetElements * state->cubeBytes;

    SET_COORDINATE(state->currentPositionX, 0, 0, 0);

    // We're not doing stuff in parallel, yet. So we skip the locking
    // part.
    // This *10 thing is completely arbitrary. The larger the table size,
    // the lower the chance of getting collisions and the better the loading
    // order will be respected. *10 doesn't seem to have much of an effect
    // on performance but we should try to find the optimal value some day.
    // Btw: A more clever implementation would be to use an array exactly the
    // size of the supercube and index using the modulo operator.
    // sadly, that realization came rather late. ;)

    /* creating the hashtables is cheap, keeping the datacubes is
     * memory expensive..  */
    for(i = 0; i <= NUM_MAG_DATASETS; i = i * 2) {
        state->Dc2Pointer[log2uint32(i)] = ht_new(state->cubeSetElements * 10);
        state->Oc2Pointer[log2uint32(i)] = ht_new(state->cubeSetElements * 10);
        if(i == 0) i = 1;
    }

    /* searches for multiple mag datasets and enables multires if more
     * than one was found */
    findAndRegisterAvailableDatasets();

    return TRUE;
}

int32_t printConfigValues() {
    printf("Configuration:\n\tExperiment:\n\t\tPath: %s\n\t\tName: %s\n\t\tBoundary (x): %d\n\t\tBoundary (y): %d\n\t\tBoundary (z): %d\n\t\tScale (x): %f\n\t\tScale (y): %f\n\t\tScale (z): %f\n\n\tData:\n\t\tCube bytes: %d\n\t\tCube edge length: %d\n\t\tCube slice area: %d\n\t\tM (cube set edge length): %d\n\t\tCube set elements: %d\n\t\tCube set bytes: %d\n\t\tZ-first cube order: %d\n",
           state->path,
           state->name,
           state->boundary.x,
           state->boundary.y,
           state->boundary.z,
           state->scale.x,
           state->scale.y,
           state->scale.z,
           state->cubeBytes,
           state->cubeEdgeLength,
           state->cubeSliceArea,
           state->M,
           state->cubeSetElements,
           state->cubeSetBytes,
           state->boergens);

    return TRUE;
}

/*
static uint32_t printUsage() {
    printf("Usage: knossos [path to knossos.conf]\n");

    return TRUE;
}

static uint32_t isPathString(char *string) {
    int i = 0;

    for(i = 0; i < strlen(string); i++) {
        // Check if isgraph is appropriate (Especially between
        // Windows / Unix).
        if(!isgraph(string[i])) return FALSE;
    }

    return TRUE;
}
*/

int32_t sendLoadSignal(uint32_t x, uint32_t y, uint32_t z, int32_t magChanged) {
    SDL_LockMutex(state->protectLoadSignal);

    state->loadSignal = TRUE;
    state->datasetChangeSignal = magChanged;

    /* Convert the coordinate to the right mag. The loader
     * is agnostic to the different dataset magnifications.
     * The int division is hopefully not too much of an issue
     * here */

    SET_COORDINATE(state->currentPositionX,
                   x / state->magnification,
                   y / state->magnification,
                   z / state->magnification);

    SDL_UnlockMutex(state->protectLoadSignal);

    SDL_CondSignal(state->conditionLoadSignal);

    return TRUE;
}

int32_t sendClientSignal() {
    SDL_LockMutex(state->protectClientSignal);
    state->clientSignal = TRUE;
    SDL_UnlockMutex(state->protectClientSignal);

    SDL_CondSignal(state->conditionClientSignal);

    return TRUE;
}

int32_t sendRemoteSignal() {
    SDL_LockMutex(state->protectRemoteSignal);
    state->remoteSignal = TRUE;
    SDL_UnlockMutex(state->protectRemoteSignal);

    SDL_CondSignal(state->conditionRemoteSignal);

    return TRUE;
}

int32_t unlockSkeleton(int32_t increment) {
    /* We cannot increment the revision count if the skeleton change was
     * not successfully commited (i.e. the skeleton changing function encountered
     * an error). In that case, the connection has to be closed and the user
     * must be notified. */

     /*
      * Increment signals either success or failure of the operation that made
      * locking the skeleton necessary.
      * It's here as a parameter for historical reasons and will be removed soon
      * unless it turns out to be useful for something else...
      *
      */

    SDL_UnlockMutex(state->protectSkeleton);

    return TRUE;
}

int32_t lockSkeleton(int32_t targetRevision) {
    /*
     * If a skeleton modifying function is called on behalf of the network client,
     * targetRevision should be set to the appropriate remote value and lockSkeleton()
     * will decide whether to commit the change or if the skeletons have gone out of sync.
     * (This means that the return value of this function if very important and always
     * has to be checked. If the function returns a failure, the skeleton change cannot
     * proceed.)
     * If the function is being called on behalf of the user, targetRevision should be
     * set to CHANGE_MANUAL (== 0).
     *
     */

     SDL_LockMutex(state->protectSkeleton);

     if(targetRevision != CHANGE_MANUAL) {
         /* We can only commit a remote skeleton change if the remote revision count
          * is exactly the local revision count plus 1.
          * If the function changing the skeleton encounters an error, unlockSkeleton() has
          * to be called without incrementing the local revision count and the skeleton
          * synchronization has to be cancelled */

        /* printf("Recieved skeleton delta to revision %d, local revision is %d.\n",
                targetRevision, state->skeletonState->skeletonRevision);
         */

        if(targetRevision != state->skeletonState->skeletonRevision + 1) {
            // Local and remote skeletons have gone out of sync.
            skeletonSyncBroken();
            return FALSE;
        }
     }

     return TRUE;
}

int32_t sendQuitSignal() {
    UI_saveSettings();

    state->quitSignal = TRUE;

    sendRemoteSignal();
    sendClientSignal();

    SDL_LockMutex(state->protectLoadSignal);
    state->loadSignal = TRUE;
    SDL_UnlockMutex(state->protectLoadSignal);

    SDL_CondSignal(state->conditionLoadSignal);
    return TRUE;
}

static int32_t readDataConfAndLocalConf() {
    int32_t length;
    char configFile[1024];

    memset(configFile, '\0', 1024);
    length = strlen(tempConfig->path);

    if(length >= 1010) {
        // We need to append "/knossos.conf"
        LOG("Data path too long.");
        _Exit(FALSE);
    }

    strcat(configFile, tempConfig->path);
    strcat(configFile, "/knossos.conf");

    LOG("Trying to read %s.", configFile);

    readConfigFile(configFile);

    readConfigFile("knossos.conf");

    return TRUE;
}

struct stateInfo *emptyState() {
    struct stateInfo *state = NULL;

    state = malloc(sizeof(struct stateInfo));
    if(state == NULL)
        return FALSE;
    memset(state, '\0', sizeof(struct stateInfo));

    state->viewerState = malloc(sizeof(struct viewerState));
    if(state->viewerState == NULL)
        return FALSE;
    memset(state->viewerState, '\0', sizeof(struct viewerState));

    state->viewerState->ag = malloc(sizeof(struct agConfig));
    if(state->viewerState->ag == NULL)
        return FALSE;
    memset(state->viewerState->ag, '\0', sizeof(struct agConfig));

    state->remoteState = malloc(sizeof(struct remoteState));
    if(state->remoteState == NULL)
        return FALSE;
    memset(state->remoteState, '\0', sizeof(struct remoteState));

    state->clientState = malloc(sizeof(struct clientState));
    if(state->clientState == NULL)
        return FALSE;
    memset(state->clientState, '\0', sizeof(struct clientState));

    state->loaderState = malloc(sizeof(struct loaderState));
    if(state->loaderState == NULL)
        return FALSE;
    memset(state->loaderState, '\0', sizeof(struct loaderState));

    state->skeletonState = malloc(sizeof(struct skeletonState));
    if(state->skeletonState == NULL)
        return FALSE;
    memset(state->skeletonState, '\0', sizeof(struct skeletonState));

    return state;
}

int32_t stripNewlines(char *string) {
        int32_t i = 0;

        for(i = 0; string[i] != '\0'; i++) {
            if(string[i] == '\n')
                string[i] = ' ';
        }

        return TRUE;
}

int32_t readConfigFile(char *path) {
    FILE *configFile;
    size_t bytesRead;
    char fileBuffer[16384];
    YY_BUFFER_STATE confParseBuffer;

    configFile = fopen(path, "r");

    if(configFile != NULL) {
        memset(fileBuffer, '\0', 16384);
        bytesRead = fread(fileBuffer, 1, 16383, configFile);
        if(bytesRead > 0) {
            stripNewlines(fileBuffer);
            confParseBuffer = yy_scan_string(fileBuffer);
            yyparse(state);
            yy_delete_buffer(confParseBuffer);
            fclose(configFile);
            return TRUE;
        }
    }
    return FALSE;

}

int32_t loadNeutralDatasetLUT(GLuint *datasetLut) {
    int32_t i;

    for(i = 0; i < 256; i++) {
        datasetLut[0 * 256 + i] = i;
        datasetLut[1 * 256 + i] = i;
        datasetLut[2 * 256 + i] = i;
    }

    return TRUE;
}

int32_t loadDefaultTreeLUT() {
    if(loadTreeColorTable("default.lut", &(state->viewerState->defaultTreeTable[0]), GL_RGB) == FALSE) {
        loadTreeLUTFallback();
        treeColorAdjustmentsChanged();
	}
	return TRUE;
}
/* searches and registers other mags of the dataset that was given when K started,
allowing K to dynamically switch the mag when the user zooms out or in */
static int32_t findAndRegisterAvailableDatasets() {
    /* state->path stores the path to the dataset K was launched with */
    uint32_t currMag, i;
    uint32_t isMultiresCompatible = FALSE;
    char currPath[1024];
    char levelUpPath[1024];
    char currKconfPath[1024];
    char datasetBaseDirName[1024];
    char datasetBaseExpName[1024];
    int32_t isPathSepTerminated = FALSE;
    uint32_t pathLen;

    memset(currPath, '\0', 1024);
    memset(levelUpPath, '\0', 1024);
    memset(currKconfPath, '\0', 1024);
    memset(datasetBaseDirName, '\0', 1024);
    memset(datasetBaseExpName, '\0', 1024);


    /* Analyze state->name to find out whether K was launched with
     * a dataset that allows multires. */

    /* Multires is only enabled if K is launched with mag1!
     * Launching it with another dataset than mag1 leads to the old
     * behavior, that only this mag is shown, this happens also
     * when the path contains no mag string. */

    if(state->path[strlen(state->path) - 1] == '/'
       || state->path[strlen(state->path) - 1] == '\\') {
        isPathSepTerminated = TRUE;
    }

    if(isPathSepTerminated) {
        if(strncmp(&state->path[strlen(state->path) - 5], "mag1", 4) == 0) {
            isMultiresCompatible = TRUE;
        }
    }
    else {
        if(strncmp(&state->path[strlen(state->path) - 4], "mag1", 4) == 0) {
            isMultiresCompatible = TRUE;
        }
    }

    if(isMultiresCompatible && (state->magnification == 1)) {
        /* take base path and go one level up */
        pathLen = strlen(state->path);

        for(i = 1; i < pathLen; i++) {
            if((state->path[pathLen-i] == '\\')
                || (state->path[pathLen-i] == '/')) {
                if(i == 1) {
                    /* This is the trailing path separator, ignore. */
                    isPathSepTerminated = TRUE;
                    continue;
                }
                /* this contains the path "one level up" */
                strncpy(levelUpPath, state->path, pathLen - i + 1);
                levelUpPath[pathLen - i + 1] = '\0';
                /* this contains the dataset dir without "mag1"
                 * K must be launched with state->path set to the
                 * mag1 dataset for multires to work! This is by convention. */
                if(isPathSepTerminated) {
                    strncpy(datasetBaseDirName, state->path + pathLen - i + 1, i - 6);
                    datasetBaseDirName[i - 6] = '\0';
                }
                else {
                    strncpy(datasetBaseDirName, state->path + pathLen - i + 1, i - 5);
                    datasetBaseDirName[i - 5] = '\0';
                }

                break;
            }
        }

        state->lowestAvailableMag = INT_MAX;
        state->highestAvailableMag = 1;
        //currMag = 1;

        /* iterate over all possible mags and test their availability */
        for(currMag = 1; currMag <= NUM_MAG_DATASETS; currMag *= 2) {

            /* compile the path to the currently tested directory */
            //if(i!=0) currMag *= 2;
    #ifdef LINUX
            sprintf(currPath,
                "%s%smag%d/",
                levelUpPath,
                datasetBaseDirName,
                currMag);
    #else
            sprintf(currPath,
                "%s%smag%d\\",
                levelUpPath,
                datasetBaseDirName,
                currMag);
    #endif
            FILE *testKconf;
            sprintf(currKconfPath, "%s%s", currPath, "knossos.conf");

            /* try fopen() on knossos.conf of currently tested dataset */
            if ((testKconf = fopen(currKconfPath, "r"))) {

                if(state->lowestAvailableMag > currMag) {
                    state->lowestAvailableMag = currMag;
                }
                state->highestAvailableMag = currMag;

                fclose(testKconf);
                /* add dataset path to magPaths; magPaths is used by the loader */

                strcpy(state->magPaths[log2uint32(currMag)], currPath);

                /* the last 4 letters are "mag1" by convention; if not,
                 * K multires won't work */
                strncpy(datasetBaseExpName,
                        state->name,
                        strlen(state->name)-4);
                datasetBaseExpName[strlen(state->name)-4] = '\0';

                strncpy(state->datasetBaseExpName,
                        datasetBaseExpName,
                        strlen(datasetBaseExpName)-1);
                state->datasetBaseExpName[strlen(datasetBaseExpName)-1] = '\0';

                sprintf(state->magNames[log2uint32(currMag)], "%smag%d", datasetBaseExpName, currMag);
            } else break;
        }

        if(state->lowestAvailableMag == INT_MAX) {
            /* This can happen if a bug in the string parsing above causes knossos to
             * search the wrong directories. We exit here to prevent guaranteed
             * subsequent crashes. */

            LOG("Unsupported data path format.");
            _Exit(FALSE);
        }

        /* Do not enable multires by default, even if more than one dataset was found.
         * Multires might be confusing to untrained tracers! Experts can easily enable it..
         * The loaded gui config might lock K to the current mag later one, which is fine. */
        if(state->highestAvailableMag > 1) {
            state->viewerState->datasetMagLock = TRUE;
        }

        if(state->highestAvailableMag > NUM_MAG_DATASETS) {
            state->highestAvailableMag = NUM_MAG_DATASETS;
            LOG("KNOSSOS currently supports only datasets downsampled by a factor of %d. This can easily be changed in the source.", NUM_MAG_DATASETS);
        }

        state->magnification = state->lowestAvailableMag;
        /*state->boundary.x *= state->magnification;
        state->boundary.y *= state->magnification;
        state->boundary.z *= state->magnification;

        state->scale.x /= (float)state->magnification;
        state->scale.y /= (float)state->magnification;
        state->scale.z /= (float)state->magnification;*/

    }
    /* no magstring found, take mag read from .conf file of dataset */
    else {
        /* state->magnification already contains the right mag! */

        pathLen = strlen(state->path);
        if(!pathLen) {
            LOG("No valid dataset specified.\n");
            _Exit(FALSE);
        }

        if((state->path[pathLen-1] == '\\')
           || (state->path[pathLen-1] == '/')) {
        #ifdef LINUX
            state->path[pathLen-1] = '/';
        #else
            state->path[pathLen-1] = '\\';
        #endif
        }
        else {
        #ifdef LINUX
            state->path[pathLen] = '/';
        #else
            state->path[pathLen] = '\\';
        #endif
            state->path[pathLen + 1] = '\0';
        }

        /* the loader uses only magNames and magPaths */
        strcpy(state->magNames[log2uint32(state->magnification)], state->name);
        strcpy(state->magPaths[log2uint32(state->magnification)], state->path);

        state->lowestAvailableMag = state->magnification;
        state->highestAvailableMag = state->magnification;

        state->boundary.x *= state->magnification;
        state->boundary.y *= state->magnification;
        strcpy(state->datasetBaseExpName, state->name);
        state->boundary.z *= state->magnification;

        state->scale.x /= (float)state->magnification;
        state->scale.y /= (float)state->magnification;
        state->scale.z /= (float)state->magnification;

        state->viewerState->datasetMagLock = TRUE;


    }
    return TRUE;
}

/* copied from http://aggregate.org/MAGIC/#Log2%20of%20an%20Integer;  */
uint32_t log2uint32(register uint32_t x) {

    x |= (x >> 1);
    x |= (x >> 2);
    x |= (x >> 4);
    x |= (x >> 8);
    x |= (x >> 16);

	return(ones32(x >> 1));
}

/* http://aggregate.org/MAGIC/#Log2%20of%20an%20Integer */
uint32_t ones32(register uint32_t x) {
        /* 32-bit recursive reduction using SWAR...
	   but first step is mapping 2-bit values
	   into sum of 2 1-bit values in sneaky way
	*/
        x -= ((x >> 1) & 0x55555555);
        x = (((x >> 2) & 0x33333333) + (x & 0x33333333));
        x = (((x >> 4) + x) & 0x0f0f0f0f);
        x += (x >> 8);
        x += (x >> 16);
        return(x & 0x0000003f);
}



#ifdef LINUX
static int32_t catchSegfault(int signum) {
    LOG("Oops, you found a bug. Tell the developers!");
    fflush(stdout);
    fflush(stderr);

    _Exit(FALSE);

}
#endif
