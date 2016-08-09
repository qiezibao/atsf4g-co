#include "session_manager.h"
#include <new>


namespace atframe {
    namespace gateway {
        namespace detail {
            template <typename T>
            static void session_manager_delete_stream_fn(uv_stream_t *handle) {
                if (NULL == handle) {
                    return;
                }

                T *real_conn = reinterpret_cast<T *>(handle);
                // must be closed
                assert(uv_is_closing(reinterpret_cast<uv_handle_t *>(handle)));
                delete real_conn;
            }

            template <typename T>
            static T *session_manager_make_stream_ptr(std::shared_ptr<uv_stream_t> &res) {
                T *real_conn = new (std::nothrow) T();
                uv_stream_t *stream_conn = reinterpret_cast<uv_stream_t *>(real_conn);
                res = std::shared_ptr<uv_stream_t>(stream_conn, session_manager_delete_stream_fn<T>);
                stream_conn->data = NULL;
                return real_conn;
            }
        }

        int session_manager::init(::atbus::node *bus_node, create_proto_fn_t fn) {
            evloop_ = bus_node->get_evloop();
            app_node_ = bus_node;
            create_proto_fn_ = fn;
            if (!fn) {
                WLOGERROR("create protocol function is required");
                return -1;
            }
            return 0;
        }

        int session_manager::listen_all() {
            int ret = 0;
            for (std::vector<std::string>::iterator iter = conf_.listen.address.begin(); iter != conf_.listen.address.end(); ++iter) {
                int res = listen((*iter).c_str());
                if (0 != res) {
                    WLOGERROR("try to listen %s failed, res: %d", (*iter).c_str(), res);
                } else {
                    ++ret;
                }
            }

            return 0;
        }

        int session_manager::listen(const char *address) {
            // make_address
            ::atbus::channel::channel_address_t addr;
            ::atbus::detail::make_address(address, addr);

            listen_handle_ptr_t res;
            // libuv listen and setup callbacks
            if (0 == UTIL_STRFUNC_STRNCASE_CMP("ipv4:", addr.scheme.c_str(), 5) ||
                0 == UTIL_STRFUNC_STRNCASE_CMP("ipv6:", addr.scheme.c_str(), 5)) {
                uv_tcp_t *tcp_handle = ::atframe::gateway::detail::session_manager_make_stream_ptr<uv_tcp_t>(res);
                if (res) {
                    uv_stream_set_blocking(res.get(), 0);
                    uv_tcp_nodelay(tcp_handle, 1);
                } else {
                    WLOGERROR("create uv_tcp_t failed.");
                    return error_code_t::EN_ECT_NETWORK;
                }

                if (0 != uv_tcp_init(evloop_, tcp_handle)) {
                    WLOGERROR("init listen to %s failed", address);
                    return error_code_t::EN_ECT_NETWORK;
                }

                if ('4' == addr.scheme[3]) {
                    sockaddr_in sock_addr;
                    uv_ip4_addr(addr.host.c_str(), addr.port, &sock_addr);
                    if (0 != uv_tcp_bind(tcp_handle, reinterpret_cast<const sockaddr *>(&sock_addr), 0)) {
                        WLOGERROR("bind sock to %s failed", address);
                        return error_code_t::EN_ECT_NETWORK;
                    }

                    if (0 != uv_listen(res.get(), conf_.backlog, on_evt_accept_tcp)) {
                        WLOGERROR("listen to %s failed", address);
                        return error_code_t::EN_ECT_NETWORK;
                    }

                    tcp_handle->data = this;
                } else {
                    sockaddr_in6 sock_addr;
                    uv_ip6_addr(addr.host.c_str(), addr.port, &sock_addr);
                    if (0 != uv_tcp_bind(tcp_handle, reinterpret_cast<const sockaddr *>(&sock_addr), 0)) {
                        WLOGERROR("bind sock to %s failed", address);
                        return error_code_t::EN_ECT_NETWORK;
                    }

                    if (0 != uv_listen(res.get(), conf_.backlog, on_evt_accept_tcp)) {
                        WLOGERROR("listen to %s failed", address);
                        return error_code_t::EN_ECT_NETWORK;
                    }

                    tcp_handle->data = this;
                }

            } else if (0 == UTIL_STRFUNC_STRNCASE_CMP("unix:", addr.scheme.c_str(), 5)) {
                uv_pipe_t *pipe_handle = ::atframe::gateway::detail::session_manager_make_stream_ptr<uv_pipe_t>(res);
                if (res) {
                    uv_stream_set_blocking(res.get(), 0);
                } else {
                    WLOGERROR("create uv_pipe_t failed.");
                    return error_code_t::EN_ECT_NETWORK;
                }

                if (0 != uv_pipe_init(evloop_, pipe_handle, 1)) {
                    WLOGERROR("init listen to %s failed", address);
                    return error_code_t::EN_ECT_NETWORK;
                }

                if (0 != uv_pipe_bind(pipe_handle, addr.host.c_str())) {
                    WLOGERROR("bind pipe to %s failed", address);
                    return error_code_t::EN_ECT_NETWORK;
                }

                if (0 != uv_listen(res.get(), conf_.backlog, on_evt_accept_pipe)) {
                    WLOGERROR("listen to %s failed", address);
                    return error_code_t::EN_ECT_NETWORK;
                }

                pipe_handle->data = this;
            } else {
                return error_code_t::EN_ECT_INVALID_ADDRESS;
            }

            if (res) {
                listen_handles_.push_back(res);
            }
            return 0;
        }

