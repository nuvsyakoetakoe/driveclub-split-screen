"""Installer messages. Keep diagnostic paths in the user's original spelling."""
RUSSIAN = {
    'Select DriveClub.exe.': 'Выберите файл DriveClub.exe.',
    'Select the folder containing DriveClub.exe': 'Выберите папку с DriveClub.exe.',
    'Close DRIVECLUB and its launcher before continuing.': 'Закройте DRIVECLUB и лаунчер, затем повторите операцию.',
    'Requires Windows port 0.4.2': 'Требуется версия порта 0.4.2 для Windows.',
    'Requires original DriveClub.exe from port 0.4.2': 'Требуется исходный DriveClub.exe от порта 0.4.2.',
    'Unsupported or modified DriveClub.exe': 'Версия DriveClub.exe не поддерживается или файл изменён.',
    'Unsupported game runtime': 'Эта версия игрового модуля не поддерживается.',
    'Start a normal race in port 0.4.2 once, then close the game.': 'Запустите обычный заезд в порте 0.4.2, затем закройте игру.',
    'Unsupported game archive': 'Архив игры не поддерживается.',
    'Invalid archive table': 'Повреждена таблица архива.',
    'Missing archive data': 'В архиве отсутствуют данные.',
    'Different game archive revision': 'Версия архива игры отличается.',
    'Unsupported compressed block': 'Формат сжатого блока не поддерживается.',
    'Invalid package': 'Некорректный пакет установки.',
    'Incomplete package or different game data': 'Пакет неполный или игровые файлы другой версии.',
    'Linked or external path: ': 'Путь указывает на ссылку или внешний файл: ',
    'Unexpected setup directory: ': 'Неожиданная папка установки: ',
    'Unrecognized recovery path': 'Неизвестный путь для восстановления.',
    'A file changed during recovery: ': 'Во время восстановления изменился файл: ',
    'Invalid recovery copy: ': 'Повреждена резервная копия: ',
    'File changed: ': 'Изменился файл: ',
    'Cannot open setup lock': 'Не удалось получить доступ к папке установки.',
    'Another installer is open for this folder': 'Для этой папки уже открыт другой установщик.',
    'A different Split Screen build is installed. Remove it with its own installer first.': 'Установлена другая сборка Split Screen. Сначала удалите её собственным установщиком.',
    'Original launcher backup is missing or changed': 'Резервная копия исходного лаунчера отсутствует или изменена.',
    'Version marker conflict': 'Содержимое файла версии отличается от ожидаемого.',
    'Existing mod file: ': 'Файл уже используется другим модом: ',
    'Installed file has external changes: ': 'Установленный файл был изменён: ',
    'Existing launcher backup': 'Резервная копия лаунчера уже существует.',
    'Another mod owns mods/global.rpk': 'Файл mods/global.rpk уже занят другим модом.',
    'Existing graphics overlay has changed': 'Файл графических настроек мода изменён.',
    'Unexpected launcher patch': 'Неожиданные изменения лаунчера в пакете.',
    'Matching game data missing: ': 'Не найдены нужные игровые данные: ',
    'Verification failed: ': 'Не удалось проверить файл: ',
    'Split Screen 0.1.0 is not installed here.': 'В этой папке Split Screen 0.1.0 не установлен.',
    'Split Screen removed.': 'Split Screen удалён.',
    'Original launcher restored.': 'Исходный лаунчер восстановлен.',
    'Kept the modified mods/global.rpk file.': 'Сохранён изменённый файл mods/global.rpk.',
    'Installation complete. Open DriveClub.exe and select Drive → Split Screen.': 'Установка завершена. Откройте DriveClub.exe и выберите Drive → Split Screen.',
}


def translate(message, language):
    if language != 'ru':
        return message
    if message in RUSSIAN:
        return RUSSIAN[message]
    for prefix, russian in RUSSIAN.items():
        if prefix.endswith(': ') and message.startswith(prefix):
            return russian + message[len(prefix):]
    return 'Подробности ошибки:\n' + message
