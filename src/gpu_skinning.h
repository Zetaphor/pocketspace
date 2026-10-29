#pragma once
#ifdef SF_GPU_SKIN_EXPERIMENT
#include <array>
#include <vector>
struct SkinJointBounds {std::array<float,3> low{},high{};bool used=false;};
inline bool gpuSkinningEnabled=false;
#endif
