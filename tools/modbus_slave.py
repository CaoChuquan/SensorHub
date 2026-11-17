# modbus_slave_sim.py - Modbus TCP 从站模拟器（开发调试用，不属于产品代码）
#
# 它是什么：
#   一个假装成"现场设备"的小程序。SensorHub 用 --modbus 启动时，会像对待真实
#   仪表那样每秒来读一次寄存器；没有真设备时，就让它来应答。
#
# 谁扮演谁：
#   本脚本 = 设备（从站 / server）      SensorHub = 上位机（主站 / client）
#   两者通过 127.0.0.1:502 通信，和真实机房里设备与上位机的关系一模一样。
#
# 点表必须和 src/data/ModbusProducer.cpp 里的 kPoints 保持一致：
#   寄存器 0/1/2 = rack01 温度/湿度/电压
#   寄存器 3/4/5 = rack02 温度/湿度/电压
#   寄存器 6/7/8 = rack03 温度/湿度/电压
#   寄存器里存的是"实际值 ×10"，245 表示 24.5℃
#
# 用法：
#   pip install pymodbus
#   python modbus_slave_sim.py                 # 监听 0.0.0.0:502，数据自动波动
#   python modbus_slave_sim.py --port 15020    # 换端口（502 被占用时用）
#   python modbus_slave_sim.py --static        # 关掉自动波动，所有寄存器固定
#
#   另开一个终端运行 SensorHub：--modbus 127.0.0.1:502
#
# 数据行为：
#   默认每秒自动小幅变化，看起来像真的温湿度变送器。
#   一旦某个寄存器被手动写入（控制台 set 或 set_value.py），该点会冻结住，
#   方便观察告警；其余点继续波动。用 auto 命令可以让冻结的点恢复波动。
#
# 控制台命令（出现 "> " 提示符后再输入）：
#   list            查看当前 9 个寄存器
#   set 0 400       把 0 号寄存器改成 400 并冻结（rack1 温度变成 40.0℃）
#   auto            让所有被冻结的点恢复自动波动
#   quit            退出
# ──────────────────────────────────────────────────────────────────────────────

import argparse
import asyncio
import sys
import threading
import time

from pymodbus.server import StartAsyncTcpServer
from pymodbus.simulator import DataType, SimData, SimDevice

SLAVE_ID = 1     # 从站号，必须和 ModbusProducer 里写的 1 一致
SCALE = 10.0     # 寄存器值 ÷ 10 = 实际值

# (测点名称, 单位, 初始寄存器值)
POINTS = [
    ("rack1 温度", "°C", 245),
    ("rack1 湿度", "%RH", 505),
    ("rack1 电压", "V", 2200),
    ("rack2 温度", "°C", 240),
    ("rack2 湿度", "%RH", 480),
    ("rack2 电压", "V", 2210),
    ("rack3 温度", "°C", 250),
    ("rack3 湿度", "%RH", 520),
    ("rack3 电压", "V", 2195),
]

# pymodbus 启动时会把寄存器复制一份自己的内存，改 SimData 里的列表没用。
# 这里借"收到请求"这个回调拿到那份正在使用的内存，后面才能改值。
#   regs：客户端正在读的那份寄存器内存
#   held：被手动写入过、冻结不再波动的寄存器编号
#   last：上一秒由波动线程写入的值，用来发现有别人改过
state = {"regs": None, "held": set(), "last": {}}


async def on_request(func_code, start_address, address, count, registers, values):
    """pymodbus 每处理一次请求都会调用这里，可以用来观察对方读了什么。"""
    state["regs"] = registers
    print(f"  <- 收到请求：功能码 {func_code}，起始地址 {address}，数量 {count}",
          flush=True)
    return None


def print_table(regs):
    print("\n  编号  测点          原始值    实际值")
    print("  " + "-" * 38)
    for i, (name, unit, initial) in enumerate(POINTS):
        raw = regs[i] if regs is not None else initial
        frozen = "  [已冻结]" if i in state["held"] else ""
        print(f"   {i}    {name:<10}  {raw:>6}   {raw / SCALE:>6.1f} {unit}{frozen}")
    if regs is None:
        print("  （客户端还没连上，上面显示的是初始值）")
    print(flush=True)


