const stats = getEngineStats();
if (typeof stats !== 'object' || !('cpuPercent' in stats) || !('ramBytes' in stats) || !('vramBytes' in stats) || !('fps' in stats)) throw Error('missing engine statistics');
if (stats.sampleId !== 0 || stats.fps !== null || stats.gpuAllocatedEstimateBytes !== null) throw Error('statistics must remain unavailable before sampling');
