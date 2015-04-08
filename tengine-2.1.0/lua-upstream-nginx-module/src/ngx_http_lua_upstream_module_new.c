
/*
 * Copyright (C) Yichun Zhang (agentzh)
 */


#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"
//#include "ngx_http_upstream.h"


#include <ngx_core.h>
#include <ngx_http.h>
#include <lauxlib.h>
#include "ngx_http_lua_api.h"

#define NGX_CHASH_VIRTUAL_NODE_NUMBER 160

ngx_module_t ngx_http_lua_upstream_module;

extern ngx_module_t ngx_http_upstream_consistent_hash_module;
//extern ngx_http_upstream_chash_srv_conf_t * ngx_http_upstream_chash_get(ngx_http_upstream_srv_conf_t *us);


static ngx_int_t ngx_http_lua_upstream_init(ngx_conf_t *cf);
static int ngx_http_lua_upstream_create_module(lua_State * L);
static int ngx_http_lua_upstream_get_upstreams(lua_State * L);
static int ngx_http_lua_upstream_get_servers(lua_State * L);
static ngx_http_upstream_main_conf_t *
ngx_http_lua_upstream_get_upstream_main_conf(lua_State *L);
static int ngx_http_lua_upstream_get_primary_peers(lua_State * L);
static int ngx_http_lua_upstream_get_backup_peers(lua_State * L);
static int ngx_http_lua_get_peer(lua_State *L,
                                 ngx_http_upstream_rr_peer_t *peer, ngx_uint_t id);
static ngx_http_upstream_srv_conf_t *
ngx_http_lua_upstream_find_upstream(lua_State *L, ngx_str_t *host);
static ngx_http_upstream_rr_peer_t *
ngx_http_lua_upstream_lookup_peer(lua_State *L);
static int ngx_http_lua_upstream_set_peer_down(lua_State * L);
static int ngx_http_lua_upstream_say_hello(lua_State* L);
static int
ngx_http_lua_upstream_add_server(lua_State * L);
static int
ngx_http_lua_upstream_add_peer(lua_State * L);
static ngx_http_upstream_server_t*
ngx_http_lua_upstream_compare_server(ngx_http_upstream_srv_conf_t * us , ngx_url_t u);
static ngx_http_upstream_srv_conf_t *
ngx_http_lua_upstream_check_peers(lua_State * L,ngx_url_t u,ngx_http_upstream_server_t ** srv);
static int
ngx_http_lua_upstream_exist_peer(ngx_http_upstream_rr_peers_t * peers , ngx_url_t u);

static int
ngx_http_lua_upstream_chash_add_peer(lua_State * L);


static ngx_int_t
ngx_http_upstream_chash_cmp(const void *one, const void *two);



typedef struct {
    time_t                                  timeout;
    ngx_int_t                               id;
    ngx_queue_t                             queue;
} ngx_http_upstream_chash_down_server_t;

typedef struct {
    u_char                                  down;
    uint32_t                                hash;
    ngx_uint_t                              index;
    ngx_uint_t                              rnindex;
    ngx_http_upstream_rr_peer_t            *peer;
} ngx_http_upstream_chash_server_t;

typedef struct {
    ngx_uint_t                              number;
    ngx_queue_t                             down_servers;
    ngx_array_t                            *values;
    ngx_array_t                            *lengths;
    ngx_segment_tree_t                     *tree;
    ngx_http_upstream_chash_server_t     ***real_node;
    ngx_http_upstream_chash_server_t       *servers;
    ngx_http_upstream_chash_down_server_t  *d_servers;
} ngx_http_upstream_chash_srv_conf_t;

typedef struct {
    uint32_t                                hash;

#if (NGX_HTTP_SSL)
    ngx_ssl_session_t                  *ssl_session;
#endif

    ngx_http_upstream_chash_server_t       *server;
    ngx_http_upstream_chash_srv_conf_t     *ucscf;
} ngx_http_upstream_chash_peer_data_t;





