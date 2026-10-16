#include "prism/render/shader_prewarm.hpp"

#include "prism/util/logging.hpp"

#include <chrono>
#include <exception>
#include <utility>

namespace {

// As the Java side gives its rebuild thread: shader includes are resolved recursively.
constexpr size_t kStackBytes = size_t{512} * 1024 * 1024;

bool finished(ShaderPrewarm::State state) {
    return state != ShaderPrewarm::State::Running;
}

} // namespace

ShaderPrewarm::~ShaderPrewarm() {
    cancelAndJoin();
}

void ShaderPrewarm::start(Work work) {
    std::scoped_lock lock(mutex_);
    reapFinished();
    if (current_ != nullptr) {
        current_->progress.cancel.store(true);
        cancelled_.push_back(std::move(current_));
    }

    auto job = std::make_unique<Job>();
    // The thread gets the job by address: the job outlives it, since it is only dropped once joined.
    Job *running = job.get();
    job->thread = prism::LargeStackThread(kStackBytes, [running, work = std::move(work)] {
        prism::enterBackgroundThread();
        const auto started = std::chrono::steady_clock::now();
        State result = State::Done;
        try {
            work(running->progress);
        } catch (const std::exception &e) {
            radiante::err() << "[Prewarm] failed: " << e.what() << std::endl;
            result = State::Failed;
        } catch (...) {
            radiante::err() << "[Prewarm] failed" << std::endl;
            result = State::Failed;
        }
        if (running->progress.cancel.load()) result = State::Cancelled;
        const auto milliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        const char *outcome = result == State::Done ? "done" : result == State::Cancelled ? "cancelled" : "failed";
        const Progress &progress = running->progress;
        radiante::out() << "[Prewarm] " << outcome << " after " << milliseconds << " ms ("
                        << progress.shadersDone.load() << "/" << progress.shadersTotal.load() << " shaders, "
                        << progress.pipelinesDone.load() << "/" << progress.pipelinesTotal.load() << " pipelines)"
                        << std::endl;
        running->state.store(result);
    });
    current_ = std::move(job);
}

void ShaderPrewarm::cancelAndJoin() {
    std::vector<std::unique_ptr<Job>> jobs;
    {
        std::scoped_lock lock(mutex_);
        jobs = std::move(cancelled_);
        cancelled_.clear();
        if (current_ != nullptr) {
            current_->progress.cancel.store(true);
            // Kept as the newest job, so its state (Cancelled once joined) is what status() reports.
            current_->thread.join();
        }
    }
    // Outside the lock: status() stays answerable while an old job finishes its pipeline.
    for (auto &job : jobs) job->thread.join();
}

ShaderPrewarm::Status ShaderPrewarm::status() {
    std::scoped_lock lock(mutex_);
    reapFinished();
    if (current_ == nullptr) return {};
    const Progress &progress = current_->progress;
    return {
        .state = current_->state.load(),
        .shadersDone = progress.shadersDone.load(),
        .shadersTotal = progress.shadersTotal.load(),
        .pipelinesDone = progress.pipelinesDone.load(),
        .pipelinesTotal = progress.pipelinesTotal.load(),
    };
}

void ShaderPrewarm::reapFinished() {
    for (auto it = cancelled_.begin(); it != cancelled_.end();) {
        if (finished((*it)->state.load())) {
            (*it)->thread.join();
            it = cancelled_.erase(it);
        } else {
            ++it;
        }
    }
}
