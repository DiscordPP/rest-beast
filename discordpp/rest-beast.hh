//
// Created by Aidan on 6/20/2018.
//

//
// Example Code Header:
// Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/beast
//

#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/strand.hpp>

#include <nlohmann/json.hpp>

#include <discordpp/botStruct.hh>

namespace discordpp{
	using json = nlohmann::json;
	namespace beast = boost::beast;         // from <boost/beast.hpp>
	namespace http = beast::http;           // from <boost/beast/http.hpp>
	namespace net = boost::asio;            // from <boost/asio.hpp>
	namespace ssl = boost::asio::ssl;       // from <boost/asio/ssl.hpp>
	using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

	// Performs an HTTP GET and prints the response
	template<class BASE>
	class RestBeast: public BASE, virtual public BotStruct, public std::enable_shared_from_this<RestBeast<BASE> >{
		std::unique_ptr<boost::asio::steady_timer> retry_;
	public:
		// Objects are constructed with a strand to
		// ensure that handlers do not execute concurrently.
		virtual void initBot(
				unsigned int apiVersionIn, const std::string &tokenIn, std::shared_ptr<boost::asio::io_context> aiocIn
		) override{
			BASE::initBot(apiVersionIn, tokenIn, aiocIn);
			ctx_ = std::make_unique<ssl::context>(ssl::context::sslv23_client);
		}

		virtual void call(
				sptr<const std::string> requestType,
				sptr<const std::string> targetURL,
				sptr<const json> body,
				sptr<const handleWrite> onWrite,
				sptr<const handleRead> onRead
		) override{
			std::ostringstream targetss;
			targetss << "/api/v" << apiVersion << *targetURL;

			runRest(
					"discordapp.com", "443", http::string_to_verb(*requestType), targetss.str().c_str(), 11,
					std::make_shared<Call>(requestType, targetURL, body, onWrite, onRead)
			);
		}

	private:
		struct Call{
			Call(
				sptr<const std::string> requestType, sptr<const std::string> targetUrl, sptr<const json> body,
				sptr<const handleWrite> onWrite, sptr<const handleRead> onRead
			): requestType(std::move(requestType)),
			   targetURL(std::move(targetUrl)),
			   body(std::move(body)), onWrite(std::move(onWrite)),
			   onRead(std::move(onRead)){}
			
			sptr<const std::string> requestType;
			sptr<const std::string> targetURL;
			sptr<const json> body;
			sptr<const handleWrite> onWrite;
			sptr<const handleRead> onRead;
		};
		struct Session{
			Session(boost::asio::io_context &aioc, ssl::context &ctx):
			resolver(aioc),
			stream(net::make_strand(aioc), ctx){}
			
			tcp::resolver resolver;
			beast::ssl_stream<beast::tcp_stream> stream;
			beast::flat_buffer buffer;
			http::request<http::string_body> req;
			http::response<http::string_body> res;
		};
		
		std::unique_ptr<ssl::context> ctx_;

		// Start the asynchronous operation
		void runRest(
				char const *host,
				char const *port,
				http::verb const requestType,
				char const *target,
				int version,
				sptr<Call> call
		){
			sptr<Session> session = std::make_shared<Session>(*aioc, *ctx_);
			
			// Set SNI Hostname (many hosts need this to handshake successfully)
			if(!SSL_set_tlsext_host_name(session->stream.native_handle(), host)){
				beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
				std::cerr << ec.message() << "\n";
				return;
			}

			// Set up an HTTP GET request message
			session->req.method(requestType);
			session->req.target(target);
			session->req.version(version);
			session->req.set(http::field::host, host);
			session->req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
			session->req.set(http::field::content_type, "application/json");
			session->req.set(http::field::authorization, token);
			std::string payload = "";
			if(call->body != nullptr && !call->body->empty()){
				payload = call->body->dump();
			}
			if(requestType == http::verb::post || requestType == http::verb::put || (call->body != nullptr && !call->body->empty())){
				session->req.body() = payload;
				session->req.prepare_payload();
			}

			// Look up the domain name
			session->resolver.async_resolve(
					host,
					port,
					std::bind(
							beast::bind_front_handler(
									&RestBeast::on_resolve,
									this->shared_from_this()
							),
							std::placeholders::_1,
							std::placeholders::_2,
							session,
							call,
							std::string(target),
							payload,
							host,
							port
					)
			);
		}

