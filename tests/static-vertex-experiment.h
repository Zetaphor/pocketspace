#pragma once
#include <stdexcept>
#include <string>
// Test-only specialization. Geometry, projection and material math stay intact.
inline std::string staticVertexExperiment(){
  std::string source=modelVertexShader;
  const std::string expression="skinEnabled?skinPoint():p";
  const auto offset=source.find(expression);
  if(offset==std::string::npos)throw std::runtime_error("static vertex experiment: skin expression changed");
  source.replace(offset,expression.size(),"p");
  return source;
}
