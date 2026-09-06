# Copyright The OpenTelemetry Authors
# SPDX-License-Identifier: Apache-2.0

include(${PROJECT_SOURCE_DIR}/cmake/pkgconfig.cmake)

if(protobuf_lib_type STREQUAL "STATIC_LIBRARY")
  set(OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE STATIC)
else()
  set(OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE)
endif()

#
# opentelemetry_otlp_common
#

add_library(opentelemetry_otlp_common ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE}
                                      src/otlp_environment.cc)
set_target_properties(opentelemetry_otlp_common PROPERTIES EXPORT_NAME
                                                           otlp_common)
set_target_version(opentelemetry_otlp_common)

target_include_directories(
  opentelemetry_otlp_common
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
         "$<INSTALL_INTERFACE:include>")

target_link_libraries(opentelemetry_otlp_common PUBLIC opentelemetry_common)

set(OPENTELEMETRY_OTLP_TARGETS opentelemetry_otlp_common)

#
# opentelemetry_otlp_recordable
#

add_library(
  opentelemetry_otlp_recordable
  ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_log_recordable.cc
  src/otlp_recordable.cc src/otlp_populate_attribute_utils.cc
  src/otlp_recordable_utils.cc src/otlp_metric_utils.cc)
set_target_properties(opentelemetry_otlp_recordable PROPERTIES EXPORT_NAME
                                                               otlp_recordable)
set_target_version(opentelemetry_otlp_recordable)

target_include_directories(
  opentelemetry_otlp_recordable
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
         "$<INSTALL_INTERFACE:include>")

list(APPEND OPENTELEMETRY_OTLP_TARGETS opentelemetry_otlp_recordable)

target_link_libraries(opentelemetry_otlp_recordable
                      PUBLIC opentelemetry_logs opentelemetry_metrics)

if(OTELCPP_WITH_OTLP_UTF8_VALIDITY AND TARGET utf8_range::utf8_validity)
  target_link_libraries(opentelemetry_otlp_recordable
                        PRIVATE utf8_range::utf8_validity)
  target_compile_definitions(opentelemetry_otlp_recordable
                             PRIVATE ENABLE_OTLP_UTF8_VALIDITY)
endif()

#
# opentelemetry_exporter_otlp_builder_utils
#

add_library(opentelemetry_exporter_otlp_builder_utils
            ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_builder_utils.cc)

set_target_properties(opentelemetry_exporter_otlp_builder_utils
                      PROPERTIES EXPORT_NAME otlp_builder_utils)
set_target_version(opentelemetry_exporter_otlp_builder_utils)

target_include_directories(
  opentelemetry_exporter_otlp_builder_utils
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
         "$<INSTALL_INTERFACE:include>")

# otlp_common rather than otlp_recordable: this target reads configuration
# models and environment defaults and touches no recordable, and depending on
# the protobuf one would pull the message runtime into every builder that links
# it -- including the protobuf-free OTLP/JSON builders, whose whole point is
# not to have it.
target_link_libraries(
  opentelemetry_exporter_otlp_builder_utils
  PUBLIC opentelemetry_otlp_common opentelemetry_configuration_core)

if(OTELCPP_INSTALL)
  opentelemetry_add_pkgconfig(
    otlp_common "OpenTelemetry OTLP - Common"
    "Common environment variable reading for OTLP exporters."
    "opentelemetry_common")

  opentelemetry_add_pkgconfig(
    otlp_recordable
    "OpenTelemetry OTLP - Recordable"
    "OTLP recordable implementation for spans, metrics, and logs."
    "opentelemetry_trace opentelemetry_metrics opentelemetry_logs opentelemetry_resources opentelemetry_proto"
  )

  opentelemetry_add_pkgconfig(
    exporter_otlp_builder_utils "OpenTelemetry OTLP - Builder Utils"
    "Common utilities for OTLP exporter builders." "opentelemetry_otlp_common")
endif()

#
# opentelemetry_exporter_otlp_json_writer
#

add_library(opentelemetry_exporter_otlp_json_writer INTERFACE)

set_target_properties(opentelemetry_exporter_otlp_json_writer
                      PROPERTIES EXPORT_NAME otlp_json_writer)

target_include_directories(
  opentelemetry_exporter_otlp_json_writer
  INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
            "$<INSTALL_INTERFACE:include>")

target_link_libraries(opentelemetry_exporter_otlp_json_writer
                      INTERFACE opentelemetry_common)

set(OPENTELEMETRY_OTLP_JSON_WRITER_TARGETS
    opentelemetry_exporter_otlp_json_writer)

if(OTELCPP_WITH_JSON_WRITER_NLOHMANN)
  add_library(
    opentelemetry_exporter_otlp_json_writer_nlohmann
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_json_writer_nlohmann.cc)

  set_target_properties(opentelemetry_exporter_otlp_json_writer_nlohmann
                        PROPERTIES EXPORT_NAME otlp_json_writer_nlohmann)
  set_target_version(opentelemetry_exporter_otlp_json_writer_nlohmann)

  target_include_directories(
    opentelemetry_exporter_otlp_json_writer_nlohmann
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
           "$<INSTALL_INTERFACE:include>")

  target_link_libraries(
    opentelemetry_exporter_otlp_json_writer_nlohmann
    PUBLIC opentelemetry_common
    PRIVATE opentelemetry_sdk "$<BUILD_INTERFACE:nlohmann_json::nlohmann_json>")

  target_compile_definitions(opentelemetry_exporter_otlp_json_writer_nlohmann
                             PUBLIC ENABLE_JSON_WRITER_NLOHMANN)

  target_link_libraries(
    opentelemetry_exporter_otlp_json_writer
    INTERFACE opentelemetry_exporter_otlp_json_writer_nlohmann)

  list(APPEND OPENTELEMETRY_OTLP_JSON_WRITER_TARGETS
       opentelemetry_exporter_otlp_json_writer_nlohmann)
endif()

if(OTELCPP_INSTALL)
  opentelemetry_add_pkgconfig(
    exporter_otlp_json_writer "OpenTelemetry OTLP - JSON Writer"
    "Token-stream JSON writer interface." "opentelemetry_common")

  if(OTELCPP_WITH_JSON_WRITER_NLOHMANN)
    opentelemetry_add_pkgconfig(
      exporter_otlp_json_writer_nlohmann
      "OpenTelemetry OTLP - JSON Writer (nlohmann backend)"
      "The nlohmann-json backed default implementation of the JsonWriter interface."
      "opentelemetry_common")
  endif()
endif()

#
# opentelemetry_exporter_otlp_json_reader
#
# The reader half of the JSON seam, laid out exactly like the writer: a
# header-only facade plus a selectable backend. It rides
# OTELCPP_WITH_JSON_WRITER_NLOHMANN rather than an option of its own,
# because a separate option would let a consumer exclude the writer backend
# and still pull nlohmann in through the reader.
#

add_library(opentelemetry_exporter_otlp_json_reader INTERFACE)

set_target_properties(opentelemetry_exporter_otlp_json_reader
                      PROPERTIES EXPORT_NAME otlp_json_reader)

target_include_directories(
  opentelemetry_exporter_otlp_json_reader
  INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
            "$<INSTALL_INTERFACE:include>")

target_link_libraries(opentelemetry_exporter_otlp_json_reader
                      INTERFACE opentelemetry_common)

set(OPENTELEMETRY_OTLP_JSON_READER_TARGETS
    opentelemetry_exporter_otlp_json_reader)

if(OTELCPP_WITH_JSON_WRITER_NLOHMANN)
  add_library(
    opentelemetry_exporter_otlp_json_reader_nlohmann
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_json_reader_nlohmann.cc)

  set_target_properties(opentelemetry_exporter_otlp_json_reader_nlohmann
                        PROPERTIES EXPORT_NAME otlp_json_reader_nlohmann)
  set_target_version(opentelemetry_exporter_otlp_json_reader_nlohmann)

  target_include_directories(
    opentelemetry_exporter_otlp_json_reader_nlohmann
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
           "$<INSTALL_INTERFACE:include>")

  target_link_libraries(
    opentelemetry_exporter_otlp_json_reader_nlohmann
    PUBLIC opentelemetry_common
    PRIVATE opentelemetry_sdk "$<BUILD_INTERFACE:nlohmann_json::nlohmann_json>")

  target_compile_definitions(opentelemetry_exporter_otlp_json_reader_nlohmann
                             PUBLIC ENABLE_JSON_READER_NLOHMANN)

  target_link_libraries(
    opentelemetry_exporter_otlp_json_reader
    INTERFACE opentelemetry_exporter_otlp_json_reader_nlohmann)

  list(APPEND OPENTELEMETRY_OTLP_JSON_READER_TARGETS
       opentelemetry_exporter_otlp_json_reader_nlohmann)
endif()

if(OTELCPP_INSTALL)
  opentelemetry_add_pkgconfig(
    exporter_otlp_json_reader "OpenTelemetry OTLP - JSON Reader"
    "Document-shaped JSON reader interface, with an nlohmann-backed default implementation."
    "opentelemetry_common")

  if(OTELCPP_WITH_JSON_WRITER_NLOHMANN)
    opentelemetry_add_pkgconfig(
      exporter_otlp_json_reader_nlohmann
      "OpenTelemetry OTLP - JSON Reader (nlohmann backend)"
      "The nlohmann-json backed default implementation of the JsonReader interface."
      "opentelemetry_common")
  endif()
endif()

#
# opentelemetry_exporter_otlp_json_partial_success
#
# The OTLP-aware half of the read side: what a partial-success response
# means, kept out of the reader so that stays a generic JSON accessor, the
# same split the mapping keeps from the writer.
#

add_library(opentelemetry_exporter_otlp_json_partial_success
            ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_json_partial_success.cc)

set_target_properties(opentelemetry_exporter_otlp_json_partial_success
                      PROPERTIES EXPORT_NAME otlp_json_partial_success)
set_target_version(opentelemetry_exporter_otlp_json_partial_success)

target_include_directories(
  opentelemetry_exporter_otlp_json_partial_success
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
         "$<INSTALL_INTERFACE:include>")

