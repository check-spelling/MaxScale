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
import * as config from '@queryEditorSrc/store/config'

export default {
    namespaced: true,
    state: {
        config,
        hidden_comp: [''],
        axios_opts: {},
    },
    mutations: {
        SET_HIDDEN_COMP(state, payload) {
            state.hidden_comp = payload
        },
        SET_AXIOS_OPTS(state, payload) {
            state.axios_opts = payload
        },
    },
}