static ngx_http_module_t ngx_http_lua_upstream_ctx = {
    NULL, /* preconfiguration */
    ngx_http_lua_upstream_init, /* postconfiguration */
    NULL, /* create main configuration */
    NULL, /* init main configuration */
    NULL, /* create server configuration */
    NULL, /* merge server configuration */
    NULL, /* create location configuration */
    NULL /* merge location configuration */
};



ngx_module_t ngx_http_lua_upstream_module = {
    NGX_MODULE_V1,
    &ngx_http_lua_upstream_ctx, /* module context */
    NULL, /* module directives */

    NGX_HTTP_MODULE, /* module type */
    NULL, /* init master */
    NULL, /* init module */
    NULL, /* init process */
    NULL, /* init thread */
    NULL, /* exit thread */
    NULL, /* exit process */
    NULL, /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_int_t
ngx_http_lua_upstream_init(ngx_conf_t *cf)
{
    if (ngx_http_lua_add_package_preload(cf, "ngx.upstream",
            ngx_http_lua_upstream_create_module)
            != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}

static int
ngx_http_lua_upstream_create_module(lua_State * L)
{
    lua_createtable(L, 0, 1);

    lua_pushcfunction(L, ngx_http_lua_upstream_get_upstreams);
    lua_setfield(L, -2, "get_upstreams");

    lua_pushcfunction(L, ngx_http_lua_upstream_get_servers);
    lua_setfield(L, -2, "get_servers");

    lua_pushcfunction(L, ngx_http_lua_upstream_get_primary_peers);
    lua_setfield(L, -2, "get_primary_peers");

    lua_pushcfunction(L, ngx_http_lua_upstream_get_backup_peers);
    lua_setfield(L, -2, "get_backup_peers");

    lua_pushcfunction(L, ngx_http_lua_upstream_set_peer_down);
    lua_setfield(L, -2, "set_peer_down");

    lua_pushcfunction(L, ngx_http_lua_upstream_say_hello);
    lua_setfield(L, -2, "say_hello");

    lua_pushcfunction(L, ngx_http_lua_upstream_add_server);
    lua_setfield(L, -2, "add_server");

    lua_pushcfunction(L, ngx_http_lua_upstream_add_peer);
    lua_setfield(L, -2, "add_peer");

    lua_pushcfunction(L, ngx_http_lua_upstream_chash_add_peer);
    lua_setfield(L, -2, "chash_add_peer");

    //    lua_pushcfunction(L, ngx_http_lua_upstream_update_peer_addr);
    //    lua_setfield(L, -2, "update_peer_addr");

    return 1;
}

static int
ngx_http_lua_upstream_say_hello(lua_State * L)
{
    lua_createtable(L, 0, 10);
    lua_pushstring(L, "helloword!");
    return 1;
}


static ngx_http_upstream_server_t*
ngx_http_lua_upstream_compare_server(ngx_http_upstream_srv_conf_t * us , ngx_url_t u )
{
    ngx_uint_t i,len;
    ngx_http_upstream_server_t *server = NULL;

    if (us->servers == NULL || us->servers->nelts == 0) {
        return NULL;
    }

    server = us->servers->elts;

    for (i = 0; i < us->servers->nelts; i++) {
        len = ( server[i].host.data == NULL ? 0 : ngx_strlen(server[i].host.data));
	if ( len == u.url.len
               && ngx_memcmp( u.url.data ,server[i].host.data , u.url.len) == 0 ) {

            return  &server[i];
        }
        
    }

    return NULL;

}


static int
ngx_http_lua_upstream_add_server(lua_State * L)
{
    ngx_str_t host, id;
    ngx_http_upstream_server_t *us;
    ngx_http_upstream_srv_conf_t *uscf;
    ngx_url_t u;
    ngx_http_request_t *r;
    ngx_int_t weight, max_fails;
    time_t fail_timeout;
    u_char *p;

    if (lua_gettop(L) != 5) {
        // four param is :"upstream name", "ip:port" , "weight" , "max_fails", 
        //"fail_time"
        // for lua code , you must pass this four param, is none ,you should 
        // consider pass default value.
        return luaL_error(L, "exactly five argument expected");
    }

    r = ngx_http_lua_get_request(L);
    if (r == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "get request error \n");
        return 2;
    }

    host.data = (u_char *) luaL_checklstring(L, 1, &host.len);

    ngx_memzero(&u, sizeof (ngx_url_t));
    p = (u_char *) luaL_checklstring(L, 2, &u.url.len);
    u.default_port = 80;

    weight = (ngx_int_t) luaL_checkint(L, 3);
    max_fails = (ngx_int_t) luaL_checkint(L, 4);
    fail_timeout = (time_t) luaL_checklong(L, 5);
#if (NGX_DEBUG)
    ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "%s %s params: %s,%s,%d,%d,%d\n", __FILE__,__FUNCTION__, host.data, p, weight, max_fails, fail_timeout);
#endif
    uscf = ngx_http_lua_upstream_find_upstream(L, &host);
    if (uscf == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "upstream not found\n");
        return 2;
    }
 
     
    // lua virtual machine memory is stack,so dup a memory 
    u.url.data = ngx_pcalloc(uscf->servers->pool,u.url.len+1);
    ngx_memcpy(u.url.data ,p ,u.url.len);

    if ( NULL != ngx_http_lua_upstream_compare_server(uscf,u) ) {
        lua_pushnil(L);
        lua_pushliteral(L,"this server is exist\n");
        return 2;
    }    

    if (uscf->servers == NULL || uscf->servers->nelts == 0) {
        //TODO: 对于 默认的空upstream来讲，nginx当前会不允许其启动，可以考虑调整策略，允许此种情况下nginx启动
        //
        lua_pushliteral(L, "upstream has no server before!\n");
        lua_newtable(L);
        return 2;
    } else {
        if (ngx_parse_url(uscf->servers->pool, &u) != NGX_OK) {
            if (u.err) {
                lua_pushnil(L);
                lua_pushliteral(L, "url parser error");
                return 2;
            }
        }

        us = ngx_array_push(uscf->servers);
        if (us == NULL) {
            lua_pushliteral(L, "us push uscf->servers failed\n");
            return 3;
        }
       
        ngx_memzero(us, sizeof (ngx_http_upstream_server_t));

        us->host = u.host;
        ngx_str_null(&id);
        us->id = id;
        us->addrs = u.addrs;
        us->naddrs = u.naddrs;
        us->weight = weight;
        us->max_fails = max_fails;
        us->fail_timeout = fail_timeout;

    }

    return 1;
}



