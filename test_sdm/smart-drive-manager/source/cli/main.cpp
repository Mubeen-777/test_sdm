
#include "MenuSystem.h"
#include "../../include/sdm_config.hpp"

#include <iostream>
#include <csignal>
#include <cstdlib>

using namespace std;

SDMServer *g_server = nullptr;

void signal_handler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
    {
        cout << endl
             << endl;
        cout << "Received shutdown signal..." << endl;
        if (g_server)
        {
            g_server->stop();
        }
        exit(0);
    }
}

int main(int argc, char *argv[])
{
    string config_file = "../../config/default.sdmconf";
    bool server_mode = true;

    for (int i = 1; i < argc; i++)
    {
        string arg = argv[i];

        if (arg == "--config" && i + 1 < argc)
        {
            config_file = argv[++i];
        }
        else if (arg == "--server")
        {
            server_mode = true;
        }
        else if (arg == "--help")
        {
            cout << "Smart Drive Manager - Usage:" << endl;
            cout << "  " << argv[0] << " [OPTIONS]" << endl;
            cout << endl;
            cout << "Options:" << endl;
            cout << "  --config FILE    Use specified configuration file" << endl;
            cout << "  --server         Run in server mode (daemon)" << endl;
            cout << "  --help           Show this help message" << endl;
            cout << endl;
            return 0;
        }
    }

    SDMConfig config;
    cout << "Loading config from: " << config_file << endl;

    if (!config.load_from_file(config_file))
    {
        cout << "Warning: Could not load config file '" << config_file << "'" << endl;
        cout << "Using default configuration..." << endl;
    }
    else
    {
        cout << "Config loaded successfully!" << endl;
    }

    cout << "Database path: " << config.database_path << endl;
    cout << "Index path: " << config.index_path << endl;
    cout << "Port: " << config.port << endl;
    cout << endl;

    if (server_mode)
    {

        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

        // Create and initialize server
        SDMServer server(config);
        g_server = &server;

        if (!server.initialize())
        {
            cerr << "ERROR: Server initialization failed!" << endl;
            return 1;
        }

        if (!server.start())
        {
            cerr << "ERROR: Server start failed!" << endl;
            return 1;
        }

        server.wait_for_shutdown();
    }
    else
    {
        MenuSystem menu(config);

        if (!menu.initialize())
        {
            cerr << "ERROR: Menu system initialization failed!" << endl;
            return 1;
        }

        menu.run();
    }

    return 0;
}