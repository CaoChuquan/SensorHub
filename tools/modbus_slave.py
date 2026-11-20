# modbus_slave_sim.py - Modbus TCP 从站模拟器（开发调试用，不属于产品代码）
#
# 它是什么：
#   一个假装成"现场设备"的小程序。SensorHub 用 --modbus 启动时，会像对待真实
#   仪表那样一台一台来读寄存器；没有真设备时，就让它来应答。
#
# 谁扮演谁：
#   本脚本 = 设备（从站 / server）      SensorHub = 上位机（主站 / client）
#   两者通过 127.0.0.1:502 通信，和真实机房里设备与上位机的关系一模一样。
#
# 模拟的是"一台网关下挂 3 台机柜"：
#   从站号 unitId 1 → rack1 温度/湿度/电压，寄存器 0/1/2
#   从站号 unitId 2 → rack2 温度/湿度/电压，寄存器 0/1/2
#   从站号 unitId 3 → rack3 温度/湿度/电压，寄存器 0/1/2
#   每台设备的寄存器表都从 0 开始，靠 unitId 区分——这是 Modbus 的标准做法。
#   寄存器里存的是"实际值 ×10"，245 表示 24.5℃。
#
#   点表要和 config/devices.json 保持一致，否则读出来的数会张冠李戴。
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
#   list              查看 3 台设备的全部寄存器
#   set 0 400         把 0 号寄存器（unitId 1、地址 0）改成 400，即 rack1 40.0℃
#   set 5 600         5 号寄存器 → unitId 2、地址 2 → rack2 电压 60.0V
#   auto              让所有被冻结的点恢复自动波动
#   quit              退出
# ──────────────────────────────────────────────────────────────────────────────

import argparse
import asyncio
import sys
import threading
import time

from pymodbus.server import StartAsyncTcpServer
from pymodbus.simulator import DataType, SimData, SimDevice

SCALE = 10.0      # 寄存器值 ÷ 10 = 实际值
UNIT_IDS = [1, 2, 3]           # 三个机柜的从站号
REGISTERS_PER_UNIT = 3         # 每台机柜 3 个寄存器

# 每台机柜的初始寄存器值：(温度, 湿度, 电压) ×10
INITIAL_VALUES = {
    1: [245, 505, 2200],
    2: [240, 480, 2210],
    3: [250, 520, 2195],
}

METRIC_NAMES = ["温度", "湿度", "电压"]
METRIC_UNITS = ["°C", "%RH", "V"]

# pymodbus 启动时会把寄存器复制一份自己的内存，改 SimData 里的列表没用。
# 这里借"收到请求"这个回调拿到每台设备正在使用的那份内存，后面才能改值。
#   regs：unitId -> 该从站的寄存器内存
#   held：被手动写入过、冻结不再波动的"扁平编号"（见 flat_index）
#   last：上一秒由波动线程写入的值，用来发现有别人改过
state = {"regs": {}, "held": set(), "last": {}}


def flat_index(unit_id, register):
    """把 (从站号, 寄存器地址) 压成一个 0~8 的编号，方便控制台输入。"""
    return UNIT_IDS.index(unit_id) * REGISTERS_PER_UNIT + register


