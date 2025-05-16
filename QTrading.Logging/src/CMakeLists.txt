add_library (QTrading.Logging.Library STATIC 
  FileLogger.cpp
 "Logger.cpp")

target_include_directories(QTrading.Logging.Library
	PUBLIC
		"${CMAKE_CURRENT_SOURCE_DIR}/../include"
		"${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Utils/include"
)

target_link_libraries(QTrading.Logging.Library 
  PRIVATE
    Boost::thread
	Arrow::arrow_shared
)
