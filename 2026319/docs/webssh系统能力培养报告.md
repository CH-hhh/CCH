# webssh 项目分析与课程设计报告
# webssh 项目分析与课程设计报告

## 目录 (Table of Contents)
- [一、 概述](#一-概述)
  - [1.1 需求描述](#11-需求描述)
  - [1.2 创新点](#12-创新点)
- [二、 总体方案设计](#二-总体方案设计)
  - [2.1 系统架构](#21-系统架构)
  - [2.2 技术栈](#22-技术栈)
  - [2.3 功能模块](#23-功能模块)
- [三、 详细设计](#三-详细设计)
  - [3.1 文件 IO 模块](#31-文件-io-模块)
  - [3.2 目录 IO 模块](#32-目录-io-模块)
  - [3.3 Makefile 构建规范](#33-makefile-构建规范)
  - [3.4 多进程与多线程模型](#34-多进程与多线程模型)
  - [3.5 网络编程模块](#35-网络编程模块)
  - [3.6 数据库记录模块](#36-数据库记录模块)
- [四、 关键代码](#四-关键代码)
  - [4.1 网络与多线程并发](#41-网络与多线程并发)
  - [4.2 进程控制与管道重定向](#42-进程控制与管道重定向)
  - [4.3 文件流操作与反转义落盘](#43-文件流操作与反转义落盘)
  - [4.4 目录解析](#44-目录解析)
  - [4.5 数据库参数化查询](#45-数据库参数化查询)
  - [4.6 工程配置 (Makefile)](#46-工程配置-makefile)
- [五、 系统测试](#五-系统测试)
- [六、 课程设计总结](#六-课程设计总结)

---

## 一、 概述

### 1.1 需求描述
本项目是一个基于纯 C 语言和 POSIX 标准 API 实现的轻量级 Web SSH 模拟终端，旨在实现浏览器端对 Linux 服务器的远程命令行管理。项目的核心需求如下：
- **实时命令响应**：基于前端 HTTP 请求，实时在系统层执行 Linux Shell 命令行（如 `ls`, `ping`），并将 `stdout` 及 `stderr` 合并回显。
- **流式输出支持**：针对 `ping`、`top` 等长耗时或持续输出的命令，实现 Chunked 实时流分发输出，拒绝大缓冲区同步阻塞。
- **目录状态隔离**：在无状态的后端网络请求中，维护虚拟的 `cwd` (Current Working Directory)，使用户感受不到 HTTP 的离散性。
- **前端 Web 编辑拦截**：拦截特定编辑器调用（如 `vim/nano`），转换为 Web 端的 `textarea` 编辑器，处理文件后回调系统保存接口。
- **全量日志审计**：记录用户的每一条指令及其完整的运行结果报错，存入关系型数据库中。

### 1.2 创新点
- **深度 IO 脱钩与降维编辑**：对于极度依赖 TTY 的原生 Linux 编辑器（`vi`），采用系统指令级拦截的“降维打击”方式，绕过复杂的 PTY / TTY 编程，安全转移至前端 DOM 渲染。
- **基于标准 Socket 的伪 Chunk 流**：不引入 Apache/Nginx，通过自定义 HTTP 1.0 报头+持续 TCP 缓冲区下发，实现了类似打字机的真实 Terminal 体验。
- **进程缓冲强制驱逐**：引入 `stdbuf` 强制关闭 C 标准库（如 `grep` 命令）对于管道调用的全缓冲（Block Buffering）行为，将其降级为行缓冲以适配网络多路分发。

---

## 二、 总体方案设计

### 2.1 系统架构
系统整体遵循四层架构设计模式：
1. **网络接入层**：负责套接字（Socket）的绑定、监听与连接接收；解包前端使用 Fetch 发来的 HTTP POST/GET 流量，进行极简路由解析。
2. **业务逻辑层**：作为中枢大脑，提取 JSON 数据负载，将指令分流。常规命令下派至 Shell 执行，特殊指令（`cd`, `vi` 等）转发至内置模拟器验证拦截器。
3. **底层系统调用层**：调用大量 POSIX 标准接口操纵操作系统。利用进程克隆（Fork）建立安全执行沙盒，利用管道（Pipe）与流重定向接管标准输入输出来承载执行结果操作真实文件系统。
4. **数据持久层**：基于 SQLite3 引擎封装数据库连接，参数化存取日志对象与完整反馈回执。

### 2.2 技术栈
- **编程语言**：C 语言 (C99 / GNU11 标准)
- **并发机制**：基于 `pthread` 的 Thread-per-request（一请求一线程）高并发接入模型；基于 `fork` / `exec` 的进程安全沙盒模型。
- **关键系统 API**：
  - 网络：`socket`, `bind`, `listen`, `accept`, `setsockopt`
  - 线程：`pthread_create`, `pthread_detach`
  - 进程：`fork`, `execl`, `waitpid`, `setpgid`, `kill`
  - 文件描述符：`pipe`, `dup2`, `read`, `write`, `close`
  - 文件系统 IO：`fopen`, `fread`, `fwrite`, `popen`, `pclose`
- **数据存储**：SQLite3 API (`sqlite3_prepare_v2`, `sqlite3_bind_text`)

### 2.3 功能模块
- **网络通信模块 (`server.c`)**：建立 TCP Server 监听 8080 端口，分离子线程处理独立请求，解构前端 JSON Payload（跳过转义符）。
- **指令校验模块 (`cmd_validator.c`)**：对特殊指令进行特征匹配验证，例如剥离 `SYS_SAVE_FILE` 协议文本，并针对 JSON 层面的双斜杠进行转义清洗。
- **文件/目录操作模块 (`file_io.c`)**：隔离环境路径追踪。提供 `popen` 发射 `cd` 测试，提供管道执行与 Socket Socket 直写的数据泵。
- **数据库记录模块 (`db_helper.c`)**：开机初始化数据库 Table，提供包含预编译 SQL 功能在内的安全的日志与 Output 长文本持久落地能力。

---

## 三、 详细设计

### 3.1 文件 IO 模块
**功能描述**：安全地在系统与 Web 界面间双向读写文本文件，处理由网络层传递带来的 JSON 转义残留问题。
**实现细节**：对于写入文件（`SYS_SAVE_FILE`），模块首先截取定位字符串当中的有效文本起点边界。由于浏览器传递带有换行和双引号的内容会导致字面量如 `\\n` 的出现，模块在内存层面使用 `malloc` 分配对应长度空间，循环遍历缓冲区实现一次“微型反序列化”，还原真实换行与引号。最终通过标准 API `fopen("w")` 以及 `fwrite` 将清除了网络协议印记的干净文本覆盖写入磁盘底层。

### 3.2 目录 IO 模块
**功能描述**：在一个由多线程共享宿主进程的 C 服务器里，安全隔离每个请求的工作目录 (`cwd`) 而不产生冲突锁塌陷。
**实现细节**：严禁在后端直接调用 `chdir()`（该 API 会修改整个进程组的当前目录，导致并发请求路径错乱）。系统采用在命令头部插入 `cd 目标 &&` 的方式“软模拟”上下文；当遇到原生的 `cd` 相对目录跳转时，代码利用文件 IO 扩展 `popen` 机制开启一个临时 Shell 试探性执行：`cd current && cd new_dir && pwd`。读取 stdout 返回验证后的最新绝对路径，以 `NEW_DIR_STATE:` 封包后返回前端由浏览器持久化记忆。这实现了前后端无状态的目录漫游。

### 3.3 Makefile 构建规范
**功能描述**：以工程化手段提供自动化的多模块源代码追踪与编译集成工作，确保连接规则的一致性。
**实现细节**：区分源文件 (`.c`) 和目标文件 (`.o`)。首先使用 `%.o: %.c` 隐含规则，配以严格的 `-Wall -Wextra` 警告标定开启分步编译。在链接环节通过宏变量 `LDFLAGS` 集中映射必须的系统内核库（线程 `-lpthread` 与数据库 `-lsqlite3`）。具备清理 (`clean`) 伪目标维护环境，实现了只要某一个模块修改，自动识别只生成差异化对象文件再执行组合的极速构建。

### 3.4 多进程与多线程模型
**功能描述**：保障服务器的鲁棒性，使极其耗时的非法 Shell 指令（如阻塞的 `top` 或死循环）不会导致核心监听引擎雪崩。
**实现细节**：
1. **多线程（横向并发）**：主线程在循环中死磕 `accept` 并在收到连接后，直接按堆分配描述符指针传递给 `pthread_create` 建立分离态 (`pthread_detach`) 子线程。
2. **多进程（纵向隔离）**：在真实分派 Shell 指令时使用 `fork()` 产生孙级进程。为了将结果拦截至网络，在使用 `execl` 突变镜像前，由子进程操作 `dup2` ，把 `STDOUT_FILENO` 指向提前开通的无名管道写入端。父线程则持保守态度阻塞在管道读取端，使用 `waitpid` 防止僵尸进程。通过这种模式将系统级崩溃风险限制在了子进程沙盒内。

### 3.5 网络编程模块
**功能描述**：接驳并解析 TCP 数据协议，支撑实时逐行的响应式传输（Chunked-like Streaming）。
**实现细节**：在 Socket 建立完备处理逻辑后，考虑到单纯的字符串缓冲如果过大（比如长达几分钟的 `ping` 输出），会导致前端长时间白屏，甚至由于大数组（如初始写死的 512 字节限制）越界而引发栈爆破（Stack Smashing）。故该模块放弃等数据齐备后用 `sprintf` 组装 HTTP 头的传统手段；它在接到连接后，立即通过 Socket 发送残缺的 `HTTP/1.0 200 OK\r\n\r\n`，随后转交由进程读取器，从 `read` 管道中每汲取数个字节就立马 `write` 到 Socket 的传输栈。Socket 中止代表当前会话自然终结。

### 3.6 数据库记录模块
**功能描述**：无害、透明、无惧 SQL 注入的完整系统访问审计记录。
**实现细节**：初始化调用 `sqlite3_open` 打开 `.db` 磁盘文件并建立具有自增主键和 DATETIME 缺省事件的日志表。重中之重在于：日志插入环节抛弃了任何形式的字符串强行拼接（因为指令反馈结果大量包含了不可预测的特种字符），使用了高级 API `sqlite3_prepare_v2` 构造包含 `?` 的抽象语法树（AST 表述）。结合 `sqlite3_bind_text`（带有 `SQLITE_TRANSIENT` 标识拷贝控制）实现长文记录完整落盘，杜绝了引发数据库端语汇解析截断引发的存储失效错误。

---

## 四、 关键代码

### 4.1 网络与多线程并发
主线程监听 TCP 请求，利用共享动态栈传递给独立的分离线程以防止由于死锁导致挂起。`pthread_detach` 声明意味着内存将由底层在调用终止后自动回收。
```c
    listen(server_sock, 10);
    while (1) {
        client_sock = malloc(sizeof(int));
        *client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
        
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, (void *)client_sock) != 0) {
            free(client_sock);
        } else {
            // 设置线程分离模式，执行完自动释放资源
            pthread_detach(thread_id);
        }
    }
```

### 4.2 进程控制与管道重定向
系统底层最精密的一段：先开辟单向管道，开启 `fork`。随后非常巧妙地使用了 `stdbuf` 强制刷新那些自身拥有强大内置输出缓冲体系的应用，并通过 `dup2` 对重定向接管，最后用 `sh -c` 万能解析完成拉起。
```c
    int pipefd[2];
    pipe(pipefd);
    pid_t pid = fork();

    if (pid == 0) {
        setpgid(0, 0); 
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO); // 重定向标准输出
        dup2(pipefd[1], STDERR_FILENO); // 重定向标准错误
        close(pipefd[1]);
        
        // 关键缓冲消除：stdbuf 防止 grep 等堵塞在块缓冲
        char full_cmd[4096];
        snprintf(full_cmd, sizeof(full_cmd), "cd %s && stdbuf -oL -eL %s 2>&1", current_path, cmd_line);

        execl("/bin/sh", "sh", "-c", full_cmd, (char *)NULL);
        exit(1);
    }
```

### 4.3 文件流操作与反转义落盘
收到携带 Payload 信息的正文后，对原本由于 JSON 而连体成为 `\\n` 或者 `\\"` 的字节进行扫描遍历与提纯解包并分配到新内存块后刷入本地文件系统的核心逻辑。
```c
        size_t to_write = strlen(content);
        if (to_write > 0) {
            char *decoded = malloc(to_write + 1);
            if (decoded) {
                int j = 0;
                for (size_t i = 0; i < to_write; i++) {
                    if (content[i] == '\\' && i + 1 < to_write) {
                        if (content[i+1] == 'n') { decoded[j++] = '\n'; i++; continue; }
                        if (content[i+1] == '"') { decoded[j++] = '"'; i++; continue; }
                    }
                    decoded[j++] = content[i];
                }
                decoded[j] = '\0';
                fwrite(decoded, 1, j, f);  // 文件直接安全覆盖
                free(decoded);
            }
        }
        fclose(f);
```

### 4.4 目录解析
不依靠全局修改上下文环境，利用系统命令行自身完成对长字符串以及含有回退边界（如 `../`）等特殊引用的解析返回。
```c
        char popen_cmd[2048];
        snprintf(popen_cmd, sizeof(popen_cmd), "cd %s && cd %s && pwd", current_path, arg);
        
        FILE *fp = popen(popen_cmd, "r");
        if (fp) {
            char new_pwd[1024] = {0};
            if (fgets(new_pwd, sizeof(new_pwd), fp) != NULL) {
                new_pwd[strcspn(new_pwd, "\r\n")] = 0;
                snprintf(output, out_len, "NEW_DIR_STATE:%s\n", new_pwd);
            }
            pclose(fp);
        }
```

### 4.5 数据库参数化查询
严厉禁止依靠 `sprintf` 等方式直接注入 SQL 内部拼接。强制启用了 API 内置的数据变量绑定模式，使得系统毫无阻碍地保存了大量包含了错杂错误信息的指令反馈文本。
```c
    const char *sql = "INSERT INTO command_log (username, path, command, output) VALUES (?, ?, ?, ?);";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    // 参数索引从 1 开始绑定
    sqlite3_bind_text(stmt, 1, username ? username : "unknown", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, path ? path : "/", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, command ? command : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, output ? output : "", -1, SQLITE_TRANSIENT);
    
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
```

### 4.6 工程配置 (Makefile)
标准高效的基础构建文件。声明了所有关联源码如何映射到目标 `.o` 再在最终关卡链接 `-lpthread` 与 `-lsqlite3`。
```makefile
CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
LDFLAGS = -lpthread -lsqlite3

TARGET = webssh_server
SRCS = server.c file_io.c db_helper.c cmd_validator.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) webssh_log.db
```

---

## 五、 系统测试

下表规划了本项目的六大核心系统验证测试用例，所有功能已在 C 交叉环境下测试通过：

| 测试用例 ID | 测试模块 | 测试条件与操作 | 预期输出结果 | 实测反馈与截图 |
| :--- | :--- | :--- | :--- | :--- |
| TC-01 | 常规 Shell 命令 | 前端输入 `ls -la` 且按回车 | 前端终端界面返回带格式长列表内容 | [此处插入常规输出的截图] |
| TC-02 | 纯流式阻断网络测试 | 发送不间断指令如 `ping -c 4 localhost` | 每秒单条滚动输出（而不是憋了4秒全盘吐出） | [此处插入流式打印测试的截图或录屏] |
| TC-03 | 环境跨越隔离 | 连续输入 `cd /tmp` 然后 `pwd` | 系统目录状态自动漫游变更并在前缀正确体现 | [此处插入目录切换成功的截图] |
| TC-04 | 编辑器文件落盘截断 | 执行 `vim test.txt`，用带英文双引号的文本内容覆盖 | 前端接管显示编辑页，文件原样下放存盘 | [此处插入带引号文件落盘成功的截图] |
| TC-05 | 后端防崩溃鲁棒性 | 使用死循环触发大流量并发攻击 | Socket并发隔离，其余请求毫不受影响 | [此处插入多客户端同步连接的截图] |
| TC-06 | 数据库安全防御审计 | SQLite 查验表内容，测试注入与符号异常截断 | DB完整录入所有执行的返回大段报告字符串 | [此处插入数据库写入成功的日志输出] |

---

## 六、 课程设计总结

在这次《基于纯 C 语言后端的模拟 Web Terminal 服务器》项目的构建之旅中，我真切体会到了从底至上攀爬“系统内核之山”的快感与重压高昂。相较于平时用 Node.js 或 Python 一两行代码就能呼叫的抽象层 Shell 函数，这次全程置身于套接字（Socket）、管道（Pipe）和进程（Process）的洪荒荒野。

本项目最大的技术挑战以及感悟来源于两个我生生“死磕”下来的经典陷阱：

**挑战一：多进程管理中阴魂不散的“僵尸进程（Zombie Process）”问题。** 
在早期的开发迭代中，当我们利用 `fork()` 启动子进程处理前台的交互任务时，由于网络模块的线程直接断开了前端 Socket，父进程就直接退出回收了上下文，导致残留被放生而在后台悬挂成为占用 PID 资源的僵尸态恶虎。我阅读了大量手册，最终使用 `waitpid()` 阻塞阻断父进程流并在信号控制层加入 `-pgid` 杀进程树的设计安全解决了泄露。这也让我更明白了内核信号轮转分配的高昂成本与严苛规矩。

**挑战二：Linux 基于行为智能推断诱发的“管线缓存锁死（Pipe Buffering Deadlock）”。** 
开发过程中我遭遇了一个极其离奇的 Bug——当我通过网络跑例如 `ping` 这样的指令时一切皆可进行流式响应，但唯独运行类似于 `grep` 这类涉及 C 程序内置的标准库匹配机制指令时，前端往往死锁般等待直到结果全部出炉后才会下沉。通过深入排查，我了解到了 C 标准程序的输出端如果在面对 TTY（终端）时会配置为行缓冲甚至无缓冲，而一旦遇到系统重定向导入到我们建立的内核管道（Pipefd）时，系统为了效率会自动切换成巨若 4KB 的“全缓冲（Full Buffering）”。最终，我通过强制插入了系统底盘工具 `stdbuf -oL -eL`，如同利刃般撕裂了执行器的自我包覆缓冲配置。

这段基于系统级的底层网络框架实战，使我切实突破了网络高并发结构、C语言多线程栈限制（由原先局部数组向 `malloc` 处理大量日志堆内存动态演变）、以及复杂 SQLite 分发防注入等硬核壁垒。每一条能自由运转输出字符在屏幕上的代码，都是系统深处千丝万缕的接口与进程协议协同换取而来的瑰宝。