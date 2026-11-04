#pragma once
#include <cstdint>
inline bool rigidBatchEnabled=true; // Comparison executable may select individual draws.
#ifdef SF_TEST_HOOKS
struct RigidBatchStats {uint64_t draws=0,mergedParts=0;};
inline RigidBatchStats rigidBatchStats;
#endif