static ngx_http_upstream_srv_conf_t *
ngx_http_lua_upstream_check_peers(lua_State * L,ngx_url_t u,ngx_http_upstream_server_t ** srv)
{
    
    ngx_uint_t i;
    ngx_http_upstream_srv_conf_t **uscfp;
    ngx_http_upstream_main_conf_t *umcf;
    ngx_http_upstream_server_t *server; 

    if (lua_gettop(L) != 1) {
        lua_pushnil(L);
        lua_pushliteral(L, "no argument expected\n");
        return NULL;
    }

    umcf = ngx_http_lua_upstream_get_upstream_main_conf(L);
    uscfp = umcf->upstreams.elts;


    for (i = 0; i < umcf->upstreams.nelts; i++) {
        server = ngx_http_lua_upstream_compare_server(uscfp[i],u );
        *srv = server;	
        if ( NULL != server ) {
		break;
         }
    }
    
    if (i >= umcf->upstreams.nelts ) {
          lua_pushnil(L);
	  lua_pushliteral(L,"not find this peer\n");
          return NULL;
    }

    return uscfp[i];

}



static int
ngx_http_lua_upstream_exist_peer(ngx_http_upstream_rr_peers_t * peers , ngx_url_t u)
{
    ngx_uint_t i,len;
    ngx_http_upstream_rr_peer_t peer;

    for (i = 0; i < peers->number; i++) {
        peer = peers->peer[i];
        len = ( peer.name.data == NULL ? 0:ngx_strlen(peer.name.data) );
        if (len == u.url.len
                && ngx_memcmp(u.url.data , peer.name.data , u.url.len) == 0) {
            return 1;
        }
    }
    
    return 0;

}



