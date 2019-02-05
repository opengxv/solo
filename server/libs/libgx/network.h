#ifndef __GX_NETWORK_H__
#define __GX_NETWORK_H__


#include <vector>
#include <unordered_map>
#include "platform.h"
#include "singleton.h"
#include "reactor.h"
#include "socket.h"
#include "peer.h"
#include "timermanager.h"
#include "reactor.h"
#include "rc.h"
#include "hash.h"

GX_NS_BEGIN

class Script;
class Network;
class NetworkNode;
class Context;

#define GX_SERVLET_TYPE(x) ((x) >> 16)
struct ServletException : std::exception {
    ServletException(int code) noexcept : rc(code) { }
    int rc;
};

struct CallCancelException : std::exception {
};

class NetworkInstance : public Object {
    friend class Network;
public:
    NetworkInstance(
        Network *network, 
        NetworkNode *node,
        const std::string &host, 
        unsigned port,
        bool ap) noexcept;

    const char *host() const noexcept {
        return _host.c_str();
    }
    unsigned port() const noexcept {
        return _port;
    }
    bool is_ap() const noexcept {
        return _ap;
    }
    const NetworkNode *node() const noexcept {
        return _node;
    }
    bool has_servlet(unsigned servlet) const noexcept {
        if (_servlets.size() <= servlet) {
            return false;
        }
        return _servlets[servlet];
    }
    unsigned id() const noexcept {
        return _id;
    }
private:
    bool listen(ptr<Reactor> reactor) noexcept;
    bool connect() noexcept;
    bool on_connection(Socket *socket, int flags) noexcept;
    bool on_accept(int, unsigned, const Address&) noexcept;
    bool on_data(ptr<Peer>, Socket&, int flags) noexcept;
private:
    unsigned _id;
    std::string _host;
    unsigned _port;
    timeval_t _timeout;
    timeval_t _interval;
    ptr<Connector> _connector;
    ptr<Listener> _listener;
    bool _ap;
    weak_ptr<Peer> _peer;
    Network *_network;
    NetworkNode *_node;
    Address _conn_addr;
    bool _is_local;
    std::vector<bool> _servlets;
};

class NetworkServlet : public Object {
    friend class NetworkServlets;
    friend class Network;
public:
    NetworkServlet(unsigned id) noexcept;
    const std::vector<ptr<NetworkInstance>> &instances() const noexcept {
        return _instances;
    }
    unsigned id() const noexcept {
        return _id;
    }
private:
    unsigned _id;
    std::vector<ptr<NetworkInstance>> _instances;
};

class NetworkServlets : public Object {
    friend class Network;
public:
    const NetworkServlet *get(unsigned index) const noexcept {
        if (index >= _servlets.size()) {
            return nullptr;
        }
        return _servlets[index];
    }
private:
    void add(unsigned index, ptr<NetworkInstance> instance) noexcept;
private:
    std::vector<ptr<NetworkServlet>> _servlets;
};

class NetworkNode : public Object {
    friend class Network;
public:
    NetworkNode(unsigned id, const std::string &name) noexcept
    : _id(id), _name(name)
    { }

    unsigned id() const noexcept {
        return _id;
    }
    const char *name() const noexcept {
        return _name.c_str();
    }
    const std::vector<ptr<NetworkInstance>> instances() const noexcept {
        return _instances;
    }
    const std::vector<ptr<NetworkInstance>> aps() const noexcept {
        return _instances;
    }
    const NetworkServlets &servlets() const noexcept {
        return _servlets;
    }
private:
    unsigned _id;
    std::string _name;
    std::vector<ptr<NetworkInstance>> _instances;
    std::vector<ptr<NetworkInstance>> _aps;
    NetworkServlets _servlets;
};

