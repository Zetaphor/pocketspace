#pragma once
#include <array>
#include <vector>
struct SkinJointBounds {std::array<float,3> low{},high{};bool used=false;};
inline bool gpuSkinningEnabled=true;
