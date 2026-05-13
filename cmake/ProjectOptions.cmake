function(ringdown_apply_project_options target)
  target_compile_features(${target} PUBLIC cxx_std_20)

  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(
      ${target}
      PRIVATE -Wall
              -Wextra
              -Wpedantic
              -Wconversion
              -Wsign-conversion
              -Wshadow)
  endif()
endfunction()
