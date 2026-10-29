#pragma once
#include <cstdint>
inline bool rigidBatchEnabled=true; // Comparison executable may select individual draws.
#if defined(SF_RIGID_BATCH_EXPERIMENT) || defined(SF_RIGID_BATCH_COUNTERS)
struct RigidBatchStats {uint64_t draws=0,mergedParts=0;};
inline RigidBatchStats rigidBatchStats;
#endif
