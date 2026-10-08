#pragma once

#include <algorithm>
#include <thread>
#include <vector>

namespace mf {

// Splits [0, count) into contiguous chunks and runs fn(begin, end) on worker threads.
template <typename Fn>
void parallelFor(int count, Fn&& fn, int minChunk = 32)
{
    if (count <= 0)
        return;
    int threads = std::max(1u, std::thread::hardware_concurrency());
    threads = std::min(threads, std::max(1, count / std::max(1, minChunk)));
    if (threads <= 1) {
        fn(0, count);
        return;
    }
    std::vector<std::thread> pool;
    pool.reserve(threads - 1);
    const int chunk = (count + threads - 1) / threads;
    for (int t = 1; t < threads; ++t) {
        const int b = t * chunk;
        const int e = std::min(count, b + chunk);
        if (b < e)
            pool.emplace_back([&fn, b, e] { fn(b, e); });
    }
    fn(0, std::min(count, chunk));
    for (auto& th : pool)
        th.join();
}

} // namespace mf
