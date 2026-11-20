# SensorHub

[![CI](https://github.com/CaoChuquan/SensorHub/actions/workflows/ci.yml/badge.svg)](https://github.com/CaoChuquan/SensorHub/actions/workflows/ci.yml)

面向桌面端的机房环境监控终端。把「采集 → 实时显示 → 存档 → 统计 → 可视化 → 告警」串成一条完整链路，用于演示一套可运行、可测试、可扩展的 Qt/C++ 监控软件。

## 功能

- **实时读数**：三台机柜的温度、湿度、电压，数值变化即时刷新；数据超过 3 秒没有更新时给出提示。
- **关注列表**：把关心的测点加入列表，重启后仍然保留。
- **统计分析**：对最近 N 条历史读数计算均值、标准差、最小值、最大值，并用 3σ 规则标出离群点。
- **时序图**：按机柜和指标查询历史曲线，自动适配坐标轴范围。
- **告警面板**：按「机柜 + 指标 + 阈值 + 方向」配置规则；数值越限时产生告警，持续越限不重复报；点「确认」只表示已看到，数值恢复正常后系统自动结束告警；程序重启后不会对同一次故障重复报警。
- **两种数据源**：内置仿真数据源，或通过 Modbus TCP 连接真实从站设备，切换只需一个命令行参数。
- **断链提示**：Modbus 连接断开时状态栏变红，恢复后自动重连。

## 技术栈

| 项目 | 说明 |
| --- | --- |
| 语言 / 标准 | C++17 |
| 框架 | Qt 6（Widgets、Sql、Charts、Network、Test） |
| 构建 | CMake + Ninja |
| 存储 | SQLite（WAL 模式） |
| 协议 | Modbus TCP（手写 MBAP 报文，功能码 03 读取保持寄存器） |
| 测试 | Qt Test + CTest，GitHub Actions 持续集成 |

## 架构

```
        SimulationProducer        ModbusProducer
          （仿真数据）             （TCP 采集）
                 \                   /
                  \                 /
                   ▼               ▼
                ┌─────────────────────┐
                │       DataHub       │  进程内发布-订阅数据平面
                └──────────┬──────────┘
                           │ publish(SensorReading)
        ┌──────────────────┼──────────────────┐
        ▼                  ▼                  ▼
  WatchlistService   ArchiveService     AlarmService
  AnalysisService    ChartService             │
        └──────────────────┼──────────────────┘
                           ▼
                 ┌───────────────────┐
                 │  5 个界面 Tab     │
                 └───────────────────┘
```

几个关键取舍：

- **数据平面而非点对点调用**：数据源只负责往 `DataHub` 发一条读数，界面和服务各自订阅，新增一个功能不需要改动采集代码。
- **依赖注入**：`DataHub`、数据库、各个 Service 都由 `main.cpp` 创建后传下去，没有全局单例，测试里可以换成临时数据库。
- **核心库与界面分离**：业务代码编译成 `sensorhub_core` 静态库，只依赖 Qt Core/Sql/Network，不依赖 Widgets。测试只链接这个库，不需要启动图形界面。

## 目录结构

```
src/
  core/        DataHub 数据平面
  data/        数据源接口、仿真数据源、Modbus 客户端、设备配置结构
  services/    关注列表、存档、统计、图表、告警等业务逻辑
  storage/     SQLite 连接与各仓储（Repository）
  ui/          主窗口与 5 个功能页面
test/          Qt Test 单元测试
tools/         Modbus 从站模拟器与手动改值脚本
```

## 构建

环境要求：Qt 6.5 及以上（含 Widgets、Sql、Charts、Network、Test）、CMake 3.16 及以上、支持 C++17 的编译器（MinGW 或 MSVC）。

```bash
cmake -B build -DCMAKE_PREFIX_PATH=<Qt 安装路径>
cmake --build build
```

在本机使用 Qt Creator 时，直接打开 `CMakeLists.txt` 选择套件构建即可。

## 运行

**仿真模式**（不接设备，直接看界面效果）：

```bash
./build/SensorHub.exe
```

**Modbus 模式**（连接从站设备）：

```bash
./build/SensorHub.exe --modbus 192.168.1.100:502
```

地址支持 `IP` 或 `IP:端口` 两种写法，省略端口时默认 502。

> 使用 Qt Creator 或多配置生成器（Visual Studio）构建时，可执行文件通常位于 `build/Debug/` 或 `build/Release/` 子目录，按实际路径运行。

### 没有真实设备时

`tools/` 下提供了一个基于 pymodbus 的从站模拟器，它扮演现场设备，SensorHub 扮演上位机主站：

```bash
pip install pymodbus
python tools/modbus_slave.py          # 默认监听 0.0.0.0:502，数值自动波动
python tools/modbus_slave.py --port 15020   # 502 被占用时换端口
```

另开一个终端运行：

```bash
./build/SensorHub.exe --modbus 127.0.0.1:502
```

模拟器的点表与程序一致：寄存器 0/1/2 对应 rack1 的温度/湿度/电压，3/4/5 对应 rack2，6/7/8 对应 rack3，寄存器值除以 10 就是实际值。

想在界面上触发告警，可以把某个寄存器改成明显超限的值：

```bash
python tools/set_value.py 0 400      # 0 号寄存器 = 400，即 rack1 温度 40.0℃
python tools/set_value.py 0 245      # 改回正常值
```

被手动写入的点会冻结在设定值，方便稳定观察告警；其余点继续波动。

## 数据存储

数据库文件位于可执行文件同级的 `data/sensorhub.db`，共四张表：

| 表 | 用途 |
| --- | --- |
| `sensor_readings` | 历史读数，按机柜与时间建索引 |
| `watchlist` | 关注列表 |
| `alarm_rules` | 告警规则 |
| `alarm_events` | 告警记录与处理状态 |

删除该文件即可清空全部历史数据，程序下次启动会自动重新建表。

## 测试

```bash
ctest --test-dir build --output-on-failure
```

当前包含 4 个测试目标，覆盖告警状态机、统计分析、存储仓储与设备配置加载。测试使用内存数据库，不会污染开发时的数据文件。

CI 在 Windows runner 上安装 Qt、编译并执行全部测试，状态见页面顶部的徽章。

## 许可证

[Mulan PSL v2](LICENSE)
