#pragma once
#include <string>
#include <vector>
#include <array>
#include <cstdint>

struct VmuAnimLayer { std::string name; bool visible = true; int frameStart = 0; int frameEnd = 0; std::string linkedAsset; };

void GenerateDreamcastPackage();
