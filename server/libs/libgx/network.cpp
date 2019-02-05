#include "network.h"
#include "peer.h"
#include "log.h"
#include "hash.h"
#include "coroutine.h"
#include "servlet.h"
#include "rc.h"
#include "application.h"

#ifndef GX_PLATFORM_WIN32
#include <unistd.h>
#endif

#include "script.h"

GX_NS_BEGIN

/* NetworkInstance */
NetworkInstance::NetworkInstance(
    Network *network,
    NetworkNode *node,
    const std::string &host,
    unsigned port,
    bool ap) noexcept
: _host(host),
  _port(port),
  _timeout(3000),
  _interval(1000),
  _ap(ap),
  _network(network),
  _node(node),
  _is_local(false)
{ }

bool NetworkInstance::listen(ptr<Reactor> reactor) noexcept {
    if (_listener) {
        return true;
    }
    Address addr;
    if (!addr.resolve(_host.c_str(), _port)) {
        return false;
    }
    _listener = object<Listener>(addr, reactor, std::bind(&NetworkInstance::on_accept, this, _1, _2, _3));
    return _listener->listen();
}

bool NetworkInstance::connect() noexcept {
    if (!_connector) {
        if (!_conn_addr.resolve(_host.c_str(), _port)) {
            return false;
        }
        _connector = object<Connector>(
            _conn_addr, _network->_reactor, _network->_timermgr,
            _timeout, _interval,
            std::bind(&NetworkInstance::on_connection, this, _1, _2));
    }

    log_debug("connect to %s[%d] = %s:%d.", _node->name(), id(), _conn_addr.host(), _conn_addr.port());
    return _connector->connect();
}

bool NetworkInstance::on_connection(Socket *socket, int flags) noexcept {
    if (flags & (Reactor::poll_in | Reactor::poll_out)) {
        socket->flags(-1);
        assert(!_peer);
        ptr<Peer> peer = object<Peer>(false);
        peer->_socket = socket;
        socket->handler(std::bind(&NetworkInstance::on_data, this, peer, _1, _2));
        socket->flags(-1);
        assert(!_peer);
        _peer = peer.get();
        _network->_connect_list.push_front(peer);
        log_debug("connect %s[%d] = %s:%d successed, at socket %d.", _node->name(), id(), _conn_addr.host(), _conn_addr.port(), socket->fd());
    }
    return true;
}

bool NetworkInstance::on_accept(int fd, unsigned flags, const Address &addr) noexcept {
    if (flags & Reactor::poll_open) {
        log_debug("listen %s[%d] = %s:%d at socket %d.", _node->name(), id(), addr.host(), addr.port(), fd);
    }

    if (flags & Reactor::poll_in) {
        log_debug("accept %s:%d at socket %d.", addr.host(), addr.port(), fd);
        ptr<Peer> peer = object<Peer>(is_ap());
        Socket *socket = _listener->reactor()->open(fd, -1, std::bind(&NetworkInstance::on_data, this, peer, _1, _2));
        peer->_socket = socket;
        peer->_network = _network;
        _network->_accept_list.push_front(peer);
    }

    return true;
}

bool NetworkInstance::on_data(ptr<Peer> peer, Socket &socket, int flags) noexcept {
    int n;
    if (flags & Reactor::poll_close) {
        int fd = socket.fd();
        fd_close(fd);
        log_debug("socket %d closed.", fd);
        if (peer.get() == _peer) {
            log_warning("connection %s[%d] = %s:%d closed, at socket %d.", _node->name(), id(), _conn_addr.host(), _conn_addr.port(), fd);
            _network->_timermgr->schedule_abs(0, [this](Timer&, timeval_t) {
                connect();
                return 0;
            });
        }
        return false;
    }

    if (flags & Reactor::poll_err) {
        return false;
    }
    if (flags & Reactor::poll_out) {
        socket.send();
    }
    if (flags & Reactor::poll_in) {
        n = socket.load();
        if (n < 0) {
            log_debug("socket %d load failed %d.", socket.fd(), n);
            return false;
        }
        while (peer->_socket && socket.input().size() > 0) {
            ProtocolInfo info;
            n = peer->unserial(info, socket.input());
            if (!n) {
                break;
            }
            else if (n > 0) {
                if (peer.get() != _peer) {
                    _network->request_handler(info, peer, socket.input());
                }
                else {
                    _network->response_handler(info, peer, socket.input());
                }
            }
            else {
                return false;
            }
        }
    }
    return true;
}

