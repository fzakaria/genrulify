// The explorer: one recorded build at a time, as a timeline of process
// groups, the optimized graph, and the details of whichever group or node
// is selected. All data is static JSON and text that the compiler wrote.

import { draw, highlight } from "./graph.js";

const $ = (id) => document.getElementById(id);

// Replaced with the Nix store path of the site when the site is built.
const STORE_PATH = "__STORE_PATH__";

// The outcome the checker assigns a process group. The values are the
// strings in graph.json and the CSS classes that color a cell or a node.
const Outcome = Object.freeze({
  NARROW: "narrow",
  NEAR: "near",
  TRACED: "traced",
  WIDE: "wide",
  LITERAL: "literal",
  DEAD: "dead",
});

// The views of the selected group, by the data-lane value of their tab.
const Lane = Object.freeze({
  PROCESS: "process",
  FILES: "files",
  MLIR: "mlir",
  BAZEL: "bazel",
  BUCK: "buck",
});

// The kinds of node in graph.json.
const Op = Object.freeze({ EXEC: "exec", LITERAL: "literal", OUTPUT: "output" });

// Programs that only run other commands. A node for one is labelled by
// what it wrote, since a row of nodes all named "bash" says nothing.
const SHELLS = new Set(["bash", "sh"]);

// The query parameter that selects a package.
const PACKAGE_PARAMETER = "pkg";

// Hex digits of the NAR hash shown in the stats line.
const SHOWN_HASH_DIGITS = 16;

// What the page is showing. `selection` is a group's step or a node's id.
const state = {
  packages: [],
  id: null,
  graph: null,
  files: new Map(),
  nodeOfStep: new Map(),
  groupOfStep: new Map(),
  selection: null,
  lane: Lane.PROCESS,
};

async function fetchText(path) {
  const response = await fetch(path);
  if (!response.ok) {
    throw new Error(`${path}: ${response.status}`);
  }
  return response.text();
}

/** A build file of the current package, fetched once. */
function packageFile(name) {
  const key = `${state.id}/${name}`;
  if (!state.files.has(key)) {
    state.files.set(key, fetchText(`data/${key}`));
  }
  return state.files.get(key);
}

function create(tag, properties = {}, children = []) {
  const created = document.createElement(tag);
  Object.assign(created, properties);
  created.append(...children);
  return created;
}

