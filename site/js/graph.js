// Lays out a build graph in layers and draws it as SVG.
//
// A node's layer is one more than the deepest node it consumes, so every
// edge points right. Nodes in one layer do not depend on each other, which
// lets a crowded layer wrap into several columns without crossing its own
// edges.
//
// Drawing is keyed by node id. A second call with fewer nodes moves the
// nodes that stay to their new places and fades out the ones that are
// gone, which is how the page shows dead-code elimination happen.

const SVG_NAMESPACE = "http://www.w3.org/2000/svg";

// Geometry, in CSS pixels.
const NODE_HEIGHT = 26;
const ROW_GAP = 10;
const COLUMN_GAP = 46;
const MARGIN = 14;
const LABEL_PADDING = 18;

// The advance of one character of the 12px monospace label font.
const CHARACTER_WIDTH = 7.3;

// A layer with more nodes than this wraps into another column.
const ROWS_PER_COLUMN = 18;

// How far an edge's control points sit from its ends, as a share of the
// horizontal distance, which gives the curve its S shape.
const EDGE_CURVE = 0.5;

// How long a removed node takes to fade, which the stylesheet's
// transition on `.node.leaving` matches.
const LEAVE_MILLISECONDS = 450;

/** Places every node. Returns the nodes with x, y, width and the canvas size. */
export function layout(nodes, labelOf) {
  // A node's layer is fixed by the nodes it consumes, which come earlier.
  const layerOf = new Map();
  for (const node of nodes) {
    let layer = 0;
    for (const input of node.inputs) {
      layer = Math.max(layer, layerOf.get(input.node) + 1);
    }
    layerOf.set(node.id, layer);
  }

  // Group nodes by layer, then wrap each layer into columns.
  const layers = [];
  for (const node of nodes) {
    const layer = layerOf.get(node.id);
    (layers[layer] ??= []).push(node);
  }
  const columns = [];
  for (const layer of layers) {
    for (let start = 0; start < layer.length; start += ROWS_PER_COLUMN) {
      columns.push(layer.slice(start, start + ROWS_PER_COLUMN));
    }
  }

  // Each column is as wide as its longest label.
  const placed = new Map();
  let x = MARGIN;
  let height = 0;
  for (const column of columns) {
    const width = Math.max(
      ...column.map((node) => labelOf(node).length * CHARACTER_WIDTH + LABEL_PADDING),
    );
    column.forEach((node, row) => {
      const y = MARGIN + row * (NODE_HEIGHT + ROW_GAP);
      placed.set(node.id, { node, x, y, width, label: labelOf(node) });
      height = Math.max(height, y + NODE_HEIGHT);
    });
    x += width + COLUMN_GAP;
  }
  return { placed, width: x - COLUMN_GAP + MARGIN, height: height + MARGIN };
}

function element(name, attributes = {}) {
  const created = document.createElementNS(SVG_NAMESPACE, name);
  for (const [key, value] of Object.entries(attributes)) {
    created.setAttribute(key, value);
  }
  return created;
}

/** The layer of `svg` with the given class, created on first use. */
function layerGroup(svg, name) {
  return svg.querySelector(`:scope > g.${name}`) ?? svg.appendChild(element("g", { class: name }));
}

/** Creates the element of one node. Its place and class are set by `draw`. */
function createNode(node, onSelect) {
  const group = element("g", { tabindex: "0", role: "button" });
  group.dataset.id = node.id;
  group.append(element("rect", { height: NODE_HEIGHT }), element("text", { x: LABEL_PADDING / 2, y: NODE_HEIGHT / 2 + 4 }));
  group.addEventListener("click", () => onSelect(node.id));
  group.addEventListener("keydown", (event) => {
    if (event.key === "Enter" || event.key === " ") {
      event.preventDefault();
      onSelect(node.id);
    }
  });
  return group;
}

/**
 * Draws the graph into `svg`. `classOf` gives a node its outcome class and
 * `onSelect` is called with a node's id when the node is activated. Nodes
 * already drawn by an earlier call keep their element and move.
 */
export function draw(svg, nodes, { labelOf, classOf, onSelect }) {
  const { placed, width, height } = layout(nodes, labelOf);
  svg.setAttribute("width", width);
  svg.setAttribute("height", height);
  svg.setAttribute("viewBox", `0 0 ${width} ${height}`);

  // Edges go under the nodes. They are redrawn and fade in, because a
  // curve between two moving nodes has no good intermediate shape.
  const previousEdges = layerGroup(svg, "edges");
  const nodeLayer = layerGroup(svg, "nodes");
  const redrawn = nodeLayer.childElementCount > 0;
  const edges = element("g", { class: redrawn ? "edges redrawn" : "edges" });
  for (const { node, x, y } of placed.values()) {
    for (const input of node.inputs) {
      const from = placed.get(input.node);
      const startX = from.x + from.width;
      const startY = from.y + NODE_HEIGHT / 2;
      const endY = y + NODE_HEIGHT / 2;
      const bend = (x - startX) * EDGE_CURVE;
      const path = element("path", {
        class: `edge ${classOf(node)}`,
        d: `M${startX},${startY} C${startX + bend},${startY} ${x - bend},${endY} ${x},${endY}`,
      });
      path.dataset.from = input.node;
      path.dataset.to = node.id;
      edges.append(path);
    }
  }

  previousEdges.replaceWith(edges);

  // Find the elements an earlier call drew. One that is already leaving
  // is not reused: a node that comes back gets a fresh element.
  const drawn = new Map();
  for (const group of nodeLayer.querySelectorAll(".node:not(.leaving)")) {
    drawn.set(group.dataset.id, group);
  }

  for (const { node, x, y, width: nodeWidth, label } of placed.values()) {
    let group = drawn.get(node.id);
    drawn.delete(node.id);
    const selected = group?.classList.contains("selected") ?? false;

    // A node that appears in a graph already on the page fades in.
    const entering = redrawn && !group;
    if (!group) {
      group = createNode(node, onSelect);
      nodeLayer.append(group);
    }
    group.setAttribute("class", `node ${classOf(node)}${selected ? " selected" : ""}${entering ? " entering" : ""}`);
    group.style.transform = `translate(${x}px, ${y}px)`;
    group.querySelector("rect").setAttribute("width", nodeWidth);
    group.querySelector("text").textContent = label;
  }

  // Whatever is left was not asked for again.
  for (const group of drawn.values()) {
    group.classList.add("leaving");
    group.classList.remove("selected");
    setTimeout(() => group.remove(), LEAVE_MILLISECONDS);
  }
}

/** Marks the selected node and the edges that touch it. */
export function highlight(svg, id) {
  svg.querySelector(".node.selected")?.classList.remove("selected");
  for (const edge of svg.querySelectorAll(".edge.touching")) {
    edge.classList.remove("touching");
  }
  if (id === null) {
    return;
  }
  const value = CSS.escape(id);
  const node = svg.querySelector(`.node:not(.leaving)[data-id="${value}"]`);
  node?.classList.add("selected");
  for (const edge of svg.querySelectorAll(`[data-from="${value}"], [data-to="${value}"]`)) {
    edge.classList.add("touching");
  }
  node?.scrollIntoView({ block: "nearest", inline: "nearest" });
}
