#include "server.cpp"


int main()
{
    Server server;

    //Инициализируем порты.
    if (!server.Initialize(8888)) {
        std::cerr << "Неудачная инициализация сервера" << std::endl;
        return 1;
    }
    std::cout << "Сервер успешно инициализирован." << std::endl;

    server.Run();
}
