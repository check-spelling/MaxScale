/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-10-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "mariadbmon_utils.hh"
#include <maxbase/assert.h>
#include <maxbase/format.hh>

using std::string;

namespace
{
enum class SyncMxs
{
    YES,
    NO
};

bool generate_traffic_and_check(TestConnections& test, mxt::MariaDB* conn, int insert_count, SyncMxs sync);
}

/**
 * Do inserts, check that results are as expected.
 *
 * @param test Test connections
 * @param conn Which specific connection to use
 * @param insert_count How many inserts should be done
 * @return True, if successful
 */
bool generate_traffic_and_check(TestConnections& test, mxt::MariaDB* conn, int insert_count)
{
    return generate_traffic_and_check(test, conn, insert_count, SyncMxs::YES);
}

bool generate_traffic_and_check_nosync(TestConnections& test, mxt::MariaDB* conn, int insert_count)
{
    return generate_traffic_and_check(test, conn, insert_count, SyncMxs::NO);
}

namespace
{
bool generate_traffic_and_check(TestConnections& test, mxt::MariaDB* conn, int insert_count, SyncMxs sync)
{
    const bool wait_sync = (sync == SyncMxs::YES);
    const char table[] = "test.t1";
    int inserts_start = 1;

    auto show_tables = conn->query("show tables from test like 't1';");
    if (show_tables && show_tables->next_row() && show_tables->get_string(0) == "t1")
    {
        auto res = conn->query_f("select count(*) from %s;", table);
        if (res && res->next_row())
        {
            inserts_start = res->get_int(0) + 1;
        }
    }
    else if (test.ok())
    {
        conn->cmd_f("create table %s(c1 int)", table);
    }

    bool ok = false;
    if (test.ok())
    {
        int inserts_end = inserts_start + insert_count;

        ok = true;
        for (int i = inserts_start; i <= inserts_end && ok; i++)
        {
            ok = conn->cmd_f("insert into %s values (%d);", table, i);
        }

        if (ok)
        {
            if (wait_sync)
            {
                test.sync_repl_slaves();
            }

            auto res = conn->query_f("SELECT * FROM %s;", table);
            if (res)
            {
                // Check all values, they should go from 1 to inserts_end
                int expected_val = 0;
                while (res->next_row() && ok)
                {
                    expected_val++;
                    auto value = res->get_int(0);
                    if (value != expected_val)
                    {
                        test.add_failure("Query returned %ld when %d was expected.", value, expected_val);
                        ok = false;
                    }
                }

                if (ok && expected_val != inserts_end)
                {
                    test.add_failure("Query returned %d rows when %d rows were expected.",
                                     expected_val, insert_count);
                    ok = false;
                }

                if (ok && wait_sync)
                {
                    // Wait for monitor to detect gtid change.
                    test.maxscale->wait_for_monitor();
                }
            }
            else
            {
                ok = false;
            }
        }
    }
    return ok;
}
}

void prepare_log_bin_failover_test(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;
    mxs.stop_maxscale();

    repl.stop_node(1);
    repl.stash_server_settings(1);
    repl.disable_server_setting(1, "log-bin");
    repl.disable_server_setting(1, "log_bin");
    repl.start_node(1);

    repl.stop_node(2);
    repl.stash_server_settings(2);
    repl.disable_server_setting(2, "log-slave-updates");
    repl.disable_server_setting(2, "log_slave_updates");
    repl.start_node(2);

    mxs.start_maxscale();
    mxs.wait_for_monitor(1);
}

void cleanup_log_bin_failover_test(TestConnections& test)
{
    // Restore server2 and 3 settings.
    auto& repl = *test.repl;
    test.tprintf("Restoring server settings.");

    repl.stop_node(1);
    repl.restore_server_settings(1);
    repl.start_node(1);

    repl.stop_node(2);
    repl.restore_server_settings(2);
    repl.start_node(2);

    test.maxscale->wait_for_monitor(1);
}

