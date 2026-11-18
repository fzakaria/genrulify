#include "plan.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include <map>
#include <set>
#include <sys/stat.h>

namespace splice_ir {
namespace {
using Json = nlohmann::json;

constexpr int kJsonIndent = 1;
constexpr std::string_view kPipePrefix = "pipe:";

// A label names at most this many processes before it counts the rest.
constexpr size_t kLabelNames = 4;
} // namespace

bool readable(const Descriptor &descriptor) {
  return (descriptor.mode & S_IRUSR) != 0;
}

bool writable(const Descriptor &descriptor) {
  return (descriptor.mode & S_IWUSR) != 0;
}

bool isPipe(const Descriptor &descriptor) {
  return descriptor.target.starts_with(kPipePrefix);
}

void savePlan(const Plan &plan, const std::filesystem::path &file) {
  Json processes = Json::array();
  for (const auto &process : plan.processes) {
    processes.push_back(toJson(process));
  }
  Json inputs = Json::array();
  for (const auto &input : plan.inputs) {
    inputs.push_back({{"layer", input.layer}, {"path", input.path}});
  }
  const Json document = {{"name", plan.name},
                         {"closure", plan.closure},
                         {"platform", plan.platform},
                         {"processes", processes},
                         {"environments", plan.environments},
                         {"inputs", inputs},
                         {"results", plan.results},
                         {"output", plan.output}};
  writeBytes(file, document.dump(kJsonIndent) + "\n");
}

Plan loadPlan(const std::filesystem::path &file) {
  const auto document = Json::parse(readBytes(file));
  Plan plan;
  plan.name = document.at("name");
  plan.closure = document.at("closure").get<Arguments>();
  plan.platform = document.at("platform");
  for (const auto &process : document.at("processes")) {
    plan.processes.push_back(processFromJson(process));
  }
  plan.environments =
      document.at("environments").get<std::vector<Arguments>>();
  for (const auto &input : document.at("inputs")) {
    plan.inputs.push_back({input.at("layer"), input.at("path")});
  }
  plan.results = document.at("results").get<std::vector<Entry>>();
  plan.output = document.at("output");
  return plan;
}

void addProcesses(Plan &plan, const Recording &recording, const Group &group) {
  // Renumber the environments the group uses from zero.
  std::map<size_t, size_t> renumbered;
  for (auto process : group.processes) {
    const auto [slot, fresh] =
        renumbered.emplace(process.environment, plan.environments.size());
    if (fresh) {
      plan.environments.push_back(
          recording.environments.at(process.environment));
    }
    process.environment = slot->second;
    plan.processes.push_back(std::move(process));
  }
}

std::optional<std::string> unreplayable(const Group &group) {
  // Find the pipes some process of the group writes.
  std::set<std::string> written;
  for (const auto &process : group.processes) {
    for (const auto &descriptor : process.fds) {
      if (isPipe(descriptor) && writable(descriptor)) {
        written.insert(descriptor.target);
      }
    }
  }

  for (const auto &process : group.processes) {
    for (const auto &descriptor : process.fds) {
      // The bytes of a pipe that only the builder wrote, as a here-document
      // is, were never in a file and are not in the recording.
      if (isPipe(descriptor)) {
        if (readable(descriptor) && !written.contains(descriptor.target)) {
          return absl::StrCat(process.name, " reads descriptor ",
                              descriptor.fd, " from a pipe the builder wrote");
        }
        continue;
      }

      // A target that is not a path, such as a socket or an eventfd, holds
      // state the recording did not capture.
      if (!descriptor.target.starts_with("/")) {
        return absl::StrCat(process.name, " holds ", descriptor.target);
      }
    }
  }
  return std::nullopt;
}

std::string label(const Group &group) {
  Arguments names;
  for (const auto &process : group.processes) {
    if (names.size() == kLabelNames) {
      names.push_back(absl::StrCat("+", group.processes.size() - kLabelNames));
      break;
    }
    names.push_back(process.name);
  }
  return absl::StrJoin(names, "|");
}
} // namespace splice_ir