def flat_to_unit_register(index):
    unit_id = UNIT_IDS[index // REGISTERS_PER_UNIT]
    register = index % REGISTERS_PER_UNIT
    return unit_id, register


def make_action(unit_id):
    """给每台从站单独做一个回调，这样能知道这条请求是哪个机柜的。"""

    async def on_request(func_code, start_address, address, count, registers, values):
        state["regs"][unit_id] = registers
        print(f"  <- unitId {unit_id}：功能码 {func_code}，"
              f"起始地址 {address}，数量 {count}", flush=True)
        return None

    return on_request


def print_table():
    for unit_id in UNIT_IDS:
        regs = state["regs"].get(unit_id)
        print(f"\n  unitId {unit_id}（rack{unit_id}）")
        print("  地址  测点        原始值    实际值")
        print("  " + "-" * 36)
        for register in range(REGISTERS_PER_UNIT):
            index = flat_index(unit_id, register)
            raw = regs[register] if regs is not None else INITIAL_VALUES[unit_id][register]
            frozen = "  [已冻结]" if index in state["held"] else ""
            print(f"   {register}    {METRIC_NAMES[register]:<6}  {raw:>6}   "
                  f"{raw / SCALE:>6.1f} {METRIC_UNITS[register]}{frozen}")
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

    total = len(UNIT_IDS) * REGISTERS_PER_UNIT
    while True:
        line = input("> ").strip()
        if not line:
            continue

        parts = line.split()
        cmd = parts[0].lower()

        if cmd in ("quit", "exit"):
            return
        if cmd == "list":
            print_table()
        elif cmd == "set":
            if len(parts) != 3:
                print("用法：set <编号> <原始值>，例如 set 0 400")
                continue
            try:
                index, value = int(parts[1]), int(parts[2])
            except ValueError:
                print("编号和值都必须是整数")
                continue
            if not 0 <= index < total:
                print(f"编号只能是 0~{total - 1}")
                continue
            if not 0 <= value <= 65535:
                print("寄存器是 16 位无符号整数，值只能是 0~65535")
                continue

            unit_id, register = flat_to_unit_register(index)
            regs = state["regs"].get(unit_id)
            if regs is None:
                print(f"unitId {unit_id} 还没有客户端连上来，等 SensorHub 连上后再改")
                continue
            regs[register] = value
            # 手动设过的点冻结住，否则下一秒就被波动线程改跑了
            state["held"].add(index)
            state["last"][index] = value
            print(f"已设置 unitId {unit_id} 地址 {register}"
                  f"（rack{unit_id} {METRIC_NAMES[register]}）= {value}"
                  f"（实际 {value / SCALE:.1f}{METRIC_UNITS[register]}），该点已冻结")
        elif cmd == "auto":
            state["held"].clear()
            state["last"].clear()
            print("已恢复自动波动")
        else:
            print("未知命令。可用：list / set <编号> <原始值> / auto / quit")


# 下面两组只在自动波动模式下使用：
# 每次改动的最大幅度（寄存器原始值），以及允许的取值范围
DRIFT_STEP = [2, 5, 3]
DRIFT_RANGE = [(180, 320), (400, 650), (2150, 2250)]


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
        for unit_id in UNIT_IDS:
            regs = state["regs"].get(unit_id)
            if regs is None:      # 这台设备还没有客户端读过，先不动
                continue
            for register in range(REGISTERS_PER_UNIT):
                index = flat_index(unit_id, register)
                if index in state["held"]:
                    continue

                previous = state["last"].get(index)
                if previous is not None and regs[register] != previous:
                    state["held"].add(index)   # 有外部写入，冻结它
                    continue

                low, high = DRIFT_RANGE[register]
                step = DRIFT_STEP[register]
                value = max(low, min(high, regs[register] + random.randint(-step, step)))
                regs[register] = value
                state["last"][index] = value


def run_server(devices, host, port):
    """在后台线程里跑 asyncio 事件循环，主线程专心处理控制台输入。"""
    asyncio.run(StartAsyncTcpServer(context=devices, address=(host, port)))


def main(args):
    devices = []
    for unit_id in UNIT_IDS:
        holding = SimData(
            address=0,
            count=REGISTERS_PER_UNIT,
            values=list(INITIAL_VALUES[unit_id]),
            datatype=DataType.REGISTERS,
        )
        # simdata 传列表即可；action 是官方提供的"每收到请求回调一次"的钩子
        devices.append(SimDevice(id=unit_id, simdata=[holding],
                                 action=make_action(unit_id)))

    print(f"Modbus 从站已启动：{args.host}:{args.port}"
          f"（从站号 {UNIT_IDS}，每台 {REGISTERS_PER_UNIT} 个保持寄存器）")
    print(f"SensorHub 连接参数：--modbus 127.0.0.1:{args.port}")

    threading.Thread(target=run_server,
                     args=(devices, args.host, args.port),
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