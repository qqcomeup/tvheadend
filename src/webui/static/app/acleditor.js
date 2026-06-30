/*
 * Access Control
 */

tvheadend.acleditor = function(panel, index)
{
    var list = 'enabled,username,password,prefix,change,' +
               'lang,webui,uilevel,uilevel_nochange,admin,' +
               'streaming,profile,conn_limit_type,conn_limit,' +
               'dvr,dvr_config,channel_min,channel_max,' +
	       'channel_tag_exclude,channel_tag,default_tab,comment';

    var list2 = 'enabled,username,password,prefix,change,' +
                'lang,webui,themeui,langui,default_tab,uilevel,uilevel_nochange,admin,' +
                'streaming,profile,conn_limit_type,conn_limit,' +
                'dvr,htsp_anonymize,dvr_config,' +
                'channel_min,channel_max,channel_tag_exclude,' +
                'channel_tag,xmltv_output_format,htsp_output_format,comment';

    tvheadend.idnode_grid(panel, {
        id: 'access_entry',
        url: 'api/access/entry',
        titleS: _('Access Entry'),
        titleP: _('Access Entries'),
        iconCls: 'acl',
        columns: {
            enabled:        { width: 120 },
            username:       { width: 250 },
            password:       { width: 250 },
            prefix:         { width: 350 },
            change:         { width: 350 },
            streaming:      { width: 350 },
            dvr:            { width: 350 },
            webui:          { width: 140 },
            admin:          { width: 100 },
            conn_limit_type:{ width: 160 },
            conn_limit:     { width: 160 },
            channel_min:    { width: 160 },
            channel_max:    { width: 160 }
        },
        tabIndex: index,
        edit: {
            params: {
                list: list2
            }
        },
        add: {
            url: 'api/access/entry',
            params: {
                list: list2
            },
            create: { }
        },
        del: true,
        move: true,
        list: list
    });
};

/*
 * Password Control
 */

tvheadend.passwdM3uBaseUrl = function()
{
    return window.location.protocol + '//' + window.location.host;
};

tvheadend.passwdCopyText = function(text, success, failure)
{
    var area;
    var copied = false;

    if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(text).then(function() {
            if (success)
                success();
        }, function() {
            if (failure)
                failure();
        });
        return;
    }

    area = document.createElement('textarea');
    area.value = text;
    area.style.position = 'fixed';
    area.style.left = '-1000px';
    area.style.top = '-1000px';
    document.body.appendChild(area);
    area.focus();
    area.select();
    try {
        copied = document.execCommand('copy');
    } finally {
        document.body.removeChild(area);
    }
    if (copied) {
        if (success)
            success();
    } else if (failure) {
        failure();
    }
};

tvheadend.passwdCopyToast = function(message, url)
{
    var toastId = 'passwd-copy-toast';
    var toast = Ext.get(toastId);

    if (!toast) {
        toast = Ext.DomHelper.append(Ext.getBody(), {
            tag: 'div',
            id: toastId,
            cls: 'passwd-copy-toast'
        }, true);
    }
    toast.update('<div class="passwd-copy-toast-title">' +
                 Ext.util.Format.htmlEncode(message) + '</div>' +
                 '<div class="passwd-copy-toast-url">' +
                 Ext.util.Format.htmlEncode(url) + '</div>');
    toast.setStyle('display', 'block');
    window.clearTimeout(tvheadend.passwdCopyToastTimer);
    tvheadend.passwdCopyToastTimer = window.setTimeout(function() {
        var t = Ext.get(toastId);
        if (t)
            t.setStyle('display', 'none');
    }, 1800);
};

tvheadend.passwdShowUrl = function(title, url)
{
    var textareaId = Ext.id();
    var statusId = Ext.id();
    var win;
    var showCopied = function() {
        var status = Ext.get(statusId);
        if (status) {
            status.update('已复制');
            status.setStyle('visibility', 'visible');
            window.setTimeout(function() {
                var s = Ext.get(statusId);
                if (s)
                    s.setStyle('visibility', 'hidden');
            }, 1600);
        }
    };

    win = new Ext.Window({
        title: title,
        modal: true,
        width: 560,
        layout: 'fit',
        bodyStyle: 'padding:10px;',
        html: '<textarea id="' + textareaId + '" readonly="readonly" ' +
              'style="width:100%;height:88px;box-sizing:border-box;">' +
              Ext.util.Format.htmlEncode(url) + '</textarea>' +
              '<div id="' + statusId + '" class="passwd-copy-status">' +
              '已复制</div>',
        buttons: [
            {
                text: '复制',
                iconCls: 'passwd-copy-url',
                handler: function() {
                    tvheadend.passwdCopyText(url, showCopied);
                }
            },
            {
                text: '关闭',
                handler: function() {
                    win.close();
                }
            }
        ],
        listeners: {
            show: function() {
                var area = document.getElementById(textareaId);
                tvheadend.passwdCopyText(url, showCopied);
                if (area) {
                    area.focus();
                    area.select();
                }
            }
        }
    });
    win.show();
};

