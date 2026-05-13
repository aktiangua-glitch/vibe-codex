#!/usr/bin/env node
/**
 * vibe-codex 项目启动器
 * 运行:
 *   ./dev
 *   node ./start.js
 */

const fs = require('fs');
const path = require('path');
const { spawn, spawnSync } = require('child_process');

const ROOT_DIR = __dirname;

const PROJECTS = {
  bean: {
    id: 'bean',
    name: 'bean-pop-studio',
    label: 'Bean Pop Studio',
    desc: '照片转拼豆图纸 · Vue 3 + Vite',
    commandDesc: 'npm run dev',
    cwd: path.join(ROOT_DIR, 'bean-pop-studio'),
    packageManager: 'npm',
    defaultPort: 4173,
    altPort: 4174,
    launch(args = []) {
      return { command: npmCommand(), args: ['run', 'dev', ...args] };
    },
  },
  canvas: {
    id: 'canvas',
    name: 'canvas-editor-studio',
    label: 'Canvas Editor Studio',
    desc: '海报/KV 画布编辑器 Demo · React + Konva',
    commandDesc: 'pnpm dev',
    cwd: path.join(ROOT_DIR, 'canvas-editor-studio'),
    packageManager: 'pnpm',
    defaultPort: 4178,
    launch() {
      return { command: pnpmCommand(), args: ['dev'] };
    },
  },
  relay: {
    id: 'relay',
    name: 'relay-drop',
    label: 'Relay Drop',
    desc: '扫码互传原型 · Vue + PeerJS + WebRTC',
    commandDesc: 'npm run dev  (web + peerjs)',
    cwd: path.join(ROOT_DIR, 'relay-drop'),
    packageManager: 'npm',
    defaultPort: 4173,
    peerPort: 9000,
    launch() {
      return { command: npmCommand(), args: ['run', 'dev'] };
    },
  },
};

const PROJECT_ORDER = ['bean', 'canvas', 'relay'];

function npmCommand() {
  return process.platform === 'win32' ? 'npm.cmd' : 'npm';
}

function pnpmCommand() {
  return process.platform === 'win32' ? 'pnpm.cmd' : 'pnpm';
}

function log(message) {
  process.stdout.write(`[dev] ${message}\n`);
}

function die(message, exitCode = 1) {
  process.stderr.write(`[dev] ${message}\n`);
  process.exit(exitCode);
}

function showUsage() {
  process.stdout.write(
    [
      'Usage:',
      '  ./dev                  # 打开交互式多选启动器',
      '  node ./start.js        # 直接运行 Node 启动器',
      '  ./dev bean            # 启动 bean-pop-studio',
      '  ./dev canvas          # 启动 canvas-editor-studio',
      '  ./dev relay           # 启动 relay-drop',
      '  ./dev bean relay      # 同时启动多个项目',
      '  ./dev all             # 启动全部项目',
      '  ./dev status          # 查看依赖和端口状态',
      '  ./dev install-all     # 安装全部依赖',
      '  ./dev help            # 查看帮助',
      '',
    ].join('\n')
  );
}

function runCapture(command, args, options = {}) {
  return spawnSync(command, args, {
    encoding: 'utf8',
    stdio: ['ignore', 'pipe', 'pipe'],
    ...options,
  });
}

function runInherit(command, args, options = {}) {
  const result = spawnSync(command, args, {
    stdio: 'inherit',
    ...options,
  });

  if (result.error) {
    throw result.error;
  }

  if (typeof result.status === 'number' && result.status !== 0) {
    process.exit(result.status);
  }
}

function commandExists(command) {
  const checker = process.platform === 'win32' ? 'where' : 'sh';
  const args =
    process.platform === 'win32' ? [command] : ['-lc', `command -v ${command}`];
  const result = runCapture(checker, args);
  return result.status === 0;
}

