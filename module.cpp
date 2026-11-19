#ifdef _WIN32
#define IN
#define CONST const
#endif
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#ifdef _WIN32
#undef IN
#undef CONST
#undef DELETE
#endif
#include "ylang/vm.hpp"
#include <iostream>

extern "C" void YlangLoadModule(std::shared_ptr<Env> env, const std::vector<std::string> &symbols) {
    auto moduleNamespace = std::make_shared<ClassObject>("Http");
		moduleNamespace->mro.push_back(moduleNamespace.get());

		auto define = [&](string name, NativeFunc f) {
			moduleNamespace->staticFields[name] = Value::Native(f);
			if (symbols.size() == 1 && symbols[0] == "*") {
				env->set(name, Value::Native(f), true);
				return;
			}
			for (const auto &s : symbols)
				if (s == name) {
					env->set(name, Value::Native(f), true);
					break;
				}
		};

		static auto responseClass = std::make_shared<ClassObject>("HttpResponse");
		auto ResultToValue = [=](const httplib::Result &res) -> Value {
			if (!res) {
				std::cerr << "HTTP request failed with error code: " << (int)res.error() << std::endl;
				return Value::None();
			}
			auto inst = std::make_shared<InstanceObject>(responseClass.get());
			inst->fields["status"] = Value::Int(res->status);
			inst->fields["body"] = Value::String(res->body);
			Value v;
			v.type = ValueType::INSTANCE;
			v.ref = inst;
			return v;
		};

		define("Get", [=](const vector<Value> &args, int l, int c) {
			if (args.size() != 2) throw ArgumentError("Http.Get(url, path)", l, c);
			string url = args[0].asString();
			if (url.find("https://") == 0) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
				string host = url.substr(8);
				httplib::SSLClient cli(host);
				cli.enable_server_certificate_verification(false);
				return ResultToValue(cli.Get(args[1].asString()));
#else
				throw RuntimeError("HTTPS not supported (OpenSSL not enabled)", l, c);
#endif
			} else {
				httplib::Client cli(url);
				return ResultToValue(cli.Get(args[1].asString()));
			}
		});

		define("Post", [=](const vector<Value> &args, int l, int c) {
			if (args.size() != 4) throw ArgumentError("Http.Post(url, path, body, contentType)", l, c);
			string url = args[0].asString();
			if (url.find("https://") == 0) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
				string host = url.substr(8);
				httplib::SSLClient cli(host);
				cli.enable_server_certificate_verification(false);
				return ResultToValue(cli.Post(args[1].asString(), args[2].asString(), args[3].asString()));
#else
				throw RuntimeError("HTTPS not supported", l, c);
#endif
			} else {
				httplib::Client cli(url);
				return ResultToValue(cli.Post(args[1].asString(), args[2].asString(), args[3].asString()));
			}
		});

		define("Put", [=](const vector<Value> &args, int l, int c) {
			if (args.size() != 4) throw ArgumentError("Http.Put(url, path, body, contentType)", l, c);
			string url = args[0].asString();
			if (url.find("https://") == 0) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
				httplib::SSLClient cli(url);
				return ResultToValue(cli.Put(args[1].asString(), args[2].asString(), args[3].asString()));
#else
				throw RuntimeError("HTTPS not supported", l, c);
#endif
			} else {
				httplib::Client cli(url);
				return ResultToValue(cli.Put(args[1].asString(), args[2].asString(), args[3].asString()));
			}
		});

		define("Delete", [=](const vector<Value> &args, int l, int c) {
			if (args.size() != 2) throw ArgumentError("Http.Delete(url, path)", l, c);
			string url = args[0].asString();
			if (url.find("https://") == 0) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
				httplib::SSLClient cli(url);
				return ResultToValue(cli.Delete(args[1].asString()));
#else
				throw RuntimeError("HTTPS not supported", l, c);
#endif
			} else {
				httplib::Client cli(url);
				return ResultToValue(cli.Delete(args[1].asString()));
			}
		});

		static auto httpServerClass = std::make_shared<ClassObject>("HttpServerObj");
		static auto httpsServerClass = std::make_shared<ClassObject>("HttpsServerObj");

		// route handlers run y-- lambdas and need method resolution; a library has no host Interpreter, so keep one bound to the caller's env
		auto interp = std::make_shared<Interpreter>();
		interp->env = env;
		auto bindRouteMethod = [interp, env](httplib::Server* svr, const string &method, const string &path, Value lambda, int l, int c) {
			auto handler = [=](const httplib::Request &req, httplib::Response &res) {
				Chunk tempChunk;
				int lambdaIdx = tempChunk.addConstant(lambda);
				int bodyIdx = tempChunk.addConstant(Value::String(req.body));
				tempChunk.write(OpCode::OP_CONSTANT, l, c);
				tempChunk.write((uint8_t)bodyIdx, l, c);
				tempChunk.write(OpCode::OP_CONSTANT, l, c);
				tempChunk.write((uint8_t)lambdaIdx, l, c);
				tempChunk.write(OpCode::OP_CALL, l, c);
				tempChunk.write((uint8_t)1, l, c);
				tempChunk.write(OpCode::OP_RETURN, l, c);
				VM tempVM;
				tempVM.globals = env;
				tempVM.methodResolver = [interp](MethodCallExpr *m) { return interp->Resolve_methods(m); };
				try {
					tempVM.run(tempChunk);
					Value ret = tempVM.stack.empty() ? Value::None() : tempVM.stack.back();
					if (ret.type == ValueType::STRING) {
						res.set_content(ret.asString(), "application/json");
					} else {
						res.set_content("Success", "text/plain");
					}
				} catch (...) {
					res.status = 500;
					res.set_content("Internal Server Error in y_lang VM", "text/plain");
				}
			};
			if (method == "GET") svr->Get(path, handler);
			else if (method == "POST") svr->Post(path, handler);
		};

		httpServerClass->methods["Get"] = ClassObject::MethodInfo{
			.func = Value::Native([=](const std::vector<Value> &args, int l, int c) -> Value {
				if (args.size() < 4) throw ArgumentError("Server.Get(path, lambda)", l, c);
				auto *self = static_cast<InstanceObject *>(args[0].ref.get());
				auto *svr = (httplib::Server *)self->fields["ptr"].aspInt();
				bindRouteMethod(svr, "GET", args[2].asString(), args[3], l, c);
				return Value::None();
			}),
			.access = AccessLevel::PUBLIC
		};
		httpServerClass->methods["Post"] = ClassObject::MethodInfo{
			.func = Value::Native([=](const std::vector<Value> &args, int l, int c) -> Value {
				if (args.size() < 4) throw ArgumentError("Server.Post(path, lambda)", l, c);
				auto *self = static_cast<InstanceObject *>(args[0].ref.get());
				auto *svr = (httplib::Server *)self->fields["ptr"].aspInt();
				bindRouteMethod(svr, "POST", args[2].asString(), args[3], l, c);
				return Value::None();
			}),
			.access = AccessLevel::PUBLIC
		};
		httpServerClass->methods["set_mount_point"] = ClassObject::MethodInfo{
			.func = Value::Native([=](const std::vector<Value> &args, int l, int c) -> Value {
				if (args.size() < 4) throw ArgumentError("Server.set_mount_point(mount, dir)", l, c);
				auto *self = static_cast<InstanceObject *>(args[0].ref.get());
				auto *svr = (httplib::Server *)self->fields["ptr"].aspInt();
				svr->set_mount_point(args[2].asString(), args[3].asString());
				return Value::None();
			}),
			.access = AccessLevel::PUBLIC
		};
		httpServerClass->methods["listen"] = ClassObject::MethodInfo{
			.func = Value::Native([=](const std::vector<Value> &args, int l, int c) -> Value {
				if (args.size() < 4) throw ArgumentError("Server.listen(host, port)", l, c);
				auto *self = static_cast<InstanceObject *>(args[0].ref.get());
				auto *svr = (httplib::Server *)self->fields["ptr"].aspInt();
				string host = args[2].asString();
				int port = (int)args[3].asInt();
				std::cout << "[ymm Http] Listening on " << host << ":" << port << "...\n";
				svr->listen(host.c_str(), port);
				return Value::None();
			}),
			.access = AccessLevel::PUBLIC
		};

		httpsServerClass->methods = httpServerClass->methods;

		moduleNamespace->staticFields["Server"] = Value::Native([](const std::vector<Value> &args, int l, int c) -> Value {
			auto *svr = new httplib::Server();
			auto inst = std::make_shared<InstanceObject>(httpServerClass.get());
			inst->fields["ptr"] = Value::pInt(svr);
			Value v; v.type = ValueType::INSTANCE; v.ref = inst;
			return v;
		});

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
		moduleNamespace->staticFields["SSLServer"] = Value::Native([](const std::vector<Value> &args, int l, int c) -> Value {
			if (args.size() < 2) throw ArgumentError("Http.SSLServer(cert_path, key_path)", l, c);
			auto *svr = new httplib::SSLServer(args[0].asString().c_str(), args[1].asString().c_str());
			auto inst = std::make_shared<InstanceObject>(httpsServerClass.get());
			inst->fields["ptr"] = Value::pInt(svr);
			Value v; v.type = ValueType::INSTANCE; v.ref = inst;
			return v;
		});
