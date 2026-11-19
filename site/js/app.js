// The explorer: one recorded build at a time, as a timeline of process
// groups, the graph under whichever optimizer passes are switched on, the
// same graph as an MLIR module, and the details of whichever group or node
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
  COPY: "copy",
  FOLDED: "folded",
  DEAD: "dead",
});

// The outcomes of a group that runs as a build rule.
const REPLAYED = new Set([Outcome.NARROW, Outcome.NEAR, Outcome.TRACED, Outcome.WIDE]);

// The views of the selected group, by the data-lane value of their tab.
const Lane = Object.freeze({
  PROCESS: "process",
  FILES: "files",
  MLIR: "mlir",
  BAZEL: "bazel",
  BUCK: "buck",
});

// The kinds of node in graph.json.
const Op = Object.freeze({ EXEC: "exec", LITERAL: "literal", COPY: "copy", OUTPUT: "output" });

// The class of a node that elimination would remove, which is drawn in grey.
const DOOMED = "doomed";

// The pass that removes operations, by the name the compiler gives it.
const ELIMINATE = "dce";

// What the page says when no pass has been touched yet.
const NO_PASS_TEXT =
  "The graph shows the recorded commands and the files the builder supplied.";

// What the page says when every pass is on and none was switched by hand.
const EVERY_PASS_TEXT =
  "The build files below use all optimizations. Each solid box is a rule.";

// What the page says about each pass, by the name the compiler gives it.
const Passes = Object.freeze({
  link: {
    label: "Link driver writes",
    text: "If the build driver writes a file whose bytes match an earlier file, use a copy of that file.",
  },
  dce: {
    label: "Eliminate dead code",
    text: "Remove operations whose results never reach the package output.",
  },
  fold: {
    label: "Fold constants",
    text: "Use the recorded entries for directories, symlinks and empty files. This can replace an entire command, such as mkdir.",
  },
  merge: {
    label: "Merge same outputs",
    text: "Reuse the input bytes when a file changes in place but its contents stay the same, as with chmod.",
  },
  raise: {
    label: "Raise copies",
    text: "If a command copies an input to another path, use the original bytes at the new path. This can replace cp or install.",
  },
  rules: {
    label: "Raise to rules",
    text: "Name rules after the work they do. For example, gcc -c becomes cc_object. The rule still runs the recorded command.",
  },
});

// Programs that only run other commands. A node for one is labelled by
// what it wrote, since a row of nodes all named "bash" says nothing.
const SHELLS = new Set(["bash", "sh"]);

// The query parameter that selects a package.
const PACKAGE_PARAMETER = "pkg";

// Hex digits of the NAR hash shown in the stats line.
const SHOWN_HASH_DIGITS = 16;

// How many operands a line of the module listing shows before it counts
// the rest. An operation given a source tree has thousands.
const SHOWN_OPERANDS = 8;

// How many entries the listing prints for the selected operation.
const SHOWN_ENTRIES = 6;