function ensurePnpm() {
  if (commandExists('pnpm')) {
    return;
  }

  if (commandExists('corepack')) {
    log('pnpm 未安装，正在执行 corepack enable');
    runInherit(process.platform === 'win32' ? 'corepack.cmd' : 'corepack', ['enable']);
  }

  if (!commandExists('pnpm')) {
    die('pnpm 仍不可用，请先安装 pnpm 或启用 corepack。');
  }
}

function hasNodeModules(projectId) {
  return fs.existsSync(path.join(PROJECTS[projectId].cwd, 'node_modules'));
}

function ensureNodeModules(projectId) {
  if (hasNodeModules(projectId)) {
    return;
  }

  const project = PROJECTS[projectId];
  log(`${project.label} 缺少 node_modules，开始安装依赖`);
  installDeps(projectId);
}

function installDeps(projectId) {
  const project = PROJECTS[projectId];

  if (project.packageManager === 'pnpm') {
    ensurePnpm();
    runInherit(pnpmCommand(), ['install'], { cwd: project.cwd });
    return;
  }

  runInherit(npmCommand(), ['install'], { cwd: project.cwd });
}

function installAll() {
  for (const projectId of PROJECT_ORDER) {
    installDeps(projectId);
  }
  log('全部依赖安装完成');
}

function portInUse(port) {
  const result = runCapture('lsof', ['-nP', `-iTCP:${port}`, '-sTCP:LISTEN']);
  return result.status === 0;
}

function showCursor() {
  process.stdout.write('\x1b[?25h');
}

function hideCursor() {
  process.stdout.write('\x1b[?25l');
}

function clearScreen() {
  process.stdout.write('\x1b[2J\x1b[H');
}

function printStatus() {
  const lines = [];

  lines.push('');
  lines.push('Project status');
  lines.push(
    `  bean-pop-studio      deps=${hasNodeModules('bean') ? 'yes' : 'no'}  port ${PROJECTS.bean.defaultPort}=${portInUse(PROJECTS.bean.defaultPort) ? 'occupied' : 'free'}`
  );
  lines.push(
    `  canvas-editor-studio deps=${hasNodeModules('canvas') ? 'yes' : 'no'}  port ${PROJECTS.canvas.defaultPort}=${portInUse(PROJECTS.canvas.defaultPort) ? 'occupied' : 'free'}`
  );
  lines.push(
    `  relay-drop           deps=${hasNodeModules('relay') ? 'yes' : 'no'}  web ${PROJECTS.relay.defaultPort}=${portInUse(PROJECTS.relay.defaultPort) ? 'occupied' : 'free'}  peer ${PROJECTS.relay.peerPort}=${portInUse(PROJECTS.relay.peerPort) ? 'occupied' : 'free'}`
  );
  lines.push('');

  process.stdout.write(`${lines.join('\n')}\n`);
}

function normalizeProjectToken(token) {
  const normalized = String(token).trim().toLowerCase();

  switch (normalized) {
    case '1':
    case 'bean':
    case 'bean-pop-studio':
      return 'bean';
    case '2':
    case 'canvas':
    case 'canvas-editor-studio':
      return 'canvas';
    case '3':
    case 'relay':
    case 'relay-drop':
      return 'relay';
    case 'all':
      return 'all';
    default:
      return null;
  }
}

function parseProjectSelection(inputTokens) {
  const tokens = inputTokens
    .flatMap((token) => String(token).split(/[,+\s]+/))
    .map((token) => token.trim())
    .filter(Boolean);

  const selected = new Set();

  for (const token of tokens) {
    const normalized = normalizeProjectToken(token);
    if (!normalized) {
      throw new Error(`无效项目选择: ${token}`);
    }

    if (normalized === 'all') {
      PROJECT_ORDER.forEach((projectId) => selected.add(projectId));
      continue;
    }

    selected.add(normalized);
  }

  return PROJECT_ORDER.filter((projectId) => selected.has(projectId));
}

function getBeanPort(selectedIds) {
  return selectedIds.has('bean') && selectedIds.has('relay')
    ? PROJECTS.bean.altPort
    : PROJECTS.bean.defaultPort;
}

