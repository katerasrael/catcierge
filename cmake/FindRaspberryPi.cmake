function (CHECK_IS_RASPBERRY_PI IS_RPI MODEL)
    if (UNIX AND NOT APPLE)
        file(READ /proc/cpuinfo CPUINFO)

        # Example of what we're looking for:
        # Hardware      : BCM2709
        # Revision      : a01041
        # Serial        : 00000000649760c2

        # Find CPU hardware.
        string(REGEX MATCH "Hardware[ \t]+:[ \t]+(BCM2[78][03][589])" RES "${CPUINFO}")
        string(REGEX REPLACE "Hardware[ \t]+:[ \t]+(BCM2[78][03][589])" "\\1" RES "${RES}")
        #message("Hardware: ${RES}")

        if ("${RES}" STREQUAL "BCM2708")
            set(${IS_RPI} 1 PARENT_SCOPE)
            set(${MODEL} "Raspberry Pi" PARENT_SCOPE)
        elseif("${RES}" STREQUAL "BCM2709")
            set(${IS_RPI} 1 PARENT_SCOPE)
            set(${MODEL} "Raspberry Pi 2" PARENT_SCOPE)
        elseif("${RES}" STREQUAL "BCM2835")
            set(${IS_RPI} 1 PARENT_SCOPE)
            set(${MODEL} "Raspberry Pi 3" PARENT_SCOPE)
        endif()
    else()
        set(${IS_RPI} 0 PARENT_SCOPE)
    endif()
endfunction()
