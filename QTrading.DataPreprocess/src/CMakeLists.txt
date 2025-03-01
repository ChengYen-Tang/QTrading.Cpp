SET(SOURCE_FILES
	"Simulator/Binance.cpp"
)

add_library (QTrading.DataPreprocess.Library STATIC ${SOURCE_FILES})
target_include_directories(QTrading.DataPreprocess.Library PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../include", "${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Utils/include", "${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Infra/include")

target_link_libraries(QTrading.DataPreprocess.Library ${Boost_LIBRARIES} Boost::chrono Boost::thread)
