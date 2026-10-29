#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace mesh {
struct Indexed {std::vector<float> vertices;std::vector<uint32_t> indices;};
// Merge only identical tuple bit patterns within a material primitive.
// Default tuples are xyz/UV; PBR experiments include every surface attribute.
// Preserve triangle order, UV seams, and the expanded CPU geometry.
template<size_t Width=5>
inline Indexed index(std::span<const float> source){
  static_assert(Width>0);
  if(source.size()%Width)throw std::invalid_argument("vertex stream must contain complete tuples");
  using Key=std::array<uint32_t,Width>;
  struct Hash {size_t operator()(const Key &key)const{size_t hash=1469598103934665603ull;for(auto word:key)hash=(hash^word)*1099511628211ull;return hash;}};
  std::unordered_map<Key,uint32_t,Hash> seen;seen.reserve(source.size()/Width);
  Indexed result;result.indices.reserve(source.size()/Width);
  for(size_t i=0;i<source.size();i+=Width){
    Key key;for(size_t j=0;j<Width;j++)key[j]=std::bit_cast<uint32_t>(source[i+j]);
    auto [it,added]=seen.emplace(key,uint32_t(result.vertices.size()/Width));
    if(added)result.vertices.insert(result.vertices.end(),source.begin()+i,source.begin()+i+Width);
    result.indices.push_back(it->second);
  }
  return result;
}
}