target_link_libraries(
  opentelemetry_exporter_otlp_json_partial_success
  PUBLIC opentelemetry_common opentelemetry_exporter_otlp_json_reader
  PRIVATE opentelemetry_sdk)

if(OTELCPP_INSTALL)
  opentelemetry_add_pkgconfig(
    exporter_otlp_json_partial_success "OpenTelemetry OTLP - JSON Partial Success"
    "Protobuf-free parsing of OTLP/JSON export responses."
    "opentelemetry_common opentelemetry_exporter_otlp_json_reader")
endif()

#
# opentelemetry_exporter_otlp_json_mapping
#
# Maps SDK data to OTLP/JSON without protobuf. Deliberately depends on the
# writer facade and the SDK only, so that a build with no OTLP protobuf
# exporter still gets it, and so that a change here cannot pull protobuf back
# into a consumer that has shed it.
#

add_library(
  opentelemetry_exporter_otlp_json_mapping
  ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE}
  src/otlp_json_log_mapping.cc
  src/otlp_json_log_recordable.cc
  src/otlp_json_mapping.cc
  src/otlp_json_metric_mapping.cc
  src/otlp_json_span_recordable.cc
  src/otlp_json_trace_mapping.cc)

set_target_properties(opentelemetry_exporter_otlp_json_mapping
                      PROPERTIES EXPORT_NAME otlp_json_mapping)
set_target_version(opentelemetry_exporter_otlp_json_mapping)

target_include_directories(
  opentelemetry_exporter_otlp_json_mapping
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
         "$<INSTALL_INTERFACE:include>")

target_link_libraries(
  opentelemetry_exporter_otlp_json_mapping
  PUBLIC opentelemetry_trace opentelemetry_metrics opentelemetry_logs
         opentelemetry_resources opentelemetry_common
         opentelemetry_exporter_otlp_json_writer)

if(OTELCPP_WITH_OTLP_UTF8_VALIDITY AND TARGET utf8_range::utf8_validity)
  target_link_libraries(opentelemetry_exporter_otlp_json_mapping
                        PRIVATE utf8_range::utf8_validity)
  target_compile_definitions(opentelemetry_exporter_otlp_json_mapping
                             PRIVATE ENABLE_OTLP_UTF8_VALIDITY)
endif()

if(OTELCPP_INSTALL)
  opentelemetry_add_pkgconfig(
    exporter_otlp_json_mapping "OpenTelemetry OTLP - JSON Mapping"
    "Protobuf-free mapping from SDK data to OTLP/JSON."
    "opentelemetry_trace opentelemetry_metrics opentelemetry_logs opentelemetry_resources opentelemetry_common opentelemetry_exporter_otlp_json_writer"
  )
endif()

#
# opentelemetry_exporter_otlp_json_converter
#

add_library(opentelemetry_exporter_otlp_json_converter
            ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_json_converter.cc)

set_target_properties(opentelemetry_exporter_otlp_json_converter
                      PROPERTIES EXPORT_NAME otlp_json_converter)
set_target_version(opentelemetry_exporter_otlp_json_converter)

target_include_directories(
  opentelemetry_exporter_otlp_json_converter
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
         "$<INSTALL_INTERFACE:include>")

target_link_libraries(
  opentelemetry_exporter_otlp_json_converter
  PUBLIC opentelemetry_sdk opentelemetry_common
         opentelemetry_exporter_otlp_json_writer
         "$<BUILD_INTERFACE:opentelemetry_proto>"
  PRIVATE "$<INSTALL_INTERFACE:opentelemetry_proto>")

if(OTELCPP_INSTALL)
  opentelemetry_add_pkgconfig(
    exporter_otlp_json_converter
    "OpenTelemetry OTLP - JSON Converter"
    "Shared reflection-based protobuf-to-JSON converter for OTLP/JSON exporters."
    "opentelemetry_sdk opentelemetry_common opentelemetry_exporter_otlp_json_writer"
  )
endif()

if(OTELCPP_WITH_OTLP_GRPC)
  if(NOT DEFINED grpc_lib_type)
    message(
      FATAL_ERROR "cmake/opentelemetry-proto.cmake should be included first")
  endif()
  if(grpc_lib_type STREQUAL "STATIC_LIBRARY" OR protobuf_lib_type STREQUAL
                                                "STATIC_LIBRARY")
    set(OPENTELEMETRY_OTLP_GRPC_CLIENT_LIB_TYPE STATIC)
  else()
    set(OPENTELEMETRY_OTLP_GRPC_CLIENT_LIB_TYPE)
  endif()

  #
  # opentelemetry_exporter_otlp_grpc_client
  #

  add_library(
    opentelemetry_exporter_otlp_grpc_client
    ${OPENTELEMETRY_OTLP_GRPC_CLIENT_LIB_TYPE} src/otlp_grpc_client.cc
    src/otlp_grpc_client_factory.cc src/otlp_grpc_utils.cc)
  set_target_properties(opentelemetry_exporter_otlp_grpc_client
                        PROPERTIES EXPORT_NAME otlp_grpc_client)
  set_target_version(opentelemetry_exporter_otlp_grpc_client)

  # Because opentelemetry_proto_grpc depends on gRPC, and gRPC can only be
  # linked in one dynamic library or executable (otherwise, there may be
  # conflicts with some global variables in certain versions of gRPC), we have
  # switched the link for opentelemetry_exporter_otlp_grpc_client into all
  # targets that depend on opentelemetry_proto_grpc.
  target_link_libraries(
    opentelemetry_exporter_otlp_grpc_client
    PUBLIC opentelemetry_common opentelemetry_otlp_common
           # gRPC::grpc++ must be linked before opentelemetry_proto_grpc.
           "$<BUILD_INTERFACE:opentelemetry_proto_grpc>"
    PRIVATE "$<INSTALL_INTERFACE:opentelemetry_proto_grpc>" gRPC::grpc++
            opentelemetry_ext)

  get_target_property(GRPC_INCLUDE_DIRECTORY gRPC::grpc++
                      INTERFACE_INCLUDE_DIRECTORIES)
  if(GRPC_INCLUDE_DIRECTORY)
    target_include_directories(
      opentelemetry_exporter_otlp_grpc_client BEFORE
      PUBLIC "$<BUILD_INTERFACE:${GRPC_INCLUDE_DIRECTORY}>")
  endif()
  target_include_directories(
    opentelemetry_exporter_otlp_grpc_client
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
           "$<INSTALL_INTERFACE:include>")

  list(APPEND OPENTELEMETRY_OTLP_GRPC_TARGETS
       opentelemetry_exporter_otlp_grpc_client opentelemetry_proto_grpc)

  #
  # opentelemetry_exporter_otlp_grpc
  #

  add_library(
    opentelemetry_exporter_otlp_grpc
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_grpc_exporter.cc
    src/otlp_grpc_exporter_factory.cc src/otlp_grpc_exporter_options.cc)

  set_target_properties(opentelemetry_exporter_otlp_grpc
                        PROPERTIES EXPORT_NAME otlp_grpc_exporter)
  set_target_version(opentelemetry_exporter_otlp_grpc)

  target_link_libraries(
    opentelemetry_exporter_otlp_grpc
    PUBLIC opentelemetry_otlp_common opentelemetry_otlp_recordable
           opentelemetry_exporter_otlp_grpc_client)

  list(APPEND OPENTELEMETRY_OTLP_GRPC_TARGETS opentelemetry_exporter_otlp_grpc)

  #
  # opentelemetry_exporter_otlp_grpc_builder
  #

  add_library(
    opentelemetry_exporter_otlp_grpc_builder
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_grpc_span_builder.cc)

  set_target_properties(opentelemetry_exporter_otlp_grpc_builder
                        PROPERTIES EXPORT_NAME otlp_grpc_exporter_builder)
  set_target_version(opentelemetry_exporter_otlp_grpc_builder)

  target_include_directories(
    opentelemetry_exporter_otlp_grpc_builder
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
           "$<INSTALL_INTERFACE:include>")

  target_link_libraries(
    opentelemetry_exporter_otlp_grpc_builder
    PUBLIC opentelemetry_configuration_core opentelemetry_trace
    PRIVATE opentelemetry_exporter_otlp_grpc
            opentelemetry_exporter_otlp_builder_utils)

  list(APPEND OPENTELEMETRY_OTLP_GRPC_BUILDER_TARGETS
       opentelemetry_exporter_otlp_grpc_builder)

  #
  # opentelemetry_exporter_otlp_grpc_log
  #

  add_library(
    opentelemetry_exporter_otlp_grpc_log
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_grpc_log_record_exporter.cc
    src/otlp_grpc_log_record_exporter_factory.cc
    src/otlp_grpc_log_record_exporter_options.cc)

  set_target_properties(opentelemetry_exporter_otlp_grpc_log
                        PROPERTIES EXPORT_NAME otlp_grpc_log_record_exporter)
  set_target_version(opentelemetry_exporter_otlp_grpc_log)

  target_link_libraries(
    opentelemetry_exporter_otlp_grpc_log
    PUBLIC opentelemetry_otlp_common opentelemetry_otlp_recordable
           opentelemetry_exporter_otlp_grpc_client)

  list(APPEND OPENTELEMETRY_OTLP_GRPC_TARGETS
       opentelemetry_exporter_otlp_grpc_log)

  #
  # opentelemetry_exporter_otlp_grpc_log_builder
  #

  add_library(
    opentelemetry_exporter_otlp_grpc_log_builder
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_grpc_log_record_builder.cc)

  set_target_properties(
    opentelemetry_exporter_otlp_grpc_log_builder
    PROPERTIES EXPORT_NAME otlp_grpc_log_record_exporter_builder)
  set_target_version(opentelemetry_exporter_otlp_grpc_log_builder)

  target_include_directories(
    opentelemetry_exporter_otlp_grpc_log_builder
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
           "$<INSTALL_INTERFACE:include>")

  target_link_libraries(
    opentelemetry_exporter_otlp_grpc_log_builder
    PUBLIC opentelemetry_configuration_core opentelemetry_logs
    PRIVATE opentelemetry_exporter_otlp_grpc_log
            opentelemetry_exporter_otlp_builder_utils)

  list(APPEND OPENTELEMETRY_OTLP_GRPC_BUILDER_TARGETS
       opentelemetry_exporter_otlp_grpc_log_builder)

  #
  # opentelemetry_exporter_otlp_grpc_metrics
  #

  add_library(
    opentelemetry_exporter_otlp_grpc_metrics
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_grpc_metric_exporter.cc
    src/otlp_grpc_metric_exporter_factory.cc
    src/otlp_grpc_metric_exporter_options.cc)

  set_target_properties(opentelemetry_exporter_otlp_grpc_metrics
                        PROPERTIES EXPORT_NAME otlp_grpc_metrics_exporter)
  set_target_version(opentelemetry_exporter_otlp_grpc_metrics)

  target_link_libraries(
    opentelemetry_exporter_otlp_grpc_metrics
    PUBLIC opentelemetry_otlp_common opentelemetry_otlp_recordable
           opentelemetry_exporter_otlp_grpc_client)

  list(APPEND OPENTELEMETRY_OTLP_GRPC_TARGETS
       opentelemetry_exporter_otlp_grpc_metrics)

  #
  # opentelemetry_exporter_otlp_grpc_metric_builder
  #

  add_library(
    opentelemetry_exporter_otlp_grpc_metric_builder
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_grpc_push_metric_builder.cc)

  set_target_properties(
    opentelemetry_exporter_otlp_grpc_metric_builder
    PROPERTIES EXPORT_NAME otlp_grpc_metric_exporter_builder)
  set_target_version(opentelemetry_exporter_otlp_grpc_metric_builder)

  target_include_directories(
    opentelemetry_exporter_otlp_grpc_metric_builder
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
           "$<INSTALL_INTERFACE:include>")

  target_link_libraries(
    opentelemetry_exporter_otlp_grpc_metric_builder
    PUBLIC opentelemetry_configuration_core opentelemetry_metrics
    PRIVATE opentelemetry_exporter_otlp_grpc_metrics
            opentelemetry_exporter_otlp_builder_utils)

  list(APPEND OPENTELEMETRY_OTLP_GRPC_BUILDER_TARGETS
       opentelemetry_exporter_otlp_grpc_metric_builder)

  if(OTELCPP_INSTALL)
    opentelemetry_add_pkgconfig(
      exporter_otlp_grpc_client "OpenTelemetry OTLP - gRPC Client"
      "gRPC client for OTLP protocol." "opentelemetry_api opentelemetry_common")

    opentelemetry_add_pkgconfig(
      exporter_otlp_grpc
      "OpenTelemetry OTLP - gRPC Span Exporter"
      "Exports trace spans via OTLP gRPC."
      "opentelemetry_otlp_common opentelemetry_otlp_recordable opentelemetry_exporter_otlp_grpc_client"
    )

    opentelemetry_add_pkgconfig(
      exporter_otlp_grpc_builder
      "OpenTelemetry OTLP - gRPC Span Builder"
      "Builder for OTLP gRPC span exporter."
      "opentelemetry_exporter_otlp_grpc opentelemetry_exporter_otlp_builder_utils opentelemetry_configuration_core"
    )

    opentelemetry_add_pkgconfig(
      exporter_otlp_grpc_log
      "OpenTelemetry OTLP - gRPC Log Exporter"
      "Exports log records via OTLP gRPC."
      "opentelemetry_otlp_common opentelemetry_otlp_recordable opentelemetry_exporter_otlp_grpc_client"
    )

    opentelemetry_add_pkgconfig(
      exporter_otlp_grpc_log_builder
      "OpenTelemetry OTLP - gRPC Log Builder"
      "Builder for OTLP gRPC log record exporter."
      "opentelemetry_exporter_otlp_grpc_log opentelemetry_exporter_otlp_builder_utils opentelemetry_configuration_core"
    )

    opentelemetry_add_pkgconfig(
      exporter_otlp_grpc_metrics
      "OpenTelemetry OTLP - gRPC Metric Exporter"
      "Exports metrics via OTLP gRPC."
      "opentelemetry_otlp_common opentelemetry_otlp_recordable opentelemetry_exporter_otlp_grpc_client"
    )

    opentelemetry_add_pkgconfig(
      exporter_otlp_grpc_metric_builder
      "OpenTelemetry OTLP - gRPC Metric Builder"
      "Builder for OTLP gRPC metric exporter."
      "opentelemetry_exporter_otlp_grpc_metrics opentelemetry_exporter_otlp_builder_utils opentelemetry_configuration_core"
    )
  endif()

