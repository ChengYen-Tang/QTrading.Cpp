cmake_minimum_required(VERSION 3.21)
project(QTrading.Cpp LANGUAGES CXX)

# C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Trace instrumentation:
# - Zero overhead unless QTRADING_ENABLE_TRACE is ON.
# - Default: ON for Debug/RelWithDebInfo, OFF for Release/MinSizeRel.
set(_qtrading_trace_default OFF)
if(DEFINED CMAKE_BUILD_TYPE)
  string(TOUPPER "${CMAKE_BUILD_TYPE}" _bt)
  if(_bt STREQUAL "DEBUG" OR _bt STREQUAL "RELWITHDEBINFO")
    set(_qtrading_trace_default ON)
  endif()
endif()
option(QTRADING_ENABLE_TRACE "Enable QTRADING_TRACE instrumentation" ${_qtrading_trace_default})
unset(_qtrading_trace_default)
unset(_bt)

# Trace verbosity:
# - When OFF, QTR_TRACE compiles to no-op even if QTRADING_TRACE is enabled.
option(QTRADING_TRACE_VERBOSE "Enable verbose QTR_TRACE output" ON)

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

include(CTest)

# 所有子项目
add_subdirectory(QTrading.Logging)
add_subdirectory(QTrading.Infra)
add_subdirectory(QTrading.Utils)
add_subdirectory(QTrading.Universe)
add_subdirectory(QTrading.Signal)
add_subdirectory(QTrading.Intent)
add_subdirectory(QTrading.Risk)
add_subdirectory(QTrading.Execution)
add_subdirectory(QTrading.Monitoring)
add_subdirectory(QTrading.Service)
