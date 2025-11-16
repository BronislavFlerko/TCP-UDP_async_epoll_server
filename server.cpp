#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <iomanip>
#include <iostream>
#include <algorithm>

class Server {

private:
         //___Приватные поля и методы:___\\

    bool server_running_;
    unsigned int total_users_;
    unsigned int current_users_;
    const std::vector<std::string> server_commands_ = {"/time", "/stats", "/shutdown"};

    int epoll_fd_;
    int tcp_socket_;
    int udp_socket_;


    //Обработка каждого TCP подключения
    void HandleNewTcpConnection() {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);

        //Принимаем новое соединение
        int client_fd = accept(tcp_socket_, (sockaddr*)&client_addr, &addr_len);

        if(client_fd == -1) {
            std::cerr << "Ошибка при принятии TCP соединения" << std::endl;
            return;
        }

        //Делаем сокет неблокирующим, что бы клиенты не застревали в очереди
        int flags = fcntl(client_fd, F_GETFL, 0);
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

        //Добавляем клиентский сокет в epoll
        epoll_event client_event{};
        client_event.events = EPOLLIN | EPOLLET; //epoll сообщит один раз когда данные появились.
        client_event.data.fd = client_fd;

        if(epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &client_event) == -1) {
            std::cerr << "Ошибка при добавлении клиента в epoll" << std::endl;
            close(client_fd);
            return;
        }

        //Обновляем статистику
        total_users_++;
        current_users_++;
    }


    //Обработка данных от TCP клиента
    void HandleTcpClient(int client_fd) {
        char buffer[1024];

        //Читаем ВСЕ доступные данные
        while(true) {
            ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                std::string message(buffer);

                //Убираем символ новой строки если есть
                if(!message.empty() && message.back() == '\n') {
                    message.pop_back();
                }

                std::cout << client_fd << ": " << message << std::endl;

                //Обрабатываем сообщение и отправляем ответ
                ProcessClientMessage(client_fd, message);
            }
            else if(bytes_read == 0) {
                //Клиент отключился
                std::cout << "Клиент " << client_fd << " отключился" << std::endl;
                HandleClientDisconnect(client_fd);
                break;
            }
            else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;  //Все данные прочитаны
                } else {
                    //Ошибка
                    HandleClientDisconnect(client_fd);
                    break;
                }
            }
        }
    }

    //Обработка отключения клиента
    void HandleClientDisconnect(int client_fd) {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
        close(client_fd);

        if (current_users_ > 0) {
            current_users_--;
        }
    }


    //Обработка UDP сообщений
    void HandleUdpMessage() {
        char buffer[1024];
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);

        ssize_t bytes_read = recvfrom(udp_socket_, buffer, sizeof(buffer) - 1, 0,
                                      (sockaddr*)&client_addr, &addr_len);

        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            std::string message(buffer);

            if (!message.empty() && message.back() == '\n') {
                message.pop_back();
            }


            std::string response = ProcessMessage(message);
            std::cout << response << std::endl;

            response += "\n";
            sendto(udp_socket_, response.c_str(), response.length(), 0,
                   (sockaddr*)&client_addr, addr_len);
        }
    }

                        //___Обработчики комманд:___\\


    std::string GetCurrentTime() {
        auto now = std::chrono::system_clock::now();
        std::time_t time = std::chrono::system_clock::to_time_t(now);
        char buffer[20];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&time));
        return std::string(buffer);
    }

    std::string GetStats() {
        return "Total users: " + std::to_string(total_users_) +
               ", Current users: " + std::to_string(current_users_);
    }


    std::string ProcessMessage(const std::string& message) {
        if (message[0] != '/') {
            return message;
        } else {
            auto it = std::find(server_commands_.begin(), server_commands_.end(), message);
            if (it != server_commands_.end()) {
                if (message == "/time") {
                    return GetCurrentTime();
                } else if (message == "/stats") {
                    return GetStats();
                } else if (message == "/shutdown") {
                    server_running_ = false;
                    return "Сервер завершает работу...";
                }
            } else {
                return "Неизвестная команда: " + message;
            }
        }
        return "";
    }


    void ProcessClientMessage(int client_fd, const std::string& message) {
        std::string response = ProcessMessage(message);
        response += "\n";
        send(client_fd, response.c_str(), response.length(), 0);
    }

