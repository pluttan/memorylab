#include <iostream>
#include <string>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <thread>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/sha.h>

#include "functions.cpp"

// Конфигурация сервера
constexpr int SERVER_PORT = 8765;
constexpr const char* SERVER_NAME = "HardwareTester";
constexpr const char* SERVER_VERSION = "1.0.0";
constexpr int BUFFER_SIZE = 4096;

/**
 * @class WebSocketServer
 * @brief WebSocket сервер для выполнения функций тестирования
 */
class WebSocketServer {
private:
    int serverSocket;
    int port;
    std::string serverName;
    bool running;
    std::vector<std::thread> clientThreads;

    /**
     * @brief Base64 кодирование
     */
    static std::string base64Encode(const unsigned char* data, size_t length) {
        static const char* base64Chars = 
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        
        std::string result;
        int i = 0;
        unsigned char charArray3[3];
        unsigned char charArray4[4];

        while (length--) {
            charArray3[i++] = *(data++);
            if (i == 3) {
                charArray4[0] = (charArray3[0] & 0xfc) >> 2;
                charArray4[1] = ((charArray3[0] & 0x03) << 4) + ((charArray3[1] & 0xf0) >> 4);
                charArray4[2] = ((charArray3[1] & 0x0f) << 2) + ((charArray3[2] & 0xc0) >> 6);
                charArray4[3] = charArray3[2] & 0x3f;

                for (i = 0; i < 4; i++)
                    result += base64Chars[charArray4[i]];
                i = 0;
            }
        }

        if (i) {
            for (int j = i; j < 3; j++)
                charArray3[j] = '\0';

            charArray4[0] = (charArray3[0] & 0xfc) >> 2;
            charArray4[1] = ((charArray3[0] & 0x03) << 4) + ((charArray3[1] & 0xf0) >> 4);
            charArray4[2] = ((charArray3[1] & 0x0f) << 2) + ((charArray3[2] & 0xc0) >> 6);

            for (int j = 0; j < i + 1; j++)
                result += base64Chars[charArray4[j]];

            while (i++ < 3)
                result += '=';
        }

        return result;
    }

    /**
     * @brief Генерирует WebSocket accept key
     */
    std::string generateAcceptKey(const std::string& clientKey) {
        std::string magic = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        
        unsigned char hash[SHA_DIGEST_LENGTH];
        SHA1(reinterpret_cast<const unsigned char*>(magic.c_str()), magic.length(), hash);
        
        return base64Encode(hash, SHA_DIGEST_LENGTH);
    }

    /**
     * @brief Парсит HTTP заголовки
     */
    std::map<std::string, std::string> parseHeaders(const std::string& request) {
        std::map<std::string, std::string> headers;
        std::istringstream stream(request);
        std::string line;
        
        // Пропускаем первую строку (GET / HTTP/1.1)
        std::getline(stream, line);
        
        while (std::getline(stream, line) && line != "\r") {
            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos) {
                std::string key = line.substr(0, colonPos);
                std::string value = line.substr(colonPos + 1);
                
                // Trim whitespace
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t\r\n") + 1);
                
                headers[key] = value;
            }
        }
        
        return headers;
    }

    /**
     * @brief Выполняет WebSocket handshake
     */
    bool performHandshake(int clientSocket, const std::string& request) {
        auto headers = parseHeaders(request);
        
        if (headers.find("Sec-WebSocket-Key") == headers.end()) {
            return false;
        }
        
        std::string acceptKey = generateAcceptKey(headers["Sec-WebSocket-Key"]);
        
        std::ostringstream response;
        response << "HTTP/1.1 101 Switching Protocols\r\n";
        response << "Upgrade: websocket\r\n";
        response << "Connection: Upgrade\r\n";
        response << "Sec-WebSocket-Accept: " << acceptKey << "\r\n";
        response << "Server: " << serverName << "/" << SERVER_VERSION << "\r\n";
        response << "\r\n";
        
        std::string responseStr = response.str();
        send(clientSocket, responseStr.c_str(), responseStr.length(), 0);
        
        return true;
    }