/** Quotes an argument the way a shell would need it typed. */
function shellQuote(argument) {
  if (/^[A-Za-z0-9_@%+=:,./-]+$/.test(argument)) {
    return argument;
  }
  return `'${argument.replaceAll("'", `'\\''`)}'`;
}

function outcomeOf(group) {
  return group.outcome ?? Outcome.NARROW;
}

/** The class that colors a graph node. */
function nodeClass(node) {
  if (node.op === Op.OUTPUT) {
    return "output";
  }
  if (node.op === Op.LITERAL) {
    return Outcome.LITERAL;
  }
  return outcomeOf(state.groupOfStep.get(node.step));
}

/** The text inside a graph node. */
function nodeLabel(node) {
  if (node.op === Op.OUTPUT) {
    return `$${node.name}`;
  }
  const used = node.writes.find((write) => write.used);
  if (node.op === Op.EXEC && SHELLS.has(node.name) && used) {
    return `${node.name} → ${used.path.split("/").pop()}`;
  }
  return node.name;
}

function renderPackages() {
  const links = state.packages.map((entry) => {
    const link = create("a", { textContent: entry.label, title: entry.note });
    link.href = `?${PACKAGE_PARAMETER}=${entry.id}`;
    link.classList.toggle("active", entry.id === state.id);
    link.addEventListener("click", (event) => {
      event.preventDefault();
      history.pushState(null, "", link.href);
      load(entry.id);
    });
    return link;
  });
  $("packages").replaceChildren(...links.flatMap((link, index) => (index ? [" · ", link] : [link])));
}

function renderStats(hash) {
  const groups = state.graph.groups;
  const count = (outcome) => groups.filter((group) => outcomeOf(group) === outcome).length;
  const pinned = groups.reduce((total, group) => total + (group.pinned?.length ?? 0), 0);
  const rules = state.graph.nodes.filter((node) => node.op !== Op.LITERAL).length;
  const parts = [
    `${groups.length.toLocaleString()} process groups`,
    `${count(Outcome.NARROW) + count(Outcome.NEAR) + count(Outcome.TRACED) + count(Outcome.WIDE)} replayed`,
    `${pinned} files pinned`,
    `${count(Outcome.LITERAL)} kept as recorded bytes`,
    `${count(Outcome.DEAD)} dead`,
    `${rules} genrules`,
  ];
  const digest = hash.trim().slice(0, "sha256:".length + SHOWN_HASH_DIGITS);
  $("stats").replaceChildren(
    `${state.graph.name}: ${parts.join(" · ")} · NAR `,
    create("code", { textContent: `${digest}…`, title: hash.trim() }),
  );
}

function renderTimeline() {
  const cells = state.graph.groups.map((group) => {
    const cell = create("button", { type: "button", title: `${group.name} at step ${group.step}` });
    cell.className = `cell ${outcomeOf(group)}`;
    cell.classList.toggle("pinned", (group.pinned?.length ?? 0) > 0);
    cell.setAttribute("role", "listitem");
    cell.dataset.step = group.step;
    cell.addEventListener("click", () => select(group.step));
    return cell;
  });
  $("timeline").replaceChildren(...cells);
}

/** One sentence on what the checker concluded about a group. */
function describeOutcome(group) {
  const pinned = group.pinned ?? [];
  const pins = pinned.length
    ? ` ${pinned.length} of its files differ on every run and are taken from the recording: ` +
      `${pinned.map((path) => path.split("/").pop()).join(", ")}.`
    : "";
  switch (outcomeOf(group)) {
    case Outcome.NARROW:
      return `Replayed and reproduced, given only the files it opened.${pins}`;
    case Outcome.NEAR:
      return (
        "Replayed and reproduced, given the files it opened and everything else in their " +
        `directories. It looks at files it never opens, with stat or a directory listing.${pins}`
      );
    case Outcome.TRACED:
      return (
        `Replayed and reproduced, given the ${group.inputs} entries a trace of its own replay ` +
        `shows it touching. It looks at files it never opens, with stat or a directory listing.${pins}`
      );
    case Outcome.WIDE:
      return (
        "Replayed and reproduced, given every entry in the tree. " +
        `It failed with only the directories it worked in, so it looks further afield.${pins}`
      );
    case Outcome.LITERAL:
      return `Kept as recorded bytes. ${group.reason.split("\n").slice(0, 2).join(" ")}`;
    default:
      return "Dead. No output depends on anything this group wrote.";
  }
}

function renderProcesses(group) {
  if (!group) {
    return [create("p", { className: "muted", textContent: "No process ran here. These bytes come from the recording." })];
  }
  return group.processes.flatMap((process) => [
    create("p", { className: "muted" }, ["in ", create("code", { textContent: process.cwd })]),
    create("pre", { textContent: process.argv.map(shellQuote).join(" ") }),
  ]);
}

function table(headings, rows) {
  const head = create("tr", {}, headings.map((heading) => create("th", { textContent: heading })));
  return create("table", {}, [create("thead", {}, [head]), create("tbody", {}, rows)]);
}

function renderFiles(node) {
  const inputs = node.inputs.map((input) => {
    const more = input.count - input.paths.length;
    const paths = input.paths.join("\n") + (more > 0 ? `\n… and ${more} more` : "");
    return create("tr", {}, [
      create("td", {}, [create("code", { textContent: input.node })]),
      create("td", { textContent: input.count }),
      create("td", {}, [create("code", { textContent: paths, style: "white-space: pre-wrap" })]),
    ]);
  });
  const parts = [create("p", { className: "muted", textContent: "Given, by the rule that wrote each version:" })];
  parts.push(inputs.length ? table(["From", "Entries", "Paths"], inputs) : create("p", { textContent: "Nothing." }));
  if (node.op === Op.OUTPUT) {
    return parts;
  }

  const writes = node.writes.map((write) => {
    const row = create("tr", {}, [
      create("td", {}, [create("code", { textContent: write.path })]),
      create("td", { textContent: `${write.kind} ${write.mode.toString(8)}` }),
      create("td", {}, [create("code", { textContent: write.content })]),
      create("td", { textContent: write.used ? "yes" : "no" }),
    ]);
    row.classList.toggle("unused", !write.used);
    return row;
  });
  const more = node.writeCount - node.writes.length;
  parts.push(
    create("p", { className: "muted", textContent: `Wrote ${node.writeCount} entries${more > 0 ? `, the first ${node.writes.length} shown` : ""}:` }),
    table(["Path", "Kind", "Content", "Used"], writes),
  );
  return parts;
}

/** The genrule named `id` in a build file's text. */
function ruleText(buildFile, id) {
  const start = buildFile.indexOf(`genrule(\n    name = "${id}"`);
  if (start < 0) {
    return "This node is a file checked into the workspace, not a rule.";
  }
  return buildFile.slice(start, buildFile.indexOf("\n)\n", start) + 2);
}

async function renderLane() {
  for (const tab of $("lanes").querySelectorAll("a")) {
    tab.classList.toggle("active", tab.dataset.lane === state.lane);
  }
  const { node, group } = selected();
  const absent = [create("p", { className: "muted", textContent: "This group was eliminated, so the optimized graph has no node for it." })];
  let content;
  if (state.lane === Lane.PROCESS) {
    content = renderProcesses(group);
  } else if (!node) {
    content = absent;
  } else if (state.lane === Lane.FILES) {
    content = renderFiles(node);
  } else if (state.lane === Lane.MLIR) {
    content = [create("pre", { textContent: node.mlir })];
  } else {
    const file = state.lane === Lane.BAZEL ? "BUILD.bazel" : "BUCK";
    content = [create("pre", { textContent: ruleText(await packageFile(file), node.id) })];
  }
  $("lane").replaceChildren(...content);
}

/** The node and the group the current selection refers to, either of which may be absent. */
function selected() {
  if (typeof state.selection === "number") {
    return { node: state.nodeOfStep.get(state.selection), group: state.groupOfStep.get(state.selection) };
  }
  const node = state.graph.nodes.find((candidate) => candidate.id === state.selection);
  const isGroup = node && (node.op === Op.EXEC || node.demoted);
  return { node, group: isGroup ? state.groupOfStep.get(node.step) : undefined };
}

function select(selection) {
  state.selection = selection;
  const { node, group } = selected();
  for (const cell of $("timeline").children) {
    cell.classList.toggle("selected", group !== undefined && Number(cell.dataset.step) === group.step);
  }
  highlight($("graph"), node?.id ?? null);

  $("detail").hidden = false;
  $("detail-title").textContent = node ? nodeLabel(node) : group.name;
  if (group) {
    $("detail-summary").textContent = `Steps ${group.step} to ${group.end}. ${describeOutcome(group)}`;
  } else if (node.op === Op.OUTPUT) {
    $("detail-summary").textContent = `The derivation output ${node.path}, serialized as a NAR.`;
  } else {
    $("detail-summary").textContent =
      node.name === "sandbox"
        ? "What Nix put in the sandbox before the builder started."
        : "Written by the builder itself, between process groups. There is no command to replay.";
  }
  renderLane();
}

async function load(id) {
  state.id = id;
  state.selection = null;
  $("detail").hidden = true;
  renderPackages();

  const [graph, hash] = await Promise.all([packageFile("graph.json"), packageFile("nar-hash")]);
  if (state.id !== id) {
    return;
  }
  state.graph = JSON.parse(graph);
  state.groupOfStep = new Map(state.graph.groups.map((group) => [group.step, group]));
  state.nodeOfStep = new Map(
    state.graph.nodes.filter((node) => node.op === Op.EXEC || node.demoted).map((node) => [node.step, node]),
  );

  renderStats(hash);
  renderTimeline();
  draw($("graph"), state.graph.nodes, { labelOf: nodeLabel, classOf: nodeClass, onSelect: select });
  for (const [link, file] of [["mlir", "graph.mlir.gz"], ["bazel", "BUILD.bazel"], ["buck", "BUCK"], ["json", "graph.json"]]) {
    $(`download-${link}`).href = `data/${id}/${file}`;
  }
}

function packageFromLocation() {
  const wanted = new URLSearchParams(location.search).get(PACKAGE_PARAMETER);
  return state.packages.some((entry) => entry.id === wanted) ? wanted : state.packages[0].id;
}

async function main() {
  $("eliminate").addEventListener("click", () => {
    const eliminated = $("timeline").classList.toggle("eliminated");
    $("eliminate").setAttribute("aria-pressed", String(eliminated));
    $("eliminate").textContent = eliminated ? "Bring the dead groups back" : "Run dead-code elimination";
  });
  for (const tab of $("lanes").querySelectorAll("a")) {
    tab.addEventListener("click", (event) => {
      event.preventDefault();
      state.lane = tab.dataset.lane;
      renderLane();
    });
  }
  window.addEventListener("popstate", () => load(packageFromLocation()));

  // The footer names the store path serving the page. A checkout served
  // without a build still has the placeholder.
  if (STORE_PATH.startsWith("/nix/store/")) {
    $("store-path-footer").textContent = STORE_PATH;
    $("store").hidden = false;
  }

  state.packages = JSON.parse(await fetchText("data/index.json"));
  await load(packageFromLocation());
}

main().catch((error) => {
  $("stats").textContent = `Could not load the recording: ${error.message}`;
});
