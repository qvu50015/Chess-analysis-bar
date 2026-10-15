#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
int main() {
  const char *setting = std::getenv("CHESS_EVAL_TEST_MODE");
  const std::string mode = setting ? setting : "normal";
  if (mode == "eof")
    return 0;
  if (mode == "hang") {
    std::signal(SIGTERM, SIG_IGN);
    for (;;)
      std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  bool uci = false, ready = false, position = false;
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line == "uci") {
      uci = true;
      std::cout << "id name test\nuci" << std::flush;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      std::cout << "ok\r\n" << std::flush;
    } else if (line == "isready" && uci) {
      ready = true;
      std::cout << "readyok\n" << std::flush;
    } else if (line.starts_with("position fen ") && ready)
      position = true;
    else if (line.starts_with("go depth ") && position) {
      if (mode == "progress" || mode == "progress_eof") {
        std::cout << "info depth 8 score cp 12\n" << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        if (mode == "progress_eof")
          return 0;
      }
      if (mode == "slow")
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
      if (mode == "search_eof")
        return 0;
      if (mode == "search_hang")
        continue;
      if (mode == "bounds")
        std::cout << "info depth 4 score cp 21 lowerbound\n";
      else if (mode == "malformed")
        std::cout << "info depth 4 score cp nope\n";
      else if (mode == "mate")
        std::cout << "info depth 4 score mate -2\n";
      else if (mode == "progress")
        std::cout << "info depth 12 score cp 31\n";
      else {
        std::cout << "info depth 1 score cp 7\ninfo depth 4 score cp 31\n";
        std::cout << "info depth 4 score cp 99 lowerbound\ninfo depth 2 score "
                     "cp 11\n";
      }
      std::cout << "bestmove (none)\n" << std::flush;
    } else if (line == "stop") {
      std::cout << "bestmove (none)\n" << std::flush;
    } else if (line == "quit")
      return 0;
    else
      return 2;
  }
}
