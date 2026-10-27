// Test-only glibc heap/RSS sampler; preload into a bounded native run.
// Never linked into the engine. No allocation trimming or engine mutations.
#define _GNU_SOURCE
#include <malloc.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static void *sample(void *unused) {
    (void)unused;
    const char *path = getenv("SF_MEMORY_TRACE");
    FILE *out = path ? fopen(path, "w") : NULL;
    if (!out) return NULL;
    fprintf(out, "monotonicSeconds,arenaBytes,inUseArenaBytes,freeArenaBytes,mmapBytes,rssKiB,anonymousKiB,privateDirtyKiB\n");
    for (;;) {
        struct mallinfo2 heap = mallinfo2();
        unsigned long rss = 0, anonymous = 0, dirty = 0, value;
        char line[256];
        FILE *maps = fopen("/proc/self/smaps_rollup", "r");
        if (maps) {
            while (fgets(line, sizeof line, maps)) {
                if (sscanf(line, "Rss: %lu", &value) == 1) rss = value;
                if (sscanf(line, "Anonymous: %lu", &value) == 1) anonymous = value;
                if (sscanf(line, "Private_Dirty: %lu", &value) == 1) dirty = value;
            }
            fclose(maps);
        }
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        fprintf(out, "%.3f,%zu,%zu,%zu,%zu,%lu,%lu,%lu\n",
                now.tv_sec + now.tv_nsec / 1e9, heap.arena, heap.uordblks,
                heap.fordblks, heap.hblkhd, rss, anonymous, dirty);
        fflush(out);
        sleep(1);
    }
    return NULL;
}

__attribute__((constructor)) static void start(void) {
    if (!getenv("SF_MEMORY_TRACE")) return;
    pthread_t thread;
    if (pthread_create(&thread, NULL, sample, NULL) == 0) pthread_detach(thread);
}
