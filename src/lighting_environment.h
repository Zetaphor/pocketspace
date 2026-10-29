#pragma once
#include <GLES3/gl3.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Scene assets share immutable CPU bake data and one lazily uploaded GPU pair.
struct LightingEnvironment {
  std::string path;
  uint32_t size=0,levels=0,lutSize=0;
  std::array<float,27> diffuse{}; // SH irradiance / pi, nine RGB coefficients.
  std::vector<uint16_t> specular,brdf;
  GLuint cube=0,lut=0;
  ~LightingEnvironment();
  LightingEnvironment()=default;
  LightingEnvironment(const LightingEnvironment&)=delete;
  bool upload();
  uint64_t gpuBytes()const;
};
std::shared_ptr<LightingEnvironment> loadLightingEnvironment(const std::string &path);
