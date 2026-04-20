# WebSSH 项目快速导读与核心源码解析

这份文档旨在用最通俗易懂的语言，带你快速看懂整个 WebSSH 项目是怎么跑起来的。我们将结合目录结构、六大受核心模块的实现原理、以及关键的代码片段进行拆解。

---

## 1. 项目目录结构与文件职责

经过重构，目前项目已经是非常标准的 C 语言工程结构：

```text
webssh/
├── Makefile                # 【构建模块】定义了如何把 C 源码编译成最终程序的规则
├── webssh_server           # 最终运行的程序（执行 ./webssh_server 启动）
├── webssh_log.db           # 【数据库模块】自动生成的 SQLite 日志数据库文件
│
├── src/                    # 核心源代码文件夹
│   ├── server.c            # 【网络/线程模块】程序入口(main)，负责启动服务器、监听端口、分发线程。
│   ├── file_io.c           # 【进程/目录模块】负责拉起 bash、处理双向管道（流式输出）以及计算 cd 命令的路径。
│   ├── db_helper.c         # 【数据库模块】负责连接 SQLite 并利用参数化技术插入日志数据。
│   └── cmd_validator.c     # 【文件模块】负责拦截编辑器(vi)命令，提取 JSON 中的文本并安全地写盘。
│
├── include/                # 头文件目录（放各个 .c 文件的函数声明）
│   ├── file_io.h           
│   ├── db_helper.h         
│   └── cmd_validator.h     
│
├── web/                    
│   └── index.html          # 前端网页页面，包含终端弹窗交互和 JS 请求。
│
└── docs/                   # 项目的说明文档目录
    └── ... (各类 md 分析报告)
```

---

## 2. 六大核心模块拆解 (在哪实现、怎么实现)

### 2.1 网络编程模块
- **所在文件**：`src/server.c`
- **怎么实现的**：
  采用最经典的 Linux 套接字 API。`main` 函数中依次调用 `socket() -> bind() -> listen() -> accept()`。
  当前端访问时，它提取 HTTP 报文，如果是 `GET /` 就把 `web/index.html` 读出来发过去；如果是 `POST /command`，就提取里面的 JSON 数据。
- **易读代码片段**（位于 `server.c`）：
```c
// 监听来自浏览器的 TCP 连接
*client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
// 一旦连接成功，不去阻塞主程序，而是甩给分离的线程去跑
pthread_create(&thread_id, NULL, handle_client, (void *)client_sock);
pthread_detach(thread_id); // 线程跑完自己回收内存，不留僵尸
```

### 2.2 多进程与多线程模型
- **所在文件**：`src/server.c` (多线程) 和 `src/file_io.c` (多进程)
- **怎么实现的**：
  **多线程**负责横向接待客人：每一次浏览器发来命令，`server.c` 都会起一个新线程（`handle_client`）专门接待。
  **多进程**负责纵向安全执行：当真要跑 `ping`, `ls` 这些命令时，`file_io.c` 会用 `fork()` 克隆一个子进程。即便用户输入了把系统卡死的死循环，也只是子进程死掉，主程序绝对安全。
- **易读代码片段**（位于 `file_io.c`）：
```c
pid_t pid = fork(); // 克隆进程
if (pid == 0) {
    // 【核心】子进程沙箱
    setpgid(0, 0); // 成立新进程组，方便后期一键全杀
    dup2(pipefd[1], STDOUT_FILENO); // 把标准输出绑在管道上，传给父进程
    execl("/bin/sh", "sh", "-c", full_cmd, NULL); // 调原生的 Shell 环境执行指令
} else {
    // 父进程只管从管道里 `read` 数据，立刻 `write` 给网络 Socket 发向前端
    global_running_pid = pid; // 记下 PID 方便中断
    waitpid(pid, NULL, 0);    // 给子进程收尸
}
```

### 2.3 目录 I/O 模块 
- **所在文件**：`src/file_io.c` 的 `handle_dir_command()`
- **怎么实现的**：
  后端服务器绝不能直接调用 C 语言的 `chdir()`，因为所有网络请求共用一个后端程序，一个改了目录，别人全都跟着乱套！
  所以我们用“假动作”：每次收到 `cd xx` 时，我们套用系统级管道 `popen("cd 旧目录 && cd xx && pwd")`，利用系统的 bash 算好绝对路径后返回给前端保管（协议标记 `NEW_DIR_STATE:`）。后续前端发任何命令，都带上这个绝对路径。

