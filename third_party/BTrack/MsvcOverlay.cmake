# Generate a Windows-only copy of the pinned GPLv3 BTrack source. Preserve the
# vendor files and all arithmetic; replace GNU VLAs with equivalent uninitialised
# RAII-owned arrays on the analysis worker, where allocation is permitted.
set(BTRACK_MSVC_VENDOR_SOURCE "${CMAKE_CURRENT_LIST_DIR}/src/BTrack.cpp")
function(btrack_generate_msvc_overlay destination)
    set(input "${BTRACK_MSVC_VENDOR_SOURCE}")
    file(READ "${input}" source)
    string(REPLACE "\r\n" "\n" source "${source}")
    string(SHA256 source_hash "${source}")
    if(NOT source_hash STREQUAL "e787d20139c1628b2710330c3eaa3ec0a1ad1d5a9f02a789c658c48dffc829b0")
        message(FATAL_ERROR "BTrack MSVC overlay: pinned source changed")
    endif()
    string(REPLACE "#include \"BTrack.h\""
        "#include \"BTrack.h\"\n#include <memory>" source "${source}")
    foreach(spec IN ITEMS
            "float|input|onsetDFBufferSize"
            "double|threshold|N"
            "double|logGaussianTransitionWeighting|windowSize"
            "double|futureCumulativeScore|onsetDFBufferSize + beatExpectationWindowSize"
            "double|beatExpectationWindow|beatExpectationWindowSize"
            "double|logGaussianTransitionWeighting|pastWindowSize")
        string(REPLACE "|" ";" fields "${spec}")
        list(GET fields 0 type)
        list(GET fields 1 name)
        list(GET fields 2 extent)
        set(old "${type} ${name}[${extent}];")
        string(FIND "${source}" "${old}" match)
        if(match EQUAL -1)
            message(FATAL_ERROR "BTrack MSVC overlay: missing declaration ${old}")
        endif()
        set(new "std::unique_ptr<${type}[]> ${name}Storage(new ${type}[${extent}]);\n    ${type}* ${name} = ${name}Storage.get();")
        string(REPLACE "${old}" "${new}" source "${source}")
    endforeach()
    get_filename_component(directory "${destination}" DIRECTORY)
    file(MAKE_DIRECTORY "${directory}")
    # Avoid unnecessary object rebuilds on a repeated configure.
    set(temporary "${destination}.tmp")
    file(WRITE "${temporary}" "${source}")
    configure_file("${temporary}" "${destination}" COPYONLY)
    file(REMOVE "${temporary}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
endfunction()
