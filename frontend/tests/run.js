// run.js - every frontend unit test module, then the run.

import { run } from "./harness.js";
import "./when.test.js";
import "./keys.test.js";
import "./layouttree.test.js";
import "./ansi.test.js";
import "./client.test.js";
import "./textedits.test.js";
import "./keybindings.test.js";

run(document.getElementById("results"));
