// Synthetic labeled glyph boards for pipeline tests, NOT website reference
// assets.
#include "chess_eval/vision/recognizer.hpp"
#include <fstream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
using namespace chess_eval::vision;
cv::Mat render(const Board &board, Orientation orientation) {
  cv::Mat image(384, 384, CV_8UC3);
  for (int row = 0; row < 8; ++row)
    for (int col = 0; col < 8; ++col) {
      auto square = image(cv::Rect(col * 48, row * 48, 48, 48));
      square.setTo((row + col) % 2 ? cv::Scalar(90, 130, 90)
                                   : cv::Scalar(145, 170, 145));
      char piece = board.squares[canonicalIndex(row, col, orientation)].piece;
      if (piece != '.')
        cv::putText(
            square, std::string(1, static_cast<char>(std::toupper(piece))),
            {7, 35}, cv::FONT_HERSHEY_SIMPLEX, 1.1,
            piece >= 'a' ? cv::Scalar(5, 5, 5) : cv::Scalar(255, 255, 255), 2,
            cv::LINE_AA);
    }
  return image;
}
int main(int argc, char **argv) {
  if (argc != 2)
    return 1;
  std::filesystem::path root(argv[1]);
  std::filesystem::create_directories(root);
  Board calibration;
  const std::string classes = ".PNBRQKpnbrqk";
  for (int i = 0; i < 64; ++i)
    calibration.squares[i] = {classes[(i / 2) % 13], 1};
  cv::imwrite((root / "synthetic-calibration.png").string(),
              render(calibration, Orientation::WhiteBottom));
  std::ofstream labels(root / "synthetic-calibration.labels");
  // Label grid intentionally need not represent a legal chess position.
  for (int i = 0; i < 64; ++i) {
    labels << (calibration.squares[i].piece == '.'
                   ? '1'
                   : calibration.squares[i].piece);
    if (i % 8 == 7 && i != 63)
      labels << '/';
  }
  labels << '\n';
  const std::string fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR";
  const auto board = parsePlacement(fen);
  cv::imwrite((root / "synthetic-white.png").string(),
              render(board, Orientation::WhiteBottom));
  cv::Mat scaled;
  cv::resize(render(board, Orientation::BlackBottom), scaled, {768, 768}, 0, 0,
             cv::INTER_LINEAR);
  cv::RNG random(421);
  cv::Mat noise(scaled.size(), CV_16SC3);
  random.fill(noise, cv::RNG::NORMAL, 0, 1.5);
  cv::Mat signedImage;
  scaled.convertTo(signedImage, CV_16SC3);
  signedImage += noise;
  signedImage.convertTo(scaled, CV_8UC3);
  cv::imwrite((root / "synthetic-black-retina.png").string(), scaled);
  std::ofstream(root / "placement.txt") << fen << '\n';
}