namespace testclient
{
Client::Client(TestConnections& test, const Settings& sett, int id, bool verbose)
    : m_test(test)
    , m_settings(sett)
    , m_id(id)
    , m_verbose(verbose)
    , m_value(1)
    , m_rand_dist(0.0, 1.0)
{
}

void Client::start()
{
    m_keep_running = true;
    m_thread = std::thread(&Client::run, this);
}

void Client::stop()
{
    m_keep_running = false;
    m_thread.join();
}

Client::Action Client::action() const
{
    double d = random_decimal_fraction();

    // 20% updates
    // 80% selects
    if (d <= 0.2)
    {
        return Action::UPDATE;
    }
    else
    {
        return Action::SELECT;
    }
}

bool Client::run_query(mxt::MariaDB& conn)
{
    bool rv = false;

    switch (action())
    {
    case Action::SELECT:
        rv = run_select(conn);
        break;

    case Action::UPDATE:
        rv = run_update(conn);
        break;
    }

    return rv;
}

bool Client::run_select(mxt::MariaDB& conn)
{
    bool rv = true;
    auto res = conn.try_query_f("SELECT * FROM test.t%d WHERE id=%i;", m_id, get_random_id());
    if (!res)
    {
        rv = false;
    }
    return rv;
}

bool Client::run_update(mxt::MariaDB& conn)
{
    bool rv = false;
    m_value = (m_value + 1) % m_settings.rows;
    int id = get_random_id();
    bool res = conn.try_cmd_f("UPDATE test.t%d SET value=%zu WHERE id=%i;", m_id, m_value, id);
    if (res)
    {
        m_values[id] = m_value;
        rv = true;
    }
    return rv;
}

int Client::get_random_id() const
{
    int id = m_settings.rows * random_decimal_fraction();

    mxb_assert(id >= 0);
    mxb_assert(id <= m_settings.rows);

    return id;
}

double Client::random_decimal_fraction() const
{
    return m_rand_dist(m_rand_gen);
}

void Client::run()
{
    mxt::MariaDB conn(m_test.logger());
    conn.set_log_query_fails(false);
    auto& sett = conn.connection_settings();
    sett.timeout = 5;
    sett.user = m_settings.user;
    sett.password = m_settings.pw;

    while (m_keep_running)
    {
        if (conn.try_open(m_settings.host, m_settings.port, "test"))
        {
            if (m_verbose)
            {
                m_test.tprintf("Client %i connected, starting queries.", m_id);
            }
            while (m_keep_running && run_query(conn))
            {
            }
        }
        else if (m_verbose)
        {
            m_test.tprintf("Test client %i connection failed: %s", m_id, conn.error());
        }

        // Wait a bit before opening another connection.
        sleep(1);
        if (m_verbose)
        {
            m_test.tprintf("Client %i connection closed.", m_id);
        }
    }
}

bool Client::create_table(mxt::MariaDB& conn)
{
    bool rval = false;

    // Make a table with two integer columns, both with values 0 -- (rows - 1).
    string tbl = mxb::string_printf("test.t%d", m_id);
    if (conn.try_cmd_f("create or replace table %s (id int, value int);", tbl.c_str()))
    {
        string insert = mxb::string_printf("insert into %s values ", tbl.c_str());
        for (int i = 0; i < m_settings.rows; i++)
        {
            string val = std::to_string(i);
            insert.append("(").append(val).append(",").append(val).append(")");

            if (i < m_settings.rows - 1)
            {
                insert += ", ";
            }
        }
        insert.append(";");
        rval = conn.try_cmd(insert);

        if (rval)
        {
            m_values.resize(m_settings.rows);
            for (int i = 0; i < m_settings.rows; i++)
            {
                m_values[i] = i;
            }
        }
    }
    return rval;
}

bool Client::drop_table(mxt::MariaDB& conn)
{
    return conn.try_cmd_f("drop table test.t%d;", m_id);
}

ClientGroup::ClientGroup(TestConnections& test, int nClients, Settings settings)
    : m_test(test)
    , m_nClients(nClients)
    , m_settings(std::move(settings))
{
}

bool ClientGroup::prepare()
{
    mxb_assert(m_clients.empty());
    for (int i = 0; i < m_nClients; i++)
    {
        auto new_client = std::make_unique<Client>(m_test, m_settings, i, m_test.verbose());
        m_clients.emplace_back(std::move(new_client));
    }

    bool success = create_tables();
    if (success)
    {
        m_test.repl->sync_slaves(0, 30);
    }
    m_test.expect(success, "Test client preparation failed.");
    return success;
}

void ClientGroup::cleanup()
{
    m_test.tprintf("Dropping tables.");
    auto sConn = m_test.maxscale->open_rwsplit_connection2();
    if (sConn->is_open())
    {
        for (auto& client : m_clients)
        {
            client->drop_table(*sConn);
        }
    }
    m_clients.clear();
}

void ClientGroup::start()
{
    m_test.tprintf("Starting %i clients. Connecting to %s:%i as '%s'.",
                   m_nClients, m_settings.host.c_str(), m_settings.port, m_settings.user.c_str());

    for (auto& client : m_clients)
    {
        client->start();
    }
}

void ClientGroup::stop()
{
    for (auto& client : m_clients)
    {
        client->stop();
    }
}

bool ClientGroup::create_tables()
{
    m_test.tprintf("Creating %zu tables.", m_clients.size());
    bool rval = false;
    auto sConn = m_test.maxscale->open_rwsplit_connection2();
    if (sConn->is_open())
    {
        rval = true;
        for (auto& client : m_clients)
        {
            if (!client->create_table(*sConn))
            {
                rval = false;
            }
        }
    }
    return rval;
}
}

