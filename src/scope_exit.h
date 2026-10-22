#pragma once
#include <utility>
template<class F> struct ScopeExit {
  F function;
  explicit ScopeExit(F f):function(std::move(f)){}
  ScopeExit(const ScopeExit&)=delete;
  ScopeExit& operator=(const ScopeExit&)=delete;
  ~ScopeExit(){function();}
};
