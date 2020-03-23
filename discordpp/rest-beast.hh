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

	// Report a failure
	void
	fail(beast::error_code ec, char const *what){
		if(std::string(what) != "shutdown" && ec.message() != "stream truncated"){
			std::cerr << what << ": " << ec.message() << "\n";
		}
	}

	// Performs an HTTP GET and prints the response
	template<class BASE>
	class RestBeast: public BASE, virtual public BotStruct, public std::enable_shared_from_this<RestBeast<BASE> >{
	public:
		// Objects are constructed with a strand to
		// ensure that handlers do not execute concurrently.
		virtual void initBot(
				unsigned int apiVersionIn, const std::string &tokenIn, std::shared_ptr<boost::asio::io_context> aiocIn
		) override{
			BASE::initBot(apiVersionIn, tokenIn, aiocIn);
			ctx_ = std::make_unique<ssl::context>(ssl::context::sslv23_client);
			resolver_ = std::make_unique<tcp::resolver>(net::make_strand(*aioc));
			stream_ = nullptr;
			needInit["RestBeast"] = false;
		}

		void call(
				sptr<const std::string> requestType,
				sptr<const std::string> targetURL,
				sptr<const json> body,
				sptr<const std::function<void(const json)>> callback
		) override{
			std::ostringstream targetss;
			targetss << "/api/v" << apiVersion << *targetURL;

			runRest(
					"discordapp.com", "443", http::string_to_verb(*requestType), targetss.str().c_str(), 11, body,
					callback
			);
		}

	private:
		std::unique_ptr<tcp::resolver> resolver_ = nullptr;
		std::unique_ptr<beast::ssl_stream<beast::tcp_stream> > stream_ = nullptr;
		beast::flat_buffer buffer_; // (Must persist between reads)
		http::request<http::string_body> req_;
		std::unique_ptr<http::response<http::string_body>> res_;
		std::unique_ptr<ssl::context> ctx_;

		// Start the asynchronous operation
		void runRest(
				char const *host,
				char const *port,
				http::verb const requestType,
				char const *target,
				int version,
				sptr<const json> &body,
				sptr<const std::function<void(const json)>> callback
		){
			stream_ = std::make_unique<beast::ssl_stream<beast::tcp_stream> >(net::make_strand(*aioc), *ctx_);
			// Set SNI Hostname (many hosts need this to handshake successfully)
			if(!SSL_set_tlsext_host_name(stream_->native_handle(), host)){
				beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
				std::cerr << ec.message() << "\n";
				return;
			}

			// Set up an HTTP GET request message
			req_ = http::request<http::string_body>();
			req_.method(requestType);
			req_.target(target);
			req_.version(version);
			req_.set(http::field::host, host);
			req_.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
			req_.set(http::field::content_type, "application/json");
			req_.set(http::field::authorization, token);
			std::string payload = "";
			if(body != nullptr && !body->empty()){
				payload = body->dump();
				req_.body() = payload;
				req_.prepare_payload();
			}

			// Look up the domain name
			resolver_->async_resolve(
					host,
					port,
					std::bind(
							beast::bind_front_handler(
									&RestBeast::on_resolve,
									this->shared_from_this()
							),
							std::placeholders::_1,
							std::placeholders::_2,
							callback,
							std::string(target),
							payload
					)
			);
		}

		void on_resolve(
				beast::error_code ec,
				tcp::resolver::results_type results,
				sptr<const std::function<void(const json)>> callback,
				const std::string target,
				const std::string payload
		){
			if(ec){
				return fail(ec, "resolve");
			}
			// Set a timeout on the operation
			beast::get_lowest_layer(*stream_).expires_after(std::chrono::seconds(30));

			// Make the connection on the IP address we get from a lookup
			beast::get_lowest_layer(*stream_).async_connect(
					results,
					std::bind(
							beast::bind_front_handler(
									&RestBeast::on_connect,
									this->shared_from_this()
							),
							std::placeholders::_1,
							std::placeholders::_2,
							callback,
							target,
							payload
					)
			);
		}

		void on_connect(
				beast::error_code ec,
				tcp::resolver::results_type::endpoint_type,
				sptr<const std::function<void(const json)>> callback,
				const std::string target,
				const std::string payload
		){
			if(ec){
				return fail(ec, "connect");
			}

			// Perform the SSL handshake
			stream_->async_handshake(
					ssl::stream_base::client,
					std::bind(
							beast::bind_front_handler(
									&RestBeast::on_handshake,
									this->shared_from_this()
							),
							std::placeholders::_1,
							callback,
							target,
							payload
					)
			);
		}

		void on_handshake(
				beast::error_code ec, sptr<const std::function<void(const json)>> callback, const std::string target,
				const std::string payload
		){
			if(ec){
				return fail(ec, "handshake");
			}

			// Set a timeout on the operation
			beast::get_lowest_layer(*stream_).expires_after(std::chrono::seconds(30));

			// Send the HTTP request to the remote host
			http::async_write(
					*stream_, req_,
					std::bind(
							beast::bind_front_handler(
									&RestBeast::on_write,
									this->shared_from_this()
							),
							std::placeholders::_1,
							std::placeholders::_2,
							callback,
							target,
							payload
					)
			);
		}

		void on_write(
				beast::error_code ec, std::size_t bytes_transferred,
				sptr<const std::function<void(const json)>> callback,
				const std::string target,
				const std::string payload
		){
			boost::ignore_unused(bytes_transferred);

			if(ec){
				return fail(ec, "write");
			}
			res_ = std::make_unique<http::response<http::string_body>>();
			// Receive the HTTP response
			http::async_read(
					*stream_, buffer_, *res_,
					std::bind(
							beast::bind_front_handler(
									&RestBeast::on_read,
									this->shared_from_this()
							),
							std::placeholders::_1,
							std::placeholders::_2,
							callback,
							target,
							payload
					)
			);
		}

		void on_read(
				beast::error_code ec, std::size_t bytes_transferred,
				sptr<const std::function<void(const json)>> callback,
				const std::string target,
				const std::string payload
		){
			boost::ignore_unused(bytes_transferred);

			if(ec){
				return fail(ec, "read");
			}

			json jres;
			{
				std::ostringstream ss;
				ss << res_->body();
				if(ss.str().at(0) != '{'){
					std::cerr << "Discord replied:\n" << ss.str() << "\nTo the following target:\n" << target
					          << "\nWith the following payload:\n" << payload << std::endl;
				}else{
					jres = json::parse(ss.str());
				}
			}

			for(auto h : {
					"X-RateLimit-Global",
					"X-RateLimit-Limit",
					"X-RateLimit-Remaining",
					"X-RateLimit-Reset",
					"X-RateLimit-Reset-After",
					"X-RateLimit-Bucket"
			}){
				auto it = res_->find(h);
				if(it != res_->end()){
					jres["header"][h] = it->value();
				}
			}

			if(jres.find("message") != jres.end()){
				std::string message = jres["message"].get<std::string>();
				if(message == "You are being rate limited."){
					throw (ratelimit){jres["retry_after"].get<int>()};
				}else if(message != ""){
					std::cout << "Discord API sent a message: \"" << message << "\"" << std::endl;
				}
			}
			if(jres.find("embed") != jres.end()){
				std::cout << "Discord API didn't like the following parts of your embed: ";
				bool first = true;
				for(json part : jres["embed"]){
					if(first){
						first = false;
					}else{
						std::cout << ", ";
					}
					std::cout << part.get<std::string>();
				}
				std::cout << std::endl;
			}

			if(callback != nullptr){
				aioc->dispatch(
						std::bind(
								*callback,
								jres
						)
				);
			}

			// Set a timeout on the operation
			beast::get_lowest_layer(*stream_).expires_after(std::chrono::seconds(30));

			// Gracefully close the stream
			stream_->async_shutdown(
					beast::bind_front_handler(
							&RestBeast::on_shutdown,
							this->shared_from_this()
					)
			);
		}

		void on_shutdown(beast::error_code ec){
			if(ec == net::error::eof){
				// Rationale:
				// http://stackoverflow.com/questions/25587403/boost-asio-ssl-async-shutdown-always-finishes-with-an-error
				ec = {};
			}
			if(ec){
				return fail(ec, "shutdown");
			}

			// If we get here then the connection is closed gracefully
		}
	};
}
