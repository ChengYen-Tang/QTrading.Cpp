# CMakeList.txt: QTrading.Service 的 CMake 專案，在此包含來源及定義
# 專案專屬邏輯。
#

# 將來源新增到此專案的可執行檔。
project ("QTrading.Service")
add_executable (QTrading.Service "QTrading.Service.cpp" "QTrading.Service.h")
target_include_directories(QTrading.Strategy.Library PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../include", "${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.DataPreprocess/include", "${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Infra/include", "${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Utils/Strategy")

target_link_libraries(QTrading.DataPreprocess.Tests
    PRIVATE
        QTrading.Infra.Library
        QTrading.DataPreprocess.Library         # 連結到剛剛建立的函式庫
        QTrading.Strategy.Library
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET QTrading.Service PROPERTY CXX_STANDARD 20)
endif()

# TODO: 新增測試，並視需要安裝目標。
