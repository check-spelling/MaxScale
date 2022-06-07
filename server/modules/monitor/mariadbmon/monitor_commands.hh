/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-04-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once
#include "mariadbmon_common.hh"
#include <condition_variable>
#include <atomic>
#include <memory>
#include <mutex>
#include "ssh_utils.hh"

class SERVER;
class MariaDBServer;
class MariaDBMonitor;

namespace mon_op
{

struct Result
{
    ~Result();
    void deep_copy_from(const Result& rhs);

    bool    success {false};
    json_t* output {nullptr};
};

/**
 * This class represents two related things: manual commands (such as manual failover) and long-running
 * automatic commands (such as automatic rebuild-server). These two types are similar in the sense that
 * both block the scheduling of further manual commands and are ran at the end of a monitor tick.
 */
class Operation
{
public:
    Operation(const Operation&) = delete;
    Operation& operator=(const Operation&) = delete;

    Operation() = default;
    virtual ~Operation() = default;

    virtual bool   run() = 0;
    virtual Result result() = 0;
    virtual bool   cancel() = 0;
};

using SOperation = std::unique_ptr<Operation>;

enum class ExecState
{
    NONE,
    SCHEDULED,
    RUNNING,
    DONE
};

struct ResultInfo
{
    Result      res;
    std::string cmd_name;
};

struct ScheduledOp
{
    std::mutex             lock;
    SOperation             op;
    std::string            op_name;
    std::atomic<ExecState> exec_state {ExecState::NONE};

    bool                        current_op_is_manual {false};
    std::condition_variable     result_ready_notifier;
    std::unique_ptr<ResultInfo> result_info;
};

using CmdMethod = std::function<Result (void)>;

/**
 * An operation, likely manual, which completes in one monitor iteration. Does not have internal state.
 */
class SimpleOp : public Operation
{
public:
    explicit SimpleOp(CmdMethod func);

    bool   run() override;
    Result result() override;
    bool   cancel() override;

private:
    CmdMethod m_func;
    Result    m_result;
};

class RebuildServer : public Operation
{
public:
    RebuildServer(MariaDBMonitor& mon, SERVER* target, SERVER* source);

    bool   run() override;
    Result result() override;
    bool   cancel() override;

private:
    SERVER* m_target_srv {nullptr};
    SERVER* m_source_srv {nullptr};

    MariaDBMonitor& m_mon;
    MariaDBServer*  m_target {nullptr};
    MariaDBServer*  m_source {nullptr};

    ssh_util::SSession m_target_ses;
    ssh_util::SSession m_source_ses;

    std::unique_ptr<ssh_util::AsyncCmd> m_target_cmd;
    std::unique_ptr<ssh_util::AsyncCmd> m_source_cmd;

    enum class State
    {
        INIT,
        START_BACKUP_SERVE,
        PREPARE_TARGET,
        START_TRANSFER,
        WAIT_TRANSFER,
        PREPARE_BINLOGS,
        START_TARGET,
        DONE,
        CLEANUP,
    };
    State m_state {State::INIT};

    Result m_result;

    bool init();
    bool serve_backup();
    bool prepare_target();
    bool start_transfer();
    bool wait_transfer();

    bool rebuild_check_preconds();
};
}