public:
    /**
     * @brief Декодирует WebSocket фрейм
     */
    std::string decodeFrame(const unsigned char* buffer, size_t length, bool& isClose) {
        isClose = false;
        
        if (length < 2) return "";
        
        unsigned char opcode = buffer[0] & 0x0F;
        bool masked = buffer[1] & 0x80;
        size_t payloadLen = buffer[1] & 0x7F;
        
        if (opcode == 0x08) {
            isClose = true;
            return "";
        }
        
        size_t offset = 2;
        
        if (payloadLen == 126) {
            payloadLen = (buffer[2] << 8) | buffer[3];
            offset = 4;
        } else if (payloadLen == 127) {
            // 64-bit length (не поддерживается для простоты)
            return "";
        }
        
        unsigned char mask[4] = {0};
        if (masked) {
            memcpy(mask, buffer + offset, 4);
            offset += 4;
        }
        
        std::string message;
        for (size_t i = 0; i < payloadLen && offset + i < length; i++) {
            if (masked) {
                message += buffer[offset + i] ^ mask[i % 4];
            } else {
                message += buffer[offset + i];
            }
        }
        
        return message;
    }

    /**
     * @brief Кодирует сообщение в WebSocket фрейм
     */
    std::vector<unsigned char> encodeFrame(const std::string& message) {
        std::vector<unsigned char> frame;
        
        // FIN + Text opcode
        frame.push_back(0x81);
        
        size_t length = message.length();
        if (length <= 125) {
            frame.push_back(static_cast<unsigned char>(length));
        } else if (length <= 65535) {
            frame.push_back(126);
            frame.push_back((length >> 8) & 0xFF);
            frame.push_back(length & 0xFF);
        } else {
            frame.push_back(127);
            for (int i = 7; i >= 0; i--) {
                frame.push_back((length >> (i * 8)) & 0xFF);
            }
        }
        
        for (char c : message) {
            frame.push_back(static_cast<unsigned char>(c));
        }
        
        return frame;
    }

    /**
     * @brief Отправляет сообщение клиенту
     */
    void sendMessage(int clientSocket, const std::string& message) {
        auto frame = encodeFrame(message);
        send(clientSocket, frame.data(), frame.size(), 0);
    }

private:
    /**
     * @brief Извлекает объект params из JSON команды
     */
    std::string extractParams(const std::string& command) {
        size_t paramsStart = command.find("\"params\"");
        if (paramsStart == std::string::npos) {
            return "{}";
        }
        
        size_t braceStart = command.find("{", paramsStart);
        if (braceStart == std::string::npos) {
            return "{}";
        }
        
        // Находим соответствующую закрывающую скобку
        int braceCount = 1;
        size_t pos = braceStart + 1;
        while (pos < command.length() && braceCount > 0) {
            if (command[pos] == '{') braceCount++;
            else if (command[pos] == '}') braceCount--;
            pos++;
        }
        
        return command.substr(braceStart, pos - braceStart);
    }

    /**
     * @brief Обрабатывает команду от клиента
     */
    std::string processCommand(const std::string& command) {
        // Формат команды: {"action": "...", "function": "...", "params": {...}}
        
        // Простой парсер JSON (для production рекомендуется использовать библиотеку)
        if (command.find("\"action\"") != std::string::npos) {
            if (command.find("\"list\"") != std::string::npos) {
                // Список доступных функций
                return functionRegistry.listFunctionsJson();
            } else if (command.find("\"execute\"") != std::string::npos) {
                // Выполнение функции
                size_t funcStart = command.find("\"function\"");
                if (funcStart != std::string::npos) {
                    size_t nameStart = command.find("\"", funcStart + 11) + 1;
                    size_t nameEnd = command.find("\"", nameStart);
                    std::string funcName = command.substr(nameStart, nameEnd - nameStart);
                    
                    // Извлекаем параметры
                    std::string params = extractParams(command);
                    
                    return functionRegistry.execute(funcName, params);
                }
                return "{\"error\":\"Function name not specified\"}";
            } else if (command.find("\"info\"") != std::string::npos) {
                // Информация о сервере
                std::ostringstream json;
                json << "{";
                json << "\"serverName\":\"" << serverName << "\",";
                json << "\"version\":\"" << SERVER_VERSION << "\",";
                json << "\"port\":" << port;
                json << "}";
                return json.str();
            } else if (command.find("\"cancel\"") != std::string::npos) {
                // Отмена текущего эксперимента
                setCancelExperiment(true);
                return "{\"status\":\"cancelling\",\"message\":\"Cancel request sent\"}";
            }
        }
        
        return "{\"error\":\"Unknown command\",\"command\":\"" + command + "\"}";
    }

    /**
     * @brief Структура для передачи данных в поток прослушивания
     */
    struct ListenerData {
        int clientSocket;
        WebSocketServer* server;
        volatile bool* stopFlag;
    };

    /**
     * @brief Поток для параллельного прослушивания входящих сообщений во время эксперимента
     */
    static void* messageListenerThread(void* arg) {
        ListenerData* data = static_cast<ListenerData*>(arg);
        char buffer[BUFFER_SIZE];
        
        // Устанавливаем неблокирующий режим с таймаутом
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms
        setsockopt(data->clientSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        
        while (!(*data->stopFlag)) {
            int bytesRead = recv(data->clientSocket, buffer, BUFFER_SIZE - 1, 0);
            if (bytesRead > 0) {
                bool isClose = false;
                std::string message = data->server->decodeFrame(
                    reinterpret_cast<unsigned char*>(buffer), 
                    bytesRead, 
                    isClose
                );
                
                if (isClose) {
                    setCancelExperiment(true);
                    break;
                }
                
                // Проверяем на команду cancel
                if (message.find("\"cancel\"") != std::string::npos) {
                    setCancelExperiment(true);
                    // Отправляем подтверждение
                    data->server->sendMessage(data->clientSocket, 
                        "{\"status\":\"cancelling\",\"message\":\"Cancel request received\"}");
                }
            }
        }
        
        // Восстанавливаем блокирующий режим
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        setsockopt(data->clientSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        
        return nullptr;
    }

    /**
     * @brief Обрабатывает клиентское соединение
     */
    void handleClient(int clientSocket) {
        char buffer[BUFFER_SIZE];
        
        // Получаем HTTP запрос для handshake
        int bytesRead = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0);
        if (bytesRead <= 0) {
            close(clientSocket);
            return;
        }
        buffer[bytesRead] = '\0';
        
        std::string request(buffer);
        
        // WebSocket handshake
        if (!performHandshake(clientSocket, request)) {
            close(clientSocket);
            return;
        }
        
        // Отправляем приветственное сообщение
        std::ostringstream welcome;
        welcome << "{\"type\":\"welcome\",\"serverName\":\"" << serverName << "\",";
        welcome << "\"version\":\"" << SERVER_VERSION << "\",";
        welcome << "\"message\":\"Connected to Hardware Tester Server\"}";
        sendMessage(clientSocket, welcome.str());
        
        // Основной цикл обработки сообщений
        while (running) {
            bytesRead = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0);
            if (bytesRead <= 0) break;
            
            bool isClose = false;
            std::string message = decodeFrame(
                reinterpret_cast<unsigned char*>(buffer), 
                bytesRead, 
                isClose
            );
            
            if (isClose) break;
            
            if (!message.empty()) {
                // Проверяем, это execute команда?
                bool isExecute = message.find("\"execute\"") != std::string::npos;
                
                pthread_t listenerThread;
                volatile bool stopListener = false;
                ListenerData listenerData = {clientSocket, this, &stopListener};
                
                if (isExecute) {
                    // Запускаем параллельный поток для прослушивания cancel команд
                    pthread_create(&listenerThread, nullptr, messageListenerThread, &listenerData);
                }
                
                std::string response = processCommand(message);
                
                if (isExecute) {
                    // Останавливаем поток прослушивания
                    stopListener = true;
                    pthread_join(listenerThread, nullptr);
                }
                
                sendMessage(clientSocket, response);
            }
        }
        
        close(clientSocket);
    }