static int
ngx_http_lua_upstream_add_peer(lua_State * L)
{
    ngx_uint_t n;
    ngx_http_upstream_server_t   *us;
    ngx_http_upstream_srv_conf_t *uscf;
    ngx_http_upstream_rr_peer_t   peer; 
    ngx_http_upstream_rr_peers_t *peers; 
    ngx_http_request_t *r;
    ngx_url_t u;
    size_t old_size,new_size;
    
    if (lua_gettop(L) != 1) {
        // one param is :  "ip:port" 
        // for lua code , you must pass this one param, is none ,you should 
        // consider pass default value.
    	//lua_pushstring(L, "exactly one argument expected\n");
        lua_pushnil(L);
        lua_pushliteral(L, "exactly one argument expected\n");
        return 2;
    }

    r = ngx_http_lua_get_request(L);
    if (r == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "get request error \n");
        return 2;
    }


    ngx_memzero(&u, sizeof (ngx_url_t));
    u.url.data = (u_char *) luaL_checklstring(L, 1, &u.url.len);
    u.default_port = 80;

#if (NGX_DEBUG)
    ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "%s %s params: %s\n", __FILE__,__FUNCTION__ , u.url.data );
#endif

    uscf = ngx_http_lua_upstream_check_peers(L,u,&us);
    if ( NULL == uscf || NULL == us) {
       return 2;
    }

    peers = uscf->peer.data;
    if ( 1 == ngx_http_lua_upstream_exist_peer(peers,u) ) {
        lua_pushnil(L);
        lua_pushliteral(L, "the peer is exist\n");
        return 2;
    }

    n = peers->number -1 ;
    n += peers->next != NULL ? peers->next->number : 0;
    old_size = n*sizeof(ngx_http_upstream_rr_peer_t) 
			+ sizeof(ngx_http_upstream_rr_peers_t)*(peers->next != NULL ? 2:1);
    new_size = sizeof(ngx_http_upstream_rr_peer_t) + old_size;
    
    peers  = ngx_prealloc(ngx_cycle->pool, uscf->peer.data, old_size, new_size );
    if (NULL == peers ) {
        lua_pushnil(L);
        lua_pushliteral(L, "peers pcalloc fail\n");
        return 2;
    }


    peer.weight = us->weight; 
    peer.effective_weight = us->weight;
    peer.current_weight= 0;
    peer.max_fails = us->max_fails;
    peer.fail_timeout = us->fail_timeout;
    peer.host = us->host;
    peer.id = us->id;
    peer.sockaddr = us->addrs->sockaddr;
    peer.socklen = us->addrs->socklen;
    peer.name = us->addrs->name;
    peer.down = us->down;

    peers->peer[peers->number++] = peer;
    peers->total_weight += peer.weight;
    peers->single = (peers->number == 1);
    peers->weighted = (peers->total_weight != peers->number);

    uscf->peer.data = peers;

    return 1;
}



static int
ngx_http_lua_upstream_chash_add_peer(lua_State * L)
{
    ngx_http_upstream_server_t   *us;
    ngx_http_request_t *r;
    ngx_http_upstream_chash_srv_conf_t * ucscf;
    ngx_http_upstream_chash_server_t * server;
    ngx_http_upstream_rr_peers_t *peers;
    ngx_http_upstream_rr_peer_t *peer;
    ngx_http_upstream_srv_conf_t *uscf;
    ngx_uint_t i,n,rnindex,number,sid,id;
    ngx_uint_t weight,hash_len,*nest;
    u_char     hash_buf[256];
    ngx_url_t u;
    size_t old_size,new_size;
    
    if ( 1 != ngx_http_lua_upstream_add_peer(L) ) {
           return 2;
    }

    r = ngx_http_lua_get_request(L);
    if (r == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "get request error \n");
        return 2;
    }
    

    ngx_memzero(&u, sizeof (ngx_url_t));
    u.url.data = (u_char *) luaL_checklstring(L, 1, &u.url.len);
    u.default_port = 80;

#if (NGX_DEBUG)
    ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "%s %s params: %s\n", __FILE__,__FUNCTION__ , u.url.data );
