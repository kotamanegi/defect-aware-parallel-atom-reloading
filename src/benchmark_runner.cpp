#include "../include/benchmark_runner.h"
#include "../include/timing.h"
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
double nowMs()
{
    return std::chrono::duration<double, std::milli>(
        WallClock::now().time_since_epoch()).count();
}

enum class Kind { PlanStarted, OperationCompleted, TrialCompleted, Timeout, Done, Error };
struct Message {
    Kind kind{};
    double value = 0.0;
    TrialSummary trial;
    char error[256]{};
};
static_assert(std::is_trivially_copyable<Message>::value);
static_assert(sizeof(Message) <= 512); // No larger than the minimum POSIX PIPE_BUF.

void sendMessage(int fd, const Message &message)
{
    ssize_t n;
    do { n = write(fd, &message, sizeof(message)); } while (n < 0 && errno == EINTR);
    if (n != sizeof(message))
        throw std::runtime_error("failed to send benchmark progress");
}

bool receiveMessage(int fd, Message &message)
{
    size_t done = 0;
    while (done < sizeof(message)) {
        const ssize_t n = read(fd, reinterpret_cast<char *>(&message) + done,
                               sizeof(message) - done);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) throw std::system_error(errno, std::generic_category(), "read benchmark progress");
        if (n == 0) {
            if (done) throw std::runtime_error("truncated benchmark progress");
            return false;
        }
        done += static_cast<size_t>(n);
    }
    return true;
}

// Reap the child even if the parent exits through an exception.
struct ChildGuard {
    pid_t pid;
    int fd;
    ~ChildGuard()
    {
        close(fd);
        if (pid > 0) {
            kill(pid, SIGKILL);
            while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
        }
    }
};

void setTimer(const itimerval &timer)
{
    if (setitimer(ITIMER_REAL, &timer, nullptr) != 0)
        throw std::system_error(errno, std::generic_category(), "set planning timer");
}

// Set a one-shot ITIMER_REAL timer in ms; ms <= 0 disarms it.
itimerval makeTimer(double ms)
{
    itimerval timer{};
    if (ms <= 0) return timer;
    const auto micros = static_cast<long long>(std::ceil(ms * 1000.0));
    timer.it_value.tv_sec = micros / 1000000;
    timer.it_value.tv_usec = micros % 1000000;
    return timer;
}

void childWork(int fd, double budget_ms, const PlanningWork &work)
{
    // The default SIGALRM action terminates the process, without a handler or
    // cooperative solver cancellation. Leave the parent's signal settings unchanged.
    struct sigaction action{};
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    sigset_t alarms;
    sigemptyset(&alarms);
    sigaddset(&alarms, SIGALRM);
    if (sigaction(SIGALRM, &action, nullptr) != 0 ||
        sigprocmask(SIG_UNBLOCK, &alarms, nullptr) != 0)
        throw std::system_error(errno, std::generic_category(), "configure planning timer");

    // Enforce a cumulative budget rather than a fixed limit per planning call:
    // stop when the total planning time exceeds budget_ms.
    // Arm a timer for the remaining budget before each call, so the OS terminates
    // the child at the budget boundary even if the call never returns.
    double accumulated = 0.0;
    SimulationTrace trace;
    trace.before_plan = [&] {
        Message message;
        message.kind = Kind::PlanStarted;
        message.value = nowMs();
        sendMessage(fd, message);
        setTimer(makeTimer(budget_ms - accumulated));
    };
    trace.after_plan = [&](double plan_ms) {
        setTimer(itimerval{});
        accumulated += plan_ms;
        if (accumulated > budget_ms) {
            Message message;
            message.kind = Kind::Timeout;
            message.value = accumulated; // Cumulative planning time at cutoff.
            sendMessage(fd, message);
            _exit(0);
        }
    };
    trace.operation_completed = [&](double plan_ms) {
        Message message;
        message.kind = Kind::OperationCompleted;
        message.value = plan_ms;
        sendMessage(fd, message);
    };
    work(trace, [&](const TrialSummary &trial) {
        Message message;
        message.kind = Kind::TrialCompleted;
        message.trial = trial;
        sendMessage(fd, message);
    });
    Message message;
    message.kind = Kind::Done;
    sendMessage(fd, message);
}
} // namespace

