# CMakeList.txt: 最上層的 CMake 專案檔，執行全域設定
# 並在這裡包含子專案。
#
cmake_minimum_required (VERSION 3.14)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_DEBUG "/MTd")   # 使用 MTd（靜態調試庫）
set(CMAKE_CXX_FLAGS_RELEASE "/MT")  # 使用 MT（靜態釋放庫）

# 啟用自動導出符號（適用於 MSVC）
if (MSVC)
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# 若使用 MinGW（GCC），則加入 `-Wl,--export-all-symbols`
if(MINGW)
    add_link_options("-Wl,--export-all-symbols")
endif()


# 如果支援，則為 MSVC 編譯器啟用熱重新載入。
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.13.0.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(googletest)

project ("QTrading.Cpp")

# 包含子專案。
#add_subdirectory ("QTrading.Service")
add_subdirectory ("QTrading.Infra")
add_subdirectory ("QTrading.Utils")
