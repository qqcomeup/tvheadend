/*
 * Base configuration
 */

tvheadend.baseconf = function(panel, index) {

    var wizardButton = {
        name: 'wizard',
        builder: function() {
            return new Ext.Toolbar.Button({
                tooltip: _('Start initial configuration wizard'),
                iconCls: 'wizard',
                text: _('Start wizard')
            });
        },
        callback: function(conf) {
            tvheadend.Ajax({
                url: 'api/wizard/start',
                success: function() {
                    window.location.reload();
                }
            });
        }
    };

    tvheadend.idnode_simple(panel, {
        id: 'base_config',
        url: 'api/config',
        title: _('Base'),
        iconCls: 'baseconf',
        tabIndex: index,
        comet: 'config',
        labelWidth: 250,
        tbar: [wizardButton],
        postsave: function(data, abuttons, form) {
            var reload = 0;
            var l = data['uilevel'];
            if (l >= 0) {
                var tr = {0:'basic',1:'advanced',2:'expert'};
                l = (l in tr) ? tr[l] : 'basic';
                if (l !== tvheadend.uilevel)
                    reload = 1;
            }
            var n = data['theme_ui'];
            if (n !== tvheadend.theme)
              reload = 1;
            var n = data['page_size_ui'];
            if (n !== tvheadend.page_size)
            reload = 1;
              var n = data['uilevel_nochange'] ? true : false;
            if (n !== tvheadend.uilevel_nochange)
                reload = 1;
            var n = data['ui_quicktips'] ? true : false;
            if (tvheadend.quicktips !== n)
                reload = 1;
            var f = form.findField('language_ui');
            if (f.initialConfig.value !== data['language_ui'])
                reload = 1;
            if (reload)
                window.location.reload();
        }
    });

};

/*
 * Imagecache configuration
 */

tvheadend.imgcacheconf = function(panel, index) {

    var cleanButton = {
        name: 'clean',
        builder: function() {
            return new Ext.Toolbar.Button({
                tooltip: _('Clean image cache on storage'),
                iconCls: 'clean',
                text: _('Clean image (icon) cache')
            });
        },
        callback: function(conf) {
            tvheadend.Ajax({
               url: 'api/imagecache/config/clean',
               params: { clean: 1 },
            });
        }
    };

    var triggerButton = {
        name: 'trigger',
        builder: function() {
            return new Ext.Toolbar.Button({
                tooltip: _('Re-fetch images'),
                iconCls: 'fetch_images',
                text: _('Re-fetch images'),
            });
        },
        callback: function(conf) {
            tvheadend.Ajax({
               url: 'api/imagecache/config/trigger',
               params: { trigger: 1 },
            });
        }
    };

    tvheadend.idnode_simple(panel, {
        url: 'api/imagecache/config',
        title: _('Image Cache'),
        iconCls: 'imgcacheconf',
        tabIndex: index,
        uilevel: 'expert',
        comet: 'imagecache',
        width: 550,
        labelWidth: 200,
        tbar: [cleanButton, triggerButton]
    });

};

/*
 * SAT>IP server configuration
 */

tvheadend.satipsrvconf = function(panel, index) {

    if (tvheadend.capabilities.indexOf('satip_server') === -1)
        return;

    var discoverButton = {
        name: 'discover',
        builder: function() {
            return new Ext.Toolbar.Button({
                tooltip: _('Look for new SAT>IP servers'),
                iconCls: 'find',
                text: _('Discover SAT>IP servers')
            });
        },
        callback: function(conf) {
            tvheadend.Ajax({
                url: 'api/hardware/satip/discover',
                params: { op: 'all' }
            });
        }
    };

    tvheadend.idnode_simple(panel, {
        url: 'api/satips/config',
        title: _('SAT>IP Server'),
        iconCls: 'satipsrvconf',
        tabIndex: index,
        comet: 'satip_server',
        width: 610,
        labelWidth: 250,
        tbar: [discoverButton]
    });
};

/*
 * Webhook target configuration
 */

