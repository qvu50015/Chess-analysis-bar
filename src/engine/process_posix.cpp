#include "chess_eval/process.hpp"
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char **environ;
namespace chess_eval {
namespace {
void ready(int fd, short events, Process::Clock::time_point deadline,
           const CancelCheck &cancel) {
  for (;;) {
    check_cancelled(cancel);
    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                         deadline - Process::Clock::now())
                         .count();
    if (remaining <= 0)
      throw std::runtime_error("Stockfish operation timed out");
    pollfd descriptor{fd, events, 0};
    int result = poll(&descriptor, 1,
                      static_cast<int>(std::min<long long>(
                          remaining, cancel ? 50 : INT_MAX)));
    if (result > 0)
      return; // read/send reports EOF or socket errors.
    if (result == 0)
      continue;
    if (errno != EINTR)
      throw std::runtime_error("Failed polling Stockfish pipe");
  }
}
} // namespace
Process::Process(const std::string &executable) {
  int sockets[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0)
    throw std::runtime_error("Cannot create engine communication socket");
  // Ensure file actions remain correct even if the caller closed stdin/stdout.
  for (int &fd : sockets) {
    if (fd <= STDERR_FILENO) {
      int replacement = fcntl(fd, F_DUPFD, STDERR_FILENO + 1);
      if (replacement < 0) {
        close(sockets[0]);
        close(sockets[1]);
        throw std::runtime_error("Cannot duplicate engine socket");
      }
      close(fd);
      fd = replacement;
    }
  }
  auto fail = [&](const std::string &message) {
    close(sockets[0]);
    close(sockets[1]);
    throw std::runtime_error(message);
  };
  if (fcntl(sockets[0], F_SETFD, FD_CLOEXEC) < 0 ||
      fcntl(sockets[1], F_SETFD, FD_CLOEXEC) < 0 ||
      fcntl(sockets[0], F_SETFL, O_NONBLOCK) < 0)
    fail("Cannot configure engine socket");
#ifdef SO_NOSIGPIPE
  int yes = 1;
  if (setsockopt(sockets[0], SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes)) < 0)
    fail("Cannot suppress engine socket SIGPIPE");
#endif
  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0)
    fail("Cannot initialize engine spawn actions");
  int error =
      posix_spawn_file_actions_adddup2(&actions, sockets[1], STDIN_FILENO);
  if (!error)
    error =
        posix_spawn_file_actions_adddup2(&actions, sockets[1], STDOUT_FILENO);
  // Keep engine chatter out of the CLI's evaluation-only stdout.
  if (!error)
    error = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO,
                                             "/dev/null", O_WRONLY, 0);
  if (!error)
    error = posix_spawn_file_actions_addclose(&actions, sockets[0]);
  if (!error)
    error = posix_spawn_file_actions_addclose(&actions, sockets[1]);
  pid_t child = -1;
  char *argv[] = {const_cast<char *>(executable.c_str()), nullptr};
  if (!error)
    error = posix_spawnp(&child, executable.c_str(), &actions, nullptr, argv,
                         environ);
  posix_spawn_file_actions_destroy(&actions);
  if (error)
    fail("Cannot launch Stockfish '" + executable +
         "': " + std::strerror(error));
  close(sockets[1]);
  socket_ = sockets[0];
  pid_ = child;
}
Process::~Process() { shutdown(); }
void Process::send_line(const std::string &line, Clock::time_point deadline,
                        const CancelCheck &cancel) {
  std::string bytes = line + '\n';
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    ready(socket_, POLLOUT, deadline, cancel);
#ifdef MSG_NOSIGNAL
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    auto written =
        send(socket_, bytes.data() + offset, bytes.size() - offset, flags);
    if (written > 0)
      offset += static_cast<std::size_t>(written);
    else if (written < 0 &&
             (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
      continue;
    else
      throw std::runtime_error(
          "Stockfish closed its input or failed while receiving a command");
  }
}
std::string Process::read_line(Clock::time_point deadline,
                               const CancelCheck &cancel) {
  while (lines_.empty()) {
    ready(socket_, POLLIN, deadline, cancel);
    char chunk[4096];
    auto count = read(socket_, chunk, sizeof(chunk));
    if (count > 0) {
      for (auto &line : buffer_.append(
               std::string_view(chunk, static_cast<std::size_t>(count))))
        lines_.push_back(std::move(line));
    } else if (count == 0)
      throw std::runtime_error("Stockfish exited unexpectedly (EOF)");
    else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
      throw std::runtime_error("Failed reading Stockfish output");
  }
  check_cancelled(cancel);
  if (Clock::now() >= deadline)
    throw std::runtime_error("Stockfish operation timed out");
  auto result = std::move(lines_.front());
  lines_.pop_front();
  return result;
}
void Process::shutdown() noexcept {
  if (pid_ < 0)
    return;
  try {
    send_line("quit", Clock::now() + std::chrono::milliseconds(100));
  } catch (...) {
  }
  close(socket_);
  socket_ = -1;
  auto reap_until = [&](std::chrono::milliseconds duration) {
    auto deadline = Clock::now() + duration;
    do {
      int status;
      const auto result = waitpid(pid_, &status, WNOHANG);
      if (result == pid_ || (result < 0 && errno == ECHILD))
        return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (Clock::now() < deadline);
    return false;
  };
  if (!reap_until(std::chrono::milliseconds(200))) {
    kill(pid_, SIGTERM);
    if (!reap_until(std::chrono::milliseconds(200))) {
      kill(pid_, SIGKILL);
      while (waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {
      }
    }
  }
  pid_ = -1;
}
} // namespace chess_eval
