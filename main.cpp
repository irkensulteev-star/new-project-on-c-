#include <iostream>
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <filesystem>
int main() {
    HANDLE hToken;
    TOKEN_PRIVILEGES tkp;
    LUID luid;
    namespace fs = std::filesystem;
    bool isAdmin = true; // По умолчанию считаем, что мы админ
        // ШАГ 1: Открываем маркер безопасности (паспорт) нашей программы
    // TOKEN_ADJUST_PRIVILEGES — говорим Windows, что хотим изменить права
    // TOKEN_QUERY — говорим, что хотим прочитать текущие права
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        std::cout << "Ошибка OpenProcessToken. Код: " << GetLastError() << std::endl;
        return 1; 
    }
    
    // ШАГ 2: Переводим текстовое имя привилегии выключения в понятный системе ID (LUID)
    // SE_SHUTDOWN_NAME — это встроенная константа Windows для "SeShutdownPrivilege"
    if (!LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &luid)) {
        std::cout << "Ошибка LookupPrivilegeValue. Код: " << GetLastError() << std::endl;
        CloseHandle(hToken); // Если упало здесь, закрываем токен, чтобы не было утечки памяти
        return 1;
    }
    
    // ШАГ 3: Заполняем структуру настроек и включаем эту привилегию
    tkp.PrivilegeCount = 1;  // Мы меняем ровно ОДНУ привилегию
    tkp.Privileges[0].Luid = luid;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED; // Флаг: АКТИВИРОВАТЬ привилегию!
    
    // Передаем заполненную структуру функции AdjustTokenPrivileges
    if (!AdjustTokenPrivileges(hToken, FALSE, &tkp, sizeof(TOKEN_PRIVILEGES), NULL, NULL)) {
        std::cout << "Ошибка AdjustTokenPrivileges. Код: " << GetLastError() << std::endl;
        CloseHandle(hToken);
        return 1;
    }
    
    // Важная проверка: функция могла сработать, но Windows могла отказать, 
    // если программа запущена не от имени администратора.
    if (GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
        std::cout << "Внимание: Программа запущена без прав администратора! Привилегия НЕ получена." << std::endl;
        CloseHandle(hToken);
         isAdmin = false; // Меняем флаг: мы НЕ админ, но из программы НЕ выходим!
    }
    
    // Если мы дошли сюда — мы победили! Права на управление питанием у нас в кармане
    std::cout << "Успех! Привилегия на выключение/перезагрузку успешно получена!" << std::endl;
    
    CloseHandle(hToken); // Обязательно закрываем хэндл токена, он нам больше не нужен
    
        // --- ГЛАВНОЕ МЕНЮ АДМИНИСТРАТОРА ---
    std::string command;

    while (true) {
        std::cout << "\n============================================\n";
        std::cout << "[ГЛАВНОЕ МЕНЮ] Доступные режимы:\n";
        std::cout << "-> reboot   (Перезагрузка компьютера)\n";
        std::cout << "-> shutdown (Выключение компьютера)\n";
        std::cout << "-> fs       (Переход к управлению файлами)\n";
        std::cout << "-> exit     (Выход из программы)\n";
        std::cout << "============================================\n";
        std::cout << "[Admin Mode] Введите команду: ";
        std::cin >> command;

        // 1. КОМАНДА ПЕРЕЗАГРУЗКИ
        if (command == "reboot") {
            if (isAdmin) {
                std::cout << "Выполняю перезагрузку ПК...\n";
                ExitWindowsEx(EWX_REBOOT | EWX_FORCE, SHTDN_REASON_MAJOR_OTHER);
            } else {
                std::cout << "Ошибка: Для перезагрузки ПК требуются права администратора!\n";
            }
        }

        // 2. КОМАНДА ВЫКЛЮЧЕНИЯ
        else if (command == "shutdown") {
            if (isAdmin) {
                std::cout << "Выполняю выключение ПК...\n";
                ExitWindowsEx(EWX_POWEROFF | EWX_FORCE, SHTDN_REASON_MAJOR_OTHER);
            } else {
                std::cout << "Ошибка: Для выключения ПК требуются права администратора!\n";
            }
        }

        // 3. РЕЖИМ ФАЙЛОВОЙ СИСТЕМЫ (ПЕРЕХОД К УПРАВЛЕНИЮ ФАЙЛАМИ)
        else if (command == "fs") {
            std::string fsCommand;
            std::cout << "\nВход в режим файловой системы. Для возврата введите 'back'.\n";

            // Вложенный цикл, который крутится только внутри файлового менеджера
            while (true) {
                std::cout << "\n[FS@" << fs::current_path().string() << "]$ ";
                std::cin >> fsCommand;

                if (fsCommand == "dir") {
                    std::cout << "\n--- Содержимое папки ---\n";
                    try {
                        for (const auto& entry : fs::directory_iterator(fs::current_path())) {
                            if (entry.is_directory()) {
                                std::cout << "[ПАПКА] " << entry.path().filename().string() << "\n";
                            } else {
                                std::cout << "[ФАЙЛ ] " << entry.path().filename().string() 
                                          << " (" << entry.file_size() << " байт)\n";
                            }
                        }
                    } catch (const std::exception& e) {
                        std::cout << "Ошибка чтения папки: " << e.what() << "\n";
                    }
                    std::cout << "------------------------\n";
                } 
                else if (fsCommand == "cd") {
                    std::string path;
                    std::cin >> path;
                    try {
                        fs::current_path(path);
                    } catch (const std::exception& e) {
                        std::cout << "Ошибка: Не удалось перейти в указанную папку.\n";
                    }
                } 
                else if (fsCommand == "back") {
                    std::cout << "Возврат в главное меню.\n";
                    break; // Выходим из файлового цикла и возвращаемся в главное меню
                } 
                else {
                    std::cout << "Неизвестная подкоманда! Доступны: dir, cd <путь>, back\n";
                }
            }
        }

        // 4. ВЫХОД ИЗ ПРОГРАММЫ
        else if (command == "exit") {
            std::cout << "Выход из панели.\n";
            break;
        } 

        // ЕСЛИ ВВЕДЕНО ЧТО-ТО СТРАННОЕ
        else {
            std::cout << "Неизвестная режим! Выберите из списка (reboot, shutdown, fs, exit).\n";
        }
    } 
    
    return 0;
}