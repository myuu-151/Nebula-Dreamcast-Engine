cmake_minimum_required(VERSION 3.20)

# Recast — navmesh generation
add_library(Recast STATIC
    Recast/Source/Recast.cpp
    Recast/Source/RecastAlloc.cpp
    Recast/Source/RecastArea.cpp
    Recast/Source/RecastAssert.cpp
    Recast/Source/RecastContour.cpp
    Recast/Source/RecastFilter.cpp
    Recast/Source/RecastLayers.cpp
    Recast/Source/RecastMesh.cpp
    Recast/Source/RecastMeshDetail.cpp
    Recast/Source/RecastRasterization.cpp
    Recast/Source/RecastRegion.cpp
)
target_include_directories(Recast PUBLIC Recast/Include)

# Detour — runtime pathfinding queries
add_library(Detour STATIC
    Detour/Source/DetourAlloc.cpp
    Detour/Source/DetourAssert.cpp
    Detour/Source/DetourCommon.cpp
    Detour/Source/DetourNavMesh.cpp
    Detour/Source/DetourNavMeshBuilder.cpp
    Detour/Source/DetourNavMeshQuery.cpp
    Detour/Source/DetourNode.cpp
)
target_include_directories(Detour PUBLIC Detour/Include)