#endif

    uscf = ngx_http_lua_upstream_check_peers(L,u,&us);
    if ( NULL == uscf || NULL == us) {
       return 2;
    }

    peers = uscf->peer.data;
    if ( NULL == peers ) {
        lua_pushnil(L);
        lua_pushliteral(L, "the peers is null\n");
        return 2;
    }
       
    ucscf = ngx_http_conf_upstream_srv_conf(uscf,ngx_http_upstream_consistent_hash_module);

    n = peers->number -1 ;
    old_size = n*sizeof(ngx_http_upstream_chash_server_t**);
    new_size = old_size + sizeof(ngx_http_upstream_chash_server_t**);   

    ucscf->real_node = ngx_prealloc(ngx_cycle->pool, ucscf->real_node, old_size, new_size );
    if (NULL == ucscf->real_node ) {
        lua_pushnil(L);
        lua_pushliteral(L, "real_node realloc fail\n");
        return 2;
    }
        
    number = peers->peer[n].weight * NGX_CHASH_VIRTUAL_NODE_NUMBER;
    ucscf->real_node[n] = ngx_pcalloc(ngx_cycle->pool, (number+1)
						*sizeof(ngx_http_upstream_chash_server_t*));
    if (NULL == ucscf->real_node[n] ) {
        lua_pushnil(L);
        lua_pushliteral(L, "real_node[n] realloc fail\n");
        return 2;
    }

    
    old_size = (ucscf->number + 1)*sizeof(ngx_http_upstream_chash_server_t);
    new_size = old_size + sizeof(ngx_http_upstream_chash_server_t)*number;   

    ucscf->servers = ngx_prealloc(ngx_cycle->pool, ucscf->servers, old_size, new_size );
    if (NULL == ucscf->servers ) {
        lua_pushnil(L);
        lua_pushliteral(L, "servers realloc fail\n");
        return 2;
    }
     
    old_size = (ucscf->number + 1)*sizeof(ngx_http_upstream_chash_server_t);
    new_size = old_size + sizeof(ngx_http_upstream_chash_server_t)*number;   

    ucscf->d_servers = ngx_prealloc(ngx_cycle->pool, ucscf->d_servers, old_size, new_size );
    if (NULL == ucscf->d_servers ) {
        lua_pushnil(L);
        lua_pushliteral(L, "d_servers realloc fail\n");
        return 2;
    }


    peer = &peers->peer[n];
    
    sid = (ngx_uint_t) ngx_atoi(peer->id.data, peer->id.len);

    if (sid == (ngx_uint_t) NGX_ERROR || sid > 65535) {

        #if (NGX_DEBUG)
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "server id  %d\n", sid );
        #endif

        ngx_snprintf(hash_buf, 256, "%V%Z", &peer->name);
        hash_len = ngx_strlen(hash_buf);
        sid = ngx_murmur_hash2(hash_buf, hash_len);
    }

    weight = peer->weight * NGX_CHASH_VIRTUAL_NODE_NUMBER;    
  
    if (weight >= 1 << 14) {
        #if (NGX_DEBUG)
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "weigth[%d] is too large, is must be less than %d\n", weight / NGX_CHASH_VIRTUAL_NODE_NUMBER,(1 << 14) / NGX_CHASH_VIRTUAL_NODE_NUMBER );
        #endif
        weight = 1 << 14;
    }
    
    number = ucscf->number;
    
    for (i = 0; i < weight; i++) {
        server = &ucscf->servers[++ucscf->number];
        server->peer = peer;
        server->rnindex = n;

        id = sid * 256 * 16 + i;
        server->hash = ngx_murmur_hash2((u_char *) (&id), 4);
   }

   ngx_qsort(ucscf->servers + 1, ucscf->number,
            sizeof(ngx_http_upstream_chash_server_t),
            (const void *)ngx_http_upstream_chash_cmp);

   nest = calloc(n+1 ,sizeof(ngx_uint_t));
   if ( NULL == nest ) {
        lua_pushnil(L);
        lua_pushliteral(L, "nest calloc fail\n");
        return 2;
  
   }

   for (i = 1; i <= ucscf->number; i++) {
        ucscf->servers[i].index = i;
        ucscf->d_servers[i].id = i;
        rnindex = ucscf->servers[i].rnindex;
        ucscf->real_node[rnindex][nest[rnindex]] = &ucscf->servers[i];
        nest[rnindex]++;
    }

   ngx_free(nest);

   old_size = ((number + 1) << 2 ) * sizeof(ngx_segment_node_t);
   new_size = ((ucscf->number +1) << 2 ) * sizeof(ngx_segment_node_t);

   ucscf->tree->segments = ngx_prealloc(ngx_cycle->pool, ucscf->tree->segments, old_size, new_size );
   if (NULL == ucscf->tree->segments ) {
        lua_pushnil(L);
        lua_pushliteral(L, "ucscf tree realloc fail\n");
        return 2;
   }

   ucscf->tree->num = ucscf->number;   
   ucscf->tree->build(ucscf->tree, 1, 1, ucscf->number);
   ngx_queue_init(&ucscf->down_servers);
 
   return 1;

}