class Network : public Object {
    friend class NetworkInstance;
public:
    bool init(Script *script, ptr<TimerManager> timermgr, ptr<Reactor> reactor) noexcept;
    const std::vector<ptr<NetworkNode>> &nodes() const noexcept {
        return _nodes;
    }
    int type() const noexcept {
        return _type;
    }
    unsigned id() const noexcept {
        return _id;
    }
    ptr<TimerManager> timer_manager() const noexcept {
        return _timermgr;
    }
    unsigned call_count() const noexcept {
        return _call_count;
    }
    bool startup(int type, unsigned id, bool ap = false) noexcept;
    void shutdown_servlets() noexcept;
	Peer *send(uint64_t id, unsigned servlet, const INotify *req,
               unsigned *seq = nullptr, 
               NetworkInstance *instance = nullptr) noexcept;
    void broadcast(unsigned servlet, const INotify *req) noexcept;
    void call(uint64_t id, unsigned servlet, IRequest *req, IResponse *rsp, NetworkInstance *instance = nullptr);
    bool ready() noexcept;

    template <typename _Message>
    typename std::enable_if<
        !std::is_void<typename _Message::response_type>::value,
        int>::type
    call(_Message &msg, NetworkInstance *instance = nullptr) {
        assert(msg.req->id());

        Obstack *pool = the_pool();
        msg.rsp = pool->construct<typename _Message::response_type>(pool);
        call(msg.req->id(), _Message::the_message_id, msg.req, msg.rsp, instance);
        if (msg.rsp->rc >= GX_ESYS_RC && msg.rsp->rc < GX_ESYS_END) {
            throw ServletException(msg.rsp->rc);
        }
        return msg.rsp->rc;
    }
    template <typename _Message>
    typename std::enable_if<
        std::is_void<typename _Message::response_type>::value,
        bool>::type
    call(_Message &msg, NetworkInstance *instance = nullptr) {
        assert(msg.req->id());
        return send(msg.req->id(), _Message::the_message_id, msg.req, nullptr, instance);
    }

    template <typename _Message>
    void broadcast(_Message &msg) {
        broadcast(_Message::the_message_id, msg.req);
    }

    template <typename _Message>
    void send(_Message &msg) {
        send(msg.req->id(), _Message::the_message_id, msg.req);
    }

    static unsigned lb_value(uint64_t id) noexcept {
        return hash_iterative(&id, sizeof(id));
    }
    bool lb_is(unsigned node_type, unsigned node_id, unsigned id) const noexcept {
        assert(node_type < _nodes.size());
        NetworkNode *node = _nodes[node_type];
        return node_id == lb_value(id) % node->_instances.size();
    }
    NetworkInstance *instance(unsigned node_type, unsigned node_id) const noexcept {
        assert(node_type < _nodes.size());
        NetworkNode *node = _nodes[node_type];
        return node->_instances[node_id];
    }
    NetworkInstance *instance() const noexcept;
    NetworkInstance *ap(unsigned node_type, unsigned node_id) const noexcept {
        assert(node_type < _nodes.size());
        NetworkNode *node = _nodes[node_type];
        return node->_aps[node_id];
    }
    NetworkInstance *ap() const noexcept;
    NetworkInstance *node_lb(unsigned node_type, unsigned id) const noexcept {
        assert(node_type < _nodes.size());
        NetworkNode *node = _nodes[node_type];
        return node->_instances[lb_value(id) % node->_instances.size()];
    }
    NetworkInstance *servlet_lb(unsigned servlet_type, uint64_t id) const noexcept {
        const NetworkServlet *servlet = _servlets.get(servlet_type);
        if (!servlet) {
            return nullptr;
        }
        return servlet->_instances[lb_value(id) % servlet->_instances.size()];
    }
private:
    timeval_t call_timeout_handler(Context *ctx, Timer&, timeval_t) noexcept;
    void request_handler(ProtocolInfo &info, Peer *peer, Stream&) noexcept;
    void response_handler(ProtocolInfo &info, Peer *peer, Stream&) noexcept;

private:
    int _type;
    unsigned _id;
    unsigned _seq;
    timeval_t _rpc_timeout;
    std::vector<ptr<NetworkNode>> _nodes;
    std::vector<ptr<NetworkInstance>> _instances;
    NetworkServlets _servlets;
    std::unordered_map<unsigned, Context*> _call_map;
    ptr<TimerManager> _timermgr;
    ptr<Reactor> _reactor;
    gx_list(Peer, _entry) _connect_list;
    gx_list(Peer, _entry) _accept_list;
    unsigned _call_count;
};

GX_NS_END

#endif


