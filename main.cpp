#include <stdio.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <signal.h>
#include <chrono>

#include "./rpc_server/rpc_server.hpp"

#include "./tls_server/tls_server.hpp"
#include "./tls_client/tls_client.hpp"
#include "./tcp_proxy/tcp_proxy.hpp"

app_stats* zone_ev_sockstats = nullptr;
rpc_server_stats* zone_rpc_server_stats = nullptr;
tls_server_stats* zone_tls_server_stats = nullptr;
tls_client_stats* zone_tls_client_stats = nullptr;
tcp_proxy_stats* zone_tcp_proxy_stats = nullptr;

static std::vector<app*>* create_app_list (json cfg_json, int z_index)
{
    std::vector<app*> *app_list = nullptr;

    auto a_list = cfg_json["zones"][z_index]["app_list"];
    for (auto a_it = a_list.begin(); a_it != a_list.end(); ++a_it)
    {
        auto app_json = a_it.value ();

        int app_enable = app_json["enable"].get<int>();
        if (app_enable ==0){
            continue;
        }

        if (zone_ev_sockstats == nullptr)
        {
            zone_ev_sockstats = new app_stats();
        }

        app* next_app = nullptr;
        const char* app_type = app_json["app_type"].get<std::string>().c_str();
        const char* app_label = app_json["app_label"].get<std::string>().c_str();

        if ( strcmp("tls_server", app_type) == 0 )
        {
            if (zone_tls_server_stats == nullptr)
            {
                zone_tls_server_stats = new tls_server_stats ();
            }

            next_app = new tls_server_app (app_json
                                            , zone_tls_server_stats
                                            , zone_ev_sockstats);

        }
        else if ( strcmp("tls_client", app_type) == 0 )
        {
            if (zone_tls_client_stats == nullptr)
            {
                zone_tls_client_stats = new tls_client_stats ();
            }
            
            next_app = new tls_client_app (app_json
                                            , zone_tls_client_stats
                                            , zone_ev_sockstats);
        }
        else if ( strcmp("tcp_proxy", app_type) == 0 )
        {
            if (zone_tcp_proxy_stats == nullptr)
            {
                zone_tcp_proxy_stats = new tcp_proxy_stats ();
            }
            
            next_app = new tcp_proxy_app (app_json
                                            , zone_tcp_proxy_stats
                                            , zone_ev_sockstats);
        }

        if (next_app)
        {
            next_app->set_app_type (app_type);
            next_app->set_app_label (app_label);

            if (app_list == nullptr)
            {
                app_list = new std::vector<app*>;
            }

            app_list->push_back (next_app);
        }
        else
        {
            printf ("unknown app_type %s\n", app_type);
            exit (-1);
        }
    }

    return app_list;
}

#define STOP_BEGIN          1
#define STOP_PROGRESS       2
#define STOP_FINISH         3
#define STOP_NOTIFICATION   4         

class zone_rpc_app : public rpc_server_app
{
public:
    int m_stop_state;
public:
    zone_rpc_app (const char* srv_ip
                    , u_short srv_port
                    , rpc_server_stats* srv_stats) 
        : rpc_server_app (srv_ip, srv_port, srv_stats){
        
        m_stop_state = 0;
    }

    int rpc_handler (const char* rpc_cmd
                        , char* rpc_resp
                        , int rpc_resp_max)
    {
        int ret = 0;

        if (strcmp(rpc_cmd, "stop") == 0) {
            if (m_stop_state < STOP_BEGIN) {
                m_stop_state = STOP_BEGIN;
            }
            switch (m_stop_state)
            {
            case STOP_BEGIN:
                strcpy (rpc_resp, "{\"cmd_resp\" : \"STOP_BEGIN\"}");
                break;
            case STOP_PROGRESS:
                strcpy (rpc_resp, "{\"cmd_resp\" : \"STOP_PROGRESS\"}");
                break;
            case STOP_FINISH:
                strcpy (rpc_resp, "{\"cmd_resp\" : \"STOP_FINISH\"}");
                break;
            default:
                strcpy (rpc_resp, "{\"cmd_resp\" : \"STOP_ERROR\"}");
                break;
            }
        } else if (strcmp(rpc_cmd, "is_init") == 0) {
            strcpy (rpc_resp, "{\"cmd_resp\" : \"init_done\"}");
        } else if (strcmp(rpc_cmd, "get_ev_sockstats") == 0) {
            json j;
            zone_ev_sockstats->dump_json(j);
            std::string s = j.dump();
            if (s.size() < (size_t) rpc_resp_max) {
                strcpy (rpc_resp, s.c_str());
                ret = s.size();
            }
        } else {
            strcpy (rpc_resp, "{\"cmd_resp\" : \"unknown_cmd\"}");
        }

        if (ret == 0)
        {
            ret = strlen(rpc_resp);
        }

        return ret;
    };
};

zone_rpc_app* zone_rpc = nullptr;

int main(int /*argc*/, char **argv) 
{
    char* rpc_ip = argv[1];
    int rpc_port = atoi (argv[2]);
    char* cfg_file = argv[3];
    int z_index = atoi (argv[4]);

    std::ifstream cfg_stream(cfg_file);
    json cfg_json = json::parse(cfg_stream);

    signal(SIGPIPE, SIG_IGN);

    OpenSSL_add_ssl_algorithms ();
    SSL_load_error_strings ();

    std::vector<app*> *app_list = create_app_list (cfg_json, z_index);

    if ( app_list )
    {
        zone_rpc_server_stats = new rpc_server_stats ();
        zone_rpc = new zone_rpc_app (rpc_ip, rpc_port, zone_rpc_server_stats);

        std::chrono::time_point<std::chrono::system_clock> start, end;
        start = std::chrono::system_clock::now();

        bool is_tick_sec = false;
        while (zone_rpc->m_stop_state < STOP_NOTIFICATION)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(1));
            end = std::chrono::system_clock::now();
            auto ms_elapsed 
                = std::chrono::duration_cast<std::chrono::milliseconds>
                (end-start);

            if (ms_elapsed.count() >= 1000)
            {
                start = end;
                is_tick_sec = true;
            }

            if (zone_rpc->m_stop_state == STOP_BEGIN 
                    && zone_rpc->m_stop_state < STOP_PROGRESS) {

                zone_rpc->m_stop_state = STOP_PROGRESS;
                for (app* app_ptr : *app_list)
                {
                    app_ptr->set_stop();
                }
            }

            if (zone_rpc->m_stop_state < STOP_FINISH) {
                for (app* app_ptr : *app_list)
                {
                    app_ptr->run_iter (is_tick_sec);
                }
            }

            zone_rpc->run_iter (is_tick_sec);

            if (zone_rpc->m_stop_state == STOP_PROGRESS){
                bool is_zero_conn_state = true;
                for (app* app_ptr : *app_list)
                {
                    if (not app_ptr->is_zero_conn_state()) {
                        is_zero_conn_state = false;
                        break;
                    }
                }
                if (is_zero_conn_state){
                    zone_rpc->m_stop_state = STOP_FINISH;
                }
            }

            if (is_tick_sec) 
            {
                zone_ev_sockstats->tick_sec ();

                is_tick_sec = false;
            }
        }
    }
    else
    {
        printf ("no apps!\n");
        exit (-1);
    }

    return 0;
}


