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

    var store = new Ext.data.JsonStore({
        root: 'entries',
        totalProperty: 'totalCount',
        id: 'id',
        fields: Target,
        url: 'api/webhook/targets/grid',
        autoLoad: true
    });

    var enabled = new Ext.ux.grid.CheckColumn({
        header: _('Enabled'),
        dataIndex: 'enabled',
        width: 70
    });

    var sslVerify = new Ext.ux.grid.CheckColumn({
        header: _('TLS'),
        dataIndex: 'ssl_verify',
        width: 50
    });

    function splitEvents(value) {
        var out = [];
        Ext.each((value || '').split(','), function(item) {
            item = item.replace(/^\s+|\s+$/g, '');
            if (item)
                out.push(item);
        });
        return out;
    }

    function recordToTarget(record) {
        var target = {
            enabled: record.get('enabled') ? true : false,
            name: record.get('name') || 'moviepilot',
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
                throw _('Headers must be valid JSON');
            }
        }
        return target;
    }

    function addTarget(data) {
        var record = new Target(Ext.apply({
            id: store.getCount() + 1,
            enabled: true,
            name: 'moviepilot',
            url: '',
            events: 'system.webhooktest,playback.*,dvr.*,dvb.*,service.error',
            token: '',
            hmac_secret: '',
            timeout: 10,
            retry_count: 2,
            retry_interval: 5,
            ssl_verify: true,
            headers: '',
            template: ''
        }, data || {}));
        grid.stopEditing();
        store.add(record);
        grid.startEditing(store.getCount() - 1, 2);
    }

    function saveTargets() {
        var targets = [];
        try {
            store.each(function(record) {
                if (record.get('url'))
                    targets.push(recordToTarget(record));
            });
        } catch (e) {
            Ext.MessageBox.alert(_('Error'), e);
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

    var sm = new Ext.grid.RowSelectionModel({
        singleSelect: true
    });

    var cm = new Ext.grid.ColumnModel([
        enabled,
        {
            header: _('Name'),
            dataIndex: 'name',
            width: 120,
            editor: new Ext.form.TextField()
        },
        {
            id: 'url',
            header: _('URL'),
            dataIndex: 'url',
            width: 360,
            editor: new Ext.form.TextField()
        },
        {
            header: _('Events'),
            dataIndex: 'events',
            width: 260,
            editor: new Ext.form.TextField()
        },
        {
            header: _('Token'),
            dataIndex: 'token',
            width: 150,
            editor: new Ext.form.TextField()
        },
        {
            header: _('HMAC Secret'),
            dataIndex: 'hmac_secret',
            width: 150,
            editor: new Ext.form.TextField()
        },
        {
            header: _('Timeout'),
            dataIndex: 'timeout',
            width: 70,
            editor: new Ext.form.NumberField({ allowDecimals: false, minValue: 1, maxValue: 60 })
        },
        {
            header: _('Retries'),
            dataIndex: 'retry_count',
            width: 70,
            editor: new Ext.form.NumberField({ allowDecimals: false, minValue: 0, maxValue: 10 })
        },
        {
            header: _('Retry interval'),
            dataIndex: 'retry_interval',
            width: 95,
            editor: new Ext.form.NumberField({ allowDecimals: false, minValue: 1, maxValue: 3600 })
        },
        sslVerify,
        {
            header: _('Headers JSON'),
            dataIndex: 'headers',
            width: 220,
            editor: new Ext.form.TextField()
        },
        {
            header: _('Template'),
            dataIndex: 'template',
            width: 220,
            editor: new Ext.form.TextField()
        }
    ]);

    var grid = new Ext.grid.EditorGridPanel({
        title: _('Webhook Targets'),
        iconCls: 'baseconf',
        tabIndex: index,
        store: store,
        cm: cm,
        sm: sm,
        plugins: [enabled, sslVerify],
        clicksToEdit: 1,
        stripeRows: true,
        autoExpandColumn: 'url',
        autoScroll: true,
        tbar: [
            {
                text: _('Add'),
                iconCls: 'add',
                handler: function() {
                    addTarget();
                }
            },
            {
                text: _('Add MoviePilot'),
                iconCls: 'add',
                tooltip: _('Add a MoviePilot Webhook target template'),
                handler: function() {
                    addTarget({
                        name: 'moviepilot',
                        url: 'https://<MoviePilot>/api/v1/plugin/tvhhelper/webhook?apikey=<API_TOKEN>'
                    });
                }
            },
            {
                text: _('Delete'),
                iconCls: 'delete',
                handler: function() {
                    var record = sm.getSelected();
                    if (record)
                        store.remove(record);
                }
            },
            '-',
            {
                text: _('Save'),
                iconCls: 'save',
                handler: saveTargets
            },
            {
                text: _('Test'),
                iconCls: 'play',
                tooltip: _('Send a test Webhook using the saved configuration'),
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
