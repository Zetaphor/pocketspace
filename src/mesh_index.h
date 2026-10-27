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
// Merge only identical position+UV bit patterns within a material primitive.
// Preserve triangle order, UV seams, and the expanded CPU geometry.
inline Indexed index(std::span<const float> source){
  if(source.size()%5)throw std::invalid_argument("vertex stream must contain xyz/uv tuples");
  using Key=std::array<uint32_t,5>;
  struct Hash {size_t operator()(const Key &key)const{size_t hash=1469598103934665603ull;for(auto word:key)hash=(hash^word)*1099511628211ull;return hash;}};
  std::unordered_map<Key,uint32_t,Hash> seen;seen.reserve(source.size()/5);
  Indexed result;result.indices.reserve(source.size()/5);
  for(size_t i=0;i<source.size();i+=5){
    Key key;for(size_t j=0;j<5;j++)key[j]=std::bit_cast<uint32_t>(source[i+j]);
    auto [it,added]=seen.emplace(key,uint32_t(result.vertices.size()/5));
    if(added)result.vertices.insert(result.vertices.end(),source.begin()+i,source.begin()+i+5);
    result.indices.push_back(it->second);
  }
  return result;
}
}