BenchmarkRun runPlanningWorker(double budget_ms, const PlanningWork &work)
{
    // Avoid overflow when converting to microseconds, and reject 0 (timer disabled).
    if (!std::isfinite(budget_ms) || budget_ms <= 0 ||
        budget_ms >= static_cast<double>(std::numeric_limits<long long>::max()) / 1000.0)
        throw std::invalid_argument("budget must be a finite positive duration in milliseconds");

    int fds[2];
    if (pipe(fds) != 0)
        throw std::system_error(errno, std::generic_category(), "create benchmark pipe");
    std::cout.flush();
    std::cerr.flush();
    const double started = nowMs();
    const pid_t pid = fork();
    if (pid < 0) {
        const int error = errno;
        close(fds[0]);
        close(fds[1]);
        throw std::system_error(error, std::generic_category(), "fork benchmark worker");
    }
    if (pid == 0) {
        close(fds[0]);
        try {
            childWork(fds[1], budget_ms, work);
            close(fds[1]);
            _exit(0);
        } catch (const std::exception &error) {
            const itimerval disabled{};
            setitimer(ITIMER_REAL, &disabled, nullptr);
            Message message;
            message.kind = Kind::Error;
            std::snprintf(message.error, sizeof(message.error), "%s", error.what());
            // Ensure this child exits even if the parent has already terminated.
            try { sendMessage(fds[1], message); } catch (...) {}
            _exit(1);
        } catch (...) {
            _exit(1);
        }
    }

    close(fds[1]);
    ChildGuard child{pid, fds[0]};
    BenchmarkRun result;
    double plan_started = 0.0;
    bool done = false;
    std::string error;
    Message message;
    while (receiveMessage(fds[0], message)) {
        switch (message.kind) {
        case Kind::PlanStarted: plan_started = message.value; break;
        case Kind::OperationCompleted:
            result.per_op_planning_wall_ms.push_back(message.value);
            break;
        case Kind::TrialCompleted: result.trials.push_back(message.trial); break;
        case Kind::Timeout:
            result.timed_out = true;
            result.timeout_planning_wall_ms = message.value;
            break;
        case Kind::Done: done = true; break;
        case Kind::Error: error = message.error; break;
        }
    }
    int status;
    pid_t waited;
    do { waited = waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
    if (waited < 0) throw std::system_error(errno, std::generic_category(), "wait for benchmark worker");
    child.pid = -1;
    const double stopped = nowMs();
    result.elapsed_ms = stopped - started;
    if (WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM && plan_started > 0) {
        result.timed_out = true;
        result.timeout_planning_wall_ms = stopped - plan_started;
    } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || (!done && !result.timed_out)) {
        throw std::runtime_error(error.empty() ? "benchmark worker exited unexpectedly" : error);
    }
    return result;
}

BenchmarkRun runBenchmark(const Parameters &p, const AlgorithmSpec &spec, double budget_ms)
{
    return runPlanningWorker(budget_ms, [&](const SimulationTrace &trace, const TrialReporter &report) {
        if (spec.reset_mip_stats) spec.reset_mip_stats();
        resetHillClimbStats();
        resetDiscardedStats();
        for (int trial = 0; trial < p.num_trials; ++trial) {
            Parameters trial_p = p;
            trial_p.trial_index = static_cast<std::uint64_t>(trial);
            TrialSummary summary;
            std::vector<double> plan_times;
            SimulationTrace trial_trace = trace;
            trial_trace.per_op_planning_wall_ms = &plan_times;
            summary.filling_rate = runSimulation(trial_p, spec.id, trial_trace);
            // Use the same measurement as maximum planning time, excluding simulator work and notifications.
            summary.runtime_ms = std::accumulate(plan_times.begin(), plan_times.end(), 0.0);
            getHillClimbStats(summary.hill_iterations, summary.hill_runs);
            getDiscardedStats(summary.discarded_atoms, summary.discarded_runs);
            if (spec.get_mip_stats) spec.get_mip_stats(summary.mip_ok, summary.mip_fallback);
            report(summary);
        }
    });
}
