#!/usr/bin/env python3
# ──────────────────────────────────────────────────────────────────────────────
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
#   python modbus_slave_sim.py                 # 监听 0.0.0.0:502，从站号 1
#   python modbus_slave_sim.py --port 15020    # 换端口（502 被占用时用）
#
#   另开一个终端运行 SensorHub：--modbus 127.0.0.1:502
#
# 控制台命令（出现 "> " 提示符后再输入）：
#   list            查看当前 9 个寄存器
#   set 0 400       把 0 号寄存器改成 400（rack01 温度变成 40.0℃）
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
    ("rack01 温度", "°C", 245),
    ("rack01 湿度", "%RH", 505),
    ("rack01 电压", "V", 2200),
    ("rack02 温度", "°C", 240),
    ("rack02 湿度", "%RH", 480),
    ("rack02 电压", "V", 2210),
    ("rack03 温度", "°C", 250),
    ("rack03 湿度", "%RH", 520),
    ("rack03 电压", "V", 2195),
]

# pymodbus 启动时会把寄存器复制一份自己的内存，改 SimData 里的列表没用。
# 这里借"收到请求"这个回调拿到那份正在使用的内存，后面才能改值。
state = {"regs": None}


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
        print(f"   {i}    {name:<10}  {raw:>6}   {raw / SCALE:>6.1f} {unit}")
    if regs is None:
        print("  （客户端还没连上，上面显示的是初始值）")
    print(flush=True)


def console_loop():
    """主线程里用阻塞式 input() 读控制台。

    早先版本把 input 放进 asyncio 的线程池里跑，在部分终端下会立刻读到 EOF，
    表现为"能打印日志但打不了字"。改成主线程阻塞读之后，任何终端都正常。
    """
    print("\n可以输入命令了：list / set <编号> <原始值> / quit", flush=True)
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
            name, unit, _ = POINTS[index]
            print(f"已设置 {name} = {value}（实际 {value / SCALE:.1f}{unit}）")
        else:
            print("未知命令。可用：list / set <编号> <原始值> / quit")


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
    arguments = parser.parse_args()

    # 中文 Windows 的控制台编码不一定支持所有字符，宁可显示成问号也不要崩溃
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")

    main(arguments)