#include "chess_eval/vision/recognizer.hpp"
#include <iostream>
#include <opencv2/imgcodecs.hpp>
int main(int argc, char **argv) {
  try {
    if (argc != 5)
      throw std::invalid_argument(
          "Usage: chess-template-tool BOARD_IMAGE PIECE_PLACEMENT white|black "
          "OUTPUT_DIRECTORY");
    const std::string orientation = argv[3];
    if (orientation != "white" && orientation != "black")
      throw std::invalid_argument(
          "Orientation must be white or black (bottom side)");
    chess_eval::vision::exportTemplates(
        cv::imread(argv[1], cv::IMREAD_COLOR),
        chess_eval::vision::parsePlacement(argv[2]),
        orientation == "white" ? chess_eval::vision::Orientation::WhiteBottom
                               : chess_eval::vision::Orientation::BlackBottom,
        argv[4]);
    std::cout << "Saved labeled square variants. Supply both backgrounds for "
                 "all 13 classes before recognition.\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
