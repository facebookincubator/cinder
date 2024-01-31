// Copyright (c) Meta Platforms, Inc. and affiliates.
import * as d3 from "d3";
import $ from "jquery";
import dagreD3 from "dagre-d3";
import { parse, ParseError } from "hir/parser.js";

function highlightDefUses(register) {
  // Clear previously highlighted register if different
  d3.selectAll(".highlight-def")
    .filter(function () {
      return !d3.select(this).classed("def-" + register.name);
    })
    .classed("highlight-def", false);
  d3.selectAll(".highlight-use")
    .filter(function () {
      return !d3.select(this).classed("use-" + register.name);
    })
    .classed("highlight-use", false);

  // Toggle highlighting for register
  d3.selectAll(".def-" + register.name).classed("highlight-def", function () {
    return !d3.select(this).classed("highlight-def");
  });
  d3.selectAll(".use-" + register.name).classed("highlight-use", function () {
    return !d3.select(this).classed("highlight-use");
  });
}

function appendInstrDiv(blockSel, instr) {
  let instrSel = blockSel.append("div").attr("class", "instr");
  // Output
  if (instr.output !== undefined) {
    let output = instr.output;
    instrSel
      .append("span")
      .attr("class", "register def def-" + output.name)
      .text(output.name + ":" + output.type)
      .on("click", function () {
        highlightDefUses(output);
      });
    instrSel.append("span").text("=");
  }
  // Opcode + immediates
  let opcode = instr.opcode;
  if (instr.immediates !== undefined && instr.immediates.length > 0) {
    opcode += "<" + instr.immediates.join(" ") + ">";
  }
  instrSel.append("span").attr("class", "opcode-and-immediates").text(opcode);
  // Operands
  if (instr.operands !== undefined) {
    instr.operands.forEach(function (operand) {
      instrSel
        .append("span")
        .attr("class", "register use use-" + operand.name)
        .text(operand.name)
        .on("click", highlightDefUses.bind(null, operand));
    });
  }
}

function makeBlockDiv(block) {
  let blockSel = d3.select("body").append("div").remove();
  blockSel
    .append("div")
    .append("span")
    .attr("class", "block-label")
    .text(block.id + ":");
  block.instrs.forEach(function (instr) {
    appendInstrDiv(blockSel, instr);
  });
  return blockSel.node();
}

const minGraphWidth = 600;
const minControlsWidth = 300;
const margin = 10;

function recreateSVG() {
  let container = d3.select("#graph-container");

  // Remove previous version
  container.selectAll("*").remove();

  // Add new container
  let width = Math.max(
    $(document).width() - minControlsWidth - 3 * margin,
    minGraphWidth
  );
  container
    .append("svg")
    .attr("width", width)
    .attr("height", $(document).height() - 2 * margin)
    .append("g");
}

function drawGraph(graph) {
  let g = new dagreD3.graphlib.Graph().setGraph({});
  graph.blocks.forEach(function (block) {
    let cfg = {
      shape: "rect",
      labelType: "html",
      label: makeBlockDiv.bind(null, block),
    };
    if (block.isEntryBlock) {
      cfg.style = "stroke: #2ca02c";
    }
    if (block.isExitBlock) {
      cfg.style = "stroke: #d62728";
    }
    g.setNode(block.id, cfg);
  });
  graph.edges.forEach(function (edge) {
    g.setEdge(edge.srcBlockId, edge.dstBlockId, {
      curve: d3.curveBasis,
    });
  });

  recreateSVG();

  let svg = d3.select("svg"),
    inner = svg.select("g");

  // Set up zoom support
  let zoom = d3.zoom().on("zoom", function (event) {
    inner.attr("transform", event.transform);
  });
  svg.call(zoom);

  // Create and run the renderer
  let render = new dagreD3.render();
  render(inner, g);

  // Center the graph
  let initialScale = 0.75;
  svg.call(
    zoom.transform,
    d3.zoomIdentity
      .translate((svg.attr("width") - g.graph().width * initialScale) / 2, 20)
      .scale(initialScale)
  );
}

function reportParseError(hir, err) {
  let lines = hir.split("\n");
  let line = lines[err.pos.lineNum - 1];
  let msg = [
    "Parse error at " + err.pos.toString() + ":",
    line,
    " ".repeat(err.pos.colNum - 1) + "^--- " + err.msg,
  ].join("\n");
  d3.select("#controls")
    .append("div")
    .attr("id", "input-error")
    .append("div")
    .append("pre")
    .text(msg);
}

recreateSVG();

d3.select("#controls").append("div").append("h3").text("HIR Viewer");

d3.select("#controls")
  .append("div")
  .append("textarea")
  .attr("placeholder", "Paste an HIR function here")
  .attr("id", "hir-program");

d3.select("#controls")
  .append("div")
  .append("button")
  .text("Go!")
  .on("click", function () {
    let hir = d3.select("#hir-program").node().value;
    d3.select("#input-error").remove();
    let graph = undefined;
    try {
      graph = parse(hir);
    } catch (err) {
      if (err instanceof ParseError) {
        reportParseError(hir, err);
      }
      throw err;
    }
    drawGraph(graph);
  });
