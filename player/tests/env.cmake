function(casdap_test_env name)
    set(root ${CMAKE_CURRENT_BINARY_DIR}/testenv/${name})
    file(MAKE_DIRECTORY ${root}/home ${root}/config ${root}/data ${root}/cache ${root}/runtime ${root}/run
         ${root}/state ${root}/sys)
    file(CHMOD ${root}/runtime ${root}/run PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
    set(env
        QT_QPA_PLATFORM=offscreen
        HOME=${root}/home
        XDG_CONFIG_HOME=${root}/config
        XDG_DATA_HOME=${root}/data
        XDG_CACHE_HOME=${root}/cache
        XDG_RUNTIME_DIR=${root}/runtime
        XDG_STATE_HOME=${root}/state
        CASDAP_RUN_DIR=${root}/run
        CASDAP_SYS_ROOT=${root}/sys
        CASDAP_ROTATE_MOTION=0
    )
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "${env}")
endfunction()
