---
name: environment-preflight
description: Mandatory first-stage environment detection skill. Identifies the operating system, distribution, architecture, available tools, memory usage, working directory, project structure, and disk usage before any coding or command execution. Helps the coding agent choose compatible, efficient, and safe commands.
---

# Environment Preflight — Mandatory First Step

## 1. Purpose

Before performing ANY coding task, you MUST identify and inspect the user's current development environment.

You must understand the machine you are operating on before deciding:

- Which commands to execute.
- Which package manager to use.
- Which dependencies are available.
- Which tools and compilers are installed.
- Which architecture to target.
- How much memory is available.
- How much disk space is available.
- Which optimizations are appropriate.
- Which implementation strategies are practical.

**NEVER assume the user's operating system, Linux distribution, architecture, hardware capabilities, or available tools.**

This skill MUST be read and executed before any other coding-agent skill.

---

## 2. Environment Detection Procedure

Perform the following steps IN ORDER.

All initial inspection should be read-only.

### Step 1 — Operating System Identification

Identify:

- Operating system.
- OS version.
- Linux distribution, if applicable.
- Distribution version.
- Kernel version.
- Current shell.
- Whether the environment is a container, virtual machine, WSL instance, remote SSH session, or native installation, when detectable.

#### Linux

Inspect:

```bash
cat /etc/os-release
uname -a
uname -r
```

If `/etc/os-release` is unavailable, use alternative identification methods.

#### Windows

Use PowerShell:

```powershell
Get-ComputerInfo | Select-Object WindowsProductName, WindowsVersion, OsArchitecture
```

#### macOS

Use:

```bash
sw_vers
uname -a
```

Do not assume that every command is available.

### Step 2 — CPU Architecture Identification

Identify:

- CPU architecture.
- Processor model, when available.
- Number of logical processors.
- Whether the environment is 32-bit or 64-bit.
- Whether the processor supports the target architecture.

#### Linux

```bash
uname -m
lscpu
```

#### Windows

```powershell
Get-CimInstance Win32_Processor | Select-Object Name, NumberOfCores, NumberOfLogicalProcessors, Architecture
```

#### macOS

```bash
uname -m
sysctl -n hw.ncpu
sysctl -n machdep.cpu.brand_string
```

Distinguish between the physical processor architecture and the architecture of the current execution environment.

For example, an ARM64 processor may be running an x86_64 virtualized environment.

### Step 3 — RAM Identification

Determine:

- Total physical memory.
- Currently used memory.
- Available memory.
- Memory utilization.
- Swap or pagefile usage, when available.

#### Linux

```bash
free -h
```

Additional information:

```bash
cat /proc/meminfo
```

#### Windows

```powershell
Get-CimInstance Win32_OperatingSystem | Select-Object TotalVisibleMemorySize, FreePhysicalMemory
```

#### macOS

```bash
memory_pressure
```

And:

```bash
sysctl -n hw.memsize
```

Do not rely exclusively on RAM capacity.

Available memory matters when selecting tools, building projects, running compilers, or launching local services.

Consider memory constraints when recommending:

- Development environments.
- Build parallelism.
- Local AI models.
- Databases.
- Containers.
- Heavy language servers.
- Multiple concurrent processes.

Never recommend memory-intensive operations without considering the available resources.

### Step 4 — Current Working Directory

Identify the exact current working directory.

Use:

```bash
pwd
```

On Windows PowerShell:

```powershell
Get-Location
```

Determine whether the current directory appears to be:

- A project root.
- A source directory.
- A home directory.
- A temporary directory.
- An unrelated location.

Never assume the current directory is the project root.

If the project location cannot be determined confidently, ask the user.

### Step 5 — Project Structure Inspection

Inspect the current directory and its relevant subdirectories.

Preferred command:

```bash
tree
```

If `tree` is unavailable, use:

```bash
ls -la
```

On Windows:

```powershell
Get-ChildItem -Force
```

If necessary, inspect relevant directories individually.

Do not install `tree` just to satisfy this requirement.

Do not recursively dump enormous directories.

Exclude or limit irrelevant and potentially sensitive directories, including:

- `.git`
- `node_modules`
- `.venv`
- `venv`
- `target`
- `dist`
- `build`
- Cache directories
- Large dependency directories

The objective is to understand the project structure, not enumerate every generated file.

Identify relevant:

- Source directories.
- Configuration files.
- Build files.
- Dependency manifests.
- Documentation.
- Existing tests.
- Entry points.

If no project is present, report that fact rather than inventing a project structure.

### Step 6 — Disk Usage Identification

Determine:

- Total storage capacity.
- Used storage.
- Available storage.
- Filesystem containing the working directory.
- Whether the project resides on removable storage, when detectable.
- Whether the filesystem is approaching capacity.

#### Linux

```bash
df -h
```

For filesystem identification:

```bash
df -h .
```

Optional:

```bash
lsblk
```

#### Windows

```powershell
Get-Volume
```

For the current working directory's drive:

```powershell
Get-PSDrive -PSProvider FileSystem
```

#### macOS

```bash
df -h
```

Disk capacity and available disk space must not be confused.

Consider available space before:

- Installing dependencies.
- Building large projects.
- Generating binaries.
- Creating archives.
- Running container builds.
- Downloading large development tools.

Never assume that a mounted removable drive is the primary system drive.

### Step 7 — Package Manager and Tool Identification