        int session_manager::reset() {
            // close all sessions
            for (session_map_t::iterator iter = actived_sessions_.begin(); iter != actived_sessions_.end(); ++iter) {
                if (iter->second) {
                    iter->second->close(close_reason_t::EN_CRT_SERVER_CLOSED);
                }
            }
            actived_sessions_.clear();

            for (std::list<session_timeout_t>::iterator iter = first_idle_.begin(); iter != first_idle_.end(); ++iter) {
                if (iter->second) {
                    iter->second->close(close_reason_t::EN_CRT_SERVER_CLOSED);
                }
            }
            first_idle_.clear();

            for (session_map_t::iterator iter = reconnect_cache_.begin(); iter != reconnect_cache_.end(); ++iter) {
                if (iter->second) {
                    iter->second->close(close_reason_t::EN_CRT_SERVER_CLOSED);
                }
            }
            reconnect_cache_.clear();

            for (std::list<session_timeout_t>::iterator iter = reconnect_timeout_.begin(); iter != reconnect_timeout_.end(); ++iter) {
                if (iter->second) {
                    iter->second->close(close_reason_t::EN_CRT_SERVER_CLOSED);
                }
            }
            reconnect_timeout_.clear();

            // close all listen socks
            for (std::list<listen_handle_ptr_t>::iterator iter = listen_handles_.begin(); iter != listen_handles_.end(); ++iter) {
                if (*iter) {
                    // ref count + 1
                    (*iter)->data = new listen_handle_ptr_t(*iter);
                    uv_close(reinterpret_cast<uv_handle_t *>((*iter).get()), on_evt_listen_closed);
                }
            }
            listen_handles_.clear();
            return 0;
        }

        int session_manager::tick() {
            time_t now = util::time::time_utility::get_now();

            // reconnect timeout
            while (!reconnect_timeout_.empty()) {
                if (reconnect_timeout_.front().timeout > now) {
                    break;
                }

                if (reconnect_timeout_.front().s) {
                    session::ptr_t s = reconnect_timeout_.front().s;
                    reconnect_cache_.erase(s->get_id());
                    s->close(close_reason_t::EN_CRT_LOGOUT);
                }
                reconnect_timeout_.pop_front();
            }

            // first idle timeout
            while (!first_idle_.empty()) {
                if (first_idle_.front().timeout > now) {
                    break;
                }

                if (first_idle_.front().s) {
                    session::ptr_t s = first_idle_.front().s;

                    if (!s->check_flag(session::flag_t::EN_FT_REGISTERED)) {
                        s->close(close_reason_t::EN_CRT_FIRST_IDLE);
                    }
                }
                first_idle_.pop_front();
            }

            return 0;
        }

        int session_manager::close(session::id_t sess_id, int reason, bool allow_reconnect) {
            session_map_t::iterator iter = actived_sessions_.find(sess_id);
            if (actived_sessions_.end() == iter) {
                return 0;
            }


            if (conf_.reconnect_timeout > 0 && allow_reconnect) {
                reconnect_timeout_.push_back(session_timeout_t());
                session_timeout_t &sess_timer = reconnect_timeout_.back();
                sess_timer.s = sess->shared_from_this();
                sess_timer.timeout = util::time::time_utility::get_now() + conf_.reconnect_timeout;
                reconnect_cache_[sess->get_id()] = s;

                // just close fd
                sess->close_fd(reason);
            } else {
                iter->second->close(reason);
            }

            // erase from activited map
            actived_sessions_.erase(iter);
            return 0;
        }

