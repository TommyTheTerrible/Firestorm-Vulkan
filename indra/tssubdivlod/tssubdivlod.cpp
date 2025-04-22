/**
 * @file tssubdivlod.cpp
 * @brief This is an implementation of a LOD management system using a novel subdivision binary tree.
 *
 * $LicenseInfo:firstyear=2024&license=lgpl$
 * TommyTheTerrible
 * Copyright (C) 2024, TommyTheTerrible
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * TommyTheTerrible: tommy@tommytheterrible.com, https://www.tommytheterrible.com/ https://github.com/TommyTheTerrible/
 * $/LicenseInfo$
 */
#include "tssubdivlod.h"
#include <string>
#include <unordered_map>
#include <list>
#define CBT_IMPLEMENTATION
#define CBT_STATIC
#include "cbt.h"
#define LEB_IMPLEMENTATION
#define LEB_STATIC
#include "leb.h"

TSSubDivisonLOD::TSSubDivisonLOD() {
    //mNodeMap = new subdiv_node_map;
}

std::string TSSubDivisonLOD::getID(uint64_t region_handle, uint64_t position_z)
{
    int32_t gridX = ((uint32_t) (region_handle >> 32));
    int32_t gridY = ((uint32_t) (region_handle & 0xFFFFFFFF));
    int32_t simZ  = ((uint32_t) floor(position_z / 1024));
    return std::to_string(gridX) + "-" + std::to_string(gridY) + "-" + std::to_string(simZ);
};

void TSSubDivisonLOD::addObject(std::string id, uint64_t node, std::string object)
{
    mNodeMap[id][node].push_back(object);
}

void TSSubDivisonLOD::removeObject(std::string id, uint64_t node, std::string object)
{
    mNodeMap[id][node].remove(object);
}

void TSSubDivisonLOD::updateObject(std::string id, uint64_t old_node, uint64_t new_node, std::string object)
{
    if (old_node > 0)
        sTSSubDivisonLOD.mNodeMap[id][old_node].remove(object);
    if (new_node > 0)
        sTSSubDivisonLOD.mNodeMap[id][new_node].push_back(object);
}

uint64_t TSSubDivisonLOD::getNumObjects(std::string id, uint64_t node)
{

    return (uint64_t)sTSSubDivisonLOD.mNodeMap[id][node].size();
}

std::list<std::string> TSSubDivisonLOD::getObjects(std::string id, uint64_t node)
{

    return sTSSubDivisonLOD.mNodeMap[id][node];
}

bool TSSubDivisonLOD::checkSim(std::string id, const uint64_t region_handle)
{
    return mNodeMap[id].size() > 0;
};

std::list<uint64_t> TSSubDivisonLOD::getNeighbors(uint64_t node)
{
    cbt_Node thisNode = cbt_CreateNode(node, 11);

    leb__SameDepthNeighborIDs neighbors = leb_DecodeSameDepthNeighborIDs(thisNode);

    std::list<uint64_t> neighbor_list;

    neighbor_list.push_back((uint64_t) neighbors.edge);
    neighbor_list.push_back((uint64_t) neighbors.left);
    neighbor_list.push_back((uint64_t) neighbors.right);

    return neighbor_list;
 }

float TSSubDivisonLOD::getNodeRadius(uint64_t node, const int depth, const float extents)
{
    float faceVertices[][3] = {
            {0.0f, 0.0f, extents},
            {extents, 0.0f, 0.0f}
        };
    cbt_Node thisNode = cbt_CreateNode(node, depth);
    leb_DecodeNodeAttributeArray_Square(thisNode, 2, faceVertices);
    float x1 = faceVertices[0][1];
    float y1 = faceVertices[0][2];
    float x2 = faceVertices[1][1];
    float y2 = faceVertices[1][2];
    float distance = (float)sqrt(pow(x1 - x2, 2) + pow(y1 - y2, 2)) / 2; // No Z, since triangles flat and divided by two for middle
    return distance;
}

uint64_t TSSubDivisonLOD::getSimNode(const uint64_t region_handle, const float vector[3], const int depth, const float extents)
{
    int simZ = (int)floor(vector[2] / 1024);
    std::string id = getID(region_handle, simZ);
    //sTSSubDivisonLOD.checkSim(id, region_handle);
    return (uint64_t) positionToNode(vector[0], vector[1], depth, extents);
};

float Wedge(const float *a, const float *b)
{
    return a[0] * b[1] - a[1] * b[0];
}

bool isInsideXY(const float faceVertices[][3], const float x, const float y)
{
    float target[2] = {x, y};
    float v1[2]     = {faceVertices[0][0], faceVertices[1][0]};
    float v2[2]     = {faceVertices[0][1], faceVertices[1][1]};
    float v3[2]     = {faceVertices[0][2], faceVertices[1][2]};
    float x1[2]     = {v2[0] - v1[0], v2[1] - v1[1]};
    float x2[2]     = {v3[0] - v2[0], v3[1] - v2[1]};
    float x3[2]     = {v1[0] - v3[0], v1[1] - v3[1]};
    float y1[2]     = {target[0] - v1[0], target[1] - v1[1]};
    float y2[2]     = {target[0] - v2[0], target[1] - v2[1]};
    float y3[2]     = {target[0] - v3[0], target[1] - v3[1]};
    float w1        = Wedge(x1, y1);
    float w2        = Wedge(x2, y2);
    float w3        = Wedge(x3, y3);

    return (w1 >= 0.0f) && (w2 >= 0.0f) && (w3 >= 0.0f);
}

uint64_t TSSubDivisonLOD::positionToNode(const float x, const float y, const int depth, const float extents)
{
    uint64_t minNodeID = (1ULL << depth);
    uint64_t maxNodeID = (2ULL << depth);

    uint64_t foundID = 0;

    for (uint64_t nodeID = maxNodeID; nodeID > minNodeID; nodeID--)
    {
        float faceVertices[][3] = {
            {0.0f, 0.0f, extents},
            {extents, 0.0f, 0.0f}
        };
        cbt_Node thisNode = cbt_CreateNode(nodeID, depth);
        leb_DecodeNodeAttributeArray_Square(thisNode, 2, faceVertices);
        if (isInsideXY(faceVertices, x, y))
        {
            foundID = nodeID;
            break;
        }
    }

    return foundID;
}