static ngx_int_t
ngx_http_upstream_chash_cmp(const void *one, const void *two)
{
    ngx_http_upstream_chash_server_t *frist, *second;

    frist = (ngx_http_upstream_chash_server_t *)one;
    second = (ngx_http_upstream_chash_server_t *) two;

    if (frist->hash > second->hash) {
        return 1;

    } else if (frist->hash == second->hash) {
        return 0;

    } else {
        return -1;
    }
}




static int
ngx_http_lua_upstream_get_upstreams(lua_State * L)
{
    ngx_uint_t i;
    ngx_http_upstream_srv_conf_t **uscfp, *uscf;
    ngx_http_upstream_main_conf_t *umcf;

    if (lua_gettop(L) != 0) {
        return luaL_error(L, "no argument expected");
    }

    umcf = ngx_http_lua_upstream_get_upstream_main_conf(L);
    uscfp = umcf->upstreams.elts;

    lua_createtable(L, umcf->upstreams.nelts, 0);

    for (i = 0; i < umcf->upstreams.nelts; i++) {

        uscf = uscfp[i];

        lua_pushlstring(L, (char *) uscf->host.data, uscf->host.len);
        if (uscf->port) {
            lua_pushfstring(L, ":%d", (int) uscf->port);
            lua_concat(L, 2);

            /* XXX maybe we should also take "default_port" into account
             * here? */
        }

        lua_rawseti(L, -2, i + 1);
    }

    return 1;
}



static int
ngx_http_lua_upstream_get_servers(lua_State * L)
{
    ngx_str_t host;
    ngx_uint_t i, j, n;
    ngx_http_upstream_server_t *server;
    ngx_http_upstream_srv_conf_t *us;

    if (lua_gettop(L) != 1) {
        return luaL_error(L, "exactly one argument expected");
    }

    host.data = (u_char *) luaL_checklstring(L, 1, &host.len);

    us = ngx_http_lua_upstream_find_upstream(L, &host);
    if (us == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "upstream not found");
        return 2;
    }

    if (us->servers == NULL || us->servers->nelts == 0) {
        lua_newtable(L);
        return 1;
    }

    server = us->servers->elts;

    lua_createtable(L, us->servers->nelts, 0);

    for (i = 0; i < us->servers->nelts; i++) {

        n = 4;

        if (server[i].backup) {
            n++;
        }

        if (server[i].down) {
            n++;
        }

        lua_createtable(L, 0, n);

        lua_pushliteral(L, "addr");

        if (server[i].naddrs == 1) {
            lua_pushlstring(L, (char *) server[i].addrs->name.data,
                    server[i].addrs->name.len);

        } else {
            lua_createtable(L, server[i].naddrs, 0);

            for (j = 0; j < server[i].naddrs; j++) {
                lua_pushlstring(L, (char *) server[i].addrs[j].name.data,
                        server[i].addrs[j].name.len);
                lua_rawseti(L, -2, j + 1);
            }
        }

        lua_rawset(L, -3);

        lua_pushliteral(L, "weight");
        lua_pushinteger(L, (lua_Integer) server[i].weight);
        lua_rawset(L, -3);

        lua_pushliteral(L, "max_fails");
        lua_pushinteger(L, (lua_Integer) server[i].max_fails);
        lua_rawset(L, -3);

        lua_pushliteral(L, "fail_timeout");
        lua_pushinteger(L, (lua_Integer) server[i].fail_timeout);
        lua_rawset(L, -3);

        if (server[i].backup) {
            lua_pushliteral(L, "backup");
            lua_pushboolean(L, 1);
            lua_rawset(L, -3);
        }

        if (server[i].down) {
            lua_pushliteral(L, "down");
            lua_pushboolean(L, 1);
            lua_rawset(L, -3);
        }

        lua_rawseti(L, -2, i + 1);
    }

    return 1;
}



