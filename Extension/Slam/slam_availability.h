#pragma once
#include "slam_runtime.h"

namespace dingosdk::slam {
// Physics samples remain useful for normal-play X-ray while attempts are
// blocked. A fresh sample must not clear the game tick's restriction.
inline void publish_attempt_availability(Snapshot& output, const char* issue, bool fresh, bool bailed) {
    output.available = !issue && fresh;
    if (issue) output.availability = issue;
    else if (fresh) output.availability = bailed
        ? "Recover and get back on your board before retrying."
        : "Ready to start an attempt.";
}
}
