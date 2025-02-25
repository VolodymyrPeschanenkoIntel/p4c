#include "backends/p4tools/modules/testgen/options.h"

#include <cstdlib>
#include <iostream>
#include <string>

#include "backends/p4tools/common/options.h"
#include "lib/exceptions.h"

#include "backends/p4tools/modules/testgen/lib/logging.h"

namespace P4Tools {

TestgenOptions &TestgenOptions::get() {
    static TestgenOptions INSTANCE;
    return INSTANCE;
}

const char *TestgenOptions::getIncludePath() {
    P4C_UNIMPLEMENTED("getIncludePath not implemented for P4Testgen.");
}

const std::set<cstring> TestgenOptions::SUPPORTED_STOP_METRICS = {"MAX_STATEMENT_COVERAGE"};
const std::set<cstring> TestgenOptions::SUPPORTED_EXPLORATION_STRATEGIES = {
    "INCREMENTAL_STACK", "RANDOM_ACCESS_STACK", "LINEAR_ENUMERATION", "MAX_COVERAGE",
    "RANDOM_ACCESS_MAX_COVERAGE"};

TestgenOptions::TestgenOptions()
    : AbstractP4cToolOptions("Generate packet tests for a P4 program.") {
    registerOption(
        "--strict", nullptr,
        [this](const char *) {
            strict = true;
            return true;
        },
        "Fail on unimplemented features instead of trying the next branch.");

    registerOption(
        "--max-tests", "maxTests",
        [this](const char *arg) {
            try {
                // Unfortunately, we can not use std::stoul because negative inputs are okay
                // according to the C++ standard.
                maxTests = std::stoll(arg);
                if (maxTests < 0) {
                    throw std::invalid_argument("Invalid input.");
                }
            } catch (std::invalid_argument &) {
                ::error("Invalid input value %1% for --max-tests. Expected positive integer.", arg);
                return false;
            }
            return true;
        },
        "Sets the maximum number of tests to be generated [default: 1]. Setting the value to 0 "
        "will generate tests until no more paths can be found.");

    registerOption(
        "--stop-metric", "stopMetric",
        [this](const char *arg) {
            stopMetric = cstring(arg).toUpper();
            if (SUPPORTED_STOP_METRICS.count(stopMetric) == 0) {
                ::error(
                    "Stop metric %1% not supported. Supported stop metrics are "
                    "%2%.",
                    stopMetric, Utils::containerToString(SUPPORTED_STOP_METRICS));
                return false;
            }
            return true;
        },
        "Stops generating tests when a particular metric is satisifed. Currently supported options "
        "are:\n\"MAX_STATEMENT_COVERAGE\".");

    registerOption(
        "--packet-size-range", "packetSizeRange",
        [this](const char *arg) {
            auto rangeStr = std::string(arg);
            size_t packetLenStr = rangeStr.find_first_of(':');
            try {
                auto minPacketLenStr = rangeStr.substr(0, packetLenStr);
                minPktSize = std::stoi(minPacketLenStr);
                if (minPktSize < 0) {
                    ::error(
                        "Invalid minimum packet size %1%. Minimum packet size must be at least 0.",
                        minPktSize);
                }
                auto maxPacketLenStr = rangeStr.substr(packetLenStr + 1);
                maxPktSize = std::stoi(maxPacketLenStr);
                if (maxPktSize < minPktSize) {
                    ::error(
                        "Invalid packet size range %1%:%2%.  The maximum packet size must be at "
                        "least the size of the minimum packet size.",
                        minPktSize, maxPktSize);
                }
            } catch (std::invalid_argument &) {
                ::error(
                    "Invalid packet size range %1%. Expected format is [min]:[max], where [min] "
                    "and [max] are integers.",
                    arg);
                return false;
            }
            return true;
        },
        "Specify the possible range of the input packet size in bits. The format is [min]:[max]. "
        "The default values are \"0:72000\". The maximum is set to jumbo frame size (9000 bytes).");

    registerOption(
        "--out-dir", "outputDir",
        [this](const char *arg) {
            outputDir = arg;
            return true;
        },
        "The output directory for the generated tests. The directory will be created, if it does "
        "not exist.");

    registerOption(
        "--test-backend", "testBackend",
        [this](const char *arg) {
            testBackend = arg;
            testBackend = testBackend.toUpper();
            return true;
        },
        "Select the test back end. P4Testgen will produce tests that correspond to the input "
        "format of this test back end.");

    registerOption(
        "--input-branches", "selectedBranches",
        [this](const char *arg) {
            selectedBranches = arg;
            // These options are mutually exclusive.
            if (trackBranches) {
                std::cerr << "--input-branches and --track-branches are mutually exclusive. Choose "
                             "one or the other."
                          << std::endl;
                exit(1);
            }
            return true;
        },
        "List of the selected branches which should be chosen for selection.");

    registerOption(
        "--track-branches", nullptr,
        [this](const char *) {
            trackBranches = true;
            // These options are mutually exclusive.
            if (!selectedBranches.empty()) {
                std::cerr << "--input-branches and --track-branches are mutually exclusive. Choose "
                             "one or the other."
                          << std::endl;
                exit(1);
            }
            return true;
        },
        "Track the branches that are chosen in the symbolic executor. This can be used for "
        "deterministic replay.");

    registerOption(
        "--with-output-packet", nullptr,
        [this](const char *) {
            withOutputPacket = true;
            if (!selectedBranches.empty()) {
                std::cerr << "--input-branches cannot guarantee --with-output-packet."
                             " Aborting."
                          << std::endl;
                exit(1);
            }
            return true;
        },
        "Produced tests must have an output packet.");

    registerOption(
        "--exploration-strategy", "explorationStrategy",
        [this](const char *arg) {
            explorationStrategy = cstring(arg).toUpper();
            if (SUPPORTED_EXPLORATION_STRATEGIES.count(explorationStrategy) == 0) {
                ::error(
                    "Exploration strategy %1% not supported. Supported exploration strategies are "
                    "%2%.",
                    explorationStrategy,
                    Utils::containerToString(SUPPORTED_EXPLORATION_STRATEGIES));
                return false;
            }
            return true;
        },
        "Selects a specific exploration strategy for test generation. Options are: "
        "INCREMENTAL_STACK, RANDOM_ACCESS_STACK, LINEAR_ENUMERATION, MAX_COVERAGE, and "
        "RANDOM_ACCESS_MAX_COVERAGE. Defaults to "
        "INCREMENTAL_STACK.");

    registerOption(
        "--pop-level", "popLevel",
        [this](const char *arg) {
            int64_t popLevelTmp = 0;
            try {
                // Unfortunately, we can not use std::stoul because negative inputs are okay
                // according to the C++ standard.
                popLevelTmp = std::stoll(arg);
                if (popLevelTmp <= 0) {
                    throw std::invalid_argument("Invalid input.");
                }
            } catch (std::invalid_argument &) {
                ::error(
                    "Invalid input value %1% for --pop-level. Expected positive, non-zero "
                    "integer.",
                    arg);
                return false;
            }
            popLevel = popLevelTmp;
            return true;
        },
        "Sets the fraction for multiPop exploration; default is 3 when meaningful strategy is "
        "activated.");

    registerOption(
        "--linear-enumeration", "linearEnumeration",
        [this](const char *arg) {
            int64_t linearEnumerationTmp = 0;
            try {
                // Unfortunately, we can not use std::stoul because negative inputs are okay
                // according to the C++ standard.
                linearEnumerationTmp = std::stoll(arg);
                if (linearEnumerationTmp <= 1) {
                    throw std::invalid_argument("Invalid input.");
                }
            } catch (std::invalid_argument &) {
                ::error(
                    "Invalid input value %1% for --linear-enumeration. Expected an integer greater "
                    "than 1.",
                    arg);
                return false;
            }
            linearEnumeration = linearEnumerationTmp;
            return true;
        },
        "Max bound for vector size in LINEAR_ENUMERATION; defaults to 2.");

    registerOption(
        "--saddle-point", "saddlePoint",
        [this](const char *arg) {
            int64_t saddlePointTmp = 0;
            try {
                // Unfortunately, we can not use std::stoul because negative inputs are okay
                // according to the C++ standard.
                saddlePointTmp = std::stoll(arg);
                if (saddlePointTmp <= 1) {
                    throw std::invalid_argument("Invalid input.");
                }
            } catch (std::invalid_argument &) {
                ::error(
                    "Invalid input value %1% for --saddle-point. Expected an integer greater than "
                    "1.",
                    arg);
                return false;
            }
            saddlePoint = saddlePointTmp;
            return true;
        },
        "Threshold to invoke multiPop on RANDOM_ACCESS_MAX_COVERAGE.");

    registerOption(
        "--print-traces", nullptr,
        [](const char *) {
            P4Testgen::enableTraceLogging();
            return true;
        },
        "Print the associated traces and test information for each generated test.");

    registerOption(
        "--print-steps", nullptr,
        [](const char *) {
            P4Testgen::enableStepLogging();
            return true;
        },
        "Print the representation of each program node while the stepper steps through the "
        "program.");

    registerOption(
        "--print-coverage", nullptr,
        [](const char *) {
            P4Testgen::enableCoverageLogging();
            return true;
        },
        "Print detailed statement coverage statistics the interpreter collects while stepping "
        "through the program.");

    registerOption(
        "--print-performance-report", nullptr,
        [](const char *) {
            P4Testgen::enablePerformanceLogging();
            return true;
        },
        "Print timing report summary at the end of the program.");

    registerOption(
        "--dcg", "DCG",
        [this](const char *) {
            dcg = true;
            return true;
        },
        R"(Build a DCG for input graph. This control flow graph directed cyclic graph can be used
        for statement reachability analysis.)");

    registerOption(
        "--pattern", "pattern",
        [this](const char *arg) {
            pattern = arg;
            return true;
        },
        "List of the selected branches which should be chosen for selection.");
}

}  // namespace P4Tools
