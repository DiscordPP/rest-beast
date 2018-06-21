//
// Created by Aidan on 6/20/2018.
//

#ifndef DISCORDPP_REST_BEAST_HH
#define DISCORDPP_REST_BEAST_HH

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>

namespace discordpp {
    using json = nlohmann::json;
    using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
    namespace ssl = boost::asio::ssl;       // from <boost/asio/ssl.hpp>
    namespace http = boost::beast::http;    // from <boost/beast/http.hpp>

    template<class BASE>
    class RestBeast : public BASE, virtual BotStruct{
        json call(std::string targetURL, std::string token, json body, std::string requestType) override {
            std::string host = "discordapp.com";
            std::ostringstream target("/api/v");
            target << apiVersion << targetURL;

            // The io_context is required for all I/O
            boost::asio::io_context ioc;

            // The SSL context is required, and holds certificates
            ssl::context ctx{ssl::context::sslv23_client};

            // This holds the root certificate used for verification
            //load_root_certificates(ctx);

            // These objects perform our I/O
            tcp::resolver resolver{ioc};
            ssl::stream<tcp::socket> stream{ioc, ctx};

            // Set SNI Hostname (many hosts need this to handshake successfully)
            if(! SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
            {
                boost::system::error_code ec{static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category()};
                throw boost::system::system_error{ec};
            }

            // Look up the domain name
            auto const results = resolver.resolve(host, "80");

            // Make the connection on the IP address we get from a lookup
            boost::asio::connect(stream.next_layer(), results.begin(), results.end());

            // Perform the SSL handshake
            stream.handshake(ssl::stream_base::client);

            // Set up an HTTP GET request message
            http::request<http::string_body> req{http::verb::get, target, 11};
            req.set(http::field::host, host);
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

            // Send the HTTP request to the remote host
            http::write(stream, req);

            // This buffer is used for reading and must be persisted
            boost::beast::flat_buffer buffer;

            // Declare a container to hold the response
            http::response<http::dynamic_body> res;

            // Receive the HTTP response
            http::read(stream, buffer, res);
            json jres(res);

            // Gracefully close the stream
            boost::system::error_code ec;
            stream.shutdown(ec);
            if(ec == boost::asio::error::eof)
            {
                // Rationale:
                // http://stackoverflow.com/questions/25587403/boost-asio-ssl-async-shutdown-always-finishes-with-an-error
                ec.assign(0, ec.category());
            }
            if(ec)
                throw boost::system::system_error{ec};

            // If we get here then the connection is closed gracefully

            try {
                //std::cout << returned.dump() << std::endl;
                std::string message = jres["message"].get<std::string>();
                //std::cout << returned.dump() << std::endl;
                if(message == "You are being rate limited."){
                    throw (ratelimit){jres["retry_after"].get<int>()};
                    //std::this_thread::sleep_for(std::chrono::milliseconds(returned["retry_after"].get<int>()));
                }else if(message != "") {
                    std::cout << "Discord API sent a message: \"" << message << "\"" << std::endl;
                }
            } catch ( std::out_of_range & e) {

            } catch ( std::domain_error & e) {

            }

            return jres;
        }
    };
}

#endif //DISCORDPP_REST_BEAST_HH