static int
ngx_http_lua_upstream_get_primary_peers(lua_State * L)
{
    ngx_str_t host;
    ngx_uint_t i;
    ngx_http_upstream_rr_peers_t *peers;
    ngx_http_upstream_srv_conf_t *us;

    if (lua_gettop(L) != 1) {
        return luaL_error(L, "exactly one argument expected");
    }

    host.data = (u_char *) luaL_checklstring(L, 1, &host.len);

    us = ngx_http_lua_upstream_find_upstream(L, &host);
    if (us == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "upstream not found");
        return 2;
    }

    peers = us->peer.data;

    if (peers == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "no peer data");
        return 2;
    }

    lua_createtable(L, peers->number, 0);

    for (i = 0; i < peers->number; i++) {
        ngx_http_lua_get_peer(L, &peers->peer[i], i);
        lua_rawseti(L, -2, i + 1);
    }

    return 1;
}


static int
ngx_http_lua_upstream_get_backup_peers(lua_State * L)
{
    ngx_str_t host;
    ngx_uint_t i;
    ngx_http_upstream_rr_peers_t *peers;
    ngx_http_upstream_srv_conf_t *us;

    if (lua_gettop(L) != 1) {
        return luaL_error(L, "exactly one argument expected");
    }

    host.data = (u_char *) luaL_checklstring(L, 1, &host.len);

    us = ngx_http_lua_upstream_find_upstream(L, &host);
    if (us == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "upstream not found");
        return 2;
    }

    peers = us->peer.data;

    if (peers == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "no peer data");
        return 2;
    }

    peers = peers->next;
    if (peers == NULL) {
        lua_newtable(L);
        return 1;
    }

    lua_createtable(L, peers->number, 0);

    for (i = 0; i < peers->number; i++) {
        ngx_http_lua_get_peer(L, &peers->peer[i], i);
        lua_rawseti(L, -2, i + 1);
    }

    return 1;
}

static int
ngx_http_lua_upstream_set_peer_down(lua_State * L)
{
    ngx_http_upstream_rr_peer_t *peer;

    if (lua_gettop(L) != 4) {
        return luaL_error(L, "exactly 4 arguments expected");
    }

    peer = ngx_http_lua_upstream_lookup_peer(L);
    if (peer == NULL) {
        return 2;
    }

    peer->down = lua_toboolean(L, 4);

    lua_pushboolean(L, 1);
    return 1;
}



static ngx_http_upstream_rr_peer_t *
ngx_http_lua_upstream_lookup_peer(lua_State *L)
{
    int id, backup;
    ngx_str_t host;
    ngx_http_upstream_srv_conf_t *us;
    ngx_http_upstream_rr_peers_t *peers;

    host.data = (u_char *) luaL_checklstring(L, 1, &host.len);

    us = ngx_http_lua_upstream_find_upstream(L, &host);
    if (us == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "upstream not found");
        return NULL;
    }

    peers = us->peer.data;

    if (peers == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "no peer data");
        return NULL;
    }

    backup = lua_toboolean(L, 2);
    if (backup) {
        peers = peers->next;
    }

    if (peers == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "no backup peers");
        return NULL;
    }

    id = luaL_checkint(L, 3);
    if (id < 0 || (ngx_uint_t) id >= peers->number) {
        lua_pushnil(L);
        lua_pushliteral(L, "bad peer id");
        return NULL;
    }

    return &peers->peer[id];
}



