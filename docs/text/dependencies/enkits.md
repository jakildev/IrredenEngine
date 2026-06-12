## Irreden Engine: enkiTS

### Details:
-   **Found in modules:** IRJob
-   **Package type:** Static library
-   **Precompiled:** No
-   **Version:** v1.11 (SHA `6ffccbdb1000253d8d513dd7a5ae9226e5023a5c`)
-   **License:** zlib
-   **Url:** https://github.com/dougbinks/enkiTS
-   **Source:** https://github.com/dougbinks/enkiTS
-   **Docs:** https://github.com/dougbinks/enkiTS

### Used For:
-   Lock-free multi-producer multi-consumer task queue with work-stealing scheduler
-   `IRJob::parallelFor` — splits a range across worker threads and blocks until all chunks finish
-   `IRJob::run` — fires a single named task on a worker and blocks
-   `IRJob::pinTo` — pins a task to a specific worker thread by 1-based id

### Build Configuration
-   Fetched at configure time via CMake `FetchContent` from `engine/job/third_party/enkiTS/`.
-   Default tag is the pinned SHA `6ffccbdb…` with `GIT_SHALLOW FALSE` for reproducibility.
-   Override at configure time via `-DIR_ENKITS_GIT_TAG=<branch-or-tag>` (set `GIT_SHALLOW TRUE` when using a mutable ref).
-   The wrapper forces `CXX_STANDARD 17` on the enkiTS target to avoid `std::is_pod` removal in C++23.

### Runtime Notes
-   Worker count comes from `WorldConfig::worker_thread_count` (`-1` = auto: `max(1, hardware_concurrency() - 2)`, capped to P-core count on Apple Silicon).
-   Workers register an easy_profiler thread name (`ir-worker-N`) on first task entry.

### Future Use:
