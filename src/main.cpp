#include <cstdio>
#include <cstring>
#include <exception>
#include <optional>
#include <string>

#include "config.h"
#include "feed.h"
#include "run.h"

using namespace hft;

namespace {

// Exactly two inputs:
//   --config <run.json>
//   <ITCH file>
struct Args {
    std::string configPath;
    std::string itchPath;
};

std::optional<Args> parseArgs(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            args.configPath = argv[++i];
        } else {
            args.itchPath = argv[i];
        }
    }
    if (args.configPath.empty() || args.itchPath.empty()) {
        std::fprintf(stderr, "usage: engine --config <run.json> <ITCH file>\n");
        return std::nullopt;
    }
    return args;
}

}

int main(int argc, char** argv) {
    const std::optional<Args> args = parseArgs(argc, argv);
    if (!args) { return 1; }

    try {
        RunConfig cfg = loadConfig(args->configPath);
        validateConfig(cfg, args->configPath);
        ItchFeed feed(args->itchPath, cfg.symbols);
        return run(feed, args->itchPath, cfg);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
