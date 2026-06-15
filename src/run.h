#pragma once

#include <string>

#include "config.h"
#include "feed.h"

namespace hft {

// Validates the alpha/lead config, resolves <datetime> in the output path,
// creates the output dir, and copies the config in for reproducibility.
// Throws std::runtime_error on a bad config.
void validateConfig(RunConfig &cfg, const std::string &configPath);

// Replays a feed under a config: every symbol gets its own book + the
// configured alphas, computed on EVERY tick. Returns a process exit code.
int run(Feed &feed, const std::string &label, const RunConfig &cfg);

}
