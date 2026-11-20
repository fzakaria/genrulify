#include "recording.h"
#include <fstream>

namespace splice_ir {
namespace fs = std::filesystem;
namespace {
using Json = nlohmann::json;

// The version of the recording layout. A reader refuses any other version.
constexpr int kFormat = 1;

constexpr int kJsonIndent = 1;

State readState(const fs::path &file) {
  State state;
  for (const Entry entry : Json::parse(readBytes(file))) {
    state.emplace(entry.path, entry);
  }
  return state;
}

Json toJson(const Descriptor &descriptor) {
  return {{"fd", descriptor.fd},
          {"target", descriptor.target},
          {"mode", descriptor.mode},
          {"flags", descriptor.flags},
          {"position", descriptor.position},
          {"input", descriptor.input ? Json(*descriptor.input) : Json()}};
}

Descriptor descriptorFromJson(const Json &json) {
  Descriptor descriptor{json.at("fd"), json.at("target"), json.at("mode"),
                        json.at("flags"), json.at("position"), std::nullopt};
  if (const auto input = json.find("input");
      input != json.end() && input->is_string()) {
    descriptor.input = input->get<std::string>();
  }
  return descriptor;
}

} // namespace

nlohmann::json toJson(const Process &process) {
  Json fds = Json::array();
  for (const auto &descriptor : process.fds) {
    fds.push_back(toJson(descriptor));
  }
  return {{"pid", process.pid},
          {"step", process.step},
          {"exit", process.exit},
          {"status", process.status},
          {"name", process.name},
          {"filename", process.filename},
          {"exe", process.exe},
          {"cwd", process.cwd},
          {"umask", process.umask},
          {"argv", process.argv},
          {"environment", process.environment},
          {"fds", fds}};
}

Process processFromJson(const nlohmann::json &json) {
  Process process;
  process.pid = json.at("pid");
  process.step = json.at("step");
  process.exit = json.at("exit");
  process.status = json.at("status");
  process.name = json.at("name");
  process.filename = json.value("filename", std::string());
  process.exe = json.at("exe");
  process.cwd = json.at("cwd");
  process.umask = json.value("umask", kBuilderUmask);
  process.argv = json.at("argv").get<Arguments>();
  process.environment = json.at("environment");
  for (const auto &descriptor : json.at("fds")) {
    process.fds.push_back(descriptorFromJson(descriptor));
  }
  return process;
}
State Recording::state(uint64_t step) const {
  auto state = readState(directory / kStatesDirectory /
                         (std::to_string(step) + ".json"));

  // The build cannot write its source tree, so one state serves every
  // step.
  state.merge(sourceState());
  return state;
}

State Recording::sourceState() const {
  if (source.empty()) {
    return {};
  }
  if (loadedSource) {
    return *loadedSource;
  }
  return readState(directory / kStatesDirectory /
                   (std::string(kSourceState) + ".json"));
}

State Recording::platform() const {
  return readState(directory / kStatesDirectory /
                   (std::string(kPlatformState) + ".json"));
}

fs::path Recording::blob(const std::string &digest) const {
  return directory / kBlobsDirectory / digest;
}

const Group &Recording::group(uint64_t step) const {
  for (const auto &group : groups) {
    if (group.start == step) {
      return group;
    }
  }
  throw std::runtime_error("the recording has no process group at step " +
                           std::to_string(step));
}

void Recording::save() const {
  Json groupList = Json::array();
  for (const auto &group : groups) {
    Json processes = Json::array();
    for (const auto &process : group.processes) {
      processes.push_back(toJson(process));
    }
    groupList.push_back({{"start", group.start},
                         {"end", group.end},
                         {"processes", processes},
                         {"reads", group.reads}});
  }
  const Json document = {{"format", kFormat},
                         {"run", run},
                         {"name", name},
                         {"outputs", outputs},
                         {"closure", closure},
                         {"roots", roots},
                         {"source", source},
                         {"open", open},
                         {"fine", fine},
                         {"initialStep", initialStep},
                         {"finalStep", finalStep},
                         {"lost", lost},
                         {"wallMilliseconds", wallMilliseconds},
                         {"environments", environments},
                         {"groups", groupList}};
  writeBytes(directory / kRecordingFile, document.dump(kJsonIndent) + "\n");
}

Recording Recording::load(const fs::path &directory) {
  const auto document = Json::parse(readBytes(directory / kRecordingFile));
  check(document.at("format") == kFormat, "unsupported recording format");
  Recording recording;
  recording.directory = directory;
  recording.run = document.at("run");
  recording.name = document.at("name");
  recording.outputs = document.at("outputs");
  recording.closure = document.at("closure").get<Arguments>();
  recording.roots = document.at("roots").get<Arguments>();

  // Only the recording of a project converted in place has a source tree.
  recording.source = document.value("source", std::string());
  const auto sourceFile =
      directory / kStatesDirectory / (std::string(kSourceState) + ".json");
  if (!recording.source.empty() && fs::exists(sourceFile)) {
    recording.loadedSource = std::make_shared<const State>(readState(sourceFile));
  }

  // Recordings made before programs could be opened have no pattern.
  recording.open = document.value("open", std::string());
  recording.fine = document.value("fine", false);
  recording.initialStep = document.at("initialStep");
  recording.finalStep = document.at("finalStep");
  recording.lost = document.at("lost");
  recording.wallMilliseconds = document.value("wallMilliseconds", uint64_t{0});
  recording.environments =
      document.at("environments").get<std::vector<Arguments>>();
  for (const auto &json : document.at("groups")) {
    Group group;
    group.start = json.at("start");
    group.end = json.at("end");
    group.reads = json.at("reads").get<Arguments>();
    for (const auto &process : json.at("processes")) {
      group.processes.push_back(processFromJson(process));
    }
    recording.groups.push_back(std::move(group));
  }
  return recording;
}

Difference difference(const State &before, const State &after) {
  Difference result;
  for (const auto &[path, entry] : after) {
    const auto previous = before.find(path);
    if (previous == before.end() || !sameVersion(previous->second, entry)) {
      result.changed.push_back(entry);
    }
  }
  for (const auto &[path, entry] : before) {
    if (!after.contains(path)) {
      result.removed.push_back(path);
    }
  }
  return result;
}

void writeState(const fs::path &file, const State &state) {
  Json entries = Json::array();
  for (const auto &[path, entry] : state) {
    entries.push_back(entry);
  }
  writeBytes(file, entries.dump(kJsonIndent) + "\n");
}
} // namespace splice_ir
