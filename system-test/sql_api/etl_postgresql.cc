/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-12-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxbase/http.hh>
#include <maxtest/generate_sql.hh>
#include <maxtest/docker.hh>

#include "etl_common.hh"
namespace
{
void compare_values(EtlTest& etl, const std::string& dsn, const sql_generation::SQLType& t)
{
    if (t.type_name == "TIMESTAMP")
    {
        std::string TIMESTAMP_SELECT =
            "SELECT "
            "CAST(EXTRACT(YEAR FROM a) AS INT) y, "
            "CAST(EXTRACT(MONTH FROM a) AS INT) m, "
            "CAST(EXTRACT(DAY FROM a) AS INT) d, "
            "CAST(EXTRACT(HOUR FROM a) AS INT) h, "
            "CAST(EXTRACT(MINUTE FROM a) AS INT) min, "
            "CAST(EXTRACT(SECOND FROM a) AS INT) sec "
            " FROM " + t.full_name;

        etl.compare_results(dsn, 0, TIMESTAMP_SELECT);
    }
    else if (t.type_name == "UUID")
    {
        etl.compare_results(dsn, 0, "SELECT LOWER(CAST(a AS VARCHAR(200))) uuid_lower FROM " + t.full_name);
    }
    else
    {
        etl.compare_results(dsn, 0, "SELECT * FROM " + t.full_name);
    }
}

std::string big_number(int n, int d)
{
    mxb_assert(d < n);
    std::string rval(n + (d ? 1 : 0), '0');
    rval.front() = '1';
    rval[n - d] = '.';
    rval.back() = '1';
    return rval;
}

std::string_view unquote(std::string_view str)
{
    return str.size() >= 2 && str.front() == '\'' && str.back() == '\'' ?
           str.substr(1, str.size() - 2) : str;
}
}

void sanity_check(TestConnections& test, EtlTest& etl, const std::string& dsn)
{
    // By default the tables are created in the public schema of the user's own default database. In our case
    // the database name is maxskysql.
    if (test.expect(etl.query_odbc(dsn, "CREATE TABLE public.sanity_check(id INT)")
                    && etl.query_odbc(dsn, "INSERT INTO public.sanity_check VALUES (1), (2), (3)"),
                    "Failed to create tables in Postgres"))
    {
        auto [ok, res] = etl.run_etl(dsn, "server1", "postgresql", EtlTest::Op::START, 15s,
                                     {EtlTable {"public", "sanity_check"}});

        if (test.expect(ok, "ETL failed: %s", res.to_string().c_str()))
        {
            etl.compare_results(dsn, 0, "SELECT id FROM public.sanity_check ORDER BY id");
        }

        test.expect(etl.query_odbc(dsn, "DROP TABLE public.sanity_check")
                    && etl.query_native("server1", "DROP TABLE public.sanity_check"),
                    "Failed to drop tables in Postgres");
    }
}

void massive_result(TestConnections& test, EtlTest& etl, const std::string& dsn)
{
    if (test.expect(!!etl.query_odbc(dsn, "CREATE TABLE public.massive_result(id INT)"),
                    "Failed to create tables in Postgres"))
    {
        auto [ok, res] = etl.run_etl(dsn, "server1", "postgresql", EtlTest::Op::START, 150s,
                                 {EtlTable {"public", "massive_result",
                                            "CREATE OR REPLACE TABLE test.massive_result(id INT PRIMARY KEY) ENGINE=MEMORY",
                                            "SELECT 1 id FROM generate_series(0, 10000000)",
                                            "REPLACE INTO test.massive_result(id) VALUES (?)"
                                  }});

        test.expect(ok, "ETL failed: %s", res.to_string().c_str());

        test.expect(etl.query_odbc(dsn, "DROP TABLE public.massive_result")
                    && etl.query_native("server1", "DROP TABLE public.sanity_check"),
                    "Failed to drop tables in Postgres");
    }
}

