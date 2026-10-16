#include "prism/util/logging.hpp"

#include <algorithm>
#include <atomic>
#include <deque>
#include <iostream>
#include <mutex>
#include <streambuf>
#include <string>
#include <string_view>

namespace radiante {
namespace {

std::atomic<bool> g_enabled{false};

// The longest line kept; the rest of a longer line is dropped.
constexpr size_t kMaxLineLength = 2048;

// The lines of one stream (diagnostics or errors) waiting for the Java side to drain them. Bounded, so a log nobody
// drains cannot grow without end: the oldest lines go first, and a very long line keeps only its start.
class LineLog {
  public:
    explicit LineLog(std::ostream &console) : console_(console) {}

    // Takes text made of whole lines, each ending in '\n'.
    void add(std::string_view text) {
        std::scoped_lock lock(mutex_);
        // Under the lock, so the console shows the lines in the order they are kept.
        if (g_enabled.load(std::memory_order_relaxed)) {
            console_.write(text.data(), static_cast<std::streamsize>(text.size()));
        }
        while (!text.empty()) {
            const size_t end = std::min(text.find('\n'), text.size());
            if (lines_.size() >= kMaxLines) { lines_.pop_front(); }
            lines_.emplace_back(text.substr(0, std::min(end, kMaxLineLength)));
            text.remove_prefix(std::min(end + 1, text.size()));
        }
    }

    std::string drain() {
        std::deque<std::string> lines;
        {
            std::scoped_lock lock(mutex_);
            lines.swap(lines_);
        }
        size_t size = 0;
        for (const std::string &line : lines) { size += line.size() + 1; }
        std::string all;
        all.reserve(size);
        for (const std::string &line : lines) {
            all += line;
            all += '\n';
        }
        return all;
    }

  private:
    static constexpr size_t kMaxLines = 256;

    std::ostream &console_;
    std::mutex mutex_;
    std::deque<std::string> lines_;
};

// Never destroyed: objects destroyed while the process ends may still log.
LineLog &infoLog() {
    static LineLog *log = new LineLog(std::cout);
    return *log;
}

LineLog &errorLog() {
    static LineLog *log = new LineLog(std::cerr);
    return *log;
}

// Collects what one thread writes and hands it to a LineLog a whole line at a time.
class LineWriter final : public std::streambuf {
  public:
    explicit LineWriter(LineLog &log) : log_(log) {}

    ~LineWriter() override {
        // A line the thread never finished still reaches the log.
        if (!pending_.empty()) {
            pending_ += '\n';
            log_.add(pending_);
        }
    }

  protected:
    int_type overflow(int_type c) override {
        if (traits_type::eq_int_type(c, traits_type::eof())) { return traits_type::not_eof(c); }
        const char character = traits_type::to_char_type(c);
        append(std::string_view(&character, 1));
        return c;
    }

    std::streamsize xsputn(const char *text, std::streamsize count) override {
        append(std::string_view(text, static_cast<size_t>(count)));
        return count;
    }

  private:
    // Adds text to pending_, keeping no more than kMaxLineLength characters of a line, so a thread that writes
    // without ever ending its line holds a bounded amount. Complete lines are then handed over.
    void append(std::string_view text) {
        bool ended = false;
        while (!text.empty()) {
            const size_t newline = text.find('\n');
            const std::string_view part = text.substr(0, newline);
            const size_t kept = std::min(part.size(), kMaxLineLength - lineLength_);
            pending_.append(part.substr(0, kept));
            lineLength_ += kept;
            if (newline == std::string_view::npos) { break; }
            pending_ += '\n';
            lineLength_ = 0;
            ended = true;
            text.remove_prefix(newline + 1);
        }
        if (ended) { publish(); }
    }

    // Hands over every complete line; the text after the last '\n' waits for the rest of its line.
    void publish() {
        const size_t end = pending_.rfind('\n') + 1;
        log_.add(std::string_view(pending_).substr(0, end));
        pending_.erase(0, end);
    }

    LineLog &log_;
    std::string pending_;
    // The characters kept of the line being written, at most kMaxLineLength.
    size_t lineLength_ = 0;
};

// Takes what is written while logging is off. The formatting still costs, but that is a few strings per pipeline
// build, not per frame, so call sites need not check.
class NullBuffer final : public std::streambuf {
  protected:
    int_type overflow(int_type c) override {
        return traits_type::not_eof(c);
    }

    std::streamsize xsputn(const char *, std::streamsize count) override {
        return count;
    }
};

// Set when this thread's streams are destroyed as it exits. A plain value needs no destruction, so it stays readable
// by the thread-local objects destroyed after the streams, and by static destructors at process exit.
thread_local bool t_streamsDestroyed = false;

// A std::ostream must not be written by two threads at once, so every thread gets its own set.
struct ThreadStreams {
    LineWriter infoWriter{infoLog()};
    LineWriter errorWriter{errorLog()};
    NullBuffer nullBuffer;
    std::ostream info{&infoWriter};
    std::ostream error{&errorWriter};
    std::ostream discard{&nullBuffer};

    ~ThreadStreams() {
        t_streamsDestroyed = true;
    }
};

thread_local ThreadStreams *t_lateStreams = nullptr;

ThreadStreams &threadStreams() {
    if (t_streamsDestroyed) {
        // Logging once the thread's streams are gone: a set of its own that is never freed, since nothing is left to
        // run on this thread that could free it. Whole lines still reach the log.
        if (t_lateStreams == nullptr) { t_lateStreams = new ThreadStreams; }
        return *t_lateStreams;
    }
    thread_local ThreadStreams streams;
    return streams;
}

} // namespace

void setLoggingEnabled(bool enabled) {
    g_enabled.store(enabled, std::memory_order_relaxed);
}

bool loggingEnabled() {
    return g_enabled.load(std::memory_order_relaxed);
}

std::ostream &out() {
    ThreadStreams &streams = threadStreams();
    return loggingEnabled() ? streams.info : streams.discard;
}

std::ostream &err() {
    return threadStreams().error;
}

std::string drainErrors() {
    return errorLog().drain();
}

std::string drainInfo() {
    return infoLog().drain();
}

} // namespace radiante
