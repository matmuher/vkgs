#include <iostream>
#include <future>

#include "argparse/argparse.hpp"

#include <pygs/engine/engine.h>
#include <pygs/scene/splats.h>

int main(int argc, char** argv) {
  argparse::ArgumentParser parser("pygs");
  parser.add_argument("-i", "--input").required().help("input ply file.");
  try {
    parser.parse_args(argc, argv);
  } catch (const std::exception& err) {
    std::cerr << err.what() << std::endl;
    std::cerr << parser;
    return 1;
  }

  try {
    auto ply_filepath = parser.get<std::string>("input");
    auto splats_future =
        std::async(std::launch::async, pygs::Splats::load, ply_filepath);

    pygs::Engine engine;
    engine.AddSplatsAsync(std::move(splats_future));
    engine.Run();
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
  }

  return 0;
}
