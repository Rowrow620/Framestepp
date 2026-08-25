function(framestepp_enable_sanitizers target)
    if(NOT FRAMESTEPP_ENABLE_SANITIZERS)
        return()
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    else()
        message(FATAL_ERROR
            "AddressSanitizer and UndefinedBehaviorSanitizer require GCC or Clang"
        )
    endif()
endfunction()
