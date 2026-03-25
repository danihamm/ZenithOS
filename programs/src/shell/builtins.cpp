/*
    * builtins.cpp
    * Shell builtin commands: help, ls, cd, man
    * Copyright (c) 2025-2026 Daniel Hammer
*/

#include "shell.h"

static void print_ls_entry(const char* entry) {
    int len = slen(entry);
    bool hadTrailingSlash = len > 0 && entry[len - 1] == '/';

    while (len > 0 && entry[len - 1] == '/') len--;

    int base = 0;
    for (int i = 0; i < len; i++) {
        if (entry[i] == '/') base = i + 1;
    }

    for (int i = base; i < len; i++) montauk::putchar(entry[i]);
    if (hadTrailingSlash) montauk::putchar('/');
}

// ---- help ----

void cmd_help() {
    montauk::print("Shell builtins:\n");
    montauk::print("  help            Show this help message\n");
    montauk::print("  ls [dir]        List files in directory\n");
    montauk::print("  cd [dir]        Change working directory\n");
    montauk::print("  pwd             Print working directory\n");
    montauk::print("  echo [-n] ...   Print arguments\n");
    montauk::print("  set [VAR=val]   Show or set shell variables\n");
    montauk::print("  unset VAR       Remove a shell variable\n");
    montauk::print("  true / false    Return success / failure\n");
    montauk::print("  N:              Switch to drive N (e.g. 1:)\n");
    montauk::print("  exit            Exit the shell\n");
    montauk::print("\n");
    montauk::print("Syntax:\n");
    montauk::print("  VAR=value       Set a shell variable\n");
    montauk::print("  $VAR ${VAR}     Variable expansion\n");
    montauk::print("  ~               Expands to home directory\n");
    montauk::print("  cmd1 ; cmd2     Run commands sequentially\n");
    montauk::print("  cmd1 && cmd2    Run cmd2 if cmd1 succeeds\n");
    montauk::print("  cmd1 || cmd2    Run cmd2 if cmd1 fails\n");
    montauk::print("  # comment       Comment (ignored)\n");
    montauk::print("\n");
    montauk::print("Built-in variables:\n");
    montauk::print("  $USER  $HOME  $PWD  $?\n");
    montauk::print("\n");
    montauk::print("System commands:\n");
    montauk::print("  man <topic>     View manual pages\n");
    montauk::print("  cat <file>      Display file contents\n");
    montauk::print("  edit [file]     Text editor\n");
    montauk::print("  whoami          Print current username\n");
    montauk::print("  info            Show system information\n");
    montauk::print("  date            Show current date and time\n");
    montauk::print("  uptime          Show uptime\n");
    montauk::print("  clear           Clear the screen\n");
    montauk::print("  fontscale [n]   Set terminal font scale (1-8)\n");
    montauk::print("  reset           Reboot the system\n");
    montauk::print("  shutdown        Shut down the system\n");
    montauk::print("\n");
    montauk::print("Network commands:\n");
    montauk::print("  ping <ip>       Send ICMP echo requests\n");
    montauk::print("  nslookup        DNS lookup\n");
    montauk::print("  ifconfig        Show/set network configuration\n");
    montauk::print("  tcpconnect      Connect to a TCP server\n");
    montauk::print("  irc             IRC client\n");
    montauk::print("  dhcp            DHCP client\n");
    montauk::print("  fetch <url>     HTTP client\n");
    montauk::print("  httpd           HTTP server\n");
    montauk::print("\n");
    montauk::print("Games:\n");
    montauk::print("  doom            DOOM\n");
    montauk::print("\n");
    montauk::print("Any .elf on the ramdisk is executable.\n");
}

// ---- ls ----

void cmd_ls(const char* arg) {
    arg = skip_spaces(arg);

    char path[128];
    if (*arg) scopy(path, arg, sizeof(path));
    else scopy(path, ".", sizeof(path));

    const char* entries[64];
    int count = montauk::readdir(path, entries, 64);
    if (count <= 0) {
        montauk::print("(empty)\n");
        return;
    }

    for (int i = 0; i < count; i++) {
        montauk::print("  ");
        print_ls_entry(entries[i]);
        montauk::putchar('\n');
    }
}

// ---- cd ----

bool switch_drive(int drive) {
    char path[8];
    build_drive_path(drive, "", path, sizeof(path));
    if (montauk::chdir(path) < 0) return false;
    sync_cwd();
    return true;
}

int cmd_cd(const char* arg) {
    arg = skip_spaces(arg);

    char target[128];
    if (*arg == '\0') {
        if (session_home[0]) scopy(target, session_home, sizeof(target));
        else scopy(target, "/", sizeof(target));
    } else {
        scopy(target, arg, sizeof(target));
    }

    if (montauk::chdir(target) < 0) {
        montauk::print("cd: no such directory: ");
        montauk::print(target);
        montauk::putchar('\n');
        return 1;
    }

    sync_cwd();
    return 0;
}

// ---- man ----

int cmd_man(const char* arg) {
    arg = skip_spaces(arg);
    if (*arg == '\0') {
        montauk::print("Usage: man <topic>\n");
        montauk::print("       man <section> <topic>\n");
        montauk::print("Try: man intro\n");
        return 1;
    }

    int pid = montauk::spawn("0:/os/man.elf", arg);
    if (pid < 0) {
        montauk::print("Error: failed to start man viewer\n");
        return 1;
    }
    montauk::waitpid(pid);
    return 0;
}