endif()

if(OTELCPP_WITH_OTLP_HTTP)

  #
  # opentelemetry_exporter_otlp_http_transport
  #
  # The OTLP/HTTP send path, with no protobuf on either end. Deliberately does
  # not link the message runtime, so that "does this exporter need protobuf?"
  # is answerable by reading this file.
  #

  add_library(
    opentelemetry_exporter_otlp_http_transport
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_http_transport.cc)
  set_target_properties(opentelemetry_exporter_otlp_http_transport
                        PROPERTIES EXPORT_NAME otlp_http_transport)
  set_target_version(opentelemetry_exporter_otlp_http_transport)

  target_link_libraries(
    opentelemetry_exporter_otlp_http_transport
    PUBLIC opentelemetry_ext opentelemetry_http_client
           opentelemetry_exporter_otlp_http_options
    PRIVATE opentelemetry_common)

  target_include_directories(
    opentelemetry_exporter_otlp_http_transport
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
           "$<INSTALL_INTERFACE:include>")

  list(APPEND OPENTELEMETRY_OTLP_HTTP_TARGETS
       opentelemetry_exporter_otlp_http_transport)

  #
  # opentelemetry_exporter_otlp_http_client
  #

  add_library(opentelemetry_exporter_otlp_http_client
              ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_http_client.cc)
  set_target_properties(opentelemetry_exporter_otlp_http_client
                        PROPERTIES EXPORT_NAME otlp_http_client)
  set_target_version(opentelemetry_exporter_otlp_http_client)

  target_link_libraries(
    opentelemetry_exporter_otlp_http_client
    PUBLIC opentelemetry_ext "$<BUILD_INTERFACE:opentelemetry_proto>"
    PRIVATE "$<INSTALL_INTERFACE:opentelemetry_proto>")

  target_link_libraries(
    opentelemetry_exporter_otlp_http_client
    PUBLIC opentelemetry_http_client opentelemetry_exporter_otlp_http_transport
    PRIVATE opentelemetry_common opentelemetry_exporter_otlp_json_converter)

  target_include_directories(
    opentelemetry_exporter_otlp_http_client
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
           "$<INSTALL_INTERFACE:include>")

  list(APPEND OPENTELEMETRY_OTLP_HTTP_TARGETS
       opentelemetry_exporter_otlp_http_client)

  #
  # opentelemetry_exporter_otlp_http_options
  #
  # The three exporter option structs. Their own target because both the
  # protobuf and the protobuf-free exporter of a signal need them, and
  # compiling them into each would put the same symbols in a binary twice.
  #

  add_library(
    opentelemetry_exporter_otlp_http_options
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_http.cc
    src/otlp_http_exporter_options.cc src/otlp_http_log_record_exporter_options.cc
    src/otlp_http_metric_exporter_options.cc)

  set_target_properties(opentelemetry_exporter_otlp_http_options
                        PROPERTIES EXPORT_NAME otlp_http_exporter_options)
  set_target_version(opentelemetry_exporter_otlp_http_options)

  target_include_directories(
    opentelemetry_exporter_otlp_http_options
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
           "$<INSTALL_INTERFACE:include>")

  target_link_libraries(opentelemetry_exporter_otlp_http_options
                        PUBLIC opentelemetry_otlp_common opentelemetry_common)

  list(APPEND OPENTELEMETRY_OTLP_HTTP_TARGETS
       opentelemetry_exporter_otlp_http_options)

  #
  # opentelemetry_exporter_otlp_json_http_client
  #
  # What the three OTLP/JSON signal exporters share once their own data is
  # encoded, in the position opentelemetry_exporter_otlp_http_client holds for
  # the protobuf ones -- and, like the transport it sits on, linking no
  # protobuf.
  #

  add_library(opentelemetry_exporter_otlp_json_http_client
              ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_json_http_send.cc)

  set_target_properties(opentelemetry_exporter_otlp_json_http_client
                        PROPERTIES EXPORT_NAME otlp_json_http_client)
  set_target_version(opentelemetry_exporter_otlp_json_http_client)

  target_link_libraries(
    opentelemetry_exporter_otlp_json_http_client
    PUBLIC opentelemetry_exporter_otlp_http_transport
           opentelemetry_exporter_otlp_json_partial_success
           opentelemetry_exporter_otlp_json_reader)

  list(APPEND OPENTELEMETRY_OTLP_HTTP_TARGETS
       opentelemetry_exporter_otlp_json_http_client)

  #
  # opentelemetry_exporter_otlp_json_http
  #
  # One target per signal, as the protobuf exporters have, so that a consumer
  # exporting only traces does not link the metric and log mappings too. That
  # is the whole point on a device with no room for the protobuf runtime.
  #

  add_library(opentelemetry_exporter_otlp_json_http
              ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_json_http_exporter.cc
              src/otlp_json_http_exporter_factory.cc)

  set_target_properties(opentelemetry_exporter_otlp_json_http
                        PROPERTIES EXPORT_NAME otlp_json_http_exporter)
  set_target_version(opentelemetry_exporter_otlp_json_http)

  target_link_libraries(
    opentelemetry_exporter_otlp_json_http
    PUBLIC opentelemetry_exporter_otlp_http_options
           opentelemetry_exporter_otlp_json_http_client
           opentelemetry_exporter_otlp_json_mapping)

  list(APPEND OPENTELEMETRY_OTLP_HTTP_TARGETS opentelemetry_exporter_otlp_json_http)

  #
  # opentelemetry_exporter_otlp_json_http_log
  #

  add_library(
    opentelemetry_exporter_otlp_json_http_log
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_json_http_log_record_exporter.cc
    src/otlp_json_http_log_record_exporter_factory.cc)

  set_target_properties(
    opentelemetry_exporter_otlp_json_http_log
    PROPERTIES EXPORT_NAME otlp_json_http_log_record_exporter)
  set_target_version(opentelemetry_exporter_otlp_json_http_log)

  target_link_libraries(
    opentelemetry_exporter_otlp_json_http_log
    PUBLIC opentelemetry_exporter_otlp_http_options
           opentelemetry_exporter_otlp_json_http_client
           opentelemetry_exporter_otlp_json_mapping)

  list(APPEND OPENTELEMETRY_OTLP_HTTP_TARGETS
       opentelemetry_exporter_otlp_json_http_log)

  #
  # opentelemetry_exporter_otlp_json_http_metric
  #

  add_library(
    opentelemetry_exporter_otlp_json_http_metric
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_json_http_metric_exporter.cc
    src/otlp_json_http_metric_exporter_factory.cc)

  set_target_properties(opentelemetry_exporter_otlp_json_http_metric
                        PROPERTIES EXPORT_NAME otlp_json_http_metric_exporter)
  set_target_version(opentelemetry_exporter_otlp_json_http_metric)

  target_link_libraries(
    opentelemetry_exporter_otlp_json_http_metric
    PUBLIC opentelemetry_exporter_otlp_http_options
           opentelemetry_exporter_otlp_json_http_client
           opentelemetry_exporter_otlp_json_mapping)

  list(APPEND OPENTELEMETRY_OTLP_HTTP_TARGETS
       opentelemetry_exporter_otlp_json_http_metric)

  #
  # opentelemetry_exporter_otlp_json_http_builder
  #
  # The file-configuration path for the protobuf-free exporters. An
  # application registers these instead of the protobuf builders, so they are
  # separate targets rather than sources of the exporter libraries: linking an
  # exporter must not decide which builder fills the registry slot.
  #

  add_library(opentelemetry_exporter_otlp_json_http_builder
              ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_json_http_span_builder.cc)

  set_target_properties(opentelemetry_exporter_otlp_json_http_builder
                        PROPERTIES EXPORT_NAME otlp_json_http_exporter_builder)
  set_target_version(opentelemetry_exporter_otlp_json_http_builder)

  target_link_libraries(
    opentelemetry_exporter_otlp_json_http_builder
    PUBLIC opentelemetry_exporter_otlp_json_http
    PRIVATE opentelemetry_exporter_otlp_builder_utils)

  list(APPEND OPENTELEMETRY_OTLP_HTTP_BUILDER_TARGETS
       opentelemetry_exporter_otlp_json_http_builder)

  #
  # opentelemetry_exporter_otlp_json_http_log_builder
  #

  add_library(
    opentelemetry_exporter_otlp_json_http_log_builder
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_json_http_log_record_builder.cc)

  set_target_properties(
    opentelemetry_exporter_otlp_json_http_log_builder
    PROPERTIES EXPORT_NAME otlp_json_http_log_record_exporter_builder)
  set_target_version(opentelemetry_exporter_otlp_json_http_log_builder)

  target_link_libraries(
    opentelemetry_exporter_otlp_json_http_log_builder
    PUBLIC opentelemetry_exporter_otlp_json_http_log
    PRIVATE opentelemetry_exporter_otlp_builder_utils)

  list(APPEND OPENTELEMETRY_OTLP_HTTP_BUILDER_TARGETS
       opentelemetry_exporter_otlp_json_http_log_builder)

  #
  # opentelemetry_exporter_otlp_json_http_metric_builder
  #

  add_library(
    opentelemetry_exporter_otlp_json_http_metric_builder
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_json_http_push_metric_builder.cc)

  set_target_properties(
    opentelemetry_exporter_otlp_json_http_metric_builder
    PROPERTIES EXPORT_NAME otlp_json_http_metric_exporter_builder)
  set_target_version(opentelemetry_exporter_otlp_json_http_metric_builder)

  target_link_libraries(
    opentelemetry_exporter_otlp_json_http_metric_builder
    PUBLIC opentelemetry_exporter_otlp_json_http_metric
    PRIVATE opentelemetry_exporter_otlp_builder_utils)

  list(APPEND OPENTELEMETRY_OTLP_HTTP_BUILDER_TARGETS
       opentelemetry_exporter_otlp_json_http_metric_builder)

  #
  # opentelemetry_exporter_otlp_http
  #

  add_library(
    opentelemetry_exporter_otlp_http
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_http_exporter.cc
    src/otlp_http_exporter_factory.cc)

  set_target_properties(opentelemetry_exporter_otlp_http
                        PROPERTIES EXPORT_NAME otlp_http_exporter)
  set_target_version(opentelemetry_exporter_otlp_http)

  target_link_libraries(
    opentelemetry_exporter_otlp_http
    PUBLIC opentelemetry_otlp_common opentelemetry_otlp_recordable
           opentelemetry_exporter_otlp_http_client
           opentelemetry_exporter_otlp_http_options)

  list(APPEND OPENTELEMETRY_OTLP_HTTP_TARGETS opentelemetry_exporter_otlp_http)

  #
  # opentelemetry_exporter_otlp_http_builder
  #

  add_library(
    opentelemetry_exporter_otlp_http_builder
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_http_span_builder.cc)

  set_target_properties(opentelemetry_exporter_otlp_http_builder
                        PROPERTIES EXPORT_NAME otlp_http_exporter_builder)
  set_target_version(opentelemetry_exporter_otlp_http_builder)

  target_include_directories(
    opentelemetry_exporter_otlp_http_builder
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
           "$<INSTALL_INTERFACE:include>")

  target_link_libraries(
    opentelemetry_exporter_otlp_http_builder
    PUBLIC opentelemetry_configuration_core opentelemetry_trace
    PRIVATE opentelemetry_exporter_otlp_http
            opentelemetry_exporter_otlp_builder_utils)

  list(APPEND OPENTELEMETRY_OTLP_HTTP_BUILDER_TARGETS
       opentelemetry_exporter_otlp_http_builder)

  #
  # opentelemetry_exporter_otlp_http_log
  #

  add_library(
    opentelemetry_exporter_otlp_http_log
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_http_log_record_exporter.cc
    src/otlp_http_log_record_exporter_factory.cc)

  set_target_properties(opentelemetry_exporter_otlp_http_log
                        PROPERTIES EXPORT_NAME otlp_http_log_record_exporter)
  set_target_version(opentelemetry_exporter_otlp_http_log)

  target_link_libraries(
    opentelemetry_exporter_otlp_http_log
    PUBLIC opentelemetry_otlp_common opentelemetry_otlp_recordable
           opentelemetry_exporter_otlp_http_client
           opentelemetry_exporter_otlp_http_options)

  list(APPEND OPENTELEMETRY_OTLP_HTTP_TARGETS
       opentelemetry_exporter_otlp_http_log)

  #
  # opentelemetry_exporter_otlp_http_log_builder
  #

  add_library(
    opentelemetry_exporter_otlp_http_log_builder
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_http_log_record_builder.cc)

  set_target_properties(
    opentelemetry_exporter_otlp_http_log_builder
    PROPERTIES EXPORT_NAME otlp_http_log_record_exporter_builder)
  set_target_version(opentelemetry_exporter_otlp_http_log_builder)

  target_include_directories(
    opentelemetry_exporter_otlp_http_log_builder
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
           "$<INSTALL_INTERFACE:include>")

  target_link_libraries(
    opentelemetry_exporter_otlp_http_log_builder
    PUBLIC opentelemetry_configuration_core opentelemetry_logs
    PRIVATE opentelemetry_exporter_otlp_http_log
            opentelemetry_exporter_otlp_builder_utils)

  list(APPEND OPENTELEMETRY_OTLP_HTTP_BUILDER_TARGETS
       opentelemetry_exporter_otlp_http_log_builder)

  #
  # opentelemetry_exporter_otlp_http_metric
  #

  add_library(
    opentelemetry_exporter_otlp_http_metric
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_http_metric_exporter.cc
    src/otlp_http_metric_exporter_factory.cc)

  set_target_properties(opentelemetry_exporter_otlp_http_metric
                        PROPERTIES EXPORT_NAME otlp_http_metric_exporter)
  set_target_version(opentelemetry_exporter_otlp_http_metric)

  target_link_libraries(
    opentelemetry_exporter_otlp_http_metric
    PUBLIC opentelemetry_otlp_common opentelemetry_otlp_recordable
           opentelemetry_exporter_otlp_http_client
           opentelemetry_exporter_otlp_http_options)

  list(APPEND OPENTELEMETRY_OTLP_HTTP_TARGETS
       opentelemetry_exporter_otlp_http_metric)

  #
  # opentelemetry_exporter_otlp_http_metric_builder
  #

  add_library(
    opentelemetry_exporter_otlp_http_metric_builder
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_http_push_metric_builder.cc)

  set_target_properties(
    opentelemetry_exporter_otlp_http_metric_builder
    PROPERTIES EXPORT_NAME otlp_http_metric_exporter_builder)
  set_target_version(opentelemetry_exporter_otlp_http_metric_builder)

  target_include_directories(
    opentelemetry_exporter_otlp_http_metric_builder
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
           "$<INSTALL_INTERFACE:include>")

  target_link_libraries(
    opentelemetry_exporter_otlp_http_metric_builder
    PUBLIC opentelemetry_configuration_core opentelemetry_metrics
    PRIVATE opentelemetry_exporter_otlp_http_metric
            opentelemetry_exporter_otlp_builder_utils)

  list(APPEND OPENTELEMETRY_OTLP_HTTP_BUILDER_TARGETS
       opentelemetry_exporter_otlp_http_metric_builder)

  if(OTELCPP_INSTALL)
    opentelemetry_add_pkgconfig(
      exporter_otlp_http_client "OpenTelemetry OTLP - HTTP Client"
      "HTTP client for OTLP protocol." "opentelemetry_api")

    opentelemetry_add_pkgconfig(
      exporter_otlp_http
      "OpenTelemetry OTLP - HTTP Span Exporter"
      "Exports trace spans via OTLP HTTP."
      "opentelemetry_otlp_common opentelemetry_otlp_recordable opentelemetry_exporter_otlp_http_client"
    )

    opentelemetry_add_pkgconfig(
      exporter_otlp_http_builder
      "OpenTelemetry OTLP - HTTP Span Builder"
      "Builder for OTLP HTTP span exporter."
      "opentelemetry_exporter_otlp_http opentelemetry_exporter_otlp_builder_utils opentelemetry_configuration_core"
    )

    opentelemetry_add_pkgconfig(
      exporter_otlp_http_log
      "OpenTelemetry OTLP - HTTP Log Exporter"
      "Exports log records via OTLP HTTP."
      "opentelemetry_otlp_common opentelemetry_otlp_recordable opentelemetry_exporter_otlp_http_client"
    )

    opentelemetry_add_pkgconfig(
      exporter_otlp_http_log_builder
      "OpenTelemetry OTLP - HTTP Log Builder"
      "Builder for OTLP HTTP log record exporter."
      "opentelemetry_exporter_otlp_http_log opentelemetry_exporter_otlp_builder_utils opentelemetry_configuration_core"
    )

    opentelemetry_add_pkgconfig(
      exporter_otlp_http_metric
      "OpenTelemetry OTLP - HTTP Metric Exporter"
      "Exports metrics via OTLP HTTP."
      "opentelemetry_otlp_common opentelemetry_otlp_recordable opentelemetry_exporter_otlp_http_client"
    )

    opentelemetry_add_pkgconfig(
      exporter_otlp_http_metric_builder
      "OpenTelemetry OTLP - HTTP Metric Builder"
      "Builder for OTLP HTTP metric exporter."
      "opentelemetry_exporter_otlp_http_metric opentelemetry_exporter_otlp_builder_utils opentelemetry_configuration_core"
    )
  endif()

