cmake_minimum_required(VERSION 3.21)
project(QTrading.Cpp LANGUAGES CXX)

# C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC 静态 CRT & 全符号导出
if(MSVC)
#  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# MinGW 全符号导出
if(MINGW)
  add_link_options("-Wl,--export-all-symbols")
endif()

# MSVC 热重载支持
if(POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
    "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,"
    "$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$"
    "<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

# 所有子项目
add_subdirectory(QTrading.Logging)
add_subdirectory(QTrading.Infra)
add_subdirectory(QTrading.Utils)
add_subdirectory(QTrading.DataPreprocess)
add_subdirectory(QTrading.Strategy)
add_subdirectory(QTrading.Service)
