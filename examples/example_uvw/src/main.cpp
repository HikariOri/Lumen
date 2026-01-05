#include <memory>
#include <uvw.hpp>

void listen(uvw::loop &loop) {
    std::shared_ptr<uvw::tcp_handle> tcp = loop.resource<uvw::tcp_handle>();

    tcp->on<uvw::listen_event>([](const uvw::listen_event &,
                                  uvw::tcp_handle &srv) {
        std::shared_ptr<uvw::tcp_handle> client =
            srv.parent().resource<uvw::tcp_handle>();

        client->on<uvw::close_event>(
            [ptr = srv.shared_from_this()](
                const uvw::close_event &, uvw::tcp_handle &) { ptr->close(); });
        client->on<uvw::end_event>(
            [](const uvw::end_event &, uvw::tcp_handle &client) {
                client.close();
            });

        srv.accept(*client);
        client->read();
    });

    tcp->bind("127.0.0.1", 4242);
    tcp->listen();
}

void conn(uvw::loop &loop) {
    auto tcp = loop.resource<uvw::tcp_handle>();

    tcp->on<uvw::error_event>([](const uvw::error_event &,
                                 uvw::tcp_handle &) { /* handle errors */ });

    tcp->on<uvw::connect_event>(
        [](const uvw::connect_event &, uvw::tcp_handle &tcp) {
            auto dataWrite = std::unique_ptr<char[]>(new char[2] { 'b', 'c' });
            tcp.write(std::move(dataWrite), 2);
            tcp.close();
        });

    tcp->connect(std::string { "127.0.0.1" }, 4242);
}

int main() {

    auto loop = uvw::loop::get_default();

    listen(*loop);

    conn(*loop);

    loop->run();

    return 0;
}