tvheadend.passwdCopyAuthUrl = function(select, type)
{
    var r = select.getSelected();
    var authcode = r ? r.get('authcode') : null;
    var base = tvheadend.passwdM3uBaseUrl();
    var url;

    if (!r) {
        Ext.MessageBox.alert('复制地址', '请先选择一个密码条目。');
        return;
    }
    if (!authcode) {
        Ext.MessageBox.alert('复制地址',
            '请先为此用户启用持久认证。');
        return;
    }

    if (type == 'xmltv') {
        url = base + '/epg?a=' + encodeURIComponent(authcode);
        tvheadend.passwdCopyText(url, function() {
            tvheadend.passwdCopyToast('已复制 XMLTV 地址', url);
        }, function() {
            tvheadend.passwdShowUrl('XMLTV 地址', url);
        });
    } else {
        url = base + '/m3u?a=' + encodeURIComponent(authcode);
        tvheadend.passwdCopyText(url, function() {
            tvheadend.passwdCopyToast('已复制 M3U 地址', url);
        }, function() {
            tvheadend.passwdShowUrl('M3U 地址', url);
        });
    }
};

tvheadend.passwdeditor = function(panel, index)
{
    var list = 'enabled,username,password,auth,authcode,comment';

    tvheadend.idnode_grid(panel, {
        url: 'api/passwd/entry',
        titleS: _('Password'),
        titleP: _('Passwords'),
        iconCls: 'pass',
        columns: {
            enabled:  { width: 120 },
            username: { width: 250 },
            password: { width: 250 },
            auth:     { width: 250 },
            authcode: { width: 350 }
        },
        tabIndex: index,
        selected: function(s, abuttons) {
            var enabled = s.getCount() == 1;
            if (abuttons.copyM3u)
                abuttons.copyM3u.setDisabled(!enabled);
            if (abuttons.copyXmltv)
                abuttons.copyXmltv.setDisabled(!enabled);
        },
        tbar: [
            {
                name: 'copyM3u',
                builder: function() {
                    return new Ext.Toolbar.Button({
                        tooltip: '复制所选用户的认证 M3U 播放列表地址',
                        iconCls: 'passwd-copy-url',
                        text: '复制 M3U 地址',
                        disabled: true
                    });
                },
                callback: function(b, e, store, select) {
                    tvheadend.passwdCopyAuthUrl(select, 'm3u');
                }
            },
            {
                name: 'copyXmltv',
                builder: function() {
                    return new Ext.Toolbar.Button({
                        tooltip: '复制所选用户的认证 XMLTV 地址',
                        iconCls: 'passwd-copy-url',
                        text: '复制 XMLTV 地址',
                        disabled: true
                    });
                },
                callback: function(b, e, store, select) {
                    tvheadend.passwdCopyAuthUrl(select, 'xmltv');
                }
            }
        ],
        edit: {
            params: {
                list: list
            }
        },
        add: {
            url: 'api/passwd/entry',
            params: {
                list: list
            },
            create: { }
        },
        del: true,
        list: list
    });
};

/*
 * IP Blocking Control
 */

tvheadend.ipblockeditor = function(panel, index)
{
    var list = 'enabled,prefix,comment';

    tvheadend.idnode_grid(panel, {
        url: 'api/ipblock/entry',
        titleS: _('IP Blocking Record'),
        titleP: _('IP Blocking Records'),
        iconCls: 'ip_block',
        columns: {
            enabled: { width: 120 },
            prefix:  { width: 350 },
            comment: { width: 250 }
        },
        tabIndex: index,
        uilevel: 'expert',
        edit: {
            params: {
                list: list
            }
        },
        add: {
            url: 'api/ipblock/entry',
            params: {
                list: list
            },
            create: { }
        },
        del: true,
        list: list
    });
};