function getProjectRenderState(projectId, selectedIds) {
  const project = PROJECTS[projectId];
  const selected = selectedIds.has(projectId);
  const depsReady = hasNodeModules(projectId);
  let portLabel = '';
  let occupied = false;
  let url = '';
  let extraUrl = '';

  if (projectId === 'bean') {
    const beanPort = getBeanPort(selectedIds);
    portLabel =
      beanPort === project.defaultPort
        ? `${beanPort}`
        : `${beanPort}  (与 relay-drop 同启时自动切换)`;
    occupied = portInUse(beanPort);
    url = `http://127.0.0.1:${beanPort}`;
  } else if (projectId === 'canvas') {
    portLabel = `${project.defaultPort}`;
    occupied = portInUse(project.defaultPort);
    url = `http://127.0.0.1:${project.defaultPort}`;
  } else {
    portLabel = `web ${project.defaultPort}  ·  peer ${project.peerPort}`;
    occupied = portInUse(project.defaultPort) || portInUse(project.peerPort);
    url = `http://127.0.0.1:${project.defaultPort}`;
    extraUrl = `http://127.0.0.1:${project.peerPort}/peerjs`;
  }

  return {
    project,
    selected,
    depsReady,
    occupied,
    portLabel,
    url,
    extraUrl,
  };
}

function getLaunchPlan(selectedProjects) {
  if (!selectedProjects.length) {
    throw new Error('至少选择一个项目');
  }

  const selectedIds = new Set(selectedProjects);
  const beanPort = getBeanPort(selectedIds);
  const ports = [];
  const entries = [];

  if (selectedIds.has('bean')) {
    ports.push(beanPort);
    entries.push({
      id: 'bean',
      label: PROJECTS.bean.label,
      cwd: PROJECTS.bean.cwd,
      url: `http://127.0.0.1:${beanPort}`,
      command:
        beanPort === PROJECTS.bean.defaultPort
          ? PROJECTS.bean.launch()
          : PROJECTS.bean.launch(['--', '--host', '0.0.0.0', '--port', String(beanPort)]),
    });
  }

  if (selectedIds.has('canvas')) {
    ports.push(PROJECTS.canvas.defaultPort);
    entries.push({
      id: 'canvas',
      label: PROJECTS.canvas.label,
      cwd: PROJECTS.canvas.cwd,
      url: `http://127.0.0.1:${PROJECTS.canvas.defaultPort}`,
      command: PROJECTS.canvas.launch(),
    });
  }

  if (selectedIds.has('relay')) {
    ports.push(PROJECTS.relay.defaultPort, PROJECTS.relay.peerPort);
    entries.push({
      id: 'relay',
      label: PROJECTS.relay.label,
      cwd: PROJECTS.relay.cwd,
      url: `http://127.0.0.1:${PROJECTS.relay.defaultPort}`,
      peerUrl: `http://127.0.0.1:${PROJECTS.relay.peerPort}/peerjs`,
      command: PROJECTS.relay.launch(),
    });
  }

  return { entries, ports };
}

function ensurePortsFree(ports) {
  for (const port of ports) {
    if (portInUse(port)) {
      throw new Error(`端口 ${port} 已被占用`);
    }
  }
}

function prepareProjects(selectedProjects) {
  for (const projectId of selectedProjects) {
    ensureNodeModules(projectId);
  }
}

function killChild(child, signal = 'SIGTERM') {
  if (!child || child.exitCode !== null || child.signalCode) {
    return;
  }

  try {
    child.kill(signal);
  } catch (_error) {
    // Ignore shutdown failures.
  }
}