endif()

if(OTELCPP_WITH_OTLP_FILE)

  #
  # opentelemetry_exporter_otlp_file_client
  #

  add_library(opentelemetry_exporter_otlp_file_client
              ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_file_client.cc)
  set_target_properties(opentelemetry_exporter_otlp_file_client
                        PROPERTIES EXPORT_NAME otlp_file_client)
  set_target_version(opentelemetry_exporter_otlp_file_client)

  target_link_libraries(
    opentelemetry_exporter_otlp_file_client
    PUBLIC opentelemetry_sdk opentelemetry_common
           "$<BUILD_INTERFACE:opentelemetry_proto>"
    PRIVATE "$<INSTALL_INTERFACE:opentelemetry_proto>"
            opentelemetry_exporter_otlp_json_converter)

  target_include_directories(
    opentelemetry_exporter_otlp_file_client
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
           "$<INSTALL_INTERFACE:include>")

  list(APPEND OPENTELEMETRY_OTLP_FILE_TARGETS
       opentelemetry_exporter_otlp_file_client)

  #
  # opentelemetry_exporter_otlp_file
  #

  add_library(
    opentelemetry_exporter_otlp_file
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_file_exporter.cc
    src/otlp_file_exporter_factory.cc src/otlp_file_exporter_options.cc)

  set_target_properties(opentelemetry_exporter_otlp_file
                        PROPERTIES EXPORT_NAME otlp_file_exporter)
  set_target_version(opentelemetry_exporter_otlp_file)

  target_link_libraries(
    opentelemetry_exporter_otlp_file
    PUBLIC opentelemetry_otlp_common opentelemetry_otlp_recordable
           opentelemetry_exporter_otlp_file_client)

  list(APPEND OPENTELEMETRY_OTLP_FILE_TARGETS opentelemetry_exporter_otlp_file)

  #
  # opentelemetry_exporter_otlp_file_builder
  #

  add_library(
    opentelemetry_exporter_otlp_file_builder
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_file_span_builder.cc)

  set_target_properties(opentelemetry_exporter_otlp_file_builder
                        PROPERTIES EXPORT_NAME otlp_file_exporter_builder)
  set_target_version(opentelemetry_exporter_otlp_file_builder)

  target_include_directories(
    opentelemetry_exporter_otlp_file_builder
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
           "$<INSTALL_INTERFACE:include>")

  target_link_libraries(
    opentelemetry_exporter_otlp_file_builder
    PUBLIC opentelemetry_configuration_core opentelemetry_trace
    PRIVATE opentelemetry_exporter_otlp_file)

  list(APPEND OPENTELEMETRY_OTLP_FILE_BUILDER_TARGETS
       opentelemetry_exporter_otlp_file_builder)

  #
  # opentelemetry_exporter_otlp_file_log
  #

  add_library(
    opentelemetry_exporter_otlp_file_log
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_file_log_record_exporter.cc
    src/otlp_file_log_record_exporter_factory.cc
    src/otlp_file_log_record_exporter_options.cc)

  set_target_properties(opentelemetry_exporter_otlp_file_log
                        PROPERTIES EXPORT_NAME otlp_file_log_record_exporter)
  set_target_version(opentelemetry_exporter_otlp_file_log)

  target_link_libraries(
    opentelemetry_exporter_otlp_file_log
    PUBLIC opentelemetry_otlp_common opentelemetry_otlp_recordable
           opentelemetry_exporter_otlp_file_client)

  list(APPEND OPENTELEMETRY_OTLP_FILE_TARGETS
       opentelemetry_exporter_otlp_file_log)

  #
  # opentelemetry_exporter_otlp_file_log_builder
  #

  add_library(
    opentelemetry_exporter_otlp_file_log_builder
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_file_log_record_builder.cc)

  set_target_properties(
    opentelemetry_exporter_otlp_file_log_builder
    PROPERTIES EXPORT_NAME otlp_file_log_record_exporter_builder)
  set_target_version(opentelemetry_exporter_otlp_file_log_builder)

  target_include_directories(
    opentelemetry_exporter_otlp_file_log_builder
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
           "$<INSTALL_INTERFACE:include>")

  target_link_libraries(
    opentelemetry_exporter_otlp_file_log_builder
    PUBLIC opentelemetry_configuration_core opentelemetry_logs
    PRIVATE opentelemetry_exporter_otlp_file_log)

  list(APPEND OPENTELEMETRY_OTLP_FILE_BUILDER_TARGETS
       opentelemetry_exporter_otlp_file_log_builder)

  #
  # opentelemetry_exporter_otlp_file_metric
  #

  add_library(
    opentelemetry_exporter_otlp_file_metric
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_file_metric_exporter.cc
    src/otlp_file_metric_exporter_factory.cc
    src/otlp_file_metric_exporter_options.cc)

  set_target_properties(opentelemetry_exporter_otlp_file_metric
                        PROPERTIES EXPORT_NAME otlp_file_metric_exporter)
  set_target_version(opentelemetry_exporter_otlp_file_metric)

  target_link_libraries(
    opentelemetry_exporter_otlp_file_metric
    PUBLIC opentelemetry_otlp_common opentelemetry_otlp_recordable
           opentelemetry_exporter_otlp_file_client)

  list(APPEND OPENTELEMETRY_OTLP_FILE_TARGETS
       opentelemetry_exporter_otlp_file_metric)

  #
  # opentelemetry_exporter_otlp_file_metric_builder
  #

  add_library(
    opentelemetry_exporter_otlp_file_metric_builder
    ${OPENTELEMETRY_OTLP_TARGETS_LIB_TYPE} src/otlp_file_push_metric_builder.cc)

  set_target_properties(
    opentelemetry_exporter_otlp_file_metric_builder
    PROPERTIES EXPORT_NAME otlp_file_metric_exporter_builder)
  set_target_version(opentelemetry_exporter_otlp_file_metric_builder)

  target_include_directories(
    opentelemetry_exporter_otlp_file_metric_builder
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
           "$<INSTALL_INTERFACE:include>")

  target_link_libraries(
    opentelemetry_exporter_otlp_file_metric_builder
    PUBLIC opentelemetry_configuration_core opentelemetry_metrics
    PRIVATE opentelemetry_exporter_otlp_file_metric
            opentelemetry_exporter_otlp_builder_utils)

  list(APPEND OPENTELEMETRY_OTLP_FILE_BUILDER_TARGETS
       opentelemetry_exporter_otlp_file_metric_builder)

  if(OTELCPP_INSTALL)
    opentelemetry_add_pkgconfig(
      exporter_otlp_file_client "OpenTelemetry OTLP - File Client"
      "File client for OTLP protocol." "opentelemetry_api opentelemetry_common")

    opentelemetry_add_pkgconfig(
      exporter_otlp_file
      "OpenTelemetry OTLP - File Span Exporter"
      "Exports trace spans to file via OTLP."
      "opentelemetry_otlp_common opentelemetry_otlp_recordable opentelemetry_exporter_otlp_file_client"
    )

    opentelemetry_add_pkgconfig(
      exporter_otlp_file_builder "OpenTelemetry OTLP - File Span Builder"
      "Builder for OTLP file span exporter."
      "opentelemetry_exporter_otlp_file opentelemetry_configuration_core")

    opentelemetry_add_pkgconfig(
      exporter_otlp_file_log
      "OpenTelemetry OTLP - File Log Exporter"
      "Exports log records to file via OTLP."
      "opentelemetry_otlp_common opentelemetry_otlp_recordable opentelemetry_exporter_otlp_file_client"
    )

    opentelemetry_add_pkgconfig(
      exporter_otlp_file_log_builder "OpenTelemetry OTLP - File Log Builder"
      "Builder for OTLP file log record exporter."
      "opentelemetry_exporter_otlp_file_log opentelemetry_configuration_core")

    opentelemetry_add_pkgconfig(
      exporter_otlp_file_metric
      "OpenTelemetry OTLP - File Metric Exporter"
      "Exports metrics to file via OTLP."
      "opentelemetry_otlp_common opentelemetry_otlp_recordable opentelemetry_exporter_otlp_file_client"
    )

    opentelemetry_add_pkgconfig(
      exporter_otlp_file_metric_builder
      "OpenTelemetry OTLP - File Metric Builder"
      "Builder for OTLP file metric exporter."
      "opentelemetry_exporter_otlp_file_metric opentelemetry_exporter_otlp_builder_utils opentelemetry_configuration_core"
    )
  endif()