// An SSA value as MLIR prints it: `%cc_5`, one result of several as
// `%tar_2#7`, and the definition of several results as `%tar_2:40`.
const VALUE_PATTERN = /(%[A-Za-z0-9_.$-]+(?:[#:]\d+)?)/;

// What the page is showing. `selection` is a group's step or a node's id.
const state = {
  packages: [],
  id: null,
  graph: null,
  files: new Map(),
  groupOfStep: new Map(),
  selection: null,
  lane: Lane.PROCESS,

  // The passes switched on, as bits: bit N is the Nth pass of the
  // pipeline. The number is also the position of the choice's graph in
  // the description.
  mask: 0,

  // The pass whose switch was used last, which the page describes.
  touched: null,

  // What the current choice needs looked up often, rebuilt by `enterChoice`.
  nodeOfStep: new Map(),
  nodeOfValue: new Map(),
  doomed: new Set(),
};

async function fetchText(path) {
  const response = await fetch(path);
  if (!response.ok) {
    throw new Error(`${path}: ${response.status}`);
  }
  return response.text();
}

/** Fetches a gzip file and parses what it holds as JSON. */
async function fetchCompressedJson(path) {
  const response = await fetch(path);
  if (!response.ok) {
    throw new Error(`${path}: ${response.status}`);
  }
  return new Response(response.body.pipeThrough(new DecompressionStream("gzip"))).json();
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

/** The nodes of the graph under a choice of passes. */
function nodesUnder(mask) {
  return state.graph.variants[mask].map((position) => state.graph.records[position]);
}

/** The nodes the page is showing. */
function shownNodes() {
  return state.shown;
}

/** The bit of a pass in a choice. */
function bitOf(pass) {
  return 1 << state.graph.passes.indexOf(pass);
}

/** How much one kind of node matters when several stand for one group. */
function rank(node) {
  return [Op.EXEC, Op.COPY, Op.LITERAL].indexOf(node.op);
}

/** Rebuilds the lookups for the current choice of passes. */
function enterChoice() {
  state.shown = nodesUnder(state.mask);
  const nodes = shownNodes();

  // A group can have a process, copies and constants in the graph at once.
  // A cell of the timeline stands for the first of those the group has.
  state.nodeOfStep = new Map();
  for (const node of nodes) {
    const stands = node.op === Op.EXEC || node.op === Op.COPY || node.demoted || node.folded;
    const known = state.nodeOfStep.get(node.step);
    if (stands && (!known || rank(node) < rank(known))) {
      state.nodeOfStep.set(node.step, node);
    }
  }

  // The values each node defines, by the name MLIR printed.
  state.nodeOfValue = new Map();
  for (const node of nodes) {
    for (const name of node.defines) {
      state.nodeOfValue.set(name, node);
    }
  }

  // The nodes elimination would remove: the ones the same choice with
  // elimination switched on does not have.
  const kept = new Set(nodesUnder(state.mask | bitOf(ELIMINATE)).map((node) => node.id));
  state.doomed = new Set(nodes.filter((node) => !kept.has(node.id)).map((node) => node.id));
}

/** The class that colors a graph node or a line of the module. */
function nodeClass(node) {
  if (state.doomed.has(node.id)) {
    return DOOMED;
  }
  if (node.op === Op.OUTPUT) {
    return "output";
  }
  if (node.op === Op.COPY) {
    return Outcome.COPY;
  }
  if (node.op === Op.LITERAL) {
    return node.folded ? Outcome.FOLDED : Outcome.LITERAL;
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
  if (node.op === Op.COPY) {
    return `copy · ${node.name}`;
  }
  if (node.rule) {
    return `${node.rule} · ${node.name}`;
  }
  if (node.folded) {
    return `const · ${node.name}`;
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
  const parts = [
    `${groups.length.toLocaleString()} process groups`,
    `${groups.filter((group) => REPLAYED.has(outcomeOf(group))).length} replayed`,
    `${count(Outcome.COPY)} copied`,
    `${count(Outcome.FOLDED)} constant`,
    `${pinned} ${pinned === 1 ? "file" : "files"} pinned`,
    `${count(Outcome.LITERAL)} kept as recorded bytes`,
    `${count(Outcome.DEAD)} unused`,
    `${ruleCount(nodesUnder(state.graph.variants.length - 1))} rules after optimization`,
  ];
  const digest = hash.trim().slice(0, "sha256:".length + SHOWN_HASH_DIGITS);
  $("stats").replaceChildren(
    `${state.graph.name}: ${parts.join(" · ")} · NAR `,
    create("code", { textContent: `${digest}…`, title: hash.trim() }),
  );
}

/** How many build rules a graph lowers to: its process groups and its outputs. */
function ruleCount(nodes) {
  return nodes.filter((node) => node.op === Op.EXEC || node.op === Op.OUTPUT).length;
}

/** The switches of the passes, and what the pass switched last does. */
function renderSwitches() {
  const passes = state.graph.passes;
  const everything = state.graph.variants.length - 1;
  const switches = passes.map((pass) => {
    const on = (state.mask & bitOf(pass)) !== 0;
    const button = create("button", { type: "button", className: "pass", textContent: Passes[pass].label });
    button.setAttribute("aria-pressed", String(on));
    button.addEventListener("click", () => {
      state.touched = pass;
      choose(state.mask ^ bitOf(pass));
    });
    return button;
  });
  const all = create("button", { type: "button", className: "every", textContent: "Select all" });
  all.disabled = state.mask === everything;
  all.addEventListener("click", () => {
    state.touched = null;
    choose(everything);
  });
  const none = create("button", { type: "button", className: "every", textContent: "Select none" });
  none.disabled = state.mask === 0;
  none.addEventListener("click", () => {
    state.touched = null;
    choose(0);
  });
  $("passes").replaceChildren(...switches, all, none);

  const rules = ruleCount(shownNodes());
  $("pass-text").replaceChildren(
    create("strong", { textContent: `${rules.toLocaleString()} build ${rules === 1 ? "rule" : "rules"}. ` }),
    state.touched ? Passes[state.touched].text : state.mask === 0 ? NO_PASS_TEXT : EVERY_PASS_TEXT,
  );
}

/** Marks the cells of groups that no longer run under the current choice. */
function markTimeline() {
  const running = new Set(shownNodes().filter((node) => node.op === Op.EXEC).map((node) => node.step));
  for (const cell of $("timeline").children) {
    const group = state.groupOfStep.get(Number(cell.dataset.step));
    const gone = !running.has(group.step) && outcomeOf(group) !== Outcome.LITERAL;
    cell.classList.toggle("gone", gone);
  }
}

/** Shows the graph under a choice of passes. */
function choose(mask) {
  state.mask = mask;
  enterChoice();

  // A selected operation that the graph no longer has has nothing to show.
  if (state.selection !== null && !selected().node) {
    state.selection = null;
    $("detail").hidden = true;
  }
  renderSwitches();
  markTimeline();
  renderGraph();
  renderModule();
  if (state.selection !== null) {
    select(state.selection);
  }
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
    ? ` Pinned to the recording: ${pinned.map((path) => path.split("/").pop()).join(", ")}.`
    : "";
  switch (outcomeOf(group)) {
    case Outcome.NARROW:
      return `Replayed with the files it opened.${pins}`;
    case Outcome.NEAR:
      return `Replayed with the files it opened and the other entries in those directories.${pins}`;
    case Outcome.TRACED:
      return `Replayed with the ${group.inputs} entries seen during tracing, including directory lookups.${pins}`;
    case Outcome.WIDE:
      return `Replayed with the whole recorded tree after a smaller set of inputs failed the check.${pins}`;
    case Outcome.LITERAL:
      return `Kept as recorded bytes. ${group.reason.split("\n").slice(0, 2).join(" ")}`;
    case Outcome.COPY:
      return "Every file used later matches an input file. The build stages those bytes where the next command expects them.";
    case Outcome.FOLDED:
      return "Later steps use only directories, symlinks or empty files from this command. The build supplies those entries directly.";
    default:
      return "Unused. The final output depends on none of the files this group wrote.";
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
  // An operation that elimination would remove is described by counts alone.
  if (node.counted) {
    const given = node.inputs.reduce((total, input) => total + input.count, 0);
    return [
      create("p", {
        textContent:
          `Given ${given.toLocaleString()} ${given === 1 ? "entry" : "entries"}, wrote ` +
          `${node.writeCount.toLocaleString()}. Nothing reads what it wrote, so elimination removes it.`,
      }),
    ];
  }

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

/** The rule for a node in a build file's text: `cc_object(` and so on, up to its closing parenthesis. */
function ruleText(buildFile, node) {
  const name = node.id.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const start = buildFile.search(new RegExp(`^[a-z_]+\\(\\n    name = "${name}",`, "m"));
  if (start < 0) {
    return node.op === Op.EXEC
      ? "The generated build files use all optimizations, which remove this group."
      : "The workspace supplies these entries directly or copies them from another file.";
  }
  return buildFile.slice(start, buildFile.indexOf("\n)\n", start) + 2);
}

async function renderLane() {
  for (const tab of $("lanes").querySelectorAll("a")) {
    tab.classList.toggle("active", tab.dataset.lane === state.lane);
  }
  const { node, group } = selected();
  const absent = [create("p", { className: "muted", textContent: "The selected optimizations remove this group from the graph." })];
  let content;
  if (state.lane === Lane.PROCESS) {
    content = renderProcesses(group);
  } else if (!node) {
    content = absent;
  } else if (state.lane === Lane.FILES) {
    content = renderFiles(node);
  } else if (state.lane === Lane.MLIR) {
    content = [create("pre", {}, valueTokens(node.mlir))];
  } else {
    const file = state.lane === Lane.BAZEL ? "BUILD.bazel" : "BUCK";
    content = [create("pre", { textContent: ruleText(await packageFile(file), node) })];
  }
  $("lane").replaceChildren(...content);
}

/** Draws the graph under the current choice and says what the grey nodes are. */
function renderGraph() {
  const doomed = state.doomed.size;
  let caption = "Each box is a build step. A line connects the step that produced a file version to a step that uses it. ";
  if (doomed > 0) {
    caption +=
      `Nothing reads what the ${doomed.toLocaleString()} grey ${doomed === 1 ? "node" : "nodes"} wrote. ` +
      "Elimination removes them.";
  } else {
    caption += "A solid box is one build rule. Select a node, or a cell above.";
  }
  $("graph-caption").textContent = caption;
  draw($("graph"), shownNodes(), { labelOf: nodeLabel, classOf: nodeClass, onSelect: select });
}

/** The name of a printed value without its sigil and its result number: `cc_5` for `%cc_5#2`. */
function valueName(printed) {
  return printed.slice(1).replace(/[#:]\d+$/, "");
}

/** Printed MLIR as text and one element per SSA value, so a value can be pointed at. */
function valueTokens(text) {
  return text.split(VALUE_PATTERN).map((part, index) => {
    // Odd parts are what the pattern captured.
    if (index % 2 === 0) {
      return part;
    }
    const token = create("span", { className: "value", textContent: part });
    token.dataset.value = valueName(part);
    return token;
  });
}

/**
 * The first line of an operation as printed, up to where its entries
 * start, with a long operand list cut short.
 */
function headLine(node) {
  const first = node.mlir.split("\n", 1)[0];
  const arrow = node.op === Op.OUTPUT ? "" : " -> ";
  const operandCount = node.inputs.reduce((total, input) => total + input.count, 0);

  // An operand list opens at the first parenthesis followed by a value.
  const open = first.indexOf("(%");
  if (operandCount === 0 || open < 0) {
    return first.replace(/ -> \[\]?$/, "") + arrow;
  }
  const operands = first
    .slice(open + 1)
    .replace(/\) -> \[\]?$|\)$/, "")
    .split(", ");
  const more = operandCount - SHOWN_OPERANDS;
  const shown = operands.slice(0, SHOWN_OPERANDS).join(", ") + (more > 0 ? `, … ${more.toLocaleString()} more` : "");
  return `${first.slice(0, open + 1)}${shown})${arrow}`;
}

/** One operation on one line, with its entries replaced by their count. */
function summaryLine(node) {
  if (node.op === Op.OUTPUT) {
    return headLine(node);
  }

  // The copies raised from one group share a line: the first, and a count.
  if (node.op === Op.COPY) {
    const more = node.writeCount - 1;
    return node.mlir.split("\n", 1)[0] + (more > 0 ? `  … and ${more.toLocaleString()} more` : "");
  }
  return `${headLine(node)}[${node.writeCount.toLocaleString()} ${node.writeCount === 1 ? "entry" : "entries"}]`;
}

/** An operation with its first entries printed. The MLIR tab of the detail panel has the rest. */
function excerpt(node) {
  if (node.op === Op.OUTPUT) {
    return headLine(node);
  }
  if (node.op === Op.COPY) {
    return node.mlir;
  }
  const entries = node.mlir.split("\n").filter((line) => line.trimStart().startsWith("#splice.entry"));
  const more = node.writeCount - SHOWN_ENTRIES;
  const lines = [`${headLine(node)}[`, ...entries.slice(0, SHOWN_ENTRIES)];
  if (more > 0) {
    lines.push(`    … ${more.toLocaleString()} more`);
  }
  return [...lines, "]"].join("\n");
}

/** The module listing: one line per operation, with the selected one printed in full. */
function renderModule() {
  const selectedId = selected().node?.id;
  const rows = shownNodes().map((node) => {
    const open = node.id === selectedId;
    const row = create("li", { className: `op ${nodeClass(node)}` }, [
      create("code", {}, valueTokens(open ? excerpt(node) : summaryLine(node))),
    ]);
    row.classList.toggle("selected", open);
    row.dataset.id = node.id;
    return row;
  });
  const listing = $("module");
  const scrolled = listing.scrollTop;
  listing.replaceChildren(...rows);
  listing.scrollTop = scrolled;

  // Bring the selected operation into view without moving the page.
  const row = listing.querySelector(".op.selected");
  if (row && (row.offsetTop < listing.scrollTop || row.offsetTop > listing.scrollTop + listing.clientHeight - row.clientHeight)) {
    listing.scrollTop = row.offsetTop - listing.clientHeight / 3;
  }
  $("module-caption").textContent = `${rows.length.toLocaleString()} operations shown.`;
}

/** Makes the module listing respond to the pointer. Called once. */
function wireModule() {
  const listing = $("module");
  const mark = (event, on) => {
    const value = event.target.closest?.(".value")?.dataset.value;
    if (value === undefined) {
      return;
    }
    for (const token of listing.querySelectorAll(`.value[data-value="${CSS.escape(value)}"]`)) {
      token.classList.toggle("same", on);
    }
  };
  listing.addEventListener("mouseover", (event) => mark(event, true));
  listing.addEventListener("mouseout", (event) => mark(event, false));

  // A value leads to the operation that defines it, and anywhere else on
  // a line selects that line's operation.
  listing.addEventListener("click", (event) => {
    const token = event.target.closest(".value");
    const row = event.target.closest(".op");
    const defining = token && state.nodeOfValue.get(token.dataset.value);
    if (defining) {
      select(defining.id);
    } else if (row) {
      select(row.dataset.id);
    }
  });
}

/** The node and the group the current selection refers to, either of which may be absent. */
function selected() {
  if (typeof state.selection === "number") {
    return { node: state.nodeOfStep.get(state.selection), group: state.groupOfStep.get(state.selection) };
  }
  const node = shownNodes().find((candidate) => candidate.id === state.selection);
  const isGroup = node && (node.op === Op.EXEC || node.op === Op.COPY || node.demoted || node.folded);
  return { node, group: isGroup ? state.groupOfStep.get(node.step) : undefined };
}

function select(selection) {
  state.selection = selection;
  const { node, group } = selected();
  for (const cell of $("timeline").children) {
    cell.classList.toggle("selected", group !== undefined && Number(cell.dataset.step) === group.step);
  }
  highlight($("graph"), node?.id ?? null);
  renderModule();

  $("detail").hidden = false;
  $("detail-title").textContent = node ? nodeLabel(node) : group.name;
  if (group) {
    $("detail-summary").textContent = `Steps ${group.step} to ${group.end}. ${describeOutcome(group)}`;
  } else if (!node) {
    $("detail-summary").textContent = "";
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

  const [graph, hash] = await Promise.all([fetchCompressedJson(`data/${id}/graph.json.gz`), packageFile("nar-hash")]);
  if (state.id !== id) {
    return;
  }
  state.graph = graph;
  state.groupOfStep = new Map(state.graph.groups.map((group) => [group.step, group]));

  // A graph drawn for another package shares no nodes with this one.
  $("graph").replaceChildren();
  renderStats(hash);
  renderTimeline();
  choose(state.mask);
  const sourceRoot = $("package-files").dataset.sourceRoot;
  for (const [link, file] of [["mlir", "graph.mlir.gz"], ["bazel", "BUILD.bazel"], ["buck", "BUCK"], ["json", "graph.json.gz"]]) {
    $(`download-${link}`).href = `${sourceRoot}/${id}/${file}`;
  }
}

function packageFromLocation() {
  const wanted = new URLSearchParams(location.search).get(PACKAGE_PARAMETER);
  return state.packages.some((entry) => entry.id === wanted) ? wanted : state.packages[0].id;
}

async function main() {
  wireModule();
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
