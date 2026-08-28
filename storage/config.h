#pragma once

// Fixed page size: matches the OS disk block size (4 KB) and is cache friendly.
// Every read/write in DiskManager transfers exactly PAGE_SIZE bytes.
constexpr int PAGE_SIZE = 4096;

// Number of frames in the buffer pool. Kept small (3) on purpose: it makes
// cache hits/misses and evictions easy to observe in the demo and tests.
// When all frames are occupied, the pool evicts a frame (round-robin),
// writing it back to disk first if it is dirty. See buffer_pool.h.
constexpr int BUFFER_POOL_SIZE = 3;
