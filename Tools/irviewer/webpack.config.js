// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
const path = require("path");

module.exports = {
  mode: "none",
  resolve: {
    modules: [path.resolve(__dirname, "src"), "node_modules"],
  },
};
