#include "chess_eval/ui/window.hpp"
#include <QApplication>
int main(int argc, char **argv) {
  QApplication application(argc, argv);
  application.setApplicationName("Chess Evaluation Bar");
  chess_eval::ui::Window window;
  window.show();
  return application.exec();
}
