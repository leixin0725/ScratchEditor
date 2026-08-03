import { spawnSync } from "node:child_process";
import { existsSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { createRequire } from "node:module";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const scriptDirectory = dirname(fileURLToPath(import.meta.url));
const projectRoot = resolve(scriptDirectory, "..");
const editor = resolve(process.argv[2] ?? join(projectRoot, "build", "release", "ScratchEditor.exe"));
const requestedCli = process.argv[3] ?? "all";
const codexExecutable = process.env.APPDATA
  ? join(
      process.env.APPDATA,
      "npm",
      "node_modules",
      "@openai",
      "codex",
      "node_modules",
      "@openai",
      "codex-win32-x64",
      "vendor",
      "x86_64-pc-windows-msvc",
      "bin",
      "codex.exe",
    )
  : undefined;

if (!existsSync(editor)) {
  throw new Error(`ScratchEditor executable does not exist: ${editor}`);
}

const require = createRequire(import.meta.url);
const nodePtyCandidates = [
  process.env.SCRATCHEDITOR_NODE_PTY,
  process.env.APPDATA
    ? join(process.env.APPDATA, "npm", "node_modules", "@google", "gemini-cli", "node_modules", "node-pty")
    : undefined,
].filter(Boolean);
const nodePtyPath = nodePtyCandidates.find((candidate) => existsSync(candidate));
if (!nodePtyPath) {
  throw new Error(
    "node-pty is required; set SCRATCHEDITOR_NODE_PTY to an installed node-pty package",
  );
}
const pty = require(nodePtyPath);

function runTuiTest({ name, command, args, extraEnvironment = {}, shellCommand = true }) {
  return new Promise((resolveTest, rejectTest) => {
    const startedAt = Date.now();
    const environment = {
      ...process.env,
      ...extraEnvironment,
      VISUAL: `${editor} --test-mode --wait`,
      EDITOR: `${editor} --test-mode --wait`,
      SCRATCHEDITOR_EXTERNAL_TEST_TEXT: "/quit",
    };
    const program = shellCommand ? "cmd.exe" : command;
    const programArguments = shellCommand ? ["/d", "/c", command, ...args] : args;
    const child = pty.spawn(program, programArguments, {
      cwd: projectRoot,
      env: environment,
      cols: 120,
      rows: 36,
      name: "xterm-256color",
    });
    let output = "";
    let timeoutError;
    const appendOutput = (chunk) => {
      output += chunk.toString("utf8");
      if (output.length > 20000) {
        output = output.slice(-20000);
      }
    };
    child.onData(appendOutput);

    const openEditorTimer = setTimeout(() => child.write("\x07"), 2200);
    const submitTimer = setTimeout(() => child.write("\r"), 4200);
    const timeoutTimer = setTimeout(() => {
      timeoutError = new Error(`${name} did not return after Ctrl+G editor completion\n${output}`);
      child.kill();
    }, 12000);

    child.onExit(({ exitCode, signal }) => {
      clearTimeout(openEditorTimer);
      clearTimeout(submitTimer);
      clearTimeout(timeoutTimer);
      const elapsedMs = Date.now() - startedAt;
      if (timeoutError) {
        rejectTest(timeoutError);
        return;
      }
      if (exitCode !== 0 || signal || elapsedMs < 4200) {
        rejectTest(new Error(
          `${name} exited unexpectedly (code=${exitCode}, signal=${signal}, elapsedMs=${elapsedMs})\n${output}`,
        ));
        return;
      }
      process.stdout.write(`${name} Ctrl+G integration passed (${elapsedMs} ms).\n`);
      resolveTest();
    });
  });
}

const piAgentDirectory = mkdtempSync(join(tmpdir(), "ScratchEditor-pi-agent-"));
const codexHomeDirectory = mkdtempSync(join(tmpdir(), "ScratchEditor-codex-home-"));
try {
  if (requestedCli === "all" || requestedCli === "pi") {
    await runTuiTest({
      name: "pi",
      command: "pi.cmd",
      args: [
        "--no-session",
        "--offline",
        "--approve",
        "--no-extensions",
        "--no-skills",
        "--no-context-files",
      ],
      extraEnvironment: {
        PI_CODING_AGENT_DIR: piAgentDirectory,
        PI_OFFLINE: "1",
      },
    });
  }
  if (requestedCli === "all" || requestedCli === "codex") {
    if (!codexExecutable || !existsSync(codexExecutable)) {
      throw new Error("The installed native Codex executable could not be located");
    }
    writeFileSync(
      join(codexHomeDirectory, "config.toml"),
      [
        'cli_auth_credentials_store = "file"',
        'approval_policy = "never"',
        'sandbox_mode = "read-only"',
        "",
        "[history]",
        'persistence = "none"',
        "",
        `[projects.'${projectRoot.toLowerCase()}']`,
        'trust_level = "trusted"',
        "",
        "[windows]",
        'sandbox = "unelevated"',
        "",
      ].join("\n"),
      "utf8",
    );
    const placeholderKey = "sk-scratch-editor-integration-test-no-request";
    const login = spawnSync(
      codexExecutable,
      ["login", "--with-api-key"],
      {
        env: { ...process.env, CODEX_HOME: codexHomeDirectory },
        input: `${placeholderKey}\n`,
        encoding: "utf8",
      },
    );
    if (login.status !== 0) {
      throw new Error(`Unable to prepare isolated Codex auth: ${login.stderr || login.stdout}`);
    }
    await runTuiTest({
      name: "Codex",
      command: codexExecutable,
      args: ["--no-alt-screen"],
      extraEnvironment: { CODEX_HOME: codexHomeDirectory },
      shellCommand: false,
    });
  }
} finally {
  const normalizedTemporaryRoot = resolve(tmpdir()).toLowerCase();
  for (const temporaryDirectory of [piAgentDirectory, codexHomeDirectory]) {
    const normalizedDirectory = resolve(temporaryDirectory).toLowerCase();
    if (normalizedDirectory.startsWith(normalizedTemporaryRoot + "\\")) {
      try {
        rmSync(temporaryDirectory, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 });
      } catch (error) {
        process.stderr.write(`Temporary CLI test cleanup deferred: ${error.message}\n`);
      }
    }
  }
}

process.exit(0);
