#include "functions.h"

#include <boost/math/constants/constants.hpp>

#include <cmath>

/** this file contains function which are not dependent from any state */

constexpr bool inRange(const int value, const int min, const int max) {
    return value >= min && value < max;
}

bool insideCurrentSupercube(const Coordinate & coord, const Coordinate & center, const int & cubesPerDimension, const int & cubeSize) {
    const int halfSupercube = cubeSize * cubesPerDimension * 0.5;
    const int xcube = center.x - center.x % cubeSize + cubeSize / 2;
    const int ycube = center.y - center.y % cubeSize + cubeSize / 2;
    const int zcube = center.z - center.z % cubeSize + cubeSize / 2;
    bool valid = true;
    valid &= inRange(coord.x, xcube - halfSupercube, xcube + halfSupercube);
    valid &= inRange(coord.y, ycube - halfSupercube, ycube + halfSupercube);
    valid &= inRange(coord.z, zcube - halfSupercube, zcube + halfSupercube);
    return valid;
}

bool currentlyVisible(const Coordinate & coord, const Coordinate & center, const int & cubesPerDimension, const int & cubeSize) {
    const bool valid = insideCurrentSupercube(coord, center, cubesPerDimension, cubeSize);
    const int xmin = center.x - center.x % cubeSize;
    const int ymin = center.y - center.y % cubeSize;
    const int zmin = center.z - center.z % cubeSize;
    const bool xvalid = valid & inRange(coord.x, xmin, xmin + cubeSize);
    const bool yvalid = valid & inRange(coord.y, ymin, ymin + cubeSize);
    const bool zvalid = valid & inRange(coord.z, zmin, zmin + cubeSize);
    return xvalid || yvalid || zvalid;
}

int roundFloat(float number) {
    if(number >= 0) return (int)(number + 0.5);
    else return (int)(number - 0.5);
}

int sgn(float number) {
    if(number > 0.) return 1;
    else if(number == 0.) return 0;
    else return -1;
}


//Some math helper functions
float radToDeg(float rad) {
    return ((180. * rad) / boost::math::constants::pi<float>());
}

float degToRad(float deg) {
    return ((deg / 180.) * boost::math::constants::pi<float>());
}

bool intersectLineAndPlane(const floatCoordinate planeNormal, const floatCoordinate planeUpVec,
                           const floatCoordinate lineUpVec, const floatCoordinate lineDirectionVec,
                           floatCoordinate & intersectionPoint) {
    if (std::abs(lineDirectionVec.dot(planeNormal)) > 0.0001) {
        const float lambda = (planeNormal.dot(planeUpVec) - planeNormal.dot(lineUpVec)) / lineDirectionVec.dot(planeNormal);
        intersectionPoint = lineUpVec + lineDirectionVec * lambda;
        return true;
    }
    return false;
}
