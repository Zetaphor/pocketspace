include(FetchContent)
# Pinned meshoptimizer 1.0. Keep the archive digest and upstream license in sync.
set(MESHOPT_INSTALL OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(Meshoptimizer
  URL https://codeload.github.com/zeux/meshoptimizer/tar.gz/73583c335e541c139821d0de2bf5f12960a04941
  URL_HASH SHA256=a1fa86eb2faaac14ce6259d72a1a3a7dcf9c7f02b1778f19a366aeba10d82425)
FetchContent_MakeAvailable(Meshoptimizer)
