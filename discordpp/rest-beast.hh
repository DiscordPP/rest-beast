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

#include <nlohmann/json.hpp>

#include <discordpp/botStruct.hh>

namespace discordpp{
    using json = nlohmann::json;
    using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
    namespace ssl = boost::asio::ssl;       // from <boost/asio/ssl.hpp>
    namespace http = boost::beast::http;    // from <boost/beast/http.hpp>

    template<class BASE>
    class RestBeast: public BASE, virtual public BotStruct{
    public:
        virtual std::future<json>
        call(const std::string &requestType, const std::string &targetURL, const json &body = {}) override{
            auto p = std::make_shared<std::promise<json>>();

            aioc->post([this, p, &requestType, &targetURL, &body](){
                std::string host = "discordapp.com";
                std::ostringstream target;
                target << "/api/v" << apiVersion << targetURL;

                // The io_context is required for all I/O
                //asio::io_context ioc;

                // The SSL context is required, and holds certificates
                ssl::context ctx{ssl::context::sslv23_client};

                // This holds the root certificate used for verification
                //load_root_certificates(ctx);

                // These objects perform our I/O
                tcp::resolver resolver{*aioc};
                ssl::stream<tcp::socket> stream{*aioc, ctx};

                // Set SNI Hostname (many hosts need this to handshake successfully)
                if(!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())){
                    boost::system::error_code ec{
                            static_cast<int>(::ERR_get_error()),
                            boost::asio::error::get_ssl_category()
                    };
                    throw boost::system::system_error{ec};
                }

                // Look up the domain name
                //auto const results = resolver.resolve(host, "443");
                auto const results = resolver.async_resolve(host, "443", boost::asio::use_future).get();

                // Make the connection on the IP address we get from a lookup
                boost::asio::async_connect(stream.next_layer(), results.begin(), results.end(),
                                           boost::asio::use_future).wait();

                // Perform the SSL handshake
                stream.async_handshake(ssl::stream_base::client, boost::asio::use_future).wait();

                // Set up an HTTP GET request message
                http::request<http::string_body> req{http::string_to_verb(requestType), target.str(), 11};
                //req.method(requestType);
                //req.target(target.str());
                //req.version(11);
                req.set(http::field::host, host);
                req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
                req.set(http::field::content_type, "application/json");
                req.set(http::field::authorization, token);
                if(!body.empty()){
                    //req.set(http::field::body, body.dump());
                    //req.set(http::field::content_length, body.dump().length());
                    req.body() = body.dump();
                    req.prepare_payload();
                }

                // Send the HTTP request to the remote host
                http::async_write(stream, req, boost::asio::use_future).wait();

                // This buffer is used for reading and must be persisted
                boost::beast::flat_buffer buffer;

                // Declare a container to hold the response
                http::response<http::string_body> res;

                // Receive the HTTP response
                http::async_read(stream, buffer, res, boost::asio::use_future).wait();

                json jres;
                {
                    std::ostringstream ss;
                    ss << res.body();
                    if(ss.str().at(0) != '{'){
                        std::cerr << ss.str() << std::endl;
                    }else{
                        jres = json::parse(ss.str());
                    }
                }

                auto grabHeader = [res, &jres](std::string name){
                    auto it = res.base().find(name);
                    if(it != res.base().end()){
                        jres["header"][name] = std::string(it->value());
                    }
                };

                grabHeader("X-RateLimit-Global");
                grabHeader("X-RateLimit-Limit");
                grabHeader("X-RateLimit-Remaining");
                grabHeader("X-RateLimit-Reset");
                grabHeader("X-RateLimit-Reset-After");
                grabHeader("X-RateLimit-Bucket");

                // Gracefully close the stream
                boost::system::error_code ec;
                stream.shutdown(ec);
                if(ec == boost::asio::error::eof || ec == boost::asio::ssl::error::stream_truncated){
                    // Rationale:
                    // http://stackoverflow.com/questions/25587403/boost-asio-ssl-async-shutdown-always-finishes-with-an-error
                    ec.assign(0, ec.category());
                }
                if(ec)
                    throw boost::system::system_error{ec};

                // If we get here then the connection is closed gracefully

                if(jres.find("message") != jres.end()){
                    std::string message = jres["message"].get<std::string>();
                    if(message == "You are being rate limited."){
                        throw (ratelimit){jres["retry_after"].get<int>()};
                        //std::this_thread::sleep_for(std::chrono::milliseconds(returned["retry_after"].get<int>()));
                    }else if(message != ""){
                        std::cout << "Discord API sent a message: \"" << message << "\"" << std::endl;
                    }
                }
                p->set_value(jres);
            });

            return p->get_future();
        }

        virtual std::future<json>
        call(const http::verb &requestType, const std::string &targetURL, const json &body = {}){
            return call(std::string(http::to_string(requestType)), targetURL, body);
        }
    };
}

#endif //DISCORDPP_REST_BEAST_HH