### 2.4 文件 I/O 模块
- **所在文件**：`src/cmd_validator.c` 的 `handle_sys_save()`
- **怎么实现的**：
  应对用户在页面修改文本保存的需求。前端发来的可能带有类似 `\n` 或 `\"` 的 JSON 转义字符。我们在这里进行遍历“清洗”，把转义符还原成真实的换行或引号，再用安全的 `fopen("w")` 覆盖到硬盘。
- **易读代码片段**（位于 `cmd_validator.c`）：
```c
// 还原由于 JSON 而引发的字符转移漏洞
if (content[i] == '\\' && i + 1 < to_write) {
    if (content[i+1] == 'n') { decoded[j++] = '\n'; i++; continue; }
    if (content[i+1] == '"') { decoded[j++] = '"'; i++; continue; }
}
// 清洗完后写入底层磁盘
fwrite(decoded, 1, j, f);
```

### 2.5 数据库存储模块
- **所在文件**：`src/db_helper.c`
- **怎么实现的**：
  抛弃了把字符串拼在一起塞给数据库的做法（比如 `sprintf(sql, "INSERT ... %s")`），因为命令结果可能长达几十 KB，且充满各种怪异的单双引号乃至恶意截断符。
  使用了 SQLite 自带的“预编译声明 + 绑定参数”API。使用 `SQLITE_TRANSIENT` 让数据库自己好好拷贝这份数据，安全、稳当。
- **易读代码片段**（位于 `db_helper.c`）：
```c
// 提前架好含有 ? 号参数化占位符的表结构树
const char *sql = "INSERT INTO command_log (username, path, command, output) VALUES (?, ?, ?, ?);";
sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
// 放心地将那些包含单引号双引号等极危字符强行绑定塞进去
sqlite3_bind_text(stmt, 4, output ? output : "", -1, SQLITE_TRANSIENT);
sqlite3_step(stmt);
```

### 2.6 Makefile 工程构建
- **所在文件**：项目根目录下的 `Makefile`
- **怎么实现的**：
  不借助花里胡哨的现代构建工具。利用 C 官方推荐依赖传递规则。标明了 `src/*.c` 到 `.o` 的编译，加上 `-Iinclude` 将头文件暴漏出去。最终在 `LDFLAGS` 追加了 `-lpthread` (用于多线程) 和 `-lsqlite3` (用于数据库)。输入 `make` 指令，全盘自动化流水线组装。

---

## 3. Web 前端设计与其他重要非核心机制

除了底层坚如磐石的 C 代码，前端 Web 面板的设计以及某些虽然没那么核心，但关乎体验的特殊协议机制同样精妙绝伦。

### 3.1 前端 Web 实时渲染流与视图降维
- **所在文件**：`web/index.html` 中的 JS 脚本 `sendCommand()`
- **怎么实现的**：
  如果没有这个机制，输入 `ping` 命令会让你对着黑屏幕发呆 4 秒钟，然后数据随着 HTTP 请求结束一口气全吐出来导致卡顿。
  为了实现打字机般的“一按即显”神还原，前端强行开启了 Fetch API 实验性质的** `ReadableStream` 现代特性**。浏览器利用一个 `Reader` 循环读取 TCP 中的块数据（Chunk），不等待网络完全结束，拿一点就往终端 DIV 里塞一点，实现视觉欺骗。
  此外前端还利用两个大 DIV (`#terminal-view` 和 `#editor-view`)，当后端传来 `EDITOR_OPEN` 标记时，瞬间把终端界面隐藏，弹出类似记事本的 textarea。
- **易读代码片段**：
```javascript
// 获取浏览器流式读取器大杀器
const reader = response.body.getReader();
const decoder = new TextDecoder('utf-8');

while (true) {
    const { done, value } = await reader.read(); // 读取一段数据块
    if (done) break; // 读取完毕才跳出
    
    const chunk = decoder.decode(value, { stream: true });
    
    // 如果是后端弹出的特殊协议，立即隐身准备切唤编辑器
    if (chunk.startsWith("EDITOR_OPEN:")) {
        outputDiv.style.display = "none";
        // 随后进入 showEditor()...
    } else {
        // 否则将字符如同真实终端机一样即时贴到屏幕上
        span.textContent = chunk;
        outputDiv.appendChild(span);
    }
}
```

