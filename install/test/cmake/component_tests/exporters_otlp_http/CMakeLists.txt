# Copyright The OpenTelemetry Authors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.16)
project(opentelemetry-cpp-exporters-otlp-http-install-test LANGUAGES CXX)

find_package(opentelemetry-cpp REQUIRED COMPONENTS exporters_otlp_http)

if(NOT TARGET protobuf::libprotobuf)
  message(FATAL_ERROR "protobuf::libprotobuf target not found")
endif()

# The protobuf-free exporters ship in this component alongside the protobuf
# ones, so "protobuf::libprotobuf is not defined" cannot be the assertion --
# the component requires it for the protobuf exporters. What is checkable is
# that no protobuf reaches the JSON targets through their exported link
# interface.
#
# This inspects the interface only. A PRIVATE dependency of a shared library is
# not part of it, so in a BUILD_SHARED_LIBS build this proves what a consumer
# links, not what the library itself was built against.
function(otel_assert_link_closure_has_no_protobuf _root)
  set(_pending "${_root}")
  set(_seen "")

  while(_pending)
    list(POP_FRONT _pending _current)

    if(_current IN_LIST _seen)
      continue()
    endif()
    list(APPEND _seen "${_current}")

    if(_current MATCHES "[Pp]rotobuf")
      message(
        FATAL_ERROR
          "${_root} reaches protobuf through ${_current}; the OTLP/JSON exporters must not link it"
      )
    endif()

    # An entry may be a bare target, or a target wrapped in one or more
    # generator expressions. Pull every target-looking name out of it rather
    # than trying to strip the wrappers, which nest.
    set(_candidates "${_current}")
    string(REGEX MATCHALL "[A-Za-z0-9_:.+-]+" _names "${_current}")
    if(_names)
      list(APPEND _candidates ${_names})
    endif()

    foreach(_candidate IN LISTS _candidates)
      if(TARGET ${_candidate})
        get_target_property(_libs ${_candidate} INTERFACE_LINK_LIBRARIES)
        if(_libs)
          list(APPEND _pending ${_libs})
        endif()
      endif()
    endforeach()
  endwhile()
endfunction()

foreach(_json_target
        otlp_json_http_client otlp_json_http_exporter
        otlp_json_http_log_record_exporter otlp_json_http_metric_exporter
        otlp_json_http_exporter_builder otlp_json_http_log_record_exporter_builder
        otlp_json_http_metric_exporter_builder)
  if(NOT TARGET opentelemetry-cpp::${_json_target})
    message(FATAL_ERROR "opentelemetry-cpp::${_json_target} target not found")
  endif()
  otel_assert_link_closure_has_no_protobuf(opentelemetry-cpp::${_json_target})
endforeach()

find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(exporters_otlp_http_test
               ${INSTALL_TEST_SRC_DIR}/test_exporters_otlp_http.cc)
target_link_libraries(
  exporters_otlp_http_test
  PRIVATE opentelemetry-cpp::otlp_http_client
          opentelemetry-cpp::otlp_http_exporter
          opentelemetry-cpp::otlp_http_log_record_exporter
          opentelemetry-cpp::otlp_http_metric_exporter
          opentelemetry-cpp::otlp_http_exporter_builder
          opentelemetry-cpp::otlp_http_log_record_exporter_builder
          opentelemetry-cpp::otlp_http_metric_exporter_builder
          opentelemetry-cpp::otlp_json_http_client
          opentelemetry-cpp::otlp_json_http_exporter
          opentelemetry-cpp::otlp_json_http_log_record_exporter
          opentelemetry-cpp::otlp_json_http_metric_exporter
          opentelemetry-cpp::otlp_json_http_exporter_builder
          opentelemetry-cpp::otlp_json_http_log_record_exporter_builder
          opentelemetry-cpp::otlp_json_http_metric_exporter_builder
          GTest::gtest
          GTest::gtest_main)

gtest_discover_tests(exporters_otlp_http_test)