tvheadend.webhookconf = function(panel, index) {

    var Target = Ext.data.Record.create([
        { name: 'id' },
        { name: 'enabled', type: 'boolean' },
        { name: 'name' },
        { name: 'url' },
        { name: 'events' },
        { name: 'token' },
        { name: 'hmac_secret' },
        { name: 'timeout', type: 'int' },
        { name: 'retry_count', type: 'int' },
        { name: 'retry_interval', type: 'int' },
        { name: 'ssl_verify', type: 'boolean' },
        { name: 'headers' },
        { name: 'template' }
    ]);

    var EVENT_GROUPS = [
        {
            title: '系统事件',
            items: [
                { value: 'system.webhooktest', label: '测试 Webhook' }
            ]
        },
        {
            title: '播放事件',
            allLabel: '全部播放事件',
            wildcard: 'playback.*',
            items: [
                { value: 'playback.start', label: '播放开始' },
                { value: 'playback.stop', label: '播放停止' }
            ]
        },
        {
            title: '录制事件',
            allLabel: '全部录制事件',
            wildcard: 'dvr.*',
            items: [
                { value: 'dvr.start', label: '录制开始' },
                { value: 'dvr.complete', label: '录制完成' },
                { value: 'dvr.error', label: '录制失败' }
            ]
        },
        {
            title: '异常事件',
            items: [
                { value: 'dvb.*', label: 'DVB 异常' },
                { value: 'service.*', label: '服务错误' }
            ]
        }
    ];

    var store = new Ext.data.JsonStore({
        root: 'entries',
        totalProperty: 'totalCount',
        id: 'id',
        fields: Target,
        url: 'api/webhook/targets/grid',
        autoLoad: true
    });

    var enabled = new Ext.ux.grid.CheckColumn({
        header: '启用',
        dataIndex: 'enabled',
        width: 70
    });
    var updatingEvents = false;

    function splitEvents(value) {
        var out = [];
        Ext.each((value || '').split(','), function(item) {
            item = item.replace(/^\s+|\s+$/g, '');
            if (item)
                out.push(item);
        });
        return out;
    }

    function joinEvents(events) {
        return events.join(',');
    }

    function hasEvent(events, value) {
        if (events.indexOf(value) !== -1)
            return true;
        if (value.indexOf('.') !== -1) {
            var prefix = value.split('.')[0] + '.*';
            if (events.indexOf(prefix) !== -1)
                return true;
        }
        return false;
    }

    function eventLabel(value) {
        var label = value;
        Ext.each(EVENT_GROUPS, function(group) {
            if (group.wildcard == value)
                label = group.allLabel || (group.title + '全部');
            Ext.each(group.items, function(item) {
                if (item.value == value)
                    label = item.label;
            });
        });
        return label;
    }

    function eventsRenderer(value) {
        var events = splitEvents(value);
        var labels = [];
        Ext.each(events, function(event) {
            labels.push(eventLabel(event));
        });
        return Ext.util.Format.htmlEncode(labels.join(', '));
    }

    function recordToTarget(record) {
        var target = {
            enabled: record.get('enabled') ? true : false,
            name: record.get('name') || 'MoviePilot',
            url: record.get('url') || '',
            events: splitEvents(record.get('events')),
            timeout: parseInt(record.get('timeout') || 10, 10),
            retry_count: parseInt(record.get('retry_count') || 0, 10),
            retry_interval: parseInt(record.get('retry_interval') || 1, 10),
            ssl_verify: record.get('ssl_verify') ? true : false
        };
        if (record.get('token'))
            target.token = record.get('token');
        if (record.get('hmac_secret'))
            target.hmac_secret = record.get('hmac_secret');
        if (record.get('template'))
            target.template = record.get('template');
        if (record.get('headers')) {
            try {
                target.headers = Ext.decode(record.get('headers'));
            } catch (e) {
                throw 'Headers 必须是合法 JSON';
            }
        }
        return target;
    }

    function defaultTarget(data) {
        return Ext.apply({
            id: store.getCount() + 1,
            enabled: true,
            name: 'MoviePilot',
            url: '',
            events: 'system.webhooktest,playback.*,dvr.*,dvb.*,service.*',
            token: '',
            hmac_secret: '',
            timeout: 10,
            retry_count: 2,
            retry_interval: 5,
            ssl_verify: true,
            headers: '',
            template: ''
        }, data || {});
    }

    function addTarget(data) {
        editTarget(null, defaultTarget(data));
    }

    function saveTargets() {
        var targets = [];
        try {
            store.each(function(record) {
                if (record.get('url'))
                    targets.push(recordToTarget(record));
            });
        } catch (e) {
            Ext.MessageBox.alert('错误', e);
            return;
        }
        tvheadend.Ajax({
            url: 'api/webhook/targets/save',
            params: {
                targets: Ext.encode(targets)
            },
            success: function() {
                store.reload();
            }
        });
    }

    function checkboxItemsForGroup(group, eventBoxes, groupBoxes) {
        var items = [];
        if (group.wildcard) {
            items.push({
                xtype: 'checkbox',
                boxLabel: group.allLabel || (group.title + '全部'),
                webhookEvent: group.wildcard,
                listeners: {
                    check: function(field, checked) {
                        if (updatingEvents)
                            return;
                        Ext.each(group.items, function(item) {
                            if (eventBoxes[item.value])
                                eventBoxes[item.value].setValue(checked);
                        });
                    }
                }
            });
        }
        Ext.each(group.items, function(item) {
            items.push({
                xtype: 'checkbox',
                boxLabel: item.label,
                style: group.wildcard ? 'margin-left:20px;' : '',
                webhookEvent: item.value,
                listeners: {
                    check: function() {
                        if (updatingEvents)
                            return;
                        if (!group.wildcard || !groupBoxes[group.wildcard])
                            return;
                        var allChecked = true;
                        Ext.each(group.items, function(child) {
                            if (!eventBoxes[child.value] || !eventBoxes[child.value].getValue())
                                allChecked = false;
                        });
                        if (groupBoxes[group.wildcard].getValue() != allChecked)
                            groupBoxes[group.wildcard].setValue(allChecked);
                    }
                }
            });
        });
        return items;
    }

    function readSelectedEvents(eventBoxes, groupBoxes) {
        var events = [];
        Ext.each(EVENT_GROUPS, function(group) {
            if (group.wildcard && groupBoxes[group.wildcard] && groupBoxes[group.wildcard].getValue()) {
                events.push(group.wildcard);
                return;
            }
            Ext.each(group.items, function(item) {
                if (eventBoxes[item.value] && eventBoxes[item.value].getValue())
                    events.push(item.value);
            });
        });
        return events;
    }

    function applySelectedEvents(events, eventBoxes, groupBoxes) {
        updatingEvents = true;
        Ext.each(EVENT_GROUPS, function(group) {
            var groupChecked = group.wildcard && events.indexOf(group.wildcard) !== -1;
            var allChecked = true;
            Ext.each(group.items, function(item) {
                var checked = groupChecked || hasEvent(events, item.value);
                if (eventBoxes[item.value])
                    eventBoxes[item.value].setValue(checked);
                if (!checked)
                    allChecked = false;
            });
            if (group.wildcard && groupBoxes[group.wildcard])
                groupBoxes[group.wildcard].setValue(groupChecked || allChecked);
        });
        updatingEvents = false;
    }

    function editTarget(record, defaults) {
        var data = record ? record.data : defaults;
        var eventBoxes = {};
        var groupBoxes = {};
        var enabledField = new Ext.form.Checkbox({
            fieldLabel: '启用',
            checked: data.enabled !== false
        });
        var nameField = new Ext.form.TextField({
            fieldLabel: '名称',
            value: data.name || 'MoviePilot',
            allowBlank: false,
            anchor: '100%'
        });
        var urlField = new Ext.form.TextField({
            fieldLabel: 'URL',
            value: data.url || '',
            allowBlank: false,
            anchor: '100%'
        });
        var tokenField = new Ext.form.TextField({
            fieldLabel: 'Token（可选）',
            value: data.token || '',
            anchor: '100%'
        });
        var hmacField = new Ext.form.TextField({
            fieldLabel: 'HMAC 密钥',
            value: data.hmac_secret || '',
            anchor: '100%'
        });
        var timeoutField = new Ext.form.NumberField({
            fieldLabel: '超时秒数',
            value: data.timeout || 10,
            allowDecimals: false,
            minValue: 1,
            maxValue: 60,
            anchor: '100%'
        });
        var retryCountField = new Ext.form.NumberField({
            fieldLabel: '重试次数',
            value: data.retry_count || 0,
            allowDecimals: false,
            minValue: 0,
            maxValue: 10,
            anchor: '100%'
        });
        var retryIntervalField = new Ext.form.NumberField({
            fieldLabel: '重试间隔秒',
            value: data.retry_interval || 1,
            allowDecimals: false,
            minValue: 1,
            maxValue: 3600,
            anchor: '100%'
        });
        var sslVerifyField = new Ext.form.Checkbox({
            fieldLabel: 'TLS 证书校验',
            checked: data.ssl_verify !== false
        });
        var headersField = new Ext.form.TextArea({
            fieldLabel: '请求头 JSON',
            value: data.headers || '',
            height: 60,
            anchor: '100%'
        });
        var templateField = new Ext.form.TextField({
            fieldLabel: '模板',
            value: data.template || '',
            anchor: '100%'
        });
        var eventFieldsets = [];

        Ext.each(EVENT_GROUPS, function(group) {
            var items = checkboxItemsForGroup(group, eventBoxes, groupBoxes);
            var fieldset = new Ext.form.FieldSet({
                title: group.title,
                autoHeight: true,
                defaultType: 'checkbox',
                items: items
            });
            Ext.each(fieldset.items.items, function(box) {
                if (box.webhookEvent) {
                    if (box.webhookEvent == group.wildcard)
                        groupBoxes[box.webhookEvent] = box;
                    else
                        eventBoxes[box.webhookEvent] = box;
                }
            });
            eventFieldsets.push(fieldset);
        });

        var form = new Ext.FormPanel({
            frame: true,
            border: false,
            labelWidth: 110,
            bodyStyle: 'padding:8px;',
            autoScroll: true,
            items: [
                enabledField,
                nameField,
                urlField,
                hmacField,
                {
                    xtype: 'fieldset',
                    title: '通知事件',
                    autoHeight: true,
                    items: eventFieldsets
                },
                {
                    xtype: 'fieldset',
                    title: '高级设置',
                    autoHeight: true,
                    checkboxToggle: true,
                    collapsed: true,
                    items: [
                        tokenField,
                        timeoutField,
                        retryCountField,
                        retryIntervalField,
                        sslVerifyField,
                        headersField,
                        templateField
                    ]
                }
            ]
        });

        function applyMoviePilotDefaults() {
            nameField.setValue('MoviePilot');
            if (!urlField.getValue())
                urlField.setValue('https://hmp.066671.xyz/api/v1/plugin/tvhhelper/webhook?apikey=你的API_TOKEN');
            retryCountField.setValue(2);
            retryIntervalField.setValue(5);
            sslVerifyField.setValue(true);
            applySelectedEvents(splitEvents('system.webhooktest,playback.*,dvr.*,dvb.*,service.*'), eventBoxes, groupBoxes);
        }

        var win = new Ext.Window({
            title: record ? '编辑 Webhook 目标' : '新增 Webhook 目标',
            iconCls: record ? 'edit' : 'add',
            modal: true,
            layout: 'fit',
            width: 760,
            height: 620,
            plain: true,
            items: form,
            buttons: [
                {
                    text: 'MoviePilot 推荐配置',
                    iconCls: 'add',
                    handler: applyMoviePilotDefaults
                },
                '->',
                {
                    text: '确定',
                    iconCls: 'save',
                    handler: function() {
                        var events = readSelectedEvents(eventBoxes, groupBoxes);
                        var headers = headersField.getValue();
                        if (!form.getForm().isValid())
                            return;
                        if (!events.length) {
                            Ext.MessageBox.alert('错误', '请至少选择一个通知事件');
                            return;
                        }
                        if (headers) {
                            try {
                                Ext.decode(headers);
                            } catch (e) {
                                Ext.MessageBox.alert('错误', 'Headers 必须是合法 JSON');
                                return;
                            }
                        }
                        var values = {
                            id: record ? record.get('id') : store.getCount() + 1,
                            enabled: enabledField.getValue() ? true : false,
                            name: nameField.getValue() || 'MoviePilot',
                            url: urlField.getValue(),
                            events: joinEvents(events),
                            token: tokenField.getValue(),
                            hmac_secret: hmacField.getValue(),
                            timeout: parseInt(timeoutField.getValue() || 10, 10),
                            retry_count: parseInt(retryCountField.getValue() || 0, 10),
                            retry_interval: parseInt(retryIntervalField.getValue() || 1, 10),
                            ssl_verify: sslVerifyField.getValue() ? true : false,
                            headers: headers,
                            template: templateField.getValue()
                        };
                        if (record) {
                            record.beginEdit();
                            Ext.iterate(values, function(key, value) {
                                record.set(key, value);
                            });
                            record.endEdit();
                        } else {
                            store.add(new Target(values));
                        }
                        win.close();
                    }
                },
                {
                    text: '取消',
                    handler: function() {
                        win.close();
                    }
                }
            ]
        });

        win.show();
        applySelectedEvents(splitEvents(data.events), eventBoxes, groupBoxes);
    }

    var sm = new Ext.grid.RowSelectionModel({
        singleSelect: true
    });

    var cm = new Ext.grid.ColumnModel([
        enabled,
        {
            header: '名称',
            dataIndex: 'name',
            width: 150
        },
        {
            id: 'url',
            header: _('URL'),
            dataIndex: 'url',
            width: 420,
            renderer: Ext.util.Format.htmlEncode
        },
        {
            header: '事件',
            dataIndex: 'events',
            width: 300,
            renderer: eventsRenderer
        },
        {
            header: '超时',
            dataIndex: 'timeout',
            width: 70
        },
        {
            header: '重试',
            dataIndex: 'retry_count',
            width: 70
        },
        {
            header: 'HMAC 签名',
            dataIndex: 'hmac_secret',
            width: 80,
            renderer: function(value) {
                return value ? '是' : '否';
            }
        },
        {
            header: 'TLS 校验',
            dataIndex: 'ssl_verify',
            width: 70,
            renderer: function(value) {
                return value ? '是' : '否';
            }
        }
    ]);

    var grid = new Ext.grid.GridPanel({
        title: 'Webhook 目标',
        iconCls: 'baseconf',
        tabIndex: index,
        store: store,
        cm: cm,
        sm: sm,
        plugins: [enabled],
        stripeRows: true,
        autoExpandColumn: 'url',
        autoScroll: true,
        viewConfig: {
            emptyText: '暂无 Webhook 目标，可点击“创建 MoviePilot Webhook”开始配置。',
            deferEmptyText: false
        },
        listeners: {
            rowdblclick: function(g, row) {
                editTarget(store.getAt(row));
            }
        },
        tbar: [
            {
                text: '新增',
                iconCls: 'add',
                handler: function() {
                    addTarget();
                }
            },
            {
                text: '创建 MoviePilot Webhook',
                iconCls: 'add',
                tooltip: '按 MoviePilot 推荐配置创建一个 Webhook 目标',
                handler: function() {
                    addTarget({
                        name: 'MoviePilot',
                        url: 'https://hmp.066671.xyz/api/v1/plugin/tvhhelper/webhook?apikey=你的API_TOKEN'
                    });
                }
            },
            {
                text: '编辑',
                iconCls: 'edit',
                handler: function() {
                    var record = sm.getSelected();
                    if (record)
                        editTarget(record);
                }
            },
            {
                text: '删除',
                iconCls: 'delete',
                handler: function() {
                    var record = sm.getSelected();
                    if (record)
                        store.remove(record);
                }
            },
            '-',
            {
                text: '保存',
                iconCls: 'save',
                handler: saveTargets
            },
            {
                text: '发送测试通知',
                iconCls: 'play',
                tooltip: '使用已保存配置发送测试 Webhook',
                handler: function() {
                    tvheadend.Ajax({
                        url: 'api/webhook/test'
                    });
                }
            }
        ]
    });

    tvheadend.paneladd(panel, grid, index);
};
