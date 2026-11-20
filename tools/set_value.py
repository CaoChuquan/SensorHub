#!/usr/bin/env python3
# ──────────────────────────────────────────────────────────────────────────────
# set_value.py - 改模拟从站某个寄存器的值（一次性命令，不需要交互式终端）
#
# 为什么需要它：
#   modbus_slave_sim.py 运行后要靠键盘输入命令，但有些编辑器自带的终端只接管
#   程序的输出、不把键盘输入交给程序，于是"打字没反应"。这个脚本用一条命令
#   直接把值写进去，完全不依赖交互式终端。
#
# 原理：
#   它扮演另一个 Modbus 主站，用功能码 06（写单个保持寄存器）直接改设备侧的值。
#   和 modbus_slave_sim.py 必须同时运行。
#
# 编号怎么算：
#   模拟器里 3 台设备各 3 个寄存器，统一编号 0~8：
#     0/1/2 → unitId 1（rack1）的温度/湿度/电压
#     3/4/5 → unitId 2（rack2）
#     6/7/8 → unitId 3（rack3）
#   脚本会自己算出该写哪个从站的第几个寄存器。
#
# 用法：
#   python set_value.py 0 400          # rack1 温度 → 40.0℃
#   python set_value.py 0 245          # 改回去
#   python set_value.py 5 2200 --port 15020
#
# 写入后该点会冻结，不再自动波动（其余点照常变化），
# 这样设一个超限值就能稳定地观察告警；在模拟器控制台输入 auto 可恢复波动。
# ──────────────────────────────────────────────────────────────────────────────

import argparse
import asyncio

from pymodbus.client import AsyncModbusTcpClient

SCALE = 10.0
REGISTERS_PER_UNIT = 3
NAMES = [
    "rack1 温度", "rack1 湿度", "rack1 电压",
    "rack2 温度", "rack2 湿度", "rack2 电压",
    "rack3 温度", "rack3 湿度", "rack3 电压",
]


async def main(args):
    unit_id = args.register // REGISTERS_PER_UNIT + 1
    register = args.register % REGISTERS_PER_UNIT

    client = AsyncModbusTcpClient(args.host, port=args.port)
    await client.connect()
    if not client.connected:
        print(f"连不上 {args.host}:{args.port}，请先启动 modbus_slave_sim.py")
        return

    reply = await client.write_register(register, args.value, device_id=unit_id)
    client.close()

    if reply.isError():
        print("写入失败：", reply)
        return

    name = NAMES[args.register] if 0 <= args.register < len(NAMES) else "?"
    print(f"已设置 {args.register} 号（unitId {unit_id} 地址 {register}，{name}）"
          f"= {args.value}，实际值 {args.value / SCALE:.1f}"
          f"（该点已冻结，不再自动波动）")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="修改 Modbus 模拟从站的寄存器值")
    parser.add_argument("register", type=int, help="统一编号 0~8（见文件头说明）")
    parser.add_argument("value", type=int, help="要写入的原始值（0~65535）")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=502)
    asyncio.run(main(parser.parse_args()))