/* NetworkServlet */
NetworkServlet::NetworkServlet(unsigned id) noexcept
: _id(id)
{ }

/* NetworkServlets */
void NetworkServlets::add(unsigned index, ptr<NetworkInstance> instance) noexcept {
    if (index >= _servlets.size()) {
        _servlets.resize(index + 1);
    }
    if (!_servlets[index]) {
        _servlets[index] = object<NetworkServlet>(index);
    }
    _servlets[index]->_instances.push_back(instance);
}

/* Network */
bool Network::init(Script *script, ptr<TimerManager> timermgr, ptr<Reactor> reactor) noexcept {
#ifdef GX_USE_LUA
    auto tab = script->read_table("the_network");
    if (tab->is_nil()) {
        return false;
    }
    for (unsigned i = 1; ; ++i) {
        auto instance_tab = tab->read_table(i);
        if (instance_tab->is_nil()) {
            break;
        }
        std::string host = instance_tab->read_string("host");
        unsigned port = instance_tab->read_integer("port");
        std::string name = instance_tab->read_string("name");
        unsigned node_id = instance_tab->read_integer("id");
        unsigned is_ap = instance_tab->read_integer("ap");

        if (_nodes.size() <= node_id) {
            _nodes.resize(node_id + 1);
        }
        ptr<NetworkNode> node = _nodes[node_id];
        if (!node) {
            node = object<NetworkNode>(node_id, name);
            _nodes[node_id] = node;
        }
        object<NetworkInstance> instance(this, node, host, port, is_ap);
        _instances.push_back(instance);
        if (is_ap) {
            instance->_id = node->_aps.size();
            node->_aps.push_back(instance);
        }
        else {
            instance->_id = node->_instances.size();
            node->_instances.push_back(instance);
        }
        auto servlets_tab = instance_tab->read_table("servlets");
        if (servlets_tab) {
            for (unsigned i = 1; ; ++i) {
                int servlet_id = servlets_tab->read_integer(i, -1);
                if (servlet_id == -1) {
                    break;
                }
                if (!is_ap) {
                    _servlets.add(servlet_id, instance);
                    node->_servlets.add(servlet_id, instance);
                }
                if (instance->_servlets.size() <= (unsigned)servlet_id) {
                    instance->_servlets.resize(servlet_id + 1);
                }
                instance->_servlets[servlet_id] = true;
            }
        }
    }
    _timermgr = timermgr;
    _reactor = reactor;
    _rpc_timeout = 3000;
    return true;
#else
	return false;
#endif
}

bool Network::startup(int type, unsigned id, bool ap) noexcept {
    _type = type;
    _id = id;
    assert(_type >= 0 && (unsigned)_type < _nodes.size());

    NetworkNode *node = _nodes[type];
    if (id < node->_aps.size()) {
        NetworkInstance *instance = node->_aps[id];
        if (instance) {
            instance->_is_local = true;
            instance->listen(_reactor);
        }
    }

    if (id < node->_instances.size()) {
        NetworkInstance *instance = node->_instances[id];
        if (instance) {
            instance->_is_local = true;
            instance->listen(_reactor);
        }
    }

    for (auto &n : _instances) {
        if (!n->is_ap()) {
            n->connect();
        }
    }
    return true;
}

void Network::shutdown_servlets() noexcept {
    NetworkNode *node = _nodes[_type];
    NetworkInstance *instance = node->_instances[_id];
    instance->_listener->close();
    instance = node->_instances[_id];
    instance->_listener->close();

    Peer *peer;
    while ((peer = _accept_list.pop_front())) {
        peer->close();
    }
}

Peer *Network::send(uint64_t id, unsigned servlet, const INotify *req, unsigned *seq_r, NetworkInstance *instance) noexcept {
    assert(id);

    if (!instance) {
        instance = servlet_lb(GX_SERVLET_TYPE(servlet), id);
        if (!instance) {
            return nullptr;
        }
        if (!instance->_peer) {
            return nullptr;
        }
    }

    unsigned seq = _seq++;
    if (!seq) {
        seq = _seq = 1;
    }

    if (seq_r) {
        *seq_r = seq;
    }

    if (instance->_peer->send(servlet, seq, req)) {
        return instance->_peer;
    }
    return nullptr;
}