static int
ngx_http_lua_get_peer(lua_State *L, ngx_http_upstream_rr_peer_t *peer,
                      ngx_uint_t id)
{
    ngx_uint_t n;

    n = 8;

    if (peer->down) {
        n++;
    }

    if (peer->accessed) {
        n++;
    }

    if (peer->checked) {
        n++;
    }

    lua_createtable(L, 0, n);

    lua_pushliteral(L, "id");
    lua_pushinteger(L, (lua_Integer) id);
    lua_rawset(L, -3);

    lua_pushliteral(L, "name");
    lua_pushlstring(L, (char *) peer->name.data, peer->name.len);
    lua_rawset(L, -3);

    lua_pushliteral(L, "weight");
    lua_pushinteger(L, (lua_Integer) peer->weight);
    lua_rawset(L, -3);

    lua_pushliteral(L, "current_weight");
    lua_pushinteger(L, (lua_Integer) peer->current_weight);
    lua_rawset(L, -3);

    lua_pushliteral(L, "effective_weight");
    lua_pushinteger(L, (lua_Integer) peer->effective_weight);
    lua_rawset(L, -3);

    lua_pushliteral(L, "fails");
    lua_pushinteger(L, (lua_Integer) peer->fails);
    lua_rawset(L, -3);

    lua_pushliteral(L, "max_fails");
    lua_pushinteger(L, (lua_Integer) peer->max_fails);
    lua_rawset(L, -3);

    lua_pushliteral(L, "fail_timeout");
    lua_pushinteger(L, (lua_Integer) peer->fail_timeout);
    lua_rawset(L, -3);

    if (peer->accessed) {
        lua_pushliteral(L, "accessed");
        lua_pushinteger(L, (lua_Integer) peer->accessed);
        lua_rawset(L, -3);
    }

    if (peer->checked) {
        lua_pushliteral(L, "checked");
        lua_pushinteger(L, (lua_Integer) peer->checked);
        lua_rawset(L, -3);
    }

    if (peer->down) {
        lua_pushliteral(L, "down");
        lua_pushboolean(L, 1);
        lua_rawset(L, -3);
    }

    return 0;
}

static ngx_http_upstream_main_conf_t *
ngx_http_lua_upstream_get_upstream_main_conf(lua_State *L)
{
    ngx_http_request_t *r;

    r = ngx_http_lua_get_request(L);

    if (r == NULL) {
        return ngx_http_cycle_get_module_main_conf(ngx_cycle,
                ngx_http_upstream_module);
    }

    return ngx_http_get_module_main_conf(r, ngx_http_upstream_module);
}


static ngx_http_upstream_srv_conf_t *
ngx_http_lua_upstream_find_upstream(lua_State *L, ngx_str_t *host)
{
    u_char *port;
    size_t len;
    ngx_int_t n;
    ngx_uint_t i;
    ngx_http_upstream_srv_conf_t **uscfp, *uscf;
    ngx_http_upstream_main_conf_t *umcf;

    umcf = ngx_http_lua_upstream_get_upstream_main_conf(L);
    uscfp = umcf->upstreams.elts;

    for (i = 0; i < umcf->upstreams.nelts; i++) {

        uscf = uscfp[i];

        if (uscf->host.len == host->len
                && ngx_memcmp(uscf->host.data, host->data, host->len) == 0) {
            return uscf;
        }
    }

    port = ngx_strlchr(host->data, host->data + host->len, ':');
    if (port) {
        port++;
        n = ngx_atoi(port, host->data + host->len - port);
        if (n < 1 || n > 65535) {
            return NULL;
        }

        /* try harder with port */

        len = port - host->data - 1;

        for (i = 0; i < umcf->upstreams.nelts; i++) {

            uscf = uscfp[i];

            if (uscf->port
                    && uscf->port == n
                    && uscf->host.len == len
                    && ngx_memcmp(uscf->host.data, host->data, len) == 0) {
                return uscf;
            }
        }
    }

    return NULL;
}

