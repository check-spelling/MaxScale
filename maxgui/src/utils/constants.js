/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import i18n from 'plugins/i18n'

export const APP_CONFIG = Object.freeze({
    productName: i18n.t('productName'),
    asciiLogo: `
.___  ___.      ___      ___   ___      _______.  ______      ___       __       _______
|   \\/   |     /   \\     \\  \\ /  /     /       | /      |    /   \\     |  |     |   ____|
|  \\  /  |    /  ^  \\     \\  V  /     |   (----'|  ,----'   /  ^  \\    |  |     |  |__
|  |\\/|  |   /  /_\\  \\     >   <       \\   \\    |  |       /  /_\\  \\   |  |     |   __|
|  |  |  |  /  _____  \\   /  .  \\  .----)   |   |  '----. /  _____  \\  |  '----.|  |____
|__|  |__| /__/     \\__\\ /__/ \\__\\ |_______/     \\______|/__/     \\__\\ |_______||_______|
`,
    SQL_NODE_TYPES: Object.freeze({
        SCHEMA: 'Schema',
        TABLES: 'Tables',
        TABLE: 'Table',
        COLS: 'Columns',
        COL: 'Column',
        TRIGGERS: 'Triggers',
        TRIGGER: 'Trigger',
        SPS: 'Stored Procedures',
        SP: 'Stored Procedure',
    }),
    SQL_SYS_SCHEMAS: ['information_schema', 'performance_schema', 'mysql', 'sys'],
    // schema tree node context option types
    SQL_NODE_CTX_OPTS: Object.freeze({
        SQL_TXT_EDITOR_OPT_TYPES: { INSERT: 'INSERT', QUERY: 'QUERY' },
        SQL_DDL_OPT_TYPES: { DD: 'DD' }, // Data definition
        SQL_ADMIN_OPT_TYPES: { USE: 'USE' }, // Data definition
    }),

    SQL_QUERY_MODES: Object.freeze({
        PRVW_DATA: 'PRVW_DATA',
        PRVW_DATA_DETAILS: 'PRVW_DATA_DETAILS',
        QUERY_VIEW: 'QUERY_VIEW',
        HISTORY: 'HISTORY',
        FAVORITE: 'FAVORITE',
    }),
    SQL_DDL_ALTER_SPECS: Object.freeze({
        COLUMNS: 'COLUMNS',
    }),
    SQL_EDITOR_MODES: Object.freeze({
        TXT_EDITOR: 'TXT_EDITOR',
        DDL_EDITOR: 'DDL_EDITOR',
    }),
    QUERY_LOG_TYPES: Object.freeze({
        USER_LOGS: i18n.t('userQueryLogs'),
        ACTION_LOGS: i18n.t('actionLogs'),
    }),
})
