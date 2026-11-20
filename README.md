# ProcFS - Hierarchical Process Tree Filesystem (FUSE)

A user-space virtual filesystem implemented in C using **FUSE (Filesystem in Userspace)** that exposes active Linux processes as a hierarchical directory tree based on parent-child relationships (`PPID` -> `PID`).

Developed for the **Operating Systems** course at the Faculty of Mathematics and Computer Science, University of Bucharest.

## Features

- **Dynamic Process Tree Structure:** Maps `/proc` entries into nested directories reflecting the live process hierarchy (e.g., `/1/<child_pid>/<grandchild_pid>`).
- **Real-Time Process Resolution:** Dynamically parses `/proc/[pid]/stat` during directory traversal to resolve each active process's parent PID (`PPID`).
- **Virtual `stats` Files:** Every PID directory includes a read-only `stats` file exposing runtime details from `/proc/[pid]/status` (state, memory usage, threads, UID/GID, etc.).

## Exposed Structure

```text
mountpoint/
└── 1/
    ├── stats
    ├── 412/
    │   ├── stats
    │   └── 890/
    │       └── stats
    └── 530/
        └── stats
```

## System Requirements

- Linux operating system (or WSL2 with FUSE support)
- `gcc` compiler
- FUSE development headers (`libfuse-dev`) and `pkg-config`

Install dependencies on Debian / Ubuntu:

```bash
sudo apt update
sudo apt install build-essential libfuse-dev pkg-config
```

## Build and Usage

1. **Compile:**
   ```bash
   gcc ProcFS.c -o procfs $(pkg-config fuse --cflags --libs)
   ```

2. **Create a mountpoint and mount the filesystem:**
   ```bash
   mkdir -p /tmp/procfs_mount
   ./procfs /tmp/procfs_mount
   ```

3. **Explore the process hierarchy:**
   ```bash
   ls -la /tmp/procfs_mount/1
   cat /tmp/procfs_mount/1/stats
   ```

4. **Unmount:**
   ```bash
   fusermount -u /tmp/procfs_mount
   ```

## Implementation Details

- **`do_getattr`**: Validates whether a requested PID exists in `/proc` and assigns file attributes (`0755` for PID directories and `0444` read-only permissions for `stats` files).
- **`do_readdir`**: Scans `/proc`, filters numeric entries, and populates the current directory with child processes whose `PPID` matches the current directory's PID.
- **`do_read`**: Extracts the target PID from the FUSE path and reads the contents of `/proc/[pid]/status` into the output buffer.
- Parsing of `/proc/[pid]/stat` explicitly handles executable names (`comm`) containing spaces or nested parentheses.
