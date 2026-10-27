# GPU producer-completion investigation

This test compares two ways to wait for this engine's rendering to finish. It does
not establish a cross-process asynchronous ownership contract. Production uses
`glFinish` and retains its current three-color-buffer lifetime behavior.

## API contract review

Reviewed on 2026-10-26 against the installed Frame SDK and the public
[OpenVR header](https://github.com/ValveSoftware/openvr/blob/master/headers/openvr.h):

- `SetOverlayTexture` accepts a `Texture_t`, whose shared-texture form carries a
  resource handle and color space, without a documented producer fence argument
  or a consumer-complete acknowledgement.
- `ClearOverlayTexture` releases the texture assigned to an overlay. That is not
  a per-frame acknowledgement that another ring buffer may be overwritten.
- `WaitFrameSync` is runtime frame pacing, not texture ownership or GPU completion.
- `NewSharedVulkanSemaphore` creates a resource, but the reviewed overlay API does
  not document attaching it to an overlay texture submission.
- Mirror-texture locks and native-overlay-handle release apply to resources
  acquired through those APIs; they are not an ownership protocol for this
  engine's imported DMA-BUF producer buffers.

This is a limitation of the reviewed contract, not a claim about every internal
SteamVR implementation. Triple buffering remains existing baseline behavior; a
fixed number of buffers is not proof that compositor consumption has completed.
Do not infer that successful visual runs authorize submitting unfinished images
or overwriting resources still in use.

Khronos specifies that a signaled
[`glFenceSync`](https://registry.khronos.org/OpenGL-Refpages/es3.0/html/glFenceSync.xhtml)
fence completes preceding commands and framebuffer effects in its command stream.
[`glClientWaitSync`](https://registry.khronos.org/OpenGL-Refpages/es3.0/html/glClientWaitSync.xhtml)
waits for that signal. The experiment inserts a fence after rendering, flushes on
the first wait, waits until signaled, deletes it, and only then submits textures.
Failure falls back to `glFinish` and causes the benchmark runner to reject the
case. It never uses a bare `glFlush` as a completion guarantee.

## Measurement method

The excluded `gpu-sync-experiment` executable builds the real engine, with a
compile-time profiling seam. Normal engine builds contain none of its timer
queries, environment settings, or alternate wait code. Native comparisons reuse
the compact overlay-comparison scene: 48 fixed bodies, a separate floor group,
four spatial body groups, identical loaded assets, hidden lasers, no grabbing,
and a statistics panel. Boxes and actual ToyCar/Avocado assets are tested.

Each mode runs for 12 seconds; samples whose intervals overlap the first five
seconds are discarded. Pair order alternates between geometries and reverses in
the second round. Summaries retain six profiling windows and seven statistics
samples per run. No group reaches the resolution ceiling in the retained cases.
GPU memory allocation and geometry are identical within each pair.

Optional
[`GL_EXT_disjoint_timer_query`](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_disjoint_timer_query.txt)
queries enclose object rendering. Results are polled for availability before
reading; disjoint results are discarded. GPU query duration includes scheduling
effects within that interval and is not exclusive GPU occupancy. CPU wait wall
time also includes waiting for preceding queued work and wakeup latency. These
numbers should not be subtracted to assert a precise synchronization overhead.
The render thread's CPU time inside the completion wait is measured separately.
Timer instrumentation can affect performance; these are scene-specific native
comparisons, not a guarantee of production FPS or headset presentation rate.

The initial [one-millisecond timeout comparison](gpu-sync-short-wait-frame.json)
repeatedly woke the render thread while the GPU was busy. It increased CPU time
inside the fence wait from about 0.20 to 0.93 ms for boxes and 0.28 to 1.88 ms for
models. Box FPS changed direction between rounds; model FPS was slightly higher
with the fence. A longer timeout is tested separately to avoid mistaking frequent
wakeups for an inherent fence cost. A long timeout returns immediately when the
fence signals; it does not deliberately add that amount of frame latency.

## Longer-wait results and decision

Measured on Steam Frame on 2026-10-26 with this test implementation, parent
revision `7edf1d5`. The [100-millisecond timeout summary](gpu-sync-frame.json)
contains two rounds in opposite orders:

| Geometry | `glFinish` FPS, rounds 1 / 2 | Fence FPS, rounds 1 / 2 | `glFinish` wait CPU ms | Fence wait CPU ms |
| --- | ---: | ---: | ---: | ---: |
| 48 boxes | 78.0 / 71.2 | 72.4 / 74.9 | 0.215 | 0.563 |
| 48 ToyCar/Avocado instances | 28.5 / 34.3 | 32.5 / 33.5 | 0.280 | 0.690 |

CPU columns average the two per-run medians of render-thread CPU time inside the
completion call, not wall time or whole-process CPU percentage. Faster FPS changes
direction between rounds for both geometries. The fence has higher wait-thread
CPU cost in every pair, even after avoiding repeated short-timeout wakeups.

With `glFinish`, measured GPU query duration was about 7.5–7.9 ms for boxes and
21.6–27.5 ms for models. Completion-wait wall time was about 11–13 ms and 27–33 ms,
respectively. The render thread consumes only a small fraction of that waiting
interval as CPU time. Completion wait is not a free block of idle GPU time that
can simply be removed; graphics work and scheduling remain on the critical path.
These measurements do not isolate every cause of the gap between GPU queries
and wall time, or guarantee the same costs in a different scene.

**Decision:** keep `glFinish` in the production renderer. Neither fence strategy
establishes a repeatable throughput improvement, and both use more CPU in the
completion wait. Do not replace completion with bare flushing or add an extra
queued frame without separately establishing texture lifetime and latency rules.
Further asynchronous work requires a documented producer/consumer handoff or
an independently validated ownership protocol, not just a different wait call.

## Validation and scope

- Sixteen bounded native runs covered boxes/models, two completion methods,
  reversed pair orders, and two fence timeout choices. No retained run had GL or
  fence errors, disjoint windows, resolution-limited groups, or wrong body counts.
- Twelve exact stereo pixel comparisons matched between completion methods,
  covering opaque, fully masked-hole, and blended geometry at two viewpoints.
- The normal engine rebuilt successfully and was **byte-identical** to the
  deployed production executable: no production overhead or behavior change.
- Documentation lint and diff whitespace checks passed. No persistent engine or
  synchronization experiment remains running.

Profiler interval boundaries, geometry, memory estimates, per-mode costs, and
sample counts are retained in the summaries. Per-case raw logs remain under
`/tmp/sf-gpu-sync-grouped` and `/tmp/sf-gpu-sync-long-wait` on Frame. The shorter-wait
summary predates the `fenceWaitMs` field; every fence in that file used one
millisecond. Public product documentation describes production functionality;
this investigation stays under `tests/`.
