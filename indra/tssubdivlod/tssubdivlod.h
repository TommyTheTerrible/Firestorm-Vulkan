/**
 * @file tssubdivlod.h
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
#pragma once
#include <string>
#include <unordered_map>
#include <vector>



// Map is organized with a string that is "GridX-GridY-(SimZ/MaxDrawDistance)", then by nodeID from CTB.
typedef std::unordered_map<std::string, std::unordered_map<uint64_t, std::list<std::string>>> subdiv_node_map;

class TSSubDivisonLOD
{
  public:
    TSSubDivisonLOD();

    //static TSSubDivisonLOD* getLOD() { return sTSSubDivisonLOD; }
    static uint64_t const CBT_DEFAULT_DEPTH = 11;
    static std::string getID(uint64_t region_handle, uint64_t simZ);
    void addObject(std::string id, uint64_t node, std::string object);
    void removeObject(std::string id, uint64_t node, std::string object);
    static void updateObject(std::string id, uint64_t old_node, uint64_t new_node, std::string object);
    static uint64_t getNumObjects(std::string id, uint64_t node);
    static std::list<std::string> getObjects(std::string id, uint64_t node);
    bool checkSim(std::string id, const uint64_t region_handle);
    static std::list<uint64_t> getNeighbors(uint64_t node);
    static float getNodeRadius(uint64_t node, const int depth, const float extents);
    static uint64_t getSimNode(const uint64_t region_handle, const float vector[3], const int depth, const float extents);
    static uint64_t positionToNode(const float x, const float y, const int depth, const float extents);
  protected:
    subdiv_node_map mNodeMap;

};

static TSSubDivisonLOD sTSSubDivisonLOD;