### 3.2 阻断执行中的任务 (全局进程中断机制)
- **所在文件**：`src/server.c` 的 `/interrupt` 路由 + `src/file_io.c` 的 `interrupt_current_process()`
- **怎么实现的**：
  如果用户手滑输入了 `while true; do echo 1; done` 这种死循环，网页就永远不能再输入了。
  为此我们在前端加入了红色的【中断(Ctrl+C)】按钮。点击后，发送独立请求到后端 `/interrupt` 路由。此时后端定义了一个全局变量 `global_running_pid` 来记住当前阻塞命令的老大 (父级进程组 ID)。利用发送 `-SIGKILL` 信号，精确连锅端掉这个命令克隆出来的整个家族！
- **附加防御（Broken Pipe 抗崩溃）**：当用户点击中断退出或关闭浏览器时，网络 Socket 瞬间归零，C 后端还在试图往死掉的 Socket 怼数据，会引发 Linux 发送 `SIGPIPE` 信号直接干掉服务器主进程！所以我们在 `file_io.c` 中注入了 `signal(SIGPIPE, SIG_IGN);` 挡下这把暗器。
- **易读代码片段**（位于 `file_io.c`）：
```c
// 记录当前正在死磕的执行进程组 PID
pid_t global_running_pid = -1;

void interrupt_current_process() {
    if (global_running_pid > 0) {
        // 【关键】利用 - 号杀掉进程组，不论是 ping 还是衍生的无数微小线程，一并拔除
        kill(-global_running_pid, SIGKILL); 
    }
}
```

### 3.3 交互式全屏指令降维保护（批处理替换）
- **所在文件**：`src/file_io.c` 的 `execute_long_process_command`
- **怎么实现的**：
  当用户想要通过浏览器运行 `top -d 2` 等这类命令时，由于 `top` 会试图向所谓的终端发送类似 `[?1l>` 的 ANSI 颜色/清屏控制符并进行缓冲等待。
  这会导致咱们的网页端接收到乱码并且被卡死不出字。所以这里实施了精准拦截，**一旦监控到以 top 打头，就强行在其末尾追加 `-b`（批处理 Batch 模式的简称）**。
  这直接剥夺了 `top` 交互与排版的花招，强制它乖乖以纯文本一帧一帧输出到水管（Pipe）里回弹前端！
- **易读代码片段**：
```c
// 针对 top 命令的极其特殊的兼容性处理：
char final_cmd[4096];
if (strncmp(cmd_line, "top", 3) == 0 && (cmd_line[3] == ' ' || cmd_line[3] == '\0')) {
    // 强制追加批处理模式（-b）让它以纯文本数据流行式不断输出！
    snprintf(final_cmd, sizeof(final_cmd), "%s -b", cmd_line);
} else {
    snprintf(final_cmd, sizeof(final_cmd), "%s", cmd_line); 
}
```

### 3.4 sudo 提权密码隐蔽注入 (前端智能魔法)
- **所在文件**：`web/index.html` 的回车监听事件
- **怎么实现的**：
  普通终端使用 `sudo` 时，系统会停止输出并向你要密码。但我们的 WebSSH 是个“哑巴”无状态通讯，`sudo` 一出现终端就会永久卡死（因为它在后台苦等只有真正键盘能给它的密码）。
  因此前端拦截了带有 `sudo ` 开头的命令串，先利用浏览器的弹窗 (`prompt`) 直接索要密码。拿到后，再将密码变成一条利用系统管道符 `|` 的隐匿指令 `echo "密码" | sudo -S (原生指令)` 后门送到服务器。`sudo -S` 参数意味着允许它从标准长管道读入密码。
- **易读代码片段**（位于 `index.html`）：
```javascript
if (cmd.startsWith("sudo ")) {
    const pwd = prompt("执行 sudo 操作需要系统提权，请输入密码：");
    if (pwd === null) return; // 放弃
    const cleanPwd = pwd.replace(/"/g, '\\"');
    
    // 把命令组装成一条自动喂食口令的复合命令交由 C 语言核心执行
    cmd = `echo "${cleanPwd}" | sudo -S ${cmd.substring(5)}`;
}
```

---

_以上导读结合了项目重构过程中的各种心血。不论是从服务器底层的指针缓冲博弈，还是到上层 JS 原生流式调用的界面小幻术，整条链路构装完成了这款属于我们自己的工业级模拟服务器。_