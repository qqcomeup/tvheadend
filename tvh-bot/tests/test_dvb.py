from tvh_bot.dvb import DvbMonitor, scan_dvb_adapters


def test_scan_dvb_adapters_lists_adapter_directories(tmp_path):
    (tmp_path / "adapter0").mkdir()
    (tmp_path / "adapter1").mkdir()
    (tmp_path / "not_adapter").mkdir()

    assert scan_dvb_adapters(tmp_path) == ["adapter0", "adapter1"]


def test_monitor_sends_drop_once_then_recovery():
    monitor = DvbMonitor(expected_count=2)

    assert monitor.evaluate(["adapter0", "adapter1"]) is None
    assert monitor.evaluate(["adapter0"]) == "drop"
    assert monitor.evaluate(["adapter0"]) is None
    assert monitor.evaluate(["adapter0", "adapter1"]) == "recover"
    assert monitor.evaluate(["adapter0", "adapter1"]) is None