		void on_resolve(
				beast::error_code ec,
				tcp::resolver::results_type results,
				sptr<Session> session,
				sptr<Call> call,
				const std::string target,
				const std::string payload,
				char const *host,
				char const *port
		){
			if(ec){
				if(connecting){
					std::cerr << "Could not resolve host...";
					retry_ = std::make_unique<boost::asio::steady_timer>(
							*aioc,
							std::chrono::steady_clock::now() + std::chrono::seconds(35)
					);
					retry_->async_wait(
							[this, host, port, session, call, target, payload](const boost::system::error_code){
								std::cerr << " retrying...\n";
								session->resolver.async_resolve(
										host,
										port,
										std::bind(
												beast::bind_front_handler(
														&RestBeast::on_resolve,
														this->shared_from_this()
												),
												std::placeholders::_1,
												std::placeholders::_2,
												session,
												call,
												std::string(target),
												payload,
												host,
												port
										)
								);
							}
					);
					return;
				}else{
					if(call->onWrite)aioc->post([call]{(*call->onWrite)(true);});
					if(call->onRead)aioc->post([call]{(*call->onRead)(true, {});});
					return fail(ec, "resolve", call);
				}
			}
			// Set a timeout on the operation
			beast::get_lowest_layer(session->stream).expires_after(std::chrono::seconds(30));

			// Make the connection on the IP address we get from a lookup
			beast::get_lowest_layer(session->stream).async_connect(
					results,
					std::bind(
							beast::bind_front_handler(
									&RestBeast::on_connect,
									this->shared_from_this()
							),
							std::placeholders::_1,
							std::placeholders::_2,
							session,
							call,
							target,
							payload
					)
			);
		}

		void on_connect(
				beast::error_code ec,
				tcp::resolver::results_type::endpoint_type,
				sptr<Session> session,
				sptr<Call> call,
				const std::string target,
				const std::string payload
		){
			if(ec){
				if(call->onWrite)aioc->post([call]{(*call->onWrite)(true);});
				if(call->onRead)aioc->post([call]{(*call->onRead)(true, {});});
				return fail(ec, "connect", call);
			}

			// Perform the SSL handshake
			session->stream.async_handshake(
					ssl::stream_base::client,
					std::bind(
							beast::bind_front_handler(
									&RestBeast::on_handshake,
									this->shared_from_this()
							),
							std::placeholders::_1,
							session,
							call,
							target,
							payload
					)
			);
		}

		void on_handshake(
				beast::error_code ec,
				sptr<Session> session,
				sptr<Call> call,
				const std::string target,
				const std::string payload
		){
			if(ec){
				if(call->onWrite)aioc->post([call]{(*call->onWrite)(true);});
				if(call->onRead)aioc->post([call]{(*call->onRead)(true, {});});
				return fail(ec, "handshake", call);
			}

			// Set a timeout on the operation
			beast::get_lowest_layer(session->stream).expires_after(std::chrono::seconds(30));

			// Send the HTTP request to the remote host
			http::async_write(
					session->stream, session->req,
					std::bind(
							beast::bind_front_handler(
									&RestBeast::on_write,
									this->shared_from_this()
							),
							std::placeholders::_1,
							std::placeholders::_2,
						    session,
							call,
							target,
							payload
					)
			);
		}

