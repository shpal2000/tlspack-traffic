libdl = meson.get_compiler('cpp').find_library('dl', required : true)

openssl = dependency('openssl')
nlohmann_json_proj = subproject('json')
nlohmann_json_dep = nlohmann_json_proj.get_variable('nlohmann_json_dep')
incdirs = include_directories(['../tlspack-socket/'])

subdir ('rpc_server')

subdir ('tls_client')
subdir ('tls_server')

subdir ('tls_client_perf')
subdir ('tls_server_perf')

subdir ('ocsp_server_perf')


executable('tlspack.exe'
            , sources : [ 'main.cpp', 'app.cpp' ]
            , dependencies: [openssl, nlohmann_json_dep, libdl]
            , include_directories : [incdirs]
            , link_with : [ev_socklib, rpc_serverlib]
            , build_rpath : '/rundir/lib'
            , install_rpath : '/rundir/lib'
            , install_dir : '/srcdir/imgdir/'
            , install : true)

