#include <iostream>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

int main() {
    setlocale(LC_ALL, "Russian");

    // 1. Инициализация сети Windows
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cout << "[СЕРВЕР] Ошибка сети!\n";
        return 1;
    }

    // 2. Создаем слушающий сокет
    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(8888); // Слушаем порт 8888

    bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(listenSocket, 1);

    std::cout << "[СЕРВЕР] Агент запущен. Ожидание пульта управления по сети...\n";
    SOCKET clientSocket = accept(listenSocket, NULL, NULL);
    std::cout << "[СЕРВЕР] Пульт управления успешно подключен!\n";
    closesocket(listenSocket);

    // 3. Цикл приема команд по сети
    char buffer[512];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        
        if (bytesReceived <= 0) {
            std::cout << "[СЕРВЕР] Пульт управления отключился.\n";
            break;
        }

        std::string command(buffer);
        std::cout << "[СЕТЬ] Прилетела команда: " << command << "\n";

        // ЕСЛИ ПРИШЕЛ ВЫХОД
        if (command == "exit") {
            break;
        }

        // МАГИЯ: Запускаем твою локальную админ-программу и передаем ей команду через командную строку Windows
        // Флаг "r" означает, что мы можем прочитать то, что программа выведет в консоль!
        std::string systemCmd = "admin_tool.exe " + command; 
        
        // Открываем скрытый поток взаимодействия с нашей программой
        FILE* pipe = _popen(systemCmd.c_str(), "r");
        if (!pipe) {
            std::string errMsg = "Ошибка запуска локального модуля admin_tool.exe\n";
            send(clientSocket, errMsg.c_str(), errMsg.length() + 1, 0);
            continue;
        }

        // Собираем всё, что твоя программа написала в ответ (например, "Скриншот успешно сделан!")
        char resultBuffer[128];
        std::string totalResponse = "";
        while (fgets(resultBuffer, sizeof(resultBuffer), pipe) != NULL) {
            totalResponse += resultBuffer;
        }
        _pclose(pipe); // Закрываем поток программы

        // Отправляем текстовый ответ обратно админу на клиентский пульт!
        send(clientSocket, totalResponse.c_str(), totalResponse.length() + 1, 0);
    }

    closesocket(clientSocket);
    WSACleanup();
    return 0;
}