public:

    Server() : server_running_(true), total_users_(0), current_users_(0),
        epoll_fd_(-1), tcp_socket_(-1), udp_socket_(-1) {}


                            //___Настройка сокетов:___\\


    //Инициализация сокетов
    bool Initialize(int port = 8888) { //базовый порт для тестов

        tcp_socket_ = socket(AF_INET, SOCK_STREAM, 0);
        if (tcp_socket_ == -1) {
            std::cerr << "Ошибка при создании TCP сокета." << std::endl;
            return false;
        }

        udp_socket_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_socket_ == -1) {
            std::cerr << "Ошибка при создании UDP сокета." << std::endl;
            close(tcp_socket_);
            return false;
        }


        //Настройка серверного адресса, принимает соединение со всех интерфейсов.
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port);


        //Привязываем сокеты к адрессу
        if (bind(tcp_socket_, (sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
            std::cerr << "Ошибка при привязке TCP сокета к порту " << port << std::endl;
            close(tcp_socket_);
            close(udp_socket_);
            return false;
        }

        if (bind(udp_socket_, (sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
            std::cerr << "Ошибка при привязке UDP сокета к порту " << port << std::endl;
            close(tcp_socket_);
            close(udp_socket_);
            return false;
        }


        //Прослушиваем TCP сокет
        if (listen(tcp_socket_, 10) == -1) {
            std::cerr << "Ошибка при переводе TCP сокета в режим прослушивания" << std::endl;
            close(tcp_socket_);
            close(udp_socket_);
            return false;
        }


        //Создание epoll
        epoll_fd_ = epoll_create1(0);
        if (epoll_fd_ == -1) {
            std::cerr << "Ошибка при создании epoll" << std::endl;
            close(tcp_socket_);
            close(udp_socket_);
            return false;
        }


        //Добавление сокетов в epoll
        epoll_event tcp_event{};
        tcp_event.events = EPOLLIN; //Данные доступны для чтения
        tcp_event.data.fd = tcp_socket_;

        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, tcp_socket_, &tcp_event) == -1) {
            std::cerr << "Ошибка при добавлении TCP сокета в epoll" << std::endl;
            close(epoll_fd_);
            close(tcp_socket_);
            close(udp_socket_);
            return false;
        }

        epoll_event udp_event{};
        udp_event.events = EPOLLIN;
        udp_event.data.fd = udp_socket_;

        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, udp_socket_, &udp_event) == -1) {
            std::cerr << "Ошибка при добавлении UDP сокета в epoll" << std::endl;
            close(epoll_fd_);
            close(tcp_socket_);
            close(udp_socket_);
            return false;
        }

        //Сервер готов принимать подключения на порту
        std::cout << "Сокеты успешно инициализированы." << std::endl;
        return true;
    }


                                    //___Главный цикл сервера:___\\


    void Run() {
        const int MAX_EVENTS = 64; //64 для баланса, соотношение MAX_EVENTS событий за один вызов к потреблению памяти (норм для тестового)
        epoll_event events[MAX_EVENTS];

        std::cout << "Сервер запущен. Ожидание подключений..." << std::endl;

        while (server_running_) {
            // Ожидаем события на сокетах
            int num_events = epoll_wait(epoll_fd_, events, MAX_EVENTS, -1);

            if (num_events == -1) {
                if (server_running_) {
                    std::cerr << "Ошибка в epoll_wait" << std::endl;
                }
                break;
            }

            // Обрабатываем каждое событие
            for (int i = 0; i < num_events; ++i) {
                int ready_fd = events[i].data.fd;

                if (ready_fd == tcp_socket_) {
                    HandleNewTcpConnection();
                }
                else if (ready_fd == udp_socket_) {
                    HandleUdpMessage();
                }
                else {
                    HandleTcpClient(ready_fd);
                }
            }
        }

        std::cout << "Сервер остановлен" << std::endl;
    }

};