void test_datatypes(TestConnections& test, EtlTest& etl, const std::string& dsn)
{
    auto dest = test.repl->get_connection(0);
    test.expect(dest.connect(), "Failed to connect to node 0: %s", dest.error());
    dest.query("SET SQL_MODE='ANSI_QUOTES'");

    for (const auto& t : sql_generation::postgres_types())
    {
        for (const auto& val : t.values)
        {
            etl.check_odbc_result(dsn, t.create_sql);
            etl.check_odbc_result(dsn, val.insert_sql);

            auto [ok, res] = etl.run_etl(dsn, "server1", "postgresql", EtlTest::Op::START, 15s,
                                         {EtlTable {t.database_name, t.table_name}});

            if (test.expect(ok, "ETL failed for %s %s: %s", t.type_name.c_str(), val.value.c_str(),
                            res.to_string().c_str()))
            {
                compare_values(etl, dsn, t);
            }

            etl.check_odbc_result(dsn, t.drop_sql);
            test.expect(dest.query(t.drop_sql), "Failed to drop: %s", dest.error());
        }
    }
}

void test_parallel_datatypes(TestConnections& test, EtlTest& etl, const std::string& dsn)
{
    auto dest = test.repl->get_connection(0);
    test.expect(dest.connect(), "Failed to connect to node 0: %s", dest.error());
    dest.query("SET SQL_MODE='ANSI_QUOTES'");

    std::vector<EtlTable> tables;

    for (const auto& t : sql_generation::postgres_types())
    {

        etl.check_odbc_result(dsn, t.create_sql);

        for (const auto& val : t.values)
        {
            etl.check_odbc_result(dsn, val.insert_sql);
        }


        tables.emplace_back(t.database_name, t.table_name);
    }


    auto [ok, res] = etl.run_etl(dsn, "server1", "postgresql", EtlTest::Op::START, 15s,
                                 tables);

    test.expect(ok, "ETL failed: %s", res.to_string().c_str());

    for (const auto& t : sql_generation::postgres_types())
    {
        compare_values(etl, dsn, t);
        etl.check_odbc_result(dsn, t.drop_sql);
        test.expect(dest.query(t.drop_sql), "Failed to drop: %s", dest.error());
    }
}

void big_numbers(TestConnections& test, EtlTest& etl, const std::string& dsn)
{
    // The arguments to DECIMAL are the precision and the scale: the total amount of numbers on both sides of
    // the decimal point and how many numbers can appear after the decimal point.
    etl.check_odbc_result(dsn, "CREATE TABLE public.big_numbers(a DECIMAL(65,38))");

    std::string sql;

    for (int i = 1; i < (65 - 38) && test.ok(); i++)
    {
        for (int d = 0; d <= 38 && d < i && test.ok(); d++)
        {
            sql += "INSERT INTO public.big_numbers VALUES (" + big_number(i, d) + ");";
        }
    }

    etl.check_odbc_result(dsn, sql);

    auto [ok, res] = etl.run_etl(dsn, "server1", "postgresql", EtlTest::Op::START, 15s,
                                 {EtlTable {"public", "big_numbers"}});

    if (test.expect(ok, "ETL failed: %s", res.to_string().c_str()))
    {
        etl.compare_results(dsn, 0, "SELECT * FROM public.big_numbers");
    }

    auto dest = test.repl->get_connection(0);
    test.expect(dest.connect(), "Failed to connect to node 0: %s", dest.error());

    const auto DROP_SQL = "DROP TABLE public.big_numbers";
    etl.check_odbc_result(dsn, DROP_SQL);
    dest.query(DROP_SQL);
}

void test_main(TestConnections& test)
{
    mxt::Docker docker(test, "postgres:14", "pg", {5432},
                       {"POSTGRES_USER=maxskysql", "POSTGRES_PASSWORD=skysql"},
                       "psql -U maxskysql -c \"SELECT 1\"");
    EtlTest etl(test);
    std::string dsn = "DRIVER=psqlodbcw.so;"
                      "UID=maxskysql;"
                      "PWD=skysql;"
                      "SERVER=127.0.0.1;"
                      "PORT=5432;"
                      "BoolsAsChar=0;";

    TestCases test_cases = {
        TESTCASE(sanity_check),
        TESTCASE(massive_result),
        TESTCASE(test_datatypes),
        TESTCASE(test_parallel_datatypes),
        TESTCASE(big_numbers),
    };

    etl.check_odbc_result(dsn, "CREATE SCHEMA test");

    etl.run_tests(dsn, test_cases);
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