function renderInteractiveSelector(cursorIndex, selectedIds, message = '') {
  const lines = [
    '',
    '  ╭────────────────────────────────────────────────────────────╮',
    '  │          vibe-codex · 选择要启动的项目（可多选）          │',
    '  ╰────────────────────────────────────────────────────────────╯',
    '',
  ];

  PROJECT_ORDER.forEach((projectId, index) => {
    const state = getProjectRenderState(projectId, selectedIds);
    const prefix = index === cursorIndex ? '  ❯ ' : '    ';
    const bullet = state.selected ? '●' : '○';
    const depText = state.depsReady ? 'deps ok' : 'deps missing';
    const portText = state.occupied ? 'port occupied' : 'port free';

    lines.push(`${prefix}${bullet} ${state.project.label}  (${state.project.name})`);
    lines.push(`      ${state.project.desc}`);
    lines.push(`      启动: ${state.project.commandDesc}`);
    lines.push(`      端口: ${state.portLabel}  ·  ${depText}  ·  ${portText}`);
    if (index < PROJECT_ORDER.length - 1) {
      lines.push('');
    }
  });

  lines.push('');
  lines.push('  启动预览');

  if (!selectedIds.size) {
    lines.push('    暂未选择项目');
  } else {
    PROJECT_ORDER.filter((projectId) => selectedIds.has(projectId)).forEach((projectId) => {
      const state = getProjectRenderState(projectId, selectedIds);
      lines.push(`    - ${state.project.label}  ->  ${state.url}`);
      if (state.extraUrl) {
        lines.push(`      PeerJS  ->  ${state.extraUrl}`);
      }
    });
  }

  lines.push('');
  lines.push('  ↑/↓ 选择  ·  Space 勾选  ·  1/2/3 快速切换  ·  Enter 启动');
  lines.push('  a 全选  ·  c 清空  ·  r 刷新  ·  q 退出');

  if (message) {
    lines.push('');
    lines.push(`  ${message}`);
  }

  return lines.join('\n');
}

function runInteractiveSelector() {
  return new Promise((resolve) => {
    let cursorIndex = 0;
    const selectedIds = new Set();
    let message = '';
    const stdin = process.stdin;

    if (!stdin.isTTY || !process.stdout.isTTY) {
      die('请在支持 TTY 的终端中运行，或使用 ./dev bean relay 这种命令方式。');
    }

    function cleanup() {
      stdin.removeListener('data', onData);
      showCursor();
      stdin.setRawMode(false);
      stdin.pause();
      clearScreen();
    }

    function draw(nextMessage = message) {
      message = nextMessage;
      clearScreen();
      process.stdout.write(renderInteractiveSelector(cursorIndex, selectedIds, message));
    }

    function toggleByProjectId(projectId) {
      if (selectedIds.has(projectId)) {
        selectedIds.delete(projectId);
      } else {
        selectedIds.add(projectId);
      }
    }

    function onData(chunk) {
      const key = String(chunk);

      if (key === '\u0003' || key === 'q' || key === 'Q') {
        cleanup();
        resolve(null);
        return;
      }

      if (key === '\r' || key === '\n') {
        if (!selectedIds.size) {
          draw('至少先勾选一个项目。');
          return;
        }
        cleanup();
        resolve(PROJECT_ORDER.filter((projectId) => selectedIds.has(projectId)));
        return;
      }

      if (key === '\u001b[A') {
        cursorIndex = (cursorIndex - 1 + PROJECT_ORDER.length) % PROJECT_ORDER.length;
        draw('');
        return;
      }

      if (key === '\u001b[B') {
        cursorIndex = (cursorIndex + 1) % PROJECT_ORDER.length;
        draw('');
        return;
      }

      if (key === ' ') {
        toggleByProjectId(PROJECT_ORDER[cursorIndex]);
        draw('');
        return;
      }

      if (key === '1' || key === '2' || key === '3') {
        const projectId = normalizeProjectToken(key);
        toggleByProjectId(projectId);
        draw('');
        return;
      }

      if (key === 'a' || key === 'A') {
        PROJECT_ORDER.forEach((projectId) => selectedIds.add(projectId));
        draw('');
        return;
      }

      if (key === 'c' || key === 'C') {
        selectedIds.clear();
        draw('');
        return;
      }

      if (key === 'r' || key === 'R') {
        draw('状态已刷新。');
      }
    }

    hideCursor();
    stdin.setEncoding('utf8');
    stdin.setRawMode(true);
    stdin.resume();
    stdin.on('data', onData);
    draw('');
  });
}

