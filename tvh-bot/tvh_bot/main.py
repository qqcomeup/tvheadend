import asyncio
import logging

from aiogram import Bot

from tvh_bot.bot import build_dispatcher
from tvh_bot.config import Settings
from tvh_bot.dvb import DvbMonitor, scan_dvb_adapters
from tvh_bot.tvh_client import TvhClient


async def monitor_dvb(settings: Settings, bot: Bot) -> None:
    monitor = DvbMonitor(settings.expected_dvb_count)
    while True:
        adapters = scan_dvb_adapters()
        event = monitor.evaluate(adapters)
        if event:
            text = (
                f"DVB 掉线告警: {len(adapters)}/{settings.expected_dvb_count}"
                if event == "drop"
                else f"DVB 已恢复: {len(adapters)}/{settings.expected_dvb_count}"
            )
            for chat_id in settings.admin_ids:
                await bot.send_message(chat_id, text)
        await asyncio.sleep(settings.check_interval_seconds)


async def main() -> None:
    logging.basicConfig(level=logging.INFO)
    settings = Settings()
    bot = Bot(settings.bot_token)
    tvh_client = TvhClient(
        settings.tvh_url,
        settings.tvh_user,
        settings.tvh_pass,
        settings.tvh_passwd_path,
    )
    dispatcher = build_dispatcher(settings, tvh_client)
    monitor_task = asyncio.create_task(monitor_dvb(settings, bot))
    try:
        await dispatcher.start_polling(bot)
    finally:
        monitor_task.cancel()
        await bot.session.close()


if __name__ == "__main__":
    asyncio.run(main())