namespace stress_test
{
void run_failover_stress_test(TestConnections& test, const BaseSettings& base_sett,
                              const testclient::Settings& client_sett)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;

    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    testclient::ClientGroup clients(test, base_sett.test_clients, client_sett);
    clients.prepare();

    if (test.ok())
    {
        clients.start();
        time_t start = time(NULL);
        sleep(1);
        int failovers = 0;

        while (test.ok() && (time(NULL) - start < base_sett.test_duration))
        {
            auto servers = mxs.get_servers();
            servers.print();
            auto master = servers.get_master();
            bool have_master = master.server_id > 0;
            int slaves = servers.get_role_info().slaves;

            if (have_master && slaves >= 1)
            {
                // Can do another failover.
                test.tprintf("Stopping master '%s'", master.name.c_str());
                int old_master_ind = master.server_id - 1;
                repl.stop_node(old_master_ind);
                mxs.sleep_and_wait_for_monitor(2, 3);

                // Failover should have happened, check.
                auto servers_after = mxs.get_servers();
                auto new_master = servers_after.get_master();
                if (new_master.server_id >= 0 && new_master.server_id != master.server_id)
                {
                    failovers++;
                    test.tprintf("Failover %i successfull.", failovers);
                }
                else if (new_master.server_id >= 0)
                {
                    test.add_failure("Master did not change, '%s' is still master.", new_master.name.c_str());
                }
                else
                {
                    test.add_failure("Failover didn't happen, no master.");
                }
                test.tprintf("Starting old master '%s'", master.name.c_str());
                repl.start_node(old_master_ind);
                sleep(1);
            }
            else if (have_master)
            {
                test.tprintf("No more slaves to promote, cannot continue.");
            }
            else
            {
                test.tprintf("No master, cannot continue");
            }

            int diverged = 3 - slaves;
            if (diverged > 0)
            {
                if (base_sett.diverging_allowed)
                {
                    test.tprintf("%i slaves have diverged.", diverged);
                }
                else
                {
                    test.add_failure("%i slaves have diverged.", diverged);
                }
            }
        }

        test.expect(failovers >= base_sett.min_expected_failovers,
                    "Expected at least %i failovers, but only managed %i.",
                    base_sett.min_expected_failovers, failovers);
        mxs.wait_for_monitor();
        clients.stop();
    }
    clients.cleanup();

    // Restore servers.
    auto servers = mxs.get_servers();
    auto roles = servers.get_role_info();
    if (roles.masters == 1 && roles.slaves == 3)
    {
        if (servers.get(0).status == mxt::ServerInfo::master_st)
        {
            // server1 is already master, no need to anything
        }
        else
        {
            mxs.maxctrl("call command mariadbmon switchover MySQL-Monitor server1");
        }
    }
    else
    {
        // Replication broken.
        mxs.maxctrl("call command mariadbmon reset-replication MySQL-Monitor server1");
    }

    mxs.wait_for_monitor(2);
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
}

void check_semisync_off(TestConnections& test)
{
    for (int i = 0; i < test.repl->N; i++)
    {
        check_semisync_status(test, i, false, false, 0);
    }
}

void check_semisync_status(TestConnections& test, int node, bool master, bool slave, int expected_clients)
{
    const string semis_master = "Rpl_semi_sync_master_status";
    const string semis_slave = "Rpl_semi_sync_slave_status";
    const string semis_clients = "Rpl_semi_sync_master_clients";

    auto conn = test.repl->backend(node)->open_connection();
    auto res = conn->query("show status like 'Rpl%';");
    if (res && res->get_col_count() == 2)
    {
        string master_val;
        string slave_val;
        int clients_val = -1;

        while (res->next_row())
        {
            string var_name = res->get_string(0);
            if (var_name == semis_master)
            {
                master_val = res->get_string(1);
            }
            else if (var_name == semis_slave)
            {
                slave_val = res->get_string(1);
            }
            else if (var_name == semis_clients)
            {
                clients_val = res->get_int(1);
            }
        }

        const char* expected_master = master ? "ON" : "OFF";
        const char* expected_slave = slave ? "ON" : "OFF";
        const char fmt[] = "Wrong value for '%s' for node%i. Expected '%s', got '%s'";
        test.expect(master_val == expected_master, fmt, semis_master.c_str(), node, expected_master,
                    master_val.c_str());
        test.expect(slave_val == expected_slave, fmt, semis_slave.c_str(), node, expected_slave,
                    slave_val.c_str());
        test.expect(clients_val == expected_clients,
                    "Wrong value for '%s' for node%i. Expected '%i', got '%i'",
                    semis_clients.c_str(), node, expected_clients, clients_val);
    }
    else
    {
        test.add_failure("No status variables matched name 'Rpl%%'.");
    }
}
}
