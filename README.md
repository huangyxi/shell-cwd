# Monitor shell's logical CWD using Linux netlink connector

This program monitors the shell's logical CWD using the Linux netlink connector by retrieving the shell's logical CWD from the PWD environment variable of the shell's child process.

> [!NOTE]
> This program only works for long-duration programs. It may not work for fast programs like `echo` or `ls` because the child process may terminate before the PWD can be retrieved and validated.


## Prerequisites

- Linux operating system
- C compiler (e.g., `gcc`)

## Compilation

To compile the program, use the following command:

```sh
gcc -o shell-cwd main.c
```

### Usage

Test the program with the following steps:

1. Open a new bash shell and get its PID `<shell_pid>`:
    ```sh
    echo $$
    ```
2. Run the program with the shell PID in **ANOTHER** shell:
    ```sh
    ./shell-cwd <shell_pid>
    ```
3. Inside the previous shell, run a command that forks a new process:
    ```sh
    sleep 1
    ```

## How It Works

1. **Setup Netlink Socket**: The program sets up a Netlink socket to listen for process events using the Connector API.
2. **Send Netlink Message**: A message is sent to the Netlink socket to start listening for process events.
3. **Process Netlink Messages**: The program processes incoming Netlink messages to detect fork events.
4. **Handle Fork Event**: When a fork event is detected, the program retrieves the `PWD` of the child process and compares it with the `cwd` of the shell process.
5. **Detect `SHLVL`**: The program detects the `SHLVL` environment variable to ensure that the child process is indeed a new shell instance created by the parent shell. This helps differentiate between the parent and child processes based on their shell levels.
6. Get `PWD`: The program retrieves the PWD environment variable from the `/proc/[pid]/environ` file of the child process.
7. **Validation**: If the physical path of the `PWD` of the child process matches the `cwd` of the shell process, the program prints the changed directory. Otherwise, it prints the reason for the mismatch to `stderr`.

### Why We Use `PWD` Environment Variable Instead of `cwd` Link

The `PWD` environment variable is used instead of the `/proc/[pid]/cwd` link because:
- The `PWD` environment variable reflects the logical current working directory as set by the shell, which may include symbolic links.
- The `/proc/[pid]/cwd` link provides the physical path, which resolves all symbolic links. This can lead to discrepancies if the shell's `PWD` includes symbolic links.

### Why We Monitor `/proc` Using `netlink` Instead of `inotify`

- The `/proc` filesystem (procfs) is a virtual filesystem that provides process and system information. `inotify` is designed to monitor real filesystems and cannot be used to monitor virtual filesystems like `\proc`.

### Why We Detect `SHLVL`

- When a new process is forked, it inherits the environment variables of its parent process. If the child process changes its working directory after being forked, the `/proc/[pid]/environ` file might still show the parent's working directory if read too early.
- Detecting `SHLVL` ensures that the child process is indeed a new shell instance created by the parent shell, helping to differentiate between the parent and child processes based on their shell levels.
