/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { Resize } from 'vuetify/lib/directives'
import MaxScaleQueryEditor from '@queryEditorSrc/MaxScaleQueryEditor.vue'
import queryEditorModules from '@queryEditorSrc/store/modules'
import commonComponents from '@queryEditorSrc/components/common'
import queryEditorStorePlugins from '@queryEditorSrc/store/plugins'

export default {
    /**
     * @param {Object} Vue - Vue instance. Automatically pass when register the plugin with Vue.use
     * @param {Object} options.store - vuex store
     * @param {Object} options.Vuetify - vuetify lib. i.e. import Vuetify from 'vuetify/lib'
     * @param {Boolean} options.isExternal - default value is true. MaxGUI has it as false because
     * appNotifier state module and other components already used by MaxGUI
     */
    install: (Vue, { store, Vuetify, isExternal = true }) => {
        if (!store) throw new Error('Please initialize plugin with a Vuex store.')

        //TODO: Prevent duplicated vuex module names, store plugin names, common components

        if (isExternal) {
            Vue.use(Vuetify, { directives: { Resize } })
            //Register common components
            Object.keys(commonComponents).forEach(name =>
                Vue.component(name, commonComponents[name])
            )
            // Register maxscale-query-editor components
            Vue.component('maxscale-query-editor', MaxScaleQueryEditor)
        }

        // Register query editor vuex modules
        Object.keys(queryEditorModules).forEach(key => {
            if (!isExternal && key === 'appNotifier') null
            else store.registerModule(key, queryEditorModules[key])
        })

        // Register store plugins
        queryEditorStorePlugins.forEach(plugin => plugin(store))
    },
}
