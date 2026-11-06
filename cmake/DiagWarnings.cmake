# =============================================================================
# cmake/DiagWarnings.cmake - target-scoped warning configuration
# (CCU-DIAG-DSN-CR-009 §4.1)
# =============================================================================
# Warnings are applied per target. Compiler choice, sysroot, target triple,
# optimisation level and debug info are NEVER set here: those come from
# CCU-BUILD toolchain/preset inputs.
function(diag_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall)
    endif()
endfunction()
