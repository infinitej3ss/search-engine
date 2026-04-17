#include <cstdio>
#include <iostream>
#include <string>

#include "engine/search_engine.hpp"

int main(int argc, char** argv) {
    std::string data_dir = ".";
    std::string weights_path = "config/weights.txt";

    // if run from data/, adjust paths
    if (argc > 1) data_dir = argv[1];

    SearchEngine engine(weights_path, data_dir);

    std::printf("ready — Ctrl+D to quit\n");

    std::string line;
    while (std::cout << "> " && std::getline(std::cin, line)) {
        if (line.empty()) continue;

        int total = 0;
        auto results = engine.search(line, 0, 10, &total);

        if (results.empty()) {
            std::cout << "  (no matches)\n";
            continue;
        }

        std::printf("  %d result(s)\n", total);
        for (size_t i = 0; i < results.size(); i++) {
            const auto& r = results[i];
            std::printf("%2zu. [%.4f] %s\n", i + 1, r.combined_score, r.url.c_str());
            if (!r.title.empty())
                std::printf("    %s\n", r.title.c_str());
            if (!r.snippet.empty())
                std::printf("    %s\n", r.snippet.c_str());
        }
    }
}
