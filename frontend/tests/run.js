// run.js - every frontend unit test module, then the run.

import { run } from "./harness.js";
import "./when.test.js";
import "./keys.test.js";
import "./layouttree.test.js";
import "./ansi.test.js";
import "./client.test.js";
import "./textedits.test.js";
import "./keybindings.test.js";
import "./docsync.test.js";
import "./lspconvert.test.js";
import "./tree.test.js";
import "./navigation.test.js";
import "./headersource.test.js";
import "./gotoall.test.js";
import "./linediff.test.js";
import "./fileuris.test.js";
import "./markdown.test.js";
import "./launch.test.js";
import "./tasks.test.js";
import "./diagnostics.test.js";
import "./debug.test.js";
import "./outputlinks.test.js";
import "./chat.test.js";

run(document.getElementById("results"));