public:
    /**
     * @brief Конструктор сервера
     * @param name Название сервера (для обнаружения в сети)
     * @param p Порт сервера
     */
    WebSocketServer(const std::string& name, int p) 
        : serverName(name), port(p), running(false), serverSocket(-1) {}

    /**
     * @brief Деструктор
     */
    ~WebSocketServer() {
        stop();
    }

    /**
     * @brief Запускает сервер
     * @return true если сервер успешно запущен
     */
    bool start() {
        // Инициализация функций
        initializeFunctions();
        
        // Создаём сокет
        serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSocket < 0) {
            std::cerr << "Error: Cannot create socket" << std::endl;
            return false;
        }
        
        // Настройка для переиспользования адреса
        int opt = 1;
        setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        // Привязка к адресу
        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(port);
        
        if (bind(serverSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0) {
            std::cerr << "Error: Cannot bind to port " << port << std::endl;
            close(serverSocket);
            return false;
        }
        
        // Начинаем слушать
        if (listen(serverSocket, 10) < 0) {
            std::cerr << "Error: Cannot listen on socket" << std::endl;
            close(serverSocket);
            return false;
        }
        
        running = true;
        
        std::cout << "========================================" << std::endl;
        std::cout << " " << serverName << " Server v" << SERVER_VERSION << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << " Status: Running" << std::endl;
        std::cout << " Port: " << port << std::endl;
        std::cout << " WebSocket URL: ws://localhost:" << port << std::endl;
        std::cout << " Network Name: " << serverName << std::endl;
        std::cout << "========================================" << std::endl;
        
        // Основной цикл принятия соединений
        while (running) {
            sockaddr_in clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);
            
            int clientSocket = accept(serverSocket, 
                reinterpret_cast<sockaddr*>(&clientAddr), 
                &clientLen);
            
            if (clientSocket < 0) {
                if (running) {
                    std::cerr << "Error: Cannot accept connection" << std::endl;
                }
                continue;
            }
            
            char clientIP[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
            std::cout << "Client connected from: " << clientIP << std::endl;
            
            // Обрабатываем клиента в отдельном потоке
            clientThreads.emplace_back(&WebSocketServer::handleClient, this, clientSocket);
        }
        
        return true;
    }

    /**
     * @brief Останавливает сервер
     */
    void stop() {
        running = false;
        
        if (serverSocket >= 0) {
            close(serverSocket);
            serverSocket = -1;
        }
        
        // Ждём завершения всех клиентских потоков
        for (auto& thread : clientThreads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        clientThreads.clear();
        
        std::cout << "Server stopped" << std::endl;
    }

    /**
     * @brief Возвращает порт сервера
     */
    int getPort() const { return port; }

    /**
     * @brief Возвращает имя сервера
     */
    const std::string& getName() const { return serverName; }
};


// ==================== MAIN ====================

int main() {
    WebSocketServer server(SERVER_NAME, SERVER_PORT);
    
    if (!server.start()) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }
    
    return 0;
}
