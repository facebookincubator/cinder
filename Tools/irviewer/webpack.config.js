// Copyright (c) Meta Platforms, Inc. and affiliates.
const path = require("path");

module.exports = {
  mode: "none",
  resolve: {
    modules: [path.resolve(__dirname, "src"), "node_modules"],
  },
};