#endif

		Value modVal;
		modVal.type = ValueType::CLASS;
		modVal.ref = moduleNamespace;
		env->set("Http", modVal, false, false);

    // Socket Module
    {
    auto moduleNamespace = std::make_shared<ClassObject>("Socket");
		moduleNamespace->mro.push_back(moduleNamespace.get());

		static auto socketClass = std::make_shared<ClassObject>("SocketObj");

		auto defineConstants = [&](string name, long val) {
			moduleNamespace->staticFields[name] = Value::Int(val);
		};
		defineConstants("AF_INET", AF_INET);
		defineConstants("AF_INET6", AF_INET6);
		defineConstants("SOCK_STREAM", SOCK_STREAM);
		defineConstants("SOCK_DGRAM", SOCK_DGRAM);
		defineConstants("SOCK_RAW", SOCK_RAW);
		defineConstants("IPPROTO_TCP", IPPROTO_TCP);
		defineConstants("IPPROTO_UDP", IPPROTO_UDP);
		defineConstants("SOL_SOCKET", SOL_SOCKET);
		defineConstants("SO_REUSEADDR", SO_REUSEADDR);
		defineConstants("SO_BROADCAST", SO_BROADCAST);

		socketClass->methods["bind"] = ClassObject::MethodInfo{
			.func = Value::Native([](const std::vector<Value> &args, int l, int c) -> Value {
				if (args.size() < 4) throw ArgumentError("bind(host, port)", l, c);
				auto *self = static_cast<InstanceObject *>(args[0].ref.get());
#ifdef _WIN32
				SOCKET sock = (SOCKET)self->fields["ptr"].aspInt();
#else
				int sock = (int)(intptr_t)self->fields["ptr"].aspInt();
#endif
				string host = args[2].asString();
				int port = (int)args[3].asInt();

				struct sockaddr_in addr;
				addr.sin_family = AF_INET;
				addr.sin_port = htons(port);
				addr.sin_addr.s_addr = inet_addr(host.c_str());

				if (::bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
					throw RuntimeError("bind() failed", l, c);
				}
				return Value::None();
			}),
			.access = AccessLevel::PUBLIC
		};

		socketClass->methods["listen"] = ClassObject::MethodInfo{
			.func = Value::Native([](const std::vector<Value> &args, int l, int c) -> Value {
				if (args.size() < 3) throw ArgumentError("listen(backlog)", l, c);
				auto *self = static_cast<InstanceObject *>(args[0].ref.get());
#ifdef _WIN32
				SOCKET sock = (SOCKET)self->fields["ptr"].aspInt();
#else
				int sock = (int)(intptr_t)self->fields["ptr"].aspInt();
#endif
				int backlog = (int)args[2].asInt();
				if (::listen(sock, backlog) < 0) {
					throw RuntimeError("listen() failed", l, c);
				}
				return Value::None();
			}),
			.access = AccessLevel::PUBLIC
		};

		socketClass->methods["accept"] = ClassObject::MethodInfo{
			.func = Value::Native([](const std::vector<Value> &args, int l, int c) -> Value {
				auto *self = static_cast<InstanceObject *>(args[0].ref.get());
#ifdef _WIN32
				SOCKET sock = (SOCKET)self->fields["ptr"].aspInt();
				struct sockaddr_in client_addr;
				int addrlen = sizeof(client_addr);
				SOCKET client_sock = ::accept(sock, (struct sockaddr*)&client_addr, &addrlen);
				if (client_sock == INVALID_SOCKET) throw RuntimeError("accept() failed", l, c);
#else
				int sock = (int)(intptr_t)self->fields["ptr"].aspInt();
				struct sockaddr_in client_addr;
				socklen_t addrlen = sizeof(client_addr);
				int client_sock = ::accept(sock, (struct sockaddr*)&client_addr, &addrlen);
				if (client_sock < 0) throw RuntimeError("accept() failed", l, c);
#endif
				auto inst = std::make_shared<InstanceObject>(socketClass.get());
				inst->fields["ptr"] = Value::pInt((void*)(intptr_t)client_sock);
				Value v;
				v.type = ValueType::INSTANCE;
				v.ref = inst;
				return v;
			}),
			.access = AccessLevel::PUBLIC
		};

		socketClass->methods["connect"] = ClassObject::MethodInfo{
			.func = Value::Native([](const std::vector<Value> &args, int l, int c) -> Value {
				if (args.size() < 4) throw ArgumentError("connect(host, port)", l, c);
				auto *self = static_cast<InstanceObject *>(args[0].ref.get());
#ifdef _WIN32
				SOCKET sock = (SOCKET)self->fields["ptr"].aspInt();
#else
				int sock = (int)(intptr_t)self->fields["ptr"].aspInt();
#endif
				string host = args[2].asString();
				int port = (int)args[3].asInt();

				struct sockaddr_in addr;
				addr.sin_family = AF_INET;
				addr.sin_port = htons(port);
				addr.sin_addr.s_addr = inet_addr(host.c_str());

				if (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
					throw RuntimeError("connect() failed", l, c);
				}
				return Value::None();
			}),
			.access = AccessLevel::PUBLIC
		};

		socketClass->methods["send"] = ClassObject::MethodInfo{
			.func = Value::Native([](const std::vector<Value> &args, int l, int c) -> Value {
				if (args.size() < 3) throw ArgumentError("send(data)", l, c);
				auto *self = static_cast<InstanceObject *>(args[0].ref.get());
#ifdef _WIN32
				SOCKET sock = (SOCKET)self->fields["ptr"].aspInt();
#else
				int sock = (int)(intptr_t)self->fields["ptr"].aspInt();
#endif
				string data = args[2].asString();
				int sent = ::send(sock, data.c_str(), data.length(), 0);
				return Value::Int(sent);
			}),
			.access = AccessLevel::PUBLIC
		};

		socketClass->methods["recv"] = ClassObject::MethodInfo{
			.func = Value::Native([](const std::vector<Value> &args, int l, int c) -> Value {
				if (args.size() < 3) throw ArgumentError("recv(size)", l, c);
				auto *self = static_cast<InstanceObject *>(args[0].ref.get());
#ifdef _WIN32
				SOCKET sock = (SOCKET)self->fields["ptr"].aspInt();
#else
				int sock = (int)(intptr_t)self->fields["ptr"].aspInt();
#endif
				int size = (int)args[2].asInt();
				std::vector<char> buffer(size);
				int received = ::recv(sock, buffer.data(), size, 0);
				if (received <= 0) return Value::String("");
				return Value::String(string(buffer.data(), received));
			}),
			.access = AccessLevel::PUBLIC
		};

		socketClass->methods["close"] = ClassObject::MethodInfo{
			.func = Value::Native([](const std::vector<Value> &args, int l, int c) -> Value {
				auto *self = static_cast<InstanceObject *>(args[0].ref.get());
#ifdef _WIN32
				SOCKET sock = (SOCKET)self->fields["ptr"].aspInt();
				::closesocket(sock);
#else
				int sock = (int)(intptr_t)self->fields["ptr"].aspInt();
				::close(sock);
#endif
				return Value::None();
			}),
			.access = AccessLevel::PUBLIC
		};
        
		
		socketClass->methods["setblocking"] = ClassObject::MethodInfo{
			.func = Value::Native([](const std::vector<Value> &args, int l, int c) -> Value {
				if (args.size() < 3) throw ArgumentError("setblocking(bool)", l, c);
				auto *self = static_cast<InstanceObject *>(args[0].ref.get());
#ifdef _WIN32
				SOCKET sock = (SOCKET)self->fields["ptr"].aspInt();
#else
				int sock = (int)(intptr_t)self->fields["ptr"].aspInt();
#endif
				bool block = args[2].asBool();
#ifdef _WIN32
				u_long mode = block ? 0 : 1;
				if (ioctlsocket(sock, FIONBIO, &mode) != NO_ERROR) {
					throw RuntimeError("ioctlsocket failed", l, c);
				}
#else
				int flags = fcntl(sock, F_GETFL, 0);
				if (flags == -1) throw RuntimeError("fcntl F_GETFL failed", l, c);
				flags = block ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
				if (fcntl(sock, F_SETFL, flags) != 0) {
					throw RuntimeError("fcntl F_SETFL failed", l, c);
				}
#endif
				return Value::None();
			}),
			.access = AccessLevel::PUBLIC
		};

		socketClass->methods["setsockopt"] = ClassObject::MethodInfo{
			.func = Value::Native([](const std::vector<Value> &args, int l, int c) -> Value {
				if (args.size() < 5) throw ArgumentError("setsockopt(level, optname, value)", l, c);
				auto *self = static_cast<InstanceObject *>(args[0].ref.get());
#ifdef _WIN32
				SOCKET sock = (SOCKET)self->fields["ptr"].aspInt();
#else
				int sock = (int)(intptr_t)self->fields["ptr"].aspInt();
#endif
				int level = (int)args[2].asInt();
				int optname = (int)args[3].asInt();
				int value = (int)args[4].asInt();
				
#ifdef _WIN32
                const char* val_ptr = (const char*)&value;
#else
                const void* val_ptr = &value;
#endif

				if (::setsockopt(sock, level, optname, val_ptr, sizeof(value)) < 0) {
					throw RuntimeError("setsockopt() failed", l, c);
				}
				return Value::None();
			}),
			.access = AccessLevel::PUBLIC
		};

		moduleNamespace->staticFields["create"] = Value::Native([](const std::vector<Value> &args, int l, int c) -> Value {
			if (args.size() < 3) throw ArgumentError("Socket.create(domain, type, protocol)", l, c);
			int domain = (int)args[0].asInt();
			int type = (int)args[1].asInt();
			int protocol = (int)args[2].asInt();
			
#ifdef _WIN32
			SOCKET sock = ::socket(domain, type, protocol);
			if (sock == INVALID_SOCKET) throw RuntimeError("Failed to create socket", l, c);
#else
			int sock = ::socket(domain, type, protocol);
			if (sock < 0) throw RuntimeError("Failed to create socket", l, c);
#endif
			auto inst = std::make_shared<InstanceObject>(socketClass.get());
			inst->fields["ptr"] = Value::pInt((void*)(intptr_t)sock);
			Value v;
			v.type = ValueType::INSTANCE;
			v.ref = inst;
			return v;
		});

		Value classVal;
		classVal.type = ValueType::CLASS;
		classVal.ref = socketClass;
		moduleNamespace->staticFields["SocketObj"] = classVal;
		
		Value modVal;
		modVal.type = ValueType::CLASS;
		modVal.ref = moduleNamespace;
		env->set("Socket", modVal, false, false);
	}
}
