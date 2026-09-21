#include <iostream>
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <filesystem>
// Функция создания скриншота экрана и сохранения его в файл BMP
bool TakeScreenshot(const std::string& filename) {
    // 1. Получаем размеры экрана
    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);

    // 2. Получаем контекст устройства (экран)
    HWND hDesktopWnd = GetDesktopWindow();
    HDC hDesktopDC = GetDC(hDesktopWnd);
    HDC hCaptureDC = CreateCompatibleDC(hDesktopDC);

    // 3. Создаем пустую картинку в памяти под размеры экрана
    HBITMAP hCaptureBitmap = CreateCompatibleBitmap(hDesktopDC, width, height);
    SelectObject(hCaptureDC, hCaptureBitmap);

    // 4. Копируем пиксели с экрана на нашу картинку (само фото)
    if (!BitBlt(hCaptureDC, 0, 0, width, height, hDesktopDC, 0, 0, SRCCOPY)) {
        ReleaseDC(hDesktopWnd, hDesktopDC);
        DeleteDC(hCaptureDC);
        return false;
    }

    // 5. Заполняем структуры для сохранения файла в формате BMP
    BITMAPFILEHEADER bfh;
    BITMAPINFOHEADER bih;

    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = width;
    bih.biHeight = height;
    bih.biPlanes = 1;
    bih.biBitCount = 24; // 24 бита на пиксель (полноцветное изображение)
    bih.biCompression = BI_RGB;
    bih.biSizeImage = 0;

    bfh.bfType = 0x4D42; // Маркер файла BMP ('BM')
    bfh.bfReserved1 = 0;
    bfh.bfReserved2 = 0;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize = bfh.bfOffBits + (width * height * 3);

    // 6. Записываем данные на жесткий диск
    FILE* fp = nullptr;
    if (fopen_s(&fp, filename.c_str(), "wb") == 0 && fp != nullptr) {
        fwrite(&bfh, sizeof(BITMAPFILEHEADER), 1, fp);
        fwrite(&bih, sizeof(BITMAPINFOHEADER), 1, fp);

        int dwSize = width * height * 3;
        BYTE* pPixels = new BYTE[dwSize];

        GetDIBits(hDesktopDC, hCaptureBitmap, 0, height, pPixels, (BITMAPINFO*)&bih, DIB_RGB_COLORS);
        fwrite(pPixels, dwSize, 1, fp);

        delete[] pPixels;
        fclose(fp);
    } else {
        return false;
    }

    // 7. Освобождаем ресурсы ОС
    ReleaseDC(hDesktopWnd, hDesktopDC);
    DeleteDC(hCaptureDC);
    DeleteObject(hCaptureBitmap);

    return true;
}

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
        std::cout << "-> screenshot (Сделать снимок экрана)\n";
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
                // 5. КОМАНДА СКРИНШОТА
        else if (command == "screenshot") {
            std::cout << "Делаю снимок экрана...\n";
            
            // Вызываем функцию, которую вставили в самый верх
            if (TakeScreenshot("screen.bmp")) {
                std::cout << "Скриншот успешно сохранен в файл 'screen.bmp'!\n";
            } else {
                std::cout << "Ошибка: Не удалось сделать скриншот.\n";
            }
        }

                // 3. РЕЖИМ ФАЙЛОВОЙ СИСТЕМЫ (ПЕРЕХОД К УПРАВЛЕНИЮ ФАЙЛАМИ)
        else if (command == "fs") {
            std::string fsCommand;
            
            // ЛАЙФХАК: Принудительно переносим пользователя в корень диска C:\ при старте
            try {
                fs::current_path("C:\\"); 
            } catch (...) {
                // Если вдруг диска C нет (например, на флешке), остаемся где были
            }

            std::cout << "\nВход в режим файловой системы. Старт с диска C:\\\n";
            std::cout << "Для возврата в главное меню введите 'back'.\n";

            // Вложенный цикл файлового менеджера
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
                // КРУТАЯ ФИЧА: Шаг назад по дереву папок (Из C:\game\cod в C:\game)
                else if (fsCommand == "up") {
                    try {
                        // Метод parent_path() автоматически отрезает последнее имя в пути
                        fs::current_path(fs::current_path().parent_path());
                    } catch (const std::exception& e) {
                        std::cout << "Ошибка: Вы уже в самом корне диска, выше подняться нельзя!\n";
                    }
                }
                else if (fsCommand == "back") {
                    std::cout << "Возврат в главное меню.\n";
                    break; 
                } 
                else {
                    std::cout << "Неизвестная подкоманда! Доступны: dir, cd <путь>, up, back\n";
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