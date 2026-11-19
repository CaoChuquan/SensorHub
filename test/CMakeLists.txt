# 单元测试
# 每个 tst_*.cpp 编译成一个独立的可执行文件，用 add_test 注册到 CTest
# 它们只链接 sensorhub_core（业务代码），不链接 Widgets，也不启动 QApplication

add_executable(tst_alarm tst_alarm.cpp)
target_link_libraries(tst_alarm PRIVATE
    sensorhub_core
    ${QT_LIB_PREFIX}::Test
)
add_test(NAME tst_alarm COMMAND tst_alarm)

add_executable(tst_analysis tst_analysis.cpp)
target_link_libraries(tst_analysis PRIVATE
    sensorhub_core
    ${QT_LIB_PREFIX}::Test
)
add_test(NAME tst_analysis COMMAND tst_analysis)

add_executable(tst_storage tst_storage.cpp)
target_link_libraries(tst_storage PRIVATE
    sensorhub_core
    ${QT_LIB_PREFIX}::Test
)
add_test(NAME tst_storage COMMAND tst_storage)