void Network::broadcast(unsigned servlet, const INotify *req) noexcept {
    const NetworkServlet *pservlet = _servlets.get(GX_SERVLET_TYPE(servlet));
    if (!pservlet) {
        return;
    }

    unsigned seq = _seq++;
    if (!seq) {
        seq = _seq = 1;
    }

    for (auto &instance : pservlet->_instances) {
        if (instance->_peer) {
            instance->_peer->send(servlet, seq, req);
        }
    }
}

void Network::call(uint64_t id, unsigned servlet, IRequest *req, IResponse *rsp, NetworkInstance *instance) {
    unsigned seq;

    assert(!Coroutine::is_main_routine());

    if (!instance) {
        instance = servlet_lb(GX_SERVLET_TYPE(servlet), id);
        if (!instance) {
            log_debug("send call failed, servlet = %x.", servlet);
            throw ServletException(GX_EBUSY);
        }
    }

    Peer *peer = send(id, servlet, req, &seq, instance);
    if (!peer) {
        log_debug("send call failed, servlet = %x, seq = %d.", servlet, seq);
        throw ServletException(GX_EBUSY);
    }

    Context *ctx = the_context();
    auto r = _call_map.emplace(seq, ctx);
    if (!r.second) {
        log_debug("dup call seq, servlet = %x, seq = %d.", servlet, seq);
        throw ServletException(GX_EBUSY);
    }

    log_debug("send call, servlet = %x, seq = %d.", servlet, seq);

    ctx->_call_result = GX_CALL_UNKNOWN;
    ctx->_timer = _timermgr->schedule(_rpc_timeout, std::bind(&Network::call_timeout_handler, this, ctx, _1, _2));

    peer->_call_list.push_front(ctx);
    ++_call_count;
    if (!Coroutine::yield()) {
        ctx->_timer->close();
        _call_map.erase(seq);
        log_debug("send call yield failed.");
        throw ServletException(GX_EBUSY);
    }
    --_call_count;
    Peer::ctx_list_t::remove(ctx);

    _call_map.erase(r.first);
    if (ctx->_timer) {
        ctx->_timer->close();
    }

    switch (ctx->_call_result) {
    case GX_CALL_OK:
        log_debug("recv response, servlet = %x, seq = %d.", servlet, seq);
        if (rsp->read_rc(peer->input())) {
            if (rsp->rc) {
                return;
            }
            if (rsp->unserial(peer->input(), ctx->pool())) {
                return;
            }
            else {
                log_error("read response failed, input size = %lu", peer->input().size());
            }
        }
        else {
            log_error("read response rc failed, input size = %lu", peer->input().size());
        }
        peer->close();
        throw CallCancelException();
    case GX_CALL_TIMEDOUT:
        log_debug("call '%d' timedout.", seq);
        throw ServletException(GX_ETIMEOUT);
    case GX_CALL_CANCEL:
        log_debug("call '%d' cancelled.", seq);
        throw CallCancelException();
    default:
        log_debug("call unknown error");
        assert(0);
        return;
    }
}

timeval_t Network::call_timeout_handler(Context *ctx, Timer&, timeval_t) noexcept {
    log_debug("call timedout.");
    ctx->call_timedout();
    return 0;
}

inline void Network::request_handler(ProtocolInfo &info, Peer *peer, Stream&) noexcept {
    ServletManager::instance()->execute(info.servlet, info.seq, info.size, peer);
}

inline void Network::response_handler(ProtocolInfo &info, Peer *peer, Stream &stream) noexcept {
    auto it = _call_map.find(info.seq);
    if (it == _call_map.end()) {
        log_debug("can't find call seq '%d'.", info.seq);
        stream.read(nullptr, info.size);
        return;
    }
    log_debug("on response servlet = %x, seq = %d, size = %d", info.servlet, info.seq, info.size);
    Context *ctx = it->second;
    assert(ctx);
    ctx->call_ok();
}

bool Network::ready() noexcept {
    for (auto &instance : _instances) {
        if (!instance->is_ap()) {
            if (!instance->_peer) {
                return false;
            }
        }
    }
    return true;
}

NetworkInstance *Network::instance() const noexcept {
    return instance(the_app->type(), the_app->id());
}

NetworkInstance *Network::ap() const noexcept {
    return ap(the_app->type(), the_app->id());
}   

GX_NS_END


