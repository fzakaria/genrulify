// Lays out the optimized build graph in layers and draws it as SVG.
//
// A node's layer is one more than the deepest node it consumes, so every
// edge points right. Nodes in one layer do not depend on each other, which
// lets a crowded layer wrap into several columns without crossing its own
// edges.

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

/**
 * Draws the graph into `svg`. `classOf` gives a node its outcome class and
 * `onSelect` is called with a node's id when the node is activated.
 */
export function draw(svg, nodes, { labelOf, classOf, onSelect }) {
  const { placed, width, height } = layout(nodes, labelOf);
  svg.replaceChildren();
  svg.setAttribute("width", width);
  svg.setAttribute("height", height);
  svg.setAttribute("viewBox", `0 0 ${width} ${height}`);

  // Edges go under the nodes.
  const edges = element("g");
  svg.append(edges);
  for (const { node, x, y } of placed.values()) {
    for (const input of node.inputs) {
      const from = placed.get(input.node);
      const startX = from.x + from.width;
      const startY = from.y + NODE_HEIGHT / 2;
      const endY = y + NODE_HEIGHT / 2;
      const bend = (x - startX) * EDGE_CURVE;
      const path = element("path", {
        class: "edge",
        d: `M${startX},${startY} C${startX + bend},${startY} ${x - bend},${endY} ${x},${endY}`,
      });
      path.dataset.from = input.node;
      path.dataset.to = node.id;
      edges.append(path);
    }
  }

  for (const { node, x, y, width: nodeWidth, label } of placed.values()) {
    const group = element("g", {
      class: `node ${classOf(node)}`,
      transform: `translate(${x},${y})`,
      tabindex: "0",
      role: "button",
    });
    group.dataset.id = node.id;
    group.append(element("rect", { width: nodeWidth, height: NODE_HEIGHT }));
    const text = element("text", { x: LABEL_PADDING / 2, y: NODE_HEIGHT / 2 + 4 });
    text.textContent = label;
    group.append(text);
    group.addEventListener("click", () => onSelect(node.id));
    group.addEventListener("keydown", (event) => {
      if (event.key === "Enter" || event.key === " ") {
        event.preventDefault();
        onSelect(node.id);
      }
    });
    svg.append(group);
  }
}

/** Marks the selected node and the edges that touch it. */
export function highlight(svg, id) {
  for (const node of svg.querySelectorAll(".node")) {
    node.classList.toggle("selected", node.dataset.id === id);
  }
  for (const edge of svg.querySelectorAll(".edge")) {
    edge.classList.toggle("touching", edge.dataset.from === id || edge.dataset.to === id);
  }
  svg.querySelector(".node.selected")?.scrollIntoView({ block: "nearest", inline: "nearest" });
}
