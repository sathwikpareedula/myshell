# 🖥️ MyShell (Unix Shell in C)

A custom Unix-like shell built in C that allows users to navigate directories, execute programs, and manage files through a menu-driven interface.

---

## 🚀 Features

* 📁 Display files and directories
* ▶️ Run executable programs
* ✏️ Open files in editor
* 📂 Change directories
* 🔄 Sort files (by size or date)
* ⏭️ Pagination (Next / Previous navigation)
* 🧠 Efficient file handling using arrays
* ⚠️ Error handling for invalid commands

---

## 🧰 Tech Stack

* Language: C  
* OS Concepts: System Calls, Process Management  
* Libraries: `stdio.h`, `dirent.h`, `unistd.h`, `sys/wait.h`

---

## 🧠 Key Concepts

* Process creation using `fork()` and `exec()`
* Directory traversal using `dirent`
* File system operations
* Command-line interface design

---

## ▶️ How to Run

⚠️ This project must be run in a **Linux/Unix environment (WSL recommended)**

```bash
gcc -std=c11 -Wall -Wextra -o myshell myshell.c
./myshell
