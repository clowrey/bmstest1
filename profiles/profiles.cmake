# Pack profile selection, to specify the defaults, limits and pack layout.
#
#   -DBMS_PROFILE=<name>      profile to build (file name without .h)
#   -DBMS_PROFILE_DIR=<dir>   optional extra directory of custom profiles,
#                             e.g. one kept outside the repo
#
# The BMS_PROFILE_DIR, profiles/custom and profiles/builtin folders are searched
# for the requested profile.

set(BMS_PROFILES_ROOT ${CMAKE_CURRENT_LIST_DIR})

set(BMS_PROFILE "bluetesla" CACHE STRING "Pack profile to build (see profiles/README.md)")
set(BMS_PROFILE_DIR "" CACHE PATH "Extra directory to search for custom pack profiles")

function(bms_list_profiles out)
    set(names)
    foreach(dir IN ITEMS ${BMS_PROFILE_DIR} ${BMS_PROFILES_ROOT}/custom ${BMS_PROFILES_ROOT}/builtin)
        file(GLOB headers ${dir}/*.h)
        foreach(h IN LISTS headers)
            get_filename_component(n ${h} NAME_WE)
            list(APPEND names ${n})
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES names)
    list(SORT names)
    string(REPLACE ";" ", " names "${names}")
    set(${out} "${names}" PARENT_SCOPE)
endfunction()

function(bms_resolve_profile name out_header)
    if(NOT name MATCHES "^[A-Za-z0-9_-]+$")
        bms_list_profiles(available)
        message(FATAL_ERROR "Invalid BMS_PROFILE '${name}'. Profiles are named by file, "
            "e.g. -DBMS_PROFILE=bluetesla. Available: ${available}")
    endif()

    set(found)
    foreach(dir IN ITEMS ${BMS_PROFILE_DIR} ${BMS_PROFILES_ROOT}/custom ${BMS_PROFILES_ROOT}/builtin)
        if(EXISTS ${dir}/${name}.h)
            list(APPEND found ${dir}/${name}.h)
        endif()
    endforeach()

    list(LENGTH found count)
    if(count EQUAL 0)
        bms_list_profiles(available)
        message(FATAL_ERROR "No pack profile '${name}'. Available: ${available}\n"
            "(Profiles are named by file now: BMS_LFP1 is lfp1, and so on.)")
    elseif(count GREATER 1)
        string(REPLACE ";" "\n  " found "${found}")
        message(FATAL_ERROR "Pack profile '${name}' is defined more than once:\n  ${found}\n"
            "Give the custom profile a different name.")
    endif()

    set(${out_header} ${found} PARENT_SCOPE)
endfunction()

bms_resolve_profile(${BMS_PROFILE} BMS_PROFILE_HEADER)
message(STATUS "Pack profile: ${BMS_PROFILE} (${BMS_PROFILE_HEADER})")

add_compile_definitions(
    BMS_PROFILE_HEADER="${BMS_PROFILE_HEADER}"
    BMS_PROFILE_NAME="${BMS_PROFILE}"
)
include_directories(${BMS_PROFILES_ROOT})
