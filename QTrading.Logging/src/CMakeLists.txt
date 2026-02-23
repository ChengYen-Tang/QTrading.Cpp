add_library (QTrading.Logging.Library STATIC 
  "FileLogger/FeatherV2.cpp"
  "FileLogger/FeatherV2Sink.cpp"
  "SinkLogger.cpp"
 "Logger.cpp"
 "LoggerBootstrap.cpp")

target_include_directories(QTrading.Logging.Library
	PUBLIC
		"${CMAKE_CURRENT_SOURCE_DIR}/../include"
		"${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Utils/include"
		"${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Infra/include"
)

target_link_libraries(QTrading.Logging.Library 
  PUBLIC
    Arrow::arrow_shared
  PRIVATE
    Boost::thread
)
