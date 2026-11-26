"""Split Screen entries appended to the game's own language catalogs."""
from driveclub_probe import test_assets


STRINGS = {
    "DCSPLIT_RACE": ("RACE", "ГОНКА"),
    "DCSPLIT_SETTINGS_TILE": ("SPLIT SCREEN SETTINGS", "НАСТРОЙКИ SPLIT SCREEN"),
    "DCSPLIT_NAME1": ("Player 1 name", "Имя игрока 1"),
    "DCSPLIT_NAME2": ("Player 2 name", "Имя игрока 2"),
    "DCSPLIT_DEVICES": ("Input devices", "Устройства игроков"),
    "DCSPLIT_KEYBOARD": ("Keyboard controls", "Управление с клавиатуры"),
    "DCSPLIT_SETTINGS": ("Split Screen settings", "Настройки Split Screen"),
    "DCSPLIT_KEYS1": ("Player 1 keys", "Клавиши игрока 1"),
    "DCSPLIT_KEYS2": ("Player 2 keys", "Клавиши игрока 2"),
    "DCSPLIT_BASE_LAYOUT": ("Default layout", "Базовая раскладка"),
    "DCSPLIT_THROTTLE": ("Accelerate", "Газ"),
    "DCSPLIT_BRAKE": ("Brake / reverse", "Тормоз / задний ход"),
    "DCSPLIT_LEFT": ("Steer left", "Поворот влево"),
    "DCSPLIT_RIGHT": ("Steer right", "Поворот вправо"),
    "DCSPLIT_HANDBRAKE": ("Handbrake", "Ручник"),
    "DCSPLIT_LOOK_LEFT": ("Look left", "Обзор влево"),
    "DCSPLIT_LOOK_RIGHT": ("Look right", "Обзор вправо"),
    "DCSPLIT_LOOK_UP": ("Look up", "Обзор вверх"),
    "DCSPLIT_LOOK_DOWN": ("Look down", "Обзор вниз"),
    "DCSPLIT_CAMERA": ("Change camera", "Смена камеры"),
    "DCSPLIT_REAR": ("Look back", "Вид назад"),
    "DCSPLIT_HUD": ("Show / hide HUD", "Показать / скрыть HUD"),
    "DCSPLIT_CAMERA_ALT": ("Change camera (alternate)", "Смена камеры (доп.)"),
    "DCSPLIT_PRESS_KEY": ("Press a key", "Нажмите клавишу"),
    "DCSPLIT_AUTO": ("Automatic", "Автоматически"),
    "DCSPLIT_KB_PAD": ("P1 — keyboard / P2 — gamepad", "P1 — клавиатура / P2 — геймпад"),
    "DCSPLIT_PAD_KB": ("P1 — gamepad / P2 — keyboard", "P1 — геймпад / P2 — клавиатура"),
    "DCSPLIT_PADS": ("Two gamepads", "Два геймпада"),
    "DCSPLIT_SHARED_KB": ("Shared keyboard", "Одна клавиатура на двоих"),
    "DCSPLIT_LAYOUT": ("Keyboard layout", "Раскладка клавиатуры"),
    "DCSPLIT_ARROWS": ("Arrow keys", "Стрелки"),
}


def prepare_languages(game, write):
    for filename, _, original in test_assets(game, "gui/lams/*.csv"):
        index = int(filename == b"gui/lams/russian.csv")
        rows = "".join(f"\x02{key}\x03,\x02{forms[index]}\x03\r\n" for key, forms in STRINGS.items())
        write(filename.decode("ascii"), original + rows.encode("utf-8"))
