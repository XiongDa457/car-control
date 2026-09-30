include_guard(GLOBAL)

function (CHECK_VAR VAR_NAME VAR_DESC)
    if (NOT DEFINED DESC)
        set(DESC "")
    endif()

    if(DEFINED ${VAR_NAME})
        set(${VAR_NAME} "${${VAR_NAME}}" CACHE STRING ${VAR_DESC} FORCE)
    elseif(DEFINED ENV{${VAR_NAME}})
        set(${VAR_NAME} "$ENV{${VAR_NAME}}" CACHE STRING ${VAR_DESC} FORCE)
    endif()

    if(NOT ${VAR_NAME})
        message(FATAL_ERROR "${VAR_NAME} is not set. Please set the ${VAR_NAME} environment variable or pass -D${VAR_NAME}=...")
    endif()
endfunction()