endif()

target_link_libraries(
  opentelemetry_otlp_recordable
  PUBLIC opentelemetry_trace opentelemetry_resources opentelemetry_proto)

otel_add_component(
  COMPONENT
  exporters_otlp_common
  DEPRECATED_NAMES
  exporters_otlp_builder_utils
  TARGETS
  opentelemetry_otlp_common
  opentelemetry_otlp_recordable
  opentelemetry_proto
  opentelemetry_exporter_otlp_builder_utils
  opentelemetry_exporter_otlp_json_converter
  opentelemetry_exporter_otlp_json_mapping
  ${OPENTELEMETRY_OTLP_JSON_WRITER_TARGETS}
  ${OPENTELEMETRY_OTLP_JSON_READER_TARGETS}
  opentelemetry_exporter_otlp_json_partial_success
  FILES_DIRECTORY
  "include/opentelemetry/exporters/otlp"
  FILES_DESTINATION
  "include/opentelemetry/exporters"
  FILES_MATCHING
  PATTERN
  "*.h"
  PATTERN
  "otlp_http*.h"
  EXCLUDE
  PATTERN
  "otlp_grpc*.h"
  EXCLUDE
  PATTERN
  "otlp_file*.h"
  EXCLUDE)

if(OTELCPP_WITH_OTLP_GRPC)
  otel_add_component(
    COMPONENT
    exporters_otlp_grpc
    DEPRECATED_NAMES
    exporters_otlp_grpc_builder
    TARGETS
    ${OPENTELEMETRY_OTLP_GRPC_TARGETS}
    ${OPENTELEMETRY_OTLP_GRPC_BUILDER_TARGETS}
    FILES_DIRECTORY
    "include/opentelemetry/exporters/otlp"
    FILES_DESTINATION
    "include/opentelemetry/exporters"
    FILES_MATCHING
    PATTERN
    "otlp_grpc*.h")