		void on_write(
				beast::error_code ec,
				std::size_t bytes_transferred,
				sptr<Session> session,
				sptr<Call> call,
				const std::string target,
				const std::string payload
		){
			boost::ignore_unused(bytes_transferred);

			if(ec){
				if(call->onWrite)aioc->post([call]{(*call->onWrite)(true);});
				if(call->onRead)aioc->post([call]{(*call->onRead)(true, {});});
				return fail(ec, "write", call);
			}

			if(call->onWrite){
				aioc->post([call]{(*call->onWrite)(false);});
			}

			// Receive the HTTP response
			http::async_read(
					session->stream, session->buffer, session->res,
					std::bind(
							beast::bind_front_handler(
									&RestBeast::on_read,
									this->shared_from_this()
							),
							std::placeholders::_1,
							std::placeholders::_2,
							session,
							call,
							target,
							payload
					)
			);
		}

		void on_read(
				beast::error_code ec,
				std::size_t bytes_transferred,
				sptr<Session> session,
				sptr<Call> call,
				const std::string target,
				const std::string payload
		){
			boost::ignore_unused(bytes_transferred);

			if(ec){
				if(call->onRead)aioc->post([call]{(*call->onRead)(true, {});});
				return fail(ec, "read", call);
			}

			json jres;
			{
				std::ostringstream ss;
				ss << session->res.body();
				const std::string &body = ss.str();
				if(!body.empty()){
					if(body.at(0) != '{'){
						std::cerr << "Discord replied:\n" << ss.str() << "\nTo the following target:\n" << target
						          << "\nWith the following payload:\n" << payload << std::endl;
					}else{
						jres = json::parse(body);
					}
				}
			}
			
			jres["result"] = session->res.result_int();

			for(
				auto h : {
					"X-RateLimit-Global",
					"X-RateLimit-Limit",
					"X-RateLimit-Remaining",
					"X-RateLimit-Reset",
					"X-RateLimit-Reset-After",
					"X-RateLimit-Bucket"
			}
					){
				auto it = session->res.find(h);
				if(it != session->res.end()){
					jres["header"][h] = it->value();
				}
			}
			
			if(jres.find("embed") != jres.end()){
				std::cout << "Discord API didn't like the following parts of your embed: ";
				bool first = true;
				for(
					json part : jres["embed"]
						){
					if(first){
						first = false;
					}else{
						std::cout << ", ";
					}
					std::cout << part.get<std::string>();
				}
				std::cout << std::endl;
			}
			
			log::log(log::info, [call, jres](std::ostream *log){
				*log << "Read " << call->targetURL << jres.dump(4) << '\n';
			});

			if(call->onRead){
				aioc->post(
						[call, jres, result = session->res.result_int()](){
							(*call->onRead)(result != 200, jres);
						}
				);
			}

			// Set a timeout on the operation
			beast::get_lowest_layer(session->stream).expires_after(std::chrono::seconds(30));
			
			// Gracefully close the stream
			session->stream.async_shutdown(
				std::bind(
					beast::bind_front_handler(
							&RestBeast::on_shutdown,
							this->shared_from_this()
					),
					std::placeholders::_1,
					session
				)
			);
		}

		void on_shutdown(beast::error_code ec, sptr<Session> session){
			if(ec == net::error::eof){
				// Rationale:
				// http://stackoverflow.com/questions/25587403/boost-asio-ssl-async-shutdown-always-finishes-with-an-error
				ec = {};
			}
			if(ec){
				return fail(ec, "shutdown", nullptr);
			}

			// If we get here then the connection is closed gracefully
		}
		
		// Report a failure
		static void fail(beast::error_code ec, char const *what, sptr<Call> call){
			if(std::string(what) != "shutdown" && ec.message() != "stream truncated"){
				log::log(log::error, [ec, what, call](std::ostream* log){
					*log << what << ": " << ec.message();
					if(call != nullptr){
						*log << ' ' << *call->targetURL << (call->body ? call->body->dump(4) : "{}");
					}
					*log << '\n';
				});
			}
		}
	};
}
