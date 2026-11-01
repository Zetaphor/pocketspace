#pragma once
#include <filesystem>
#include <string>

inline bool resolveCatalogScript(const std::filesystem::path &root,
                                 const std::filesystem::path &name,
                                 std::filesystem::path &resolved,
                                 std::string &error){
  if(name.empty()||name.is_absolute()||name.extension()!=".js"){
    error="catalog script must be a relative .js path";return false;
  }
  for(const auto &part:name)if(part==".."){
    error="catalog path traversal is forbidden";return false;
  }
  std::error_code ec;
  const auto catalog=std::filesystem::canonical(root,ec);
  if(ec||!std::filesystem::is_directory(catalog,ec)){
    error="scripts directory is unavailable";return false;
  }
  const auto candidate=std::filesystem::canonical(catalog/name,ec);
  if(ec||!std::filesystem::is_regular_file(candidate,ec)){
    error="catalog script is not readable";return false;
  }
  auto rootPart=catalog.begin(),filePart=candidate.begin();
  for(;rootPart!=catalog.end();++rootPart,++filePart)if(filePart==candidate.end()||*rootPart!=*filePart){
    error="catalog script escapes scripts directory";return false;
  }
  if(filePart==candidate.end()){
    error="catalog script is not a file below the scripts directory";return false;
  }
  resolved=candidate;error.clear();return true;
}
