/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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
import { lodash } from '@share/utils/helpers'
import queryHelper from '@queryEditorSrc/store/queryHelper'

const statesToBeSynced = queryHelper.syncStateCreator('schemaSidebar')

const memStates = queryHelper.memStateCreator('schemaSidebar')

export default {
    namespaced: true,
    state: {
        ...memStates,
        ...statesToBeSynced,
    },
    mutations: {
        ...queryHelper.memStatesMutationCreator(memStates),
        ...queryHelper.syncedStateMutationsCreator({
            statesToBeSynced,
            persistedArrayPath: 'wke.worksheets_arr',
        }),
    },
    actions: {
        async initialFetch({ dispatch }) {
            await dispatch('fetchSchemas')
            await dispatch('queryConn/updateActiveDb', {}, { root: true })
        },
        /**
         * @param {Object} payload.node - A node object having children nodes
         * @param {Array} payload.db_tree - Array of tree node to be updated
         * @param {Array} payload.cmpList - Array of completion list for editor
         * @returns {Array} { new_db_tree: {}, new_cmp_list: [] }
         */
        async getNewDbTree({ rootState }, { node, db_tree, cmpList }) {
            const {
                SQL_NODE_TYPES: { TABLES, SPS, COLS, TRIGGERS },
            } = rootState.queryEditorConfig.config
            try {
                const dbName = queryHelper.getDbName(node)
                const tblName = queryHelper.getTblName(node)

                const { nodes, cmpList: partCmpList } = await queryHelper.getNodeData({
                    scope: this,
                    node,
                    dbName,
                    tblName,
                })

                //TODO: DRY these, so VIEWS and VIEW nodes can be updated
                switch (node.type) {
                    case TABLES:
                    case SPS: {
                        const new_db_tree = queryHelper.updateDbChild({
                            db_tree,
                            dbName,
                            childType: node.type,
                            nodes,
                        })
                        return { new_db_tree, new_cmp_list: [...cmpList, ...partCmpList] }
                    }
                    case COLS:
                    case TRIGGERS: {
                        const new_db_tree = queryHelper.updateTblChild({
                            db_tree,
                            dbName,
                            tblName,
                            childType: node.type,
                            nodes,
                        })
                        return { new_db_tree, new_cmp_list: [...cmpList, ...partCmpList] }
                    }
                }
            } catch (e) {
                this.vue.$logger('store-schemaSidebar-getNewDbTree').error(e)
                return { new_db_tree: {}, new_cmp_list: [] }
            }
        },
        async loadChildNodes({ commit, dispatch, rootState, getters }, node) {
            const active_wke_id = rootState.wke.active_wke_id
            try {
                const { new_db_tree, new_cmp_list } = await dispatch('getNewDbTree', {
                    node,
                    db_tree: getters.getDbTreeData,
                    cmpList: getters.getDbCmplList,
                })
                commit('PATCH_DB_TREE_MAP', {
                    id: active_wke_id,
                    payload: {
                        data: new_db_tree,
                        db_completion_list: new_cmp_list,
                    },
                })
            } catch (e) {
                this.vue.$logger(`store-schemaSidebar-loadChildNodes`).error(e)
            }
        },
        async fetchSchemas({ commit, dispatch, state, rootState }) {
            const active_wke_id = rootState.wke.active_wke_id
            const active_sql_conn = rootState.queryConn.active_sql_conn
            const expanded_nodes = this.vue.$helpers.lodash.cloneDeep(state.expanded_nodes)
            try {
                commit('PATCH_DB_TREE_MAP', {
                    id: active_wke_id,
                    payload: {
                        loading_db_tree: true,
                    },
                })
                const { nodes, cmpList } = await queryHelper.getNodeData({
                    scope: this,
                    isRoot: true,
                })
                if (nodes.length) {
                    let tree = nodes
                    let completionList = cmpList
                    const {
                        TABLES,
                        SPS,
                        COLS,
                        TRIGGERS,
                    } = rootState.queryEditorConfig.config.SQL_NODE_TYPES
                    const nodesHaveChild = [TABLES, SPS, COLS, TRIGGERS]
                    for (const node of expanded_nodes) {
                        if (nodesHaveChild.includes(node.type)) {
                            const { new_db_tree, new_cmp_list } = await dispatch('getNewDbTree', {
                                node,
                                db_tree: tree,
                                cmpList: completionList,
                            })
                            if (!this.vue.$typy(new_db_tree).isEmptyObject) tree = new_db_tree
                            if (completionList.length) completionList = new_cmp_list
                        }
                    }
                    commit('PATCH_DB_TREE_MAP', {
                        id: active_wke_id,
                        payload: {
                            loading_db_tree: false,
                            data: tree,
                            db_completion_list: completionList,
                            data_of_conn: active_sql_conn.name,
                        },
                    })
                }
            } catch (e) {
                commit('PATCH_DB_TREE_MAP', {
                    id: active_wke_id,
                    payload: {
                        loading_db_tree: false,
                    },
                })
                this.vue.$logger(`store-schemaSidebar-fetchSchemas`).error(e)
            }
        },

        /**
         * This action is used to execute statement or statements.
         * Since users are allowed to modify the auto-generated SQL statement,
         * they can add more SQL statements after or before the auto-generated statement
         * which may receive error. As a result, the action log still log it as a failed action.
         * This can be fixed if a SQL parser is introduced.
         * @param {String} payload.sql - sql to be executed
         * @param {String} payload.action - action name. e.g. DROP TABLE table_name
         * @param {Boolean} payload.showSnackbar - show successfully snackbar message
         */
        async exeStmtAction({ rootState, dispatch, commit }, { sql, action, showSnackbar = true }) {
            const active_sql_conn = rootState.queryConn.active_sql_conn
            const active_wke_id = rootState.wke.active_wke_id
            const request_sent_time = new Date().valueOf()
            try {
                let stmt_err_msg_obj = {}
                let res = await this.vue.$queryHttp.post(`/sql/${active_sql_conn.id}/queries`, {
                    sql,
                    max_rows: rootState.queryPersisted.query_row_limit,
                })
                const results = this.vue.$typy(res, 'data.data.attributes.results').safeArray
                const errMsgs = results.filter(res => this.vue.$typy(res, 'errno').isDefined)
                // if multi statement mode, it'll still return only an err msg obj
                if (errMsgs.length) stmt_err_msg_obj = errMsgs[0]
                commit('PATCH_EXE_STMT_RESULT_MAP', {
                    id: active_wke_id,
                    payload: {
                        data: res.data.data.attributes,
                        stmt_err_msg_obj,
                    },
                })
                let queryAction
                if (!this.vue.$typy(stmt_err_msg_obj).isEmptyObject)
                    queryAction = this.vue.$mxs_t('errors.failedToExeAction', { action })
                else {
                    queryAction = this.vue.$mxs_t('info.exeActionSuccessfully', { action })
                    if (showSnackbar)
                        commit(
                            'mxsApp/SET_SNACK_BAR_MESSAGE',
                            { text: [queryAction], type: 'success' },
                            { root: true }
                        )
                }
                dispatch(
                    'queryPersisted/pushQueryLog',
                    {
                        startTime: request_sent_time,
                        name: queryAction,
                        sql,
                        res,
                        connection_name: active_sql_conn.name,
                        queryType: rootState.queryEditorConfig.config.QUERY_LOG_TYPES.ACTION_LOGS,
                    },
                    { root: true }
                )
            } catch (e) {
                this.vue.$logger(`store-schemaSidebar-exeStmtAction`).error(e)
            }
        },
    },
    getters: {
        // sidebar getters
        getDbSql: (state, getters, rootState) => {
            const { SQL_SYS_SCHEMAS: SYS_S } = rootState.queryEditorConfig.config
            let sql = 'SELECT * FROM information_schema.SCHEMATA'
            if (!rootState.queryPersisted.query_show_sys_schemas_flag)
                sql += ` WHERE SCHEMA_NAME NOT IN(${SYS_S.map(db => `'${db}'`).join(',')})`
            sql += ' ORDER BY SCHEMA_NAME;'
            return sql
        },
        getCurrDbTree: (state, getters, rootState) =>
            state.db_tree_map[rootState.wke.active_wke_id] || {},
        getActivePrvwTblNode: (state, getters) => {
            return getters.getCurrDbTree.active_prvw_tbl_node || {}
        },
        getDbTreeData: (state, getters) => {
            return getters.getCurrDbTree.data || []
        },
        getLoadingDbTree: (state, getters) => getters.getCurrDbTree.loading_db_tree || false,
        getDbCmplList: (state, getters) => {
            if (getters.getCurrDbTree.db_completion_list)
                return lodash.uniqBy(getters.getCurrDbTree.db_completion_list, 'label')
            return []
        },
        // exe_stmt_result_map getters
        getExeStmtResultMap: (state, getters, rootState) =>
            state.exe_stmt_result_map[rootState.wke.active_wke_id] || {},
    },
}
