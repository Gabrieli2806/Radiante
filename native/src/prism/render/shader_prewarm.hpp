#pragma once

#include "prism/util/background_work.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

// Compiles, on a background thread while the game keeps drawing with its current pipeline, what a pipeline built with
// new settings will need: the shaders (into the SPIR-V cache) and the ray tracing pipelines (into the driver's
// pipeline cache). The rebuild that follows finds both warm and takes a fraction of a second instead of many. Nothing
// it makes is used: it only fills the caches, so a prewarm that is cancelled, fails or warms the wrong thing costs
// time, never correctness.
//
// One job runs at a time. Starting another cancels the running one, which stops at its next step (a pipeline the
// driver is compiling finishes first) and is joined later, so starting never waits for it.
class ShaderPrewarm {
  public:
    enum class State : int32_t {
        Idle = 0,
        Running = 1,
        Done = 2,
        Failed = 3,
        Cancelled = 4,
    };

    // What a job reports while it runs, and the flag that asks it to stop.
    struct Progress {
        std::atomic<bool> cancel{false};
        std::atomic<uint32_t> shadersDone{0};
        std::atomic<uint32_t> shadersTotal{0};
        std::atomic<uint32_t> pipelinesDone{0};
        std::atomic<uint32_t> pipelinesTotal{0};
    };

    // Runs on the job's thread; checks progress.cancel between steps and returns early when it is set. Must not take
    // the framework's frame lock: Framework::recreate waits for the job while holding it.
    using Work = std::function<void(Progress &progress)>;

    struct Status {
        State state = State::Idle;
        uint32_t shadersDone = 0;
        uint32_t shadersTotal = 0;
        uint32_t pipelinesDone = 0;
        uint32_t pipelinesTotal = 0;
    };

    ShaderPrewarm() = default;
    ~ShaderPrewarm();
    ShaderPrewarm(const ShaderPrewarm &) = delete;
    ShaderPrewarm &operator=(const ShaderPrewarm &) = delete;

    // Cancels the running job (without waiting for it) and starts `work` as the new one.
    void start(Work work);
    // Cancels every job and waits until none runs: before a rebuild or shutdown, which must not race a compile that
    // still holds the old pipeline's objects.
    void cancelAndJoin();
    // The newest job's state and counts; Idle before the first.
    Status status();

  private:
    struct Job {
        Progress progress;
        std::atomic<State> state{State::Running};
        prism::LargeStackThread thread;
    };

    // Joins the cancelled jobs that have finished. Under mutex_.
    void reapFinished();

    std::mutex mutex_;
    std::unique_ptr<Job> current_;
    std::vector<std::unique_ptr<Job>> cancelled_;
};
