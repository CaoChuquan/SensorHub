# SensorHub

[![CI](https://github.com/CaoChuquan/SensorHub/actions/workflows/ci.yml/badge.svg)](https://github.com/CaoChuquan/SensorHub/actions/workflows/ci.yml)

A desktop monitoring terminal for data-center rooms. It connects the full chain — acquisition, live display, persistence, statistics, visualization, and alarming — into one runnable, testable, and extensible Qt/C++ application.

## Features

- **Live readings**: temperature, humidity, and voltage for three racks, refreshed as data arrives; a banner appears when readings have not updated for more than 3 seconds.
- **Watchlist**: pin the measurements you care about, persisted across restarts.
- **Statistics**: mean, standard deviation, minimum, and maximum over the latest N archived readings, with 3-sigma outlier detection.
- **Time-series chart**: query historical curves by rack and metric with automatically fitted axes.
- **Alarm panel**: rules combine rack, metric, threshold, and direction. A violation creates one alarm instead of repeating on every sample; acknowledging only marks it as seen, and the alarm resolves automatically when the value returns to normal. Restarting the application does not re-raise the same ongoing violation.
- **Two data sources**: a built-in simulator, or a real Modbus TCP slave selected by a single command-line flag.
- **Link status**: the status bar turns red when the Modbus connection drops and recovers after automatic reconnection.

## Tech Stack

| Item | Details |
| --- | --- |
| Language / standard | C++17 |
| Framework | Qt 6 (Widgets, Sql, Charts, Network, Test) |
| Build | CMake + Ninja |
| Storage | SQLite (WAL mode) |
| Protocol | Modbus TCP (hand-written MBAP frames, function code 03) |
| Testing | Qt Test + CTest, GitHub Actions CI |

## Architecture

```
        SimulationProducer        ModbusProducer
          (simulated data)        (TCP acquisition)
                 \                   /
                  \                 /
                   ▼               ▼
                ┌─────────────────────┐
                │       DataHub       │  in-process publish/subscribe
                └──────────┬──────────┘
                           │ publish(SensorReading)
        ┌──────────────────┼──────────────────┐
        ▼                  ▼                  ▼
  WatchlistService   ArchiveService     AlarmService
  AnalysisService    ChartService             │
        └──────────────────┼──────────────────┘
                           ▼
                 ┌───────────────────┐
                 │   Five UI tabs    │
                 └───────────────────┘
```

Design decisions worth noting:

- **A data plane instead of point-to-point calls**: producers publish a reading to `DataHub`, and screens/services subscribe independently, so adding a feature does not require touching the acquisition code.
- **Dependency injection**: `DataHub`, the database, and every service are created in `main.cpp` and passed in; there are no global singletons, and tests can substitute a temporary database.
- **Core library separated from the UI**: business code is built into the `sensorhub_core` static library, which depends only on Qt Core/Sql/Network, never Widgets. Tests link that library without starting a GUI.

## Project Layout

```
src/
  core/        DataHub data plane
  data/        Producer interface, simulator, Modbus client, device config types
  services/    Watchlist, archive, analysis, chart, and alarm logic
  storage/     SQLite connection and repositories
  ui/          Main window and the five feature tabs
test/          Qt Test unit tests
tools/         Modbus slave simulator and a one-shot value writer
```

## Building

Requirements: Qt 6.5 or newer (Widgets, Sql, Charts, Network, Test), CMake 3.16+, and a C++17 compiler (MinGW or MSVC).

```bash
cmake -B build -DCMAKE_PREFIX_PATH=<path-to-Qt>
cmake --build build
```

With Qt Creator, simply open `CMakeLists.txt` and pick a kit.

## Running

**Simulation mode** (no hardware required):

```bash
./build/SensorHub.exe
```

**Modbus mode**:

```bash
./build/SensorHub.exe --modbus 192.168.1.100:502
```

The address accepts either `IP` or `IP:port`; the port defaults to 502.

### Without real hardware

`tools/` contains a pymodbus-based slave simulator that plays the field device while SensorHub acts as the Modbus master:

```bash
pip install pymodbus
python tools/modbus_slave.py                # listens on 0.0.0.0:502, values drift over time
python tools/modbus_slave.py --port 15020   # use another port if 502 is taken
```

Then, in a second terminal:

```bash
./build/SensorHub.exe --modbus 127.0.0.1:502
```

The register map matches the application: registers 0/1/2 hold temperature, humidity, and voltage for rack1, 3/4/5 for rack2, and 6/7/8 for rack3. Register values are divided by 10 to obtain the real measurement.

To trigger an alarm, write an out-of-range value to one register:

```bash
python tools/set_value.py 0 400      # register 0 = 400 → rack1 temperature 40.0
python tools/set_value.py 0 245      # restore a normal value
```

A register written this way stays frozen at that value so the alarm can be observed steadily; other registers keep drifting.

## Data Storage

The database lives next to the executable at `data/sensorhub.db` and contains four tables:

| Table | Purpose |
| --- | --- |
| `sensor_readings` | Historical readings, indexed by rack and time |
| `watchlist` | Pinned measurements |
| `alarm_rules` | Alarm rules |
| `alarm_events` | Alarm records and handling state |

Deleting the file clears all history; the schema is recreated on the next start.

## Testing

```bash
ctest --test-dir build --output-on-failure
```

There are four test targets covering the alarm state machine, statistics, storage repositories, and device-config parsing. Tests use an in-memory database and never touch development data.

CI installs Qt on a Windows runner, builds the project, and runs the full test suite; see the badge at the top of this page.

## License

[Mulan PSL v2](LICENSE)