        int session_manager::post_data(bus_id_t tid, ::atframe::gw::ss_msg &msg) {
            return post_data(tid, ::atframe::component::service_type::EN_ATST_GATEWAY, msg);
        }

        int session_manager::post_data(bus_id_t tid, int type, ::atframe::gw::ss_msg &msg) {
            // send to server with type = ::atframe::component::service_type::EN_ATST_GATEWAY
            std::stringstream ss;
            msgpack::pack(ss, msg);
            std::string packed_buffer;
            ss.str().swap(packed_buffer);

            return post_data(tid, type, packed_buffer.data(), packed_buffer.size());
        }

        int session_manager::post_data(bus_id_t tid, int type, const void *buffer, size_t s) {
            // send to process
            if (!app_node_) {
                return error_code_t::EN_ECT_HANDLE_NOT_FOUND;
            }

            return app_node_->send_data(tid, type, buffer, s);
        }

        int session_manager::push_data(session::id_t sess_id, const void *buffer, size_t s) {
            session_map_t::iterator iter = actived_sessions_.find(sess_id);
            if (actived_sessions_.end() == iter) {
                return error_code_t::EN_ECT_SESSION_NOT_FOUND;
            }

            return iter->second->send_to_client(buffer, s);
        }

        int session_manager::broadcast_data(const void *buffer, size_t s) {
            int ret = error_code_t::EN_ECT_SESSION_NOT_FOUND;
            for (session_map_t::iterator iter = actived_sessions_.begin(); iter != actived_sessions_.end(); ++iter) {
                if (iter->second->check_flag(session::flag_t::EN_FT_REGISTERED)) {
                    int res = iter->second->send_to_client(buffer, s);
                    if (0 != res) {
                        WLOGERROR("broadcast data to session 0x%llx failed, res: %d", iter->first, res);
                    }

                    if (0 != ret) {
                        ret = res;
                    }
                }
            }

            return ret;
        }

        int session_manager::set_session_router(session::id_t sess_id, ::atbus::node::id_t router) {
            session_map_t::iterator iter = actived_sessions_.find(sess_id);
            if (actived_sessions_.end() == iter) {
                return error_code_t::EN_ECT_SESSION_NOT_FOUND;
            }

            return iter->second->set_router(router);
        }

        int session_manager::reconnect(session &new_sess, session::id_t old_sess_id) {
            // find old session
            session_map_t::iterator iter = reconnect_cache_.find(old_sess_id);
            if (iter == reconnect_cache_.end()) {
                return error_code_t::EN_ECT_SESSION_NOT_FOUND;
            }

            // check if old session not reconnected
            if (iter->second->check_flag(session::flag_t::EN_FT_RECONNECTED)) {
                WLOGERROR("session %s:%d try to reconnect %llx, but old session already reconnected", new_sess.get_peer_host().c_str(),
                          new_sess.get_peer_port(), old_sess_id);
                return error_code_t::EN_ECT_SESSION_NOT_FOUND;
            }

            // run proto check
            if (NULL == new_sess.get_protocol_handle() || NULL == iter->second->get_protocol_handle()) {
                return error_code_t::EN_ECT_BAD_PROTOCOL;
            }
            if (!new_sess.get_protocol_handle()->check_reconnect(iter->second->get_protocol_handle())) {
                return error_code_t::EN_ECT_REFUSE_RECONNECT;
            }

            // init with reconnect
            int ret = new_sess.init_reconnect(*iter->second);
            new_sess.get_protocol_handle()->set_private_data(&new_sess);
            iter->second->get_protocol_handle()->set_private_data((*iter->second).get());
            return ret;
        }