Identify the package manager based on the actual operating system and distribution.

For Linux, possible package managers include:

| Distribution | Typical package manager |
|---|---|
| Arch Linux | pacman |
| Manjaro | pacman |
| Fedora | dnf |
| Ubuntu | apt |
| Debian | apt |
| openSUSE | zypper |
| Alpine | apk |
| NixOS | nix |

Do not select a package manager merely because a familiar command exists.

Check the distribution first.

Also identify relevant available development tools when applicable:

- Git.
- Rust / Cargo.
- Node.js / npm.
- Python.
- Java.
- GCC / Clang.
- CMake.
- Make.
- Docker / Podman.
- Available IDEs and code editors.

Use version commands where appropriate:

```bash
git --version
rustc --version
cargo --version
node --version
npm --version
python3 --version
```

Only inspect tools relevant to the current task.

**Never install missing tools during preflight.**

---

## 3. Environment-Aware Decision Making

After collecting the information, use it to make technical decisions.

### Operating system compatibility

Select commands, APIs, dependencies, and implementation strategies compatible with the detected environment.

Examples:

- Do not recommend `apt` for Arch Linux.
- Do not recommend `pacman` for Fedora.
- Do not assume GNU utilities exist on macOS.
- Do not use Linux-specific commands in Windows PowerShell.
- Do not assume systemd is available.
- Do not assume Docker is installed.

### Architecture compatibility

Select compatible:

- Binary targets.
- Compiler targets.
- Libraries.
- Native dependencies.
- Precompiled packages.
- Release artifacts.

Never assume x86_64 when the machine is ARM64.

### Memory optimization

When memory is limited:

- Prefer lightweight dependencies.
- Avoid unnecessarily parallel builds.
- Avoid unnecessary background processes.
- Avoid launching multiple language servers.
- Prefer efficient data structures.
- Avoid unnecessary memory copies.
- Consider incremental compilation.
- Avoid recommending large local AI models without checking feasibility.

### Storage optimization

When storage is limited:

- Avoid unnecessary dependencies.
- Avoid redundant build artifacts.
- Avoid unnecessary downloads.
- Avoid duplicating large files.
- Consider the actual filesystem containing the project.
- Never delete caches, build outputs, or user files without authorization.

### Tool selection

Choose tools based on:

1. Compatibility.
2. Availability.
3. Resource consumption.
4. Reliability.
5. Existing project conventions.
6. User preferences.

Do not replace existing tools simply because you prefer another tool.

---

## 4. Safety and Privacy

Environment inspection MUST be non-destructive.

You are NOT permitted to:

- Execute `sudo` commands.
- Install packages.
- Modify system configuration.
- Delete files.
- Change permissions.
- Modify environment variables.
- Mount or unmount filesystems.
- Change partitions.
- Modify boot configuration.

If elevated privileges are required, explain the operation and provide the command for the user to execute themselves.

Never dump sensitive information unnecessarily.

In particular, do not expose:

- Passwords.
- Authentication tokens.
- API keys.
- Private SSH keys.
- Environment-variable secrets.
- Personal files unrelated to the project.

If an inspection command unexpectedly reveals sensitive information, do not reproduce it in your report.

---

## 5. Preflight Report

After completing inspection, produce a concise environment report.

Use this format:

### Environment Report

**Operating System**
- OS:
- Distribution:
- Version:
- Kernel:
- Environment type:

**Hardware**
- CPU:
- Architecture:
- Logical processors:

**Memory**
- Total RAM:
- Used RAM:
- Available RAM:
- Swap/pagefile:

**Storage**
- Current filesystem:
- Total capacity:
- Used:
- Available:

**Development Environment**
- Current directory:
- Shell:
- Package manager:
- Available development tools:

**Project**
- Project root:
- Main languages:
- Frameworks:
- Relevant directory structure:

### Environment Considerations

Briefly explain:

- Important hardware limitations.
- Relevant software compatibility requirements.
- Potential performance bottlenecks.
- Storage constraints.
- Recommended implementation considerations.

Do not invent information that could not be detected.

Mark unavailable information as `Unknown` or `Not detected`.

---

## 6. Failure and Missing Information

If a detection command is unavailable:

1. Try a suitable alternative.
2. Do not install a replacement.
3. Do not assume the information based on incomplete evidence.
4. Mark the result as unavailable if it cannot be established.

If the environment cannot be identified sufficiently to choose safe commands, ask the user.

If the same detection operation fails three consecutive times, follow the Three-Failure Escalation Rule from the Strict Coding Agent skill.

---

## 7. Completion Gate

The preflight procedure MUST finish before the coding agent begins implementation.

The agent must establish, at minimum:

- Operating system.
- Architecture.
- Available RAM.
- Current working directory.
- Project structure, using `tree` or `ls` as appropriate.
- Available disk space.
- Correct package manager, if package management is relevant.

Once completed:

1. Present the Environment Report.
2. Preserve the findings as working context.
3. Read and follow the Strict Coding Agent skill.
4. Continue with its mandatory workflow.

If any critical information remains unknown, do not silently guess.

---

## 8. Final Principle

**Know the machine before working on the machine.**

A coding agent that does not understand its environment cannot reliably select commands, dependencies, build configurations, or optimization strategies.

Inspect first.

Understand second.

Choose compatible tools third.

Modify only after the coding-agent workflow authorizes implementation.

This skill is mandatory before every new coding session and whenever the execution environment materially changes.
