cmake_minimum_required(VERSION 3.13)

project(OsLaba2Var2Common)

add_library(OsLaba2Var2Common INTERFACE)
target_sources(OsLaba2Var2Common INTERFACE
        include/utils.h
        include/os2var2_common.h
        include/nlohmann/adl_serializer.hpp
        include/nlohmann/detail/conversions/from_json.hpp
        include/nlohmann/detail/conversions/to_chars.hpp
        include/nlohmann/detail/conversions/to_json.hpp
        include/nlohmann/detail/exceptions.hpp
        include/nlohmann/detail/input/binary_reader.hpp
        include/nlohmann/detail/input/input_adapters.hpp
        include/nlohmann/detail/input/json_sax.hpp
        include/nlohmann/detail/input/lexer.hpp
        include/nlohmann/detail/input/parser.hpp
        include/nlohmann/detail/input/position_t.hpp
        include/nlohmann/detail/iterators/internal_iterator.hpp
        include/nlohmann/detail/iterators/iteration_proxy.hpp
        include/nlohmann/detail/iterators/iterator_traits.hpp
        include/nlohmann/detail/iterators/iter_impl.hpp
        include/nlohmann/detail/iterators/json_reverse_iterator.hpp
        include/nlohmann/detail/iterators/primitive_iterator.hpp
        include/nlohmann/detail/json_pointer.hpp
        include/nlohmann/detail/json_ref.hpp
        include/nlohmann/detail/macro_scope.hpp
        include/nlohmann/detail/macro_unscope.hpp
        include/nlohmann/detail/meta/cpp_future.hpp
        include/nlohmann/detail/meta/detected.hpp
        include/nlohmann/detail/meta/is_sax.hpp
        include/nlohmann/detail/meta/type_traits.hpp
        include/nlohmann/detail/meta/void_t.hpp
        include/nlohmann/detail/output/binary_writer.hpp
        include/nlohmann/detail/output/output_adapters.hpp
        include/nlohmann/detail/output/serializer.hpp
        include/nlohmann/detail/value_t.hpp
        include/nlohmann/json.hpp
        include/nlohmann/json_fwd.hpp
        )

target_include_directories(OsLaba2Var2Common
        INTERFACE
            "${PROJECT_SOURCE_DIR}/include"
        )

target_link_libraries(OsLaba2Var2Common
        INTERFACE
            ws2_32)