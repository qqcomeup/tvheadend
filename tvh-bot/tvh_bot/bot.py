from html import escape

from aiogram import Bot, Dispatcher, F, Router
from aiogram.filters import Command
from aiogram.types import CallbackQuery, InlineKeyboardButton, InlineKeyboardMarkup, Message

from tvh_bot.config import Settings
from tvh_bot.dvb import scan_dvb_adapters
from tvh_bot.security import is_admin
from tvh_bot.tvh_client import TvhClient, TvhUser, token_for_user
from tvh_bot.urls import build_epg_url, build_m3u_url


def format_status_message(
    tvh_ok: bool,
    version: str | None,
    adapters: list[str],
    expected_dvb_count: int,
) -> str:
    status = "OK" if tvh_ok else "失败"
    version_text = version or "未知"
    adapter_text = ", ".join(adapters) if adapters else "未发现"
    return (
        f"TVH: {status}\n"
        f"版本: {version_text}\n"
        f"DVB: {len(adapters)}/{expected_dvb_count}\n"
        f"设备: {adapter_text}"
    )


def format_user_message(public_base_url: str, user: TvhUser) -> str:
    if not user.token:
        return f"用户: {escape(user.username)}\nToken: 未设置"
    return (
        f"用户: {escape(user.username)}\n"
        f"Token: <code>{escape(user.token)}</code>\n"
        f"M3U: <code>{escape(build_m3u_url(public_base_url, user.token))}</code>\n"
        f"EPG: <code>{escape(build_epg_url(public_base_url, user.token))}</code>"
    )


def build_dispatcher(settings: Settings, tvh_client: TvhClient) -> Dispatcher:
    router = Router()

    async def require_admin(message: Message) -> bool:
        chat_id = message.chat.id if message.chat else 0
        if is_admin(chat_id, settings.admin_ids):
            return True
        await message.answer("无权限。")
        return False

    @router.message(Command("start", "help"))
    async def start(message: Message) -> None:
        if not await require_admin(message):
            return
        await message.answer(
            "TVH 管理机器人\n"
            "/status 查看 TVH 状态\n"
            "/dvb 查看 DVB 设备\n"
            "/users 查看用户 M3U/EPG"
        )

    @router.message(Command("status"))
    async def status(message: Message) -> None:
        if not await require_admin(message):
            return
        tvh_ok, version = await tvh_client.get_status()
        adapters = scan_dvb_adapters()
        await message.answer(
            format_status_message(tvh_ok, version, adapters, settings.expected_dvb_count)
        )

    @router.message(Command("dvb"))
    async def dvb(message: Message) -> None:
        if not await require_admin(message):
            return
        adapters = scan_dvb_adapters()
        await message.answer(
            f"DVB: {len(adapters)}/{settings.expected_dvb_count}\n"
            f"设备: {', '.join(adapters) if adapters else '未发现'}"
        )

    @router.message(Command("users"))
    async def users(message: Message) -> None:
        if not await require_admin(message):
            return
        tvh_users = await tvh_client.get_users()
        buttons = [
            [InlineKeyboardButton(text=user.username, callback_data=f"user:{user.username}")]
            for user in tvh_users
        ]
        if not buttons:
            await message.answer("未读取到 TVH 用户。")
            return
        await message.answer("选择用户：", reply_markup=InlineKeyboardMarkup(inline_keyboard=buttons))

    @router.callback_query(F.data.startswith("user:"))
    async def user_callback(callback: CallbackQuery) -> None:
        chat_id = callback.message.chat.id if callback.message and callback.message.chat else 0
        if not is_admin(chat_id, settings.admin_ids):
            await callback.answer("无权限。", show_alert=True)
            return
        username = callback.data.split(":", 1)[1] if callback.data else ""
        tvh_users = await tvh_client.get_users()
        token = token_for_user(tvh_users, username)
        await callback.message.answer(
            format_user_message(settings.public_base_url, TvhUser(username, token)),
            parse_mode="HTML",
        )
        await callback.answer()

    dispatcher = Dispatcher()
    dispatcher.include_router(router)
    return dispatcher


async def run_bot(settings: Settings) -> None:
    bot = Bot(settings.bot_token)
    tvh_client = TvhClient(settings.tvh_url, settings.tvh_user, settings.tvh_pass)
    dispatcher = build_dispatcher(settings, tvh_client)
    await dispatcher.start_polling(bot)
