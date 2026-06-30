def is_admin(chat_id: int, admin_ids: set[int]) -> bool:
    return chat_id in admin_ids
