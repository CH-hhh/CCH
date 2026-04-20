---
name: create-web-ssh
description: "Use when: The user wants to build a Web-based simulated SSH command-line server using pure C/C++ standard library and POSIX APIs, without complex third-party frameworks."
---

# Web-based Simulated SSH Server Builder

You are a senior C/C++ engineer. Your task is to build a "Web-based simulated SSH command-line server" from scratch using the simplest and most basic C/C++ standard library and POSIX APIs.

## Core Principles
- **No complex third-party frameworks**: Do not use Boost, POCO, libuv, etc.
- **Lightweight & Readable**: Code must be direct and easy to understand.

## Functional Modules & Technology Stack

1.  **Network Programming (Web Server & Communication)**:
    -   Use the basic `<sys/socket.h>` to implement a native TCP Server.
    -   Listen on a specified IP and port, receive HTTP requests, and return simple HTML/JS frontend pages.
    -   Frontend-backend communication uses simple AJAX (fetch API) to pass commands and results.

2.  **Multi-process/Multi-threading (Concurrency & Asynchrony)**:
    -   Use `<pthread.h>`.
    -   Allocate a lightweight thread to handle each new user connection or command request to avoid blocking the main process.
    -   Include simple user login verification logic.

3.  **File I/O (Simulated File Operations)**:
    -   Use the standard C library `<stdio.h>` or basic POSIX APIs (`open`, `read`, `write`, `remove`).
    -   Parse and simulate the execution of basic file commands (e.g., `touch`, `rm`, `cat`, `echo`).

4.  **Directory I/O (Simulated Directory Operations)**:
    -   Use `<dirent.h>` (`opendir`, `readdir`), `<unistd.h>` (`chdir`, `getcwd`), and `<sys/stat.h>` (`mkdir`).
    -   Parse and execute directory navigation and manipulation commands (`cd`, `ls`, `mkdir`, `rmdir`).

5.  **Database (Command & Time Recording)**:
    -   Use SQLite3 (the most lightweight embedded database in C/C++).
    -   Create a simple table with fields: `username`, `command`, `execute_time` (timestamp).
    -   Automatically insert a record after each command execution.

6.  **Build System (Makefile)**:
    -   Provide a simple and intuitive `Makefile`.
    -   Must include `all` (compile the server executable) and `clean` (remove `.o` files and executables) targets.

## Output Workflow
1.  **Project Structure**: Define and explain the file tree.
2.  **Makefile**: Provide the build script.
3.  **Backend Core**: Provide the C/C++ implementation with clear Chinese comments, focusing on concurrency and I/O.
4.  **Frontend**: Provide the minimalist HTML/JS code.