        void session_manager::on_evt_accept_tcp(uv_stream_t *server, int status) {
            if (0 != status) {
                WLOGERROR("accept tcp socket failed, status: %d", status);
                return;
            }

            // server's data is session_manager
            session_manager *mgr = reinterpret_cast<session_manager *>(server->data);
            assert(mgr);

            session::ptr_t sess;

            {
                std::unique_ptr< ::atframe::gateway::proto_base> proto;
                if (mgr->create_proto_fn_) {
                    mgr->create_proto_fn_().swap(proto);
                }

                // create proto object and session object
                if (proto) {
                    sess = session::create(mgr, proto);
                }
            }

            if (sess && NULL != sess->get_protocol_handle()) {
                sess->get_protocol_handle()->set_private_data(sess.get());
            } else {
                WLOGERROR("create proto fn is null or create proto object failed or create session failed");
                listen_handle_ptr_t sp;
                uv_tcp_t *sock = detail::session_manager_make_stream_ptr<uv_tcp_t>(sp);
                if (NULL != sock) {
                    uv_tcp_init(server->loop, sock);
                    uv_accept(server, reinterpret_cast<uv_stream_t *>(sock));
                    sock->data = new listen_handle_ptr_t(sp);
                    uv_close(reinterpret_cast<uv_handle_t *>(sock), on_evt_listen_closed);
                }
                return;
            }

            // setup send buffer size
            sess->get_protocol_handle()->set_recv_buffer_limit(ATBUS_MACRO_MSG_LIMIT, 2);
            sess->get_protocol_handle()->set_send_buffer_limit(mgr->conf_.send_buffer_size, 0);

            // setup default router
            sess->set_router(mgr->conf_.default_router);

            // create proto object and session object
            int res = sess->accept_tcp(server);
            if (0 != res) {
                sess->close(close_reason_t::EN_CRT_SERVER_BUSY);
                return;
            }

            // check session number limit
            if (mgr->conf_.limits.max_client_number > 0 &&
                mgr->actived_sessions_.size() + mgr->actived_sessions_.size() >= mgr->conf_.limits.max_client_number) {
                sess->close(close_reason_t::EN_CRT_SERVER_BUSY);
                return;
            }

            if (on_create_session_fn_) {
                on_create_session_fn_(sess, sp.get());
            }
        }

        void session_manager::on_evt_accept_pipe(uv_stream_t *server, int status) {
            if (0 != status) {
                WLOGERROR("accept tcp socket failed, status: %d", status);
                return;
            }

            // server's data is session_manager
            session_manager *mgr = reinterpret_cast<session_manager *>(server->data);
            assert(mgr);
            std::unique_ptr< ::atframe::gateway::proto_base> proto;
            if (mgr->create_proto_fn_) {
                mgr->create_proto_fn_().swap(proto);
            }

            session::ptr_t sess;
            // create proto object and session object
            if (proto) {
                sess = session::create(mgr, proto);
            }

            if (!sess) {
                WLOGERROR("create proto fn is null or create proto object failed or create session failed");
                listen_handle_ptr_t sp;
                uv_pipe_t *sock = detail::session_manager_make_stream_ptr<uv_pipe_t>(sp);
                if (NULL != sock) {
                    uv_pipe_init(server->loop, sock, 1);
                    uv_accept(server, reinterpret_cast<uv_stream_t *>(sock));
                    sock->data = new listen_handle_ptr_t(sp);
                    uv_close(reinterpret_cast<uv_handle_t *>(sock), on_evt_listen_closed);
                }
                return;
            }

            // setup send buffer size
            proto->set_recv_buffer_limit(ATBUS_MACRO_MSG_LIMIT, 2);
            proto->set_send_buffer_limit(mgr->conf_.send_buffer_size, 0);

            // setup default router
            sess->set_router(mgr->conf_.default_router);

            int res = sess->accept_pipe(server);
            if (0 != res) {
                sess->close(close_reason_t::EN_CRT_SERVER_BUSY);
                return;
            }

            // check session number limit
            if (mgr->conf_.limits.max_client_number > 0 &&
                mgr->actived_sessions_.size() + mgr->actived_sessions_.size() >= mgr->conf_.limits.max_client_number) {
                sess->close(close_reason_t::EN_CRT_SERVER_BUSY);
                return;
            }

            if (on_create_session_fn_) {
                on_create_session_fn_(sess, sp.get());
            }
        }

        void session_manager::on_evt_listen_closed(uv_handle_t *handle) {
            // delete shared ptr
            listen_handle_ptr_t *ptr = reinterpret_cast<listen_handle_ptr_t *>(handle->data);
            delete ptr;
        }
    }
}