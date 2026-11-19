cmake_minimum_required(VERSION 3.16)

project(SensorHub)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# 凡是含 Q_OBJECT 的头文件都需要 moc 生成代码，AUTOMOC 必须开
set(CMAKE_AUTOMOC ON)
# 生成 <构建目录>/compile_commands.json，供编辑器做 IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 寻找 Qt 与Sql 模块（SQLite 存档）
find_package(Qt6 COMPONENTS Widgets Sql Charts Network QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 COMPONENTS Widgets Sql Charts Network REQUIRED)
    set(QT_LIB_PREFIX Qt5)
else()
    set(QT_LIB_PREFIX Qt6)
endif()

# 编译警告
if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 设置静态库生成在 <构建目录>/lib/
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 核心库（不含任何界面代码）
# 为什么要拆成库：
#   测试程序需要链接业务代码，但不该链接 QWidget。如果业务代码和界面混在同一个
#   可执行目标里，测试要么跑不起来，要么得把整个界面一起链进去。
#   拆成静态库后，测试只链 sensorhub_core，不需要 QApplication
#
# 这一层包含：数据平面、数据源、存储、服务。
# 它只依赖 Qt::Core / Qt::Sql / Qt::Network —— 刻意不依赖 Widgets，
# 这是一种"编译期约束"：以后谁在服务层误 include 了 QWidget，会立刻编译失败
add_library(sensorhub_core STATIC
    # 数据平面
    src/core/DataHub.cpp
    # 数据源
    src/data/ISensorProducer.cpp
    src/data/ModbusProducer.cpp
    src/data/SimulationProducer.cpp
    # 服务
    src/services/AlarmService.cpp
    src/services/AnalysisService.cpp
    src/services/ChartService.cpp
    src/services/SensorArchiveService.cpp
    src/services/WatchlistService.cpp
    # 存储
    src/storage/AlarmRepository.cpp
    src/storage/Database.cpp
    src/storage/SensorRepository.cpp
    src/storage/WatchlistRepository.cpp
)
target_include_directories(sensorhub_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(sensorhub_core PUBLIC
    ${QT_LIB_PREFIX}::Core
    ${QT_LIB_PREFIX}::Sql
    ${QT_LIB_PREFIX}::Network
)

# 应用程序（界面 + 组装）
# 每新增一个界面 .cpp 都要登记在这里（头文件不需要，AUTOMOC 会扫描）
add_executable(SensorHub
    src/main.cpp
    src/ui/AlarmScreen.cpp
    src/ui/AnalysisScreen.cpp
    src/ui/ChartScreen.cpp
    src/ui/MainWindow.cpp
    src/ui/SensorScreen.cpp
    src/ui/WatchlistScreen.cpp
)
target_link_libraries(SensorHub PRIVATE
    sensorhub_core
    ${QT_LIB_PREFIX}::Widgets
    ${QT_LIB_PREFIX}::Charts
)