def console_loop():
    """主线程里用阻塞式 input() 读控制台。

    早先版本把 input 放进 asyncio 的线程池里跑，在部分终端下会立刻读到 EOF，
    表现为"能打印日志但打不了字"。改成主线程阻塞读之后，任何终端都正常。

    但还有一种情况会造成"打字没反应"：IDE 自带的终端有时只接管了程序的输出，
    没把键盘输入交给程序。所以这里先自检一下，发现不对就直接告诉用户换窗口。
    """
    if sys.stdin.isatty():
        print("\n可以输入命令了：list / set <编号> <原始值> / auto / quit", flush=True)
    else:
        print("\n[注意] 当前终端没有把键盘输入交给本程序（IDE 终端里常见）。",
              flush=True)
        print("       如果打字没反应，请改用独立的命令行窗口运行：", flush=True)
        print("       start cmd /k python tools\\modbus_slave_sim.py", flush=True)
    while True:
        line = input("> ").strip()
        if not line:
            continue

        parts = line.split()
        cmd = parts[0].lower()

        if cmd in ("quit", "exit"):
            return
        if cmd == "list":
            print_table(state["regs"])
        elif cmd == "set":
            if len(parts) != 3:
                print("用法：set <编号> <原始值>，例如 set 0 400")
                continue
            try:
                index, value = int(parts[1]), int(parts[2])
            except ValueError:
                print("编号和值都必须是整数")
                continue
            if not 0 <= index < len(POINTS):
                print(f"编号只能是 0~{len(POINTS) - 1}")
                continue
            if not 0 <= value <= 65535:
                print("寄存器是 16 位无符号整数，值只能是 0~65535")
                continue
            if state["regs"] is None:
                print("还没有客户端连上来，等 SensorHub 连上后再改")
                continue
            state["regs"][index] = value
            # 手动设过的点冻结住，否则下一秒就被波动线程改跑了
            state["held"].add(index)
            state["last"][index] = value
            name, unit, _ = POINTS[index]
            print(f"已设置 {name} = {value}（实际 {value / SCALE:.1f}{unit}），"
                  f"该点已冻结")
        elif cmd == "auto":
            state["held"].clear()
            state["last"].clear()
            print("已恢复自动波动")
        else:
            print("未知命令。可用：list / set <编号> <原始值> / auto / quit")


# 下面两组只在 --drift（自动波动）模式下使用：
# 每次改动的最大幅度（寄存器原始值），以及允许的取值范围
DRIFT_STEP = [2, 5, 3] * 3
DRIFT_RANGE = [(180, 320), (400, 650), (2150, 2250)] * 3


def drift_loop():
    """每秒把寄存器值随机微调一点，让界面数字像真设备一样缓慢起伏。

    两个例外：
      1. 被手动写入过的点（state["held"]）不再波动，这样告警能一直挂着；
      2. 发现某个值和自己上次写的不一样，说明有别的程序（set_value.py）
         刚写过它，同样冻结该点。
    """
    import random
    while True:
        time.sleep(1)
        regs = state["regs"]
        if regs is None:      # 客户端还没连上，先不动
            continue
        for i in range(len(POINTS)):
            if i in state["held"]:
                continue

            previous = state["last"].get(i)
            if previous is not None and regs[i] != previous:
                state["held"].add(i)   # 有外部写入，冻结它
                continue

            low, high = DRIFT_RANGE[i]
            step = DRIFT_STEP[i]
            value = max(low, min(high, regs[i] + random.randint(-step, step)))
            regs[i] = value
            state["last"][i] = value


def run_server(device, host, port):
    """在后台线程里跑 asyncio 事件循环，主线程专心处理控制台输入。"""
    asyncio.run(StartAsyncTcpServer(context=device, address=(host, port)))


def main(args):
    holding = SimData(
        address=0,
        count=len(POINTS),
        values=[p[2] for p in POINTS],
        datatype=DataType.REGISTERS,
    )
    # simdata 传列表即可；action 是官方提供的"每收到请求回调一次"的钩子
    device = SimDevice(id=SLAVE_ID, simdata=[holding], action=on_request)

    print(f"Modbus 从站已启动：{args.host}:{args.port}"
          f"（从站号 {SLAVE_ID}，{len(POINTS)} 个保持寄存器）")
    print(f"SensorHub 连接参数：--modbus 127.0.0.1:{args.port}")

    threading.Thread(target=run_server,
                     args=(device, args.host, args.port),
                     daemon=True).start()

    if args.static:
        print("固定值模式：所有寄存器保持不变（--static）")
    else:
        print("自动波动已开启：数值每秒小幅变化；手动写入的点会自动冻结")
        threading.Thread(target=drift_loop, daemon=True).start()

    try:
        console_loop()
    except EOFError:
        print("\n控制台不可用（stdin 已关闭），服务器继续运行，按 Ctrl+C 退出",
              flush=True)
        try:
            while True:
                time.sleep(60)
        except KeyboardInterrupt:
            pass
    except KeyboardInterrupt:
        print("\n已退出", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Modbus TCP 从站模拟器（调试用）")
    parser.add_argument("--host", default="0.0.0.0", help="监听地址，默认 0.0.0.0")
    parser.add_argument("--port", type=int, default=502, help="监听端口，默认 502")
    parser.add_argument("--static", action="store_true",
                        help="关闭自动波动，所有寄存器保持固定值")
    parser.add_argument("--drift", action="store_true",
                        help="（默认已开启自动波动，此参数仅为兼容保留）")
    arguments = parser.parse_args()

    # 行缓冲：每条输出立刻刷出去。终端把输出接到管道时默认是块缓冲，
    # 会出现"命令其实执行了，但屏幕上什么都没有"，看起来就像输入没生效。
    # 顺带容忍中文 Windows 控制台编码不全，宁可显示成问号也不要崩溃。
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True, errors="replace")

    main(arguments)