endif()

if(OTELCPP_WITH_OTLP_HTTP)
  otel_add_component(
    COMPONENT
    exporters_otlp_http
    DEPRECATED_NAMES
    exporters_otlp_http_builder
    TARGETS
    ${OPENTELEMETRY_OTLP_HTTP_TARGETS}
    ${OPENTELEMETRY_OTLP_HTTP_BUILDER_TARGETS}
    FILES_DIRECTORY
    "include/opentelemetry/exporters/otlp"
    FILES_DESTINATION
    "include/opentelemetry/exporters"
    FILES_MATCHING
    PATTERN
    "otlp_http*.h")

endif()

if(OTELCPP_WITH_OTLP_FILE)
  otel_add_component(
    COMPONENT
    exporters_otlp_file
    DEPRECATED_NAMES
    exporters_otlp_file_builder
    TARGETS
    ${OPENTELEMETRY_OTLP_FILE_TARGETS}
    ${OPENTELEMETRY_OTLP_FILE_BUILDER_TARGETS}
    FILES_DIRECTORY
    "include/opentelemetry/exporters/otlp"
    FILES_DESTINATION
    "include/opentelemetry/exporters"
    FILES_MATCHING
    PATTERN
    "otlp_file*.h")

endif()

if(OTELCPP_BUILD_TESTING)
  add_executable(otlp_recordable_test test/otlp_recordable_test.cc)
  target_link_libraries(
    otlp_recordable_test ${GTEST_BOTH_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT}
    opentelemetry_otlp_recordable protobuf::libprotobuf)
  gtest_add_tests(
    TARGET otlp_recordable_test
    TEST_PREFIX exporter.otlp.
    TEST_LIST otlp_recordable_test)

  add_executable(otlp_log_recordable_test test/otlp_log_recordable_test.cc)
  target_link_libraries(otlp_log_recordable_test ${GTEST_BOTH_LIBRARIES}
                        ${CMAKE_THREAD_LIBS_INIT} opentelemetry_otlp_recordable)
  if(OTELCPP_WITH_OTLP_UTF8_VALIDITY AND TARGET utf8_range::utf8_validity)
    target_compile_definitions(otlp_log_recordable_test
                               PRIVATE ENABLE_OTLP_UTF8_VALIDITY)
  endif()
  gtest_add_tests(
    TARGET otlp_log_recordable_test
    TEST_PREFIX exporter.otlp.
    TEST_LIST otlp_log_recordable_test)

  add_executable(otlp_populate_attribute_utils_test
                 test/otlp_populate_attribute_utils_test.cc)
  target_link_libraries(
    otlp_populate_attribute_utils_test ${GTEST_BOTH_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT} opentelemetry_otlp_recordable)
  if(WITH_OTLP_UTF8_VALIDITY AND TARGET utf8_range::utf8_validity)
    target_compile_definitions(otlp_populate_attribute_utils_test
                               PRIVATE ENABLE_OTLP_UTF8_VALIDITY)
  endif()
  gtest_add_tests(
    TARGET otlp_populate_attribute_utils_test
    TEST_PREFIX exporter.otlp.
    TEST_LIST otlp_populate_attribute_utils_test)

  add_executable(otlp_metrics_serialization_test
                 test/otlp_metrics_serialization_test.cc)
  target_link_libraries(
    otlp_metrics_serialization_test ${GTEST_BOTH_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT} opentelemetry_otlp_recordable
    protobuf::libprotobuf)
  gtest_add_tests(
    TARGET otlp_metrics_serialization_test
    TEST_PREFIX exporter.otlp.
    TEST_LIST otlp_metrics_serialization_test)

  # Compares the two mappings through one writer, so it needs a backend to
  # produce bytes with, and the protobuf path to compare against.
  if(OTELCPP_WITH_JSON_WRITER_NLOHMANN)
    add_executable(otlp_json_trace_equivalence_test
                   test/otlp_json_trace_equivalence_test.cc)
    target_link_libraries(
      otlp_json_trace_equivalence_test
      ${GTEST_BOTH_LIBRARIES}
      ${CMAKE_THREAD_LIBS_INIT}
      opentelemetry_exporter_otlp_json_mapping
      opentelemetry_exporter_otlp_json_converter
      opentelemetry_otlp_recordable
      opentelemetry_exporter_otlp_json_writer
      protobuf::libprotobuf)
    gtest_add_tests(
      TARGET otlp_json_trace_equivalence_test
      TEST_PREFIX exporter.otlp.
      TEST_LIST otlp_json_trace_equivalence_test)

    add_executable(otlp_json_metric_equivalence_test
                   test/otlp_json_metric_equivalence_test.cc)
    target_link_libraries(
      otlp_json_metric_equivalence_test
      ${GTEST_BOTH_LIBRARIES}
      ${CMAKE_THREAD_LIBS_INIT}
      opentelemetry_exporter_otlp_json_mapping
      opentelemetry_exporter_otlp_json_converter
      opentelemetry_otlp_recordable
      opentelemetry_exporter_otlp_json_writer
      protobuf::libprotobuf)
    gtest_add_tests(
      TARGET otlp_json_metric_equivalence_test
      TEST_PREFIX exporter.otlp.
      TEST_LIST otlp_json_metric_equivalence_test)

    add_executable(otlp_json_log_equivalence_test
                   test/otlp_json_log_equivalence_test.cc)
    target_link_libraries(
      otlp_json_log_equivalence_test
      ${GTEST_BOTH_LIBRARIES}
      ${CMAKE_THREAD_LIBS_INIT}
      opentelemetry_exporter_otlp_json_mapping
      opentelemetry_exporter_otlp_json_converter
      opentelemetry_otlp_recordable
      opentelemetry_exporter_otlp_json_writer
      protobuf::libprotobuf)
    gtest_add_tests(
      TARGET otlp_json_log_equivalence_test
      TEST_PREFIX exporter.otlp.
      TEST_LIST otlp_json_log_equivalence_test)
  endif()

  if(OTELCPP_WITH_JSON_WRITER_NLOHMANN)
    add_executable(otlp_json_writer_nlohmann_test
                   test/otlp_json_writer_nlohmann_test.cc)
    target_link_libraries(
      otlp_json_writer_nlohmann_test ${GTEST_BOTH_LIBRARIES}
      ${CMAKE_THREAD_LIBS_INIT} opentelemetry_exporter_otlp_json_writer)
    gtest_add_tests(
      TARGET otlp_json_writer_nlohmann_test
      TEST_PREFIX exporter.otlp.
      TEST_LIST otlp_json_writer_nlohmann_test)

    add_executable(otlp_json_reader_test test/otlp_json_reader_test.cc)
    target_link_libraries(
      otlp_json_reader_test ${GTEST_BOTH_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT}
      opentelemetry_exporter_otlp_json_reader)
    gtest_add_tests(
      TARGET otlp_json_reader_test
      TEST_PREFIX exporter.otlp.
      TEST_LIST otlp_json_reader_test)

    add_executable(otlp_json_partial_success_test
                   test/otlp_json_partial_success_test.cc)
    target_link_libraries(
      otlp_json_partial_success_test ${GTEST_BOTH_LIBRARIES}
      ${CMAKE_THREAD_LIBS_INIT} opentelemetry_exporter_otlp_json_partial_success
      opentelemetry_sdk opentelemetry_test_common)
    gtest_add_tests(
      TARGET otlp_json_partial_success_test
      TEST_PREFIX exporter.otlp.
      TEST_LIST otlp_json_partial_success_test)
  endif()

  if(OTELCPP_WITH_OTLP_GRPC)
    add_executable(otlp_grpc_exporter_test test/otlp_grpc_exporter_test.cc)
    target_link_libraries(
      otlp_grpc_exporter_test ${GTEST_BOTH_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT}
      ${GMOCK_LIB} opentelemetry_exporter_otlp_grpc opentelemetry_test_common)
    gtest_add_tests(
      TARGET otlp_grpc_exporter_test
      TEST_PREFIX exporter.otlp.
      TEST_LIST otlp_grpc_exporter_test)

    add_executable(otlp_grpc_target_test test/otlp_grpc_target_test.cc)
    target_link_libraries(
      otlp_grpc_target_test ${GTEST_BOTH_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT}
      ${GMOCK_LIB} opentelemetry_exporter_otlp_grpc)
    gtest_add_tests(
      TARGET otlp_grpc_target_test
      TEST_PREFIX exporter.otlp.
      TEST_LIST otlp_grpc_target_test)

    add_executable(otlp_grpc_exporter_factory_test
                   test/otlp_grpc_exporter_factory_test.cc)
    target_link_libraries(
      otlp_grpc_exporter_factory_test ${GTEST_BOTH_LIBRARIES}
      ${CMAKE_THREAD_LIBS_INIT} ${GMOCK_LIB} opentelemetry_exporter_otlp_grpc)
    gtest_add_tests(
      TARGET otlp_grpc_exporter_factory_test
      TEST_PREFIX exporter.otlp.
      TEST_LIST otlp_grpc_exporter_factory_test)

    add_executable(otlp_grpc_log_record_exporter_test
                   test/otlp_grpc_log_record_exporter_test.cc)
    target_link_libraries(
      otlp_grpc_log_record_exporter_test
      ${GTEST_BOTH_LIBRARIES}
      ${CMAKE_THREAD_LIBS_INIT}
      ${GMOCK_LIB}
      opentelemetry_exporter_otlp_grpc
      opentelemetry_exporter_otlp_grpc_log
      opentelemetry_trace
      opentelemetry_logs
      opentelemetry_test_common)
    gtest_add_tests(
      TARGET otlp_grpc_log_record_exporter_test
      TEST_PREFIX exporter.otlp.
      TEST_LIST otlp_grpc_log_record_exporter_test)

    add_executable(otlp_grpc_log_record_exporter_factory_test
                   test/otlp_grpc_log_record_exporter_factory_test.cc)
    target_link_libraries(
      otlp_grpc_log_record_exporter_factory_test ${GTEST_BOTH_LIBRARIES}
      ${CMAKE_THREAD_LIBS_INIT} ${GMOCK_LIB}
      opentelemetry_exporter_otlp_grpc_log opentelemetry_logs)
    gtest_add_tests(
      TARGET otlp_grpc_log_record_exporter_factory_test
      TEST_PREFIX exporter.otlp.
      TEST_LIST otlp_grpc_log_record_exporter_factory_test)

    add_executable(otlp_grpc_metric_exporter_test
                   test/otlp_grpc_metric_exporter_test.cc)
    target_link_libraries(
      otlp_grpc_metric_exporter_test
      ${GTEST_BOTH_LIBRARIES}
      ${CMAKE_THREAD_LIBS_INIT}
      ${GMOCK_LIB}
      opentelemetry_exporter_otlp_grpc
      opentelemetry_exporter_otlp_grpc_metrics
      opentelemetry_test_common)
    gtest_add_tests(
      TARGET otlp_grpc_metric_exporter_test
      TEST_PREFIX exporter.otlp.
      TEST_LIST otlp_grpc_metric_exporter_test)

    add_executable(otlp_grpc_metric_exporter_factory_test
                   test/otlp_grpc_metric_exporter_factory_test.cc)
    target_link_libraries(
      otlp_grpc_metric_exporter_factory_test ${GTEST_BOTH_LIBRARIES}
      ${CMAKE_THREAD_LIBS_INIT} ${GMOCK_LIB}
      opentelemetry_exporter_otlp_grpc_metrics opentelemetry_metrics)
    gtest_add_tests(
      TARGET otlp_grpc_metric_exporter_factory_test
      TEST_PREFIX exporter.otlp.
      TEST_LIST otlp_grpc_metric_exporter_factory_test)
  endif()

  if(OTELCPP_WITH_OTLP_HTTP)
    if(OTELCPP_WITH_JSON_WRITER_NLOHMANN)
      add_executable(otlp_http_exporter_test test/otlp_http_exporter_test.cc)
      target_link_libraries(
        otlp_http_exporter_test
        ${GTEST_BOTH_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        ${GMOCK_LIB}
        opentelemetry_exporter_otlp_http
        opentelemetry_http_client_nosend
        nlohmann_json::nlohmann_json
        protobuf::libprotobuf
        opentelemetry_test_common)
      if(WIN32)
        target_link_libraries(otlp_http_exporter_test ws2_32)
      endif()
      gtest_add_tests(
        TARGET otlp_http_exporter_test
        TEST_PREFIX exporter.otlp.
        TEST_LIST otlp_http_exporter_test)
    endif()

    # Links the OTLP/JSON exporters and the no-send client and nothing else:
    # the target list is what proves these exporters need no protobuf, so do
    # not add protobuf::libprotobuf here to fix a link error.
    if(OTELCPP_WITH_JSON_WRITER_NLOHMANN)
      add_executable(otlp_json_http_builder_test test/otlp_json_http_builder_test.cc)
      target_link_libraries(
        otlp_json_http_builder_test
        ${GTEST_BOTH_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        ${GMOCK_LIB}
        opentelemetry_exporter_otlp_json_http_builder
        opentelemetry_exporter_otlp_json_http_log_builder
        opentelemetry_exporter_otlp_json_http_metric_builder
        opentelemetry_test_common)
      gtest_add_tests(
        TARGET otlp_json_http_builder_test
        TEST_PREFIX exporter.otlp.
        TEST_LIST otlp_json_http_builder_test)

      # test name -> the single-signal library it exercises
      set(_json_http_tests
          otlp_json_http_exporter_test opentelemetry_exporter_otlp_json_http
          otlp_json_http_exporter_factory_test opentelemetry_exporter_otlp_json_http
          otlp_json_http_log_record_exporter_test
          opentelemetry_exporter_otlp_json_http_log
          otlp_json_http_log_record_exporter_factory_test
          opentelemetry_exporter_otlp_json_http_log
          otlp_json_http_metric_exporter_test
          opentelemetry_exporter_otlp_json_http_metric
          otlp_json_http_metric_exporter_factory_test
          opentelemetry_exporter_otlp_json_http_metric)

      list(LENGTH _json_http_tests _json_http_tests_length)
      math(EXPR _json_http_last "${_json_http_tests_length} - 1")
      foreach(_i RANGE 0 ${_json_http_last} 2)
        list(GET _json_http_tests ${_i} _json_http_test)
        math(EXPR _j "${_i} + 1")
        list(GET _json_http_tests ${_j} _json_http_lib)

        add_executable(${_json_http_test} test/${_json_http_test}.cc)
        target_link_libraries(
          ${_json_http_test} ${GTEST_BOTH_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT}
          ${GMOCK_LIB} ${_json_http_lib} opentelemetry_http_client_nosend
          opentelemetry_test_common)
        if(WIN32)
          target_link_libraries(${_json_http_test} ws2_32)
        endif()
        gtest_add_tests(
          TARGET ${_json_http_test}
          TEST_PREFIX exporter.otlp.
          TEST_LIST ${_json_http_test})
      endforeach()
    endif()

    # Links the transport and the no-send client and nothing else: the target
    # list is what proves the send path needs no protobuf, so do not add
    # protobuf::libprotobuf here to fix a link error -- fix the split instead.
    add_executable(otlp_http_transport_test test/otlp_http_transport_test.cc)
    target_link_libraries(
      otlp_http_transport_test ${GTEST_BOTH_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT}
      ${GMOCK_LIB} opentelemetry_exporter_otlp_http_transport
      opentelemetry_http_client_nosend opentelemetry_test_common)
    if(WIN32)
      target_link_libraries(otlp_http_transport_test ws2_32)
    endif()
    gtest_add_tests(
      TARGET otlp_http_transport_test
      TEST_PREFIX exporter.otlp.
      TEST_LIST otlp_http_transport_test)

    add_executable(otlp_http_exporter_factory_test
                   test/otlp_http_exporter_factory_test.cc)
    target_link_libraries(
      otlp_http_exporter_factory_test ${GTEST_BOTH_LIBRARIES}
      ${CMAKE_THREAD_LIBS_INIT} ${GMOCK_LIB} opentelemetry_exporter_otlp_http)
    gtest_add_tests(
      TARGET otlp_http_exporter_factory_test
      TEST_PREFIX exporter.otlp.
      TEST_LIST otlp_http_exporter_factory_test)

    if(OTELCPP_WITH_JSON_WRITER_NLOHMANN)
      add_executable(otlp_http_log_record_exporter_test
                     test/otlp_http_log_record_exporter_test.cc)
      target_link_libraries(
        otlp_http_log_record_exporter_test
        ${GTEST_BOTH_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        ${GMOCK_LIB}
        opentelemetry_exporter_otlp_http_log
        opentelemetry_logs
        opentelemetry_http_client_nosend
        nlohmann_json::nlohmann_json
        opentelemetry_test_common)
      if(WIN32)
        target_link_libraries(otlp_http_log_record_exporter_test ws2_32)
      endif()
      gtest_add_tests(
        TARGET otlp_http_log_record_exporter_test
        TEST_PREFIX exporter.otlp.
        TEST_LIST otlp_http_log_record_exporter_test)
    endif()

    add_executable(otlp_http_log_record_exporter_factory_test
                   test/otlp_http_log_record_exporter_factory_test.cc)
    target_link_libraries(
      otlp_http_log_record_exporter_factory_test ${GTEST_BOTH_LIBRARIES}
      ${CMAKE_THREAD_LIBS_INIT} ${GMOCK_LIB}
      opentelemetry_exporter_otlp_http_log opentelemetry_logs)
    gtest_add_tests(
      TARGET otlp_http_log_record_exporter_factory_test
      TEST_PREFIX exporter.otlp.
      TEST_LIST otlp_http_log_record_exporter_factory_test)

    if(OTELCPP_WITH_JSON_WRITER_NLOHMANN)
      add_executable(otlp_http_metric_exporter_test
                     test/otlp_http_metric_exporter_test.cc)
      target_link_libraries(
        otlp_http_metric_exporter_test
        ${GTEST_BOTH_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        ${GMOCK_LIB}
        opentelemetry_exporter_otlp_http_metric
        opentelemetry_metrics
        opentelemetry_http_client_nosend
        nlohmann_json::nlohmann_json
        protobuf::libprotobuf
        opentelemetry_test_common)
      gtest_add_tests(
        TARGET otlp_http_metric_exporter_test
        TEST_PREFIX exporter.otlp.
        TEST_LIST otlp_http_metric_exporter_test)
    endif()

    add_executable(otlp_http_metric_exporter_factory_test
                   test/otlp_http_metric_exporter_factory_test.cc)
    target_link_libraries(
      otlp_http_metric_exporter_factory_test ${GTEST_BOTH_LIBRARIES}
      ${CMAKE_THREAD_LIBS_INIT} ${GMOCK_LIB}
      opentelemetry_exporter_otlp_http_metric opentelemetry_metrics)
    gtest_add_tests(
      TARGET otlp_http_metric_exporter_factory_test
      TEST_PREFIX exporter.otlp.
      TEST_LIST otlp_http_metric_exporter_factory_test)

    add_executable(otlp_http_exporter_custom_client_test
                   test/otlp_http_exporter_custom_client_test.cc)
    target_link_libraries(
      otlp_http_exporter_custom_client_test ${GTEST_BOTH_LIBRARIES}
      ${CMAKE_THREAD_LIBS_INIT} ${GMOCK_LIB} opentelemetry_exporter_otlp_http
      opentelemetry_http_client_nosend)
    gtest_add_tests(
      TARGET otlp_http_exporter_custom_client_test
      TEST_PREFIX exporter.otlp.
      TEST_LIST otlp_http_exporter_custom_client_test)

    add_executable(otlp_http_metric_exporter_custom_client_test
                   test/otlp_http_metric_exporter_custom_client_test.cc)
    target_link_libraries(
      otlp_http_metric_exporter_custom_client_test ${GTEST_BOTH_LIBRARIES}
      ${CMAKE_THREAD_LIBS_INIT} opentelemetry_exporter_otlp_http_metric
      opentelemetry_metrics opentelemetry_http_client_nosend)
    gtest_add_tests(
      TARGET otlp_http_metric_exporter_custom_client_test
      TEST_PREFIX exporter.otlp.
      TEST_LIST otlp_http_metric_exporter_custom_client_test)

    add_executable(otlp_http_log_record_exporter_custom_client_test
                   test/otlp_http_log_record_exporter_custom_client_test.cc)
    target_link_libraries(
      otlp_http_log_record_exporter_custom_client_test ${GTEST_BOTH_LIBRARIES}
      ${CMAKE_THREAD_LIBS_INIT} opentelemetry_exporter_otlp_http_log
      opentelemetry_logs opentelemetry_http_client_nosend)
    gtest_add_tests(
      TARGET otlp_http_log_record_exporter_custom_client_test
      TEST_PREFIX exporter.otlp.
      TEST_LIST otlp_http_log_record_exporter_custom_client_test)

    if(OTELCPP_WITH_JSON_WRITER_NLOHMANN)
      add_executable(otlp_http_exporter_custom_json_writer_test
                     test/otlp_http_exporter_custom_json_writer_test.cc)
      target_link_libraries(
        otlp_http_exporter_custom_json_writer_test
        ${GTEST_BOTH_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        ${GMOCK_LIB}
        opentelemetry_exporter_otlp_http
        opentelemetry_http_client_nosend
        opentelemetry_exporter_otlp_json_writer)
      gtest_add_tests(
        TARGET otlp_http_exporter_custom_json_writer_test
        TEST_PREFIX exporter.otlp.
        TEST_LIST otlp_http_exporter_custom_json_writer_test)

      add_executable(otlp_http_metric_exporter_custom_json_writer_test
                     test/otlp_http_metric_exporter_custom_json_writer_test.cc)
      target_link_libraries(
        otlp_http_metric_exporter_custom_json_writer_test
        ${GTEST_BOTH_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        opentelemetry_exporter_otlp_http_metric
        opentelemetry_metrics
        opentelemetry_http_client_nosend
        opentelemetry_exporter_otlp_json_writer)
      gtest_add_tests(
        TARGET otlp_http_metric_exporter_custom_json_writer_test
        TEST_PREFIX exporter.otlp.
        TEST_LIST otlp_http_metric_exporter_custom_json_writer_test)

      add_executable(
        otlp_http_log_record_exporter_custom_json_writer_test
        test/otlp_http_log_record_exporter_custom_json_writer_test.cc)
      target_link_libraries(
        otlp_http_log_record_exporter_custom_json_writer_test
        ${GTEST_BOTH_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        opentelemetry_exporter_otlp_http_log
        opentelemetry_logs
        opentelemetry_http_client_nosend
        opentelemetry_exporter_otlp_json_writer)
      gtest_add_tests(
        TARGET otlp_http_log_record_exporter_custom_json_writer_test
        TEST_PREFIX exporter.otlp.
        TEST_LIST otlp_http_log_record_exporter_custom_json_writer_test)
    endif()
  endif()

  if(OTELCPP_WITH_OTLP_FILE)
    if(OTELCPP_WITH_JSON_WRITER_NLOHMANN)
      add_executable(otlp_file_client_test test/otlp_file_client_test.cc)
      target_link_libraries(
        otlp_file_client_test
        ${GTEST_BOTH_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        ${GMOCK_LIB}
        opentelemetry_exporter_otlp_file
        opentelemetry_otlp_recordable
        opentelemetry_exporter_otlp_json_writer
        nlohmann_json::nlohmann_json)
      gtest_add_tests(
        TARGET otlp_file_client_test
        TEST_PREFIX exporter.otlp.
        TEST_LIST otlp_file_client_test)
    endif()

    if(OTELCPP_WITH_JSON_WRITER_NLOHMANN)
      add_executable(otlp_file_exporter_test test/otlp_file_exporter_test.cc)
      target_link_libraries(
        otlp_file_exporter_test
        ${GTEST_BOTH_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        ${GMOCK_LIB}
        opentelemetry_exporter_otlp_file
        nlohmann_json::nlohmann_json
        protobuf::libprotobuf)
      gtest_add_tests(
        TARGET otlp_file_exporter_test
        TEST_PREFIX exporter.otlp.
        TEST_LIST otlp_file_exporter_test)
    endif()

    add_executable(otlp_file_exporter_factory_test
                   test/otlp_file_exporter_factory_test.cc)
    target_link_libraries(
      otlp_file_exporter_factory_test ${GTEST_BOTH_LIBRARIES}
      ${CMAKE_THREAD_LIBS_INIT} ${GMOCK_LIB} opentelemetry_exporter_otlp_file)
    gtest_add_tests(
      TARGET otlp_file_exporter_factory_test
      TEST_PREFIX exporter.otlp.
      TEST_LIST otlp_file_exporter_factory_test)

    if(OTELCPP_WITH_JSON_WRITER_NLOHMANN)
      add_executable(otlp_file_log_record_exporter_test
                     test/otlp_file_log_record_exporter_test.cc)
      target_link_libraries(
        otlp_file_log_record_exporter_test
        ${GTEST_BOTH_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        ${GMOCK_LIB}
        opentelemetry_exporter_otlp_file_log
        opentelemetry_logs
        nlohmann_json::nlohmann_json)
      gtest_add_tests(
        TARGET otlp_file_log_record_exporter_test
        TEST_PREFIX exporter.otlp.
        TEST_LIST otlp_file_log_record_exporter_test)
    endif()

    add_executable(otlp_file_log_record_exporter_factory_test
                   test/otlp_file_log_record_exporter_factory_test.cc)
    target_link_libraries(
      otlp_file_log_record_exporter_factory_test ${GTEST_BOTH_LIBRARIES}
      ${CMAKE_THREAD_LIBS_INIT} ${GMOCK_LIB}
      opentelemetry_exporter_otlp_file_log opentelemetry_logs)
    gtest_add_tests(
      TARGET otlp_file_log_record_exporter_factory_test
      TEST_PREFIX exporter.otlp.
      TEST_LIST otlp_file_log_record_exporter_factory_test)

    if(OTELCPP_WITH_JSON_WRITER_NLOHMANN)
      add_executable(otlp_file_metric_exporter_test
                     test/otlp_file_metric_exporter_test.cc)
      target_link_libraries(
        otlp_file_metric_exporter_test
        ${GTEST_BOTH_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        ${GMOCK_LIB}
        opentelemetry_exporter_otlp_file_metric
        opentelemetry_metrics
        nlohmann_json::nlohmann_json
        protobuf::libprotobuf)
      gtest_add_tests(
        TARGET otlp_file_metric_exporter_test
        TEST_PREFIX exporter.otlp.
        TEST_LIST otlp_file_metric_exporter_test)
    endif()

    add_executable(otlp_file_metric_exporter_factory_test
                   test/otlp_file_metric_exporter_factory_test.cc)
    target_link_libraries(
      otlp_file_metric_exporter_factory_test ${GTEST_BOTH_LIBRARIES}
      ${CMAKE_THREAD_LIBS_INIT} ${GMOCK_LIB}
      opentelemetry_exporter_otlp_file_metric opentelemetry_metrics)
    gtest_add_tests(
      TARGET otlp_file_metric_exporter_factory_test
      TEST_PREFIX exporter.otlp.
      TEST_LIST otlp_file_metric_exporter_factory_test)
  endif()
endif() # OTELCPP_BUILD_TESTING

if(OTELCPP_BUILD_TESTING
   AND OTELCPP_WITH_BENCHMARK
   AND OTELCPP_WITH_OTLP_HTTP)
  add_executable(otlp_http_metric_exporter_benchmark
                 benchmark/otlp_http_metric_exporter_benchmark.cc)

  target_link_libraries(
    otlp_http_metric_exporter_benchmark
    PRIVATE opentelemetry_exporter_otlp_http_metric opentelemetry_metrics
            opentelemetry_resources benchmark::benchmark
            ${CMAKE_THREAD_LIBS_INIT})
endif()

if(OTELCPP_BUILD_TESTING AND OTELCPP_WITH_BENCHMARK)
  add_executable(otlp_recordable_benchmark test/otlp_recordable_benchmark.cc)

  target_link_libraries(
    otlp_recordable_benchmark
    PRIVATE opentelemetry_otlp_recordable
            opentelemetry_trace
            opentelemetry_metrics
            opentelemetry_resources
            benchmark::benchmark
            opentelemetry_test_common
            ${CMAKE_THREAD_LIBS_INIT})
endif()
