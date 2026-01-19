project(QTrading.Intent LANGUAGES CXX)

add_library(QTrading.Intent.Library
  src/IntentModule.cpp
  src/BasisIntentBuilder.cpp
)

target_include_directories(QTrading.Intent.Library PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(QTrading.Intent.Library PUBLIC
  QTrading.Signal.Library
  QTrading.Infra.Library
)

set_target_properties(QTrading.Intent.Library PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

add_subdirectory(tests)
