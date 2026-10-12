import { spawnSync } from 'node:child_process';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const token = process.env.ANDY_REPO_TOKEN;
if (!token?.trim()) {
  console.error('ANDY_REPO_TOKEN is empty or unavailable to this workflow run.');
  process.exit(1);
}

const headers = {
  Accept: 'application/vnd.github+json',
  Authorization: `Bearer ${token}`,
  'X-GitHub-Api-Version': '2022-11-28',
};

async function githubGet(endpoint) {
  let response;
  try {
    response = await fetch(`https://api.github.com${endpoint}`, { headers });
  } catch (error) {
    throw new Error(`GitHub API request failed: ${error.message}`);
  }

  const body = await response.json().catch(() => ({}));
  if (!response.ok) {
    const detail = typeof body.message === 'string' ? `: ${body.message}` : '';
    throw new Error(`GitHub API returned HTTP ${response.status} for ${endpoint}${detail}`);
  }
  return body;
}

try {
  const user = await githubGet('/user');
  const repo = await githubGet('/repos/andy64lol/andy-repo');

  if (repo.permissions?.push !== true) {
    throw new Error(`GitHub authenticated ${user.login}, but reports no push access to andy64lol/andy-repo.`);
  }

  const scriptDir = path.dirname(fileURLToPath(import.meta.url));
  const askpass = path.join(scriptDir, 'git-askpass.sh');
  const workspace = process.env.GITHUB_WORKSPACE || process.cwd();
  const result = spawnSync(
    'git',
    [
      '-c',
      'credential.helper=',
      'clone',
      '--quiet',
      'https://github.com/andy64lol/andy-repo.git',
      path.join(workspace, 'andy-repo'),
    ],
    {
      cwd: process.env.RUNNER_TEMP || os.tmpdir(),
      env: {
        ...process.env,
        GIT_CONFIG_GLOBAL: '/dev/null',
        GIT_CONFIG_NOSYSTEM: '1',
        GIT_ASKPASS: askpass,
        GIT_TERMINAL_PROMPT: '0',
      },
      stdio: 'inherit',
    },
  );

  if (result.error) throw new Error(`Could not start git clone: ${result.error.message}`);
  if (result.status !== 0) throw new Error(`git clone failed with exit code ${result.status ?? 'unknown'}.`);

  console.log(`Authenticated as ${user.login}; push access confirmed and andy-repo cloned.`);
} catch (error) {
  console.error(`andy-repo checkout failed: ${error.message}`);
  process.exit(1);
}