async function startSelectedProjects(selectedProjects) {
  const plan = getLaunchPlan(selectedProjects);

  prepareProjects(selectedProjects);
  ensurePortsFree(plan.ports);

  process.stdout.write('\n  >>> 启动中...\n\n');

  for (const entry of plan.entries) {
    log(`${entry.label}: ${entry.url}`);
    if (entry.peerUrl) {
      log(`PeerJS signaling: ${entry.peerUrl}`);
    }
  }

  const children = [];
  let shuttingDown = false;
  let finalExitCode = 0;
  let hardKillHandle = null;

  const stopChildren = (signal = 'SIGTERM') => {
    for (const child of children) {
      killChild(child, signal);
    }
  };

  const armHardKill = () => {
    if (hardKillHandle) {
      return;
    }
    hardKillHandle = setTimeout(() => {
      for (const child of children) {
        killChild(child, 'SIGKILL');
      }
    }, 1500);
  };

  const teardownSignals = () => {
    process.removeListener('SIGINT', onSigint);
    process.removeListener('SIGTERM', onSigterm);
    if (hardKillHandle) {
      clearTimeout(hardKillHandle);
      hardKillHandle = null;
    }
  };

  const onSigint = () => {
    if (shuttingDown) {
      return;
    }
    shuttingDown = true;
    finalExitCode = 130;
    stopChildren('SIGTERM');
    armHardKill();
  };

  const onSigterm = () => {
    if (shuttingDown) {
      return;
    }
    shuttingDown = true;
    finalExitCode = 143;
    stopChildren('SIGTERM');
    armHardKill();
  };

  process.on('SIGINT', onSigint);
  process.on('SIGTERM', onSigterm);

  await new Promise((resolve) => {
    let remaining = plan.entries.length;

    const maybeResolve = () => {
      if (remaining > 0) {
        return;
      }
      teardownSignals();
      resolve();
    };

    for (const entry of plan.entries) {
      const child = spawn(entry.command.command, entry.command.args, {
        cwd: entry.cwd,
        stdio: 'inherit',
        env: { ...process.env },
      });

      children.push(child);

      child.on('error', (error) => {
        if (!shuttingDown) {
          shuttingDown = true;
          finalExitCode = 1;
          log(`${entry.label} 启动失败: ${error.message}`);
          stopChildren('SIGTERM');
          armHardKill();
        }
      });

      child.on('exit', (code, signal) => {
        remaining -= 1;

        if (!shuttingDown) {
          shuttingDown = true;
          finalExitCode =
            typeof code === 'number'
              ? code === 0 && plan.entries.length > 1
                ? 1
                : code
              : signal === 'SIGINT'
                ? 130
                : 1;

          log(`${entry.label} 已退出，正在停止其他服务`);
          stopChildren('SIGTERM');
          armHardKill();
        }

        maybeResolve();
      });
    }
  });

  process.exit(finalExitCode);
}

async function main() {
  const args = process.argv.slice(2);

  if (!args.length) {
    const selectedProjects = await runInteractiveSelector();
    if (!selectedProjects) {
      process.exit(0);
    }
    await startSelectedProjects(selectedProjects);
    return;
  }

  const command = args.join(' ').trim();

  if (command === 'help' || command === '-h' || command === '--help') {
    showUsage();
    return;
  }

  if (command === 'status') {
    printStatus();
    return;
  }

  if (command === 'install-all' || command === 'install') {
    installAll();
    return;
  }

  let selectedProjects;
  try {
    selectedProjects = parseProjectSelection(args);
  } catch (error) {
    die(`${error.message}。可执行 ./dev help 查看用法。`);
  }

  await startSelectedProjects(selectedProjects);
}

main().catch((error) => {
  die(error instanceof Error ? error.message : String(error));
});
