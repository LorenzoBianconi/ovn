/* Copyright (c) 2020, Red Hat, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <config.h>

#include "lb.h"
#include "lib/ovn-util.h"
#include "ovn/lex.h"

/* OpenvSwitch lib includes. */
#include "openvswitch/vlog.h"

VLOG_DEFINE_THIS_MODULE(lb);

static bool
ovn_lb_backend_init_explicit(const struct ovn_lb_vip *lb_vip,
                             struct ovn_lb_backend *backend,
                             const char *token, struct ds *errors)
{
    int backend_addr_family;
    if (!ip_address_and_port_from_lb_key(token, &backend->ip_str,
                                         &backend->ip, &backend->port,
                                         &backend_addr_family)) {
        if (lb_vip->port_str) {
            ds_put_format(errors, "%s: should be an IP address and a "
                                   "port number with : as a separator, ",
                          token);
        } else {
            ds_put_format(errors, "%s: should be an IP address, ", token);
        }
        return false;
    }

    if (lb_vip->address_family != backend_addr_family) {
        free(backend->ip_str);
        ds_put_format(errors, "%s: IP address family is different from "
                               "VIP %s, ",
                      token, lb_vip->vip_str);
        return false;
    }

    if (lb_vip->port_str) {
        if (!backend->port) {
            free(backend->ip_str);
            ds_put_format(errors, "%s: should be an IP address and "
                                   "a port number with : as a separator, ",
                          token);
            return false;
        }
    } else {
        if (backend->port) {
            free(backend->ip_str);
            ds_put_format(errors, "%s: should be an IP address, ", token);
            return false;
        }
    }

    backend->port_str =
        backend->port ? xasprintf("%"PRIu16, backend->port) : NULL;
    return true;
}

/* Format for backend ips: "IP1:port1,IP2:port2,...". */
static char *
ovn_lb_backends_init_explicit(struct ovn_lb_vip *lb_vip, const char *value)
{
    struct ds errors = DS_EMPTY_INITIALIZER;
    char *tokstr = xstrdup(value);
    char *save_ptr = NULL;
    lb_vip->backends = VECTOR_EMPTY_INITIALIZER(struct ovn_lb_backend);

    for (char *token = strtok_r(tokstr, ",", &save_ptr);
        token != NULL;
        token = strtok_r(NULL, ",", &save_ptr)) {

        struct ovn_lb_backend backend = {0};
        if (!ovn_lb_backend_init_explicit(lb_vip, &backend, token, &errors)) {
            continue;
        }

        vector_push(&lb_vip->backends, &backend);
    }
    free(tokstr);

    if (ds_last(&errors) != EOF) {
        ds_chomp(&errors, ' ');
        ds_chomp(&errors, ',');
        ds_put_char(&errors, '.');
        return ds_steal_cstr(&errors);
    }
    return NULL;
}

char *
ovn_lb_vip_init_explicit(struct ovn_lb_vip *lb_vip, const char *lb_key,
                         const char *lb_value)
{
    if (!ip_address_and_port_from_lb_key(lb_key, &lb_vip->vip_str,
                                         &lb_vip->vip, &lb_vip->vip_port,
                                         &lb_vip->address_family)) {
        return xasprintf("%s: should be an IP address (or an IP address "
                         "and a port number with : as a separator).", lb_key);
    }

    lb_vip->port_str = lb_vip->vip_port
                       ? xasprintf("%"PRIu16, lb_vip->vip_port)
                       : NULL;

    return ovn_lb_backends_init_explicit(lb_vip, lb_value);
}

static bool
ovn_lb_backend_init_template(struct ovn_lb_backend *backend, const char *token,
                             struct ds *errors)
{
    char *atom = xstrdup(token);
    char *save_ptr = NULL;
    bool success = false;
    char *backend_ip = NULL;
    char *backend_port = NULL;

    for (char *subatom = strtok_r(atom, ":", &save_ptr); subatom;
         subatom = strtok_r(NULL, ":", &save_ptr)) {
        if (backend_ip && backend_port) {
            success = false;
            break;
        }
        success = true;
        if (!backend_ip) {
            backend_ip = xstrdup(subatom);
        } else {
            backend_port = xstrdup(subatom);
        }
    }

    if (success) {
        backend->ip_str = backend_ip;
        backend->port_str = backend_port;
        backend->port = 0;
        memset(&backend->ip, 0, sizeof backend->ip);
    } else {
        ds_put_format(errors, "%s: should be a template of the form: "
                      "'^backendip_variable1[:^port_variable1|:port]', ",
                      atom);
        free(backend_port);
        free(backend_ip);
    }
    free(atom);

    return success;
}

/* Parses backends of a templated LB VIP.
 * For now only the following template forms are supported:
 * A.
 *   ^backendip_variable1[:^port_variable1|:port],
 *   ^backendip_variable2[:^port_variable2|:port]
 *
 * B.
 *   ^backends_variable1,^backends_variable2 is also a thing
 *      where 'backends_variable1' may expand to IP1_1:PORT1_1 on chassis-1
 *                                               IP1_2:PORT1_2 on chassis-2
 *        and 'backends_variable2' may expand to IP2_1:PORT2_1 on chassis-1
 *                                               IP2_2:PORT2_2 on chassis-2
 * C.
 *    backendip1[:port],backendip2[:port]
 */
static char *
ovn_lb_backends_init_template(struct ovn_lb_vip *lb_vip, const char *value_)
{
    struct ds errors = DS_EMPTY_INITIALIZER;
    char *value = xstrdup(value_);
    char *save_ptr = NULL;
    lb_vip->backends = VECTOR_EMPTY_INITIALIZER(struct ovn_lb_backend);

    for (char *token = strtok_r(value, ",", &save_ptr); token;
         token = strtok_r(NULL, ",", &save_ptr)) {


        struct ovn_lb_backend backend = {0};
        if (token[0] && token[0] == '^') {
            if (!ovn_lb_backend_init_template(&backend, token, &errors)) {
                continue;
            }
        } else {
            if (!ovn_lb_backend_init_explicit(lb_vip, &backend,
                                              token, &errors)) {
                continue;
            }
        }

        vector_push(&lb_vip->backends, &backend);
    }

    free(value);
    if (ds_last(&errors) != EOF) {
        ds_chomp(&errors, ' ');
        ds_chomp(&errors, ',');
        ds_put_char(&errors, '.');
        return ds_steal_cstr(&errors);
    }
    return NULL;
}

/* Parses a VIP of a templated LB.
 * For now only the following template forms are supported:
 *   ^vip_variable[:^port_variable|:port]
 */
static char *
ovn_lb_vip_init_template(struct ovn_lb_vip *lb_vip, const char *lb_key_,
                         const char *lb_value, int address_family)
{
    char *save_ptr = NULL;
    char *lb_key = xstrdup(lb_key_);
    bool success = false;

    for (char *atom = strtok_r(lb_key, ":", &save_ptr); atom;
         atom = strtok_r(NULL, ":", &save_ptr)) {
        if (lb_vip->vip_str && lb_vip->port_str) {
            success = false;
            break;
        }
        success = true;
        if (!lb_vip->vip_str) {
            lb_vip->vip_str = xstrdup(atom);
            lb_vip->template_vips = !!strchr(atom, LEX_TEMPLATE_PREFIX);
        } else {
            lb_vip->port_str = xstrdup(atom);
        }
    }
    free(lb_key);

    if (!success) {
        return xasprintf("%s: should be a template of the form: "
                         "'^vip_variable[:^port_variable|:port]'.",
                         lb_key_);
    }

    /* Backends can either be templates or explicit IPs and ports. */
    lb_vip->address_family = address_family;
    char *template_error = ovn_lb_backends_init_template(lb_vip, lb_value);

    if (template_error) {
        char *error = xasprintf("invalid backend: template (%s)",
                                template_error);
            free(template_error);
            return error;
    }
    return NULL;
}

/* Returns NULL on success, an error string on failure.  The caller is
 * responsible for destroying 'lb_vip' in all cases.
 */
char *
ovn_lb_vip_init(struct ovn_lb_vip *lb_vip, const char *lb_key,
                const char *lb_value, bool template, int address_family)
{
    memset(lb_vip, 0, sizeof *lb_vip);

    return !template
           ?  ovn_lb_vip_init_explicit(lb_vip, lb_key, lb_value)
           :  ovn_lb_vip_init_template(lb_vip, lb_key, lb_value,
                                       address_family);
}

static void
ovn_lb_backends_destroy(struct ovn_lb_vip *vip)
{
    struct ovn_lb_backend *backend;
    VECTOR_FOR_EACH_PTR (&vip->backends, backend) {
        free(backend->ip_str);
        free(backend->port_str);
    }
    vector_destroy(&vip->backends);
}

void
ovn_lb_vip_destroy(struct ovn_lb_vip *vip)
{
    free(vip->vip_str);
    free(vip->port_str);
    ovn_lb_backends_destroy(vip);
}

static void
ovn_lb_vip_format__(const struct ovn_lb_vip *vip, struct ds *s,
                    bool needs_brackets)
{
    if (needs_brackets) {
        ds_put_char(s, '[');
    }
    ds_put_cstr(s, vip->vip_str);
    if (needs_brackets) {
        ds_put_char(s, ']');
    }
    if (vip->port_str) {
        ds_put_format(s, ":%s", vip->port_str);
    }
}

void
ovn_lb_vip_format(const struct ovn_lb_vip *vip, struct ds *s, bool template)
{
    bool needs_brackets = vip->address_family == AF_INET6 && vip->port_str
                          && !template;
    ovn_lb_vip_format__(vip, s, needs_brackets);
}

void
ovn_lb_vip_backends_format(const struct ovn_lb_vip *vip, struct ds *s)
{
    const struct ovn_lb_backend *backend;
    VECTOR_FOR_EACH_PTR (&vip->backends, backend) {
        bool needs_brackets = vip->address_family == AF_INET6 &&
                              vip->port_str &&
                              !ipv6_addr_equals(&backend->ip, &in6addr_any);
        if (needs_brackets) {
            ds_put_char(s, '[');
        }
        ds_put_cstr(s, backend->ip_str);
        if (needs_brackets) {
            ds_put_char(s, ']');
        }
        if (backend->port_str) {
            ds_put_format(s, ":%s", backend->port_str);
        }
        ds_put_char(s, ',');
    }
    ds_chomp(s, ',');
}

/* Formats the VIP in the way the ovn-controller expects it, that is,
 * template IPv6 variables need to be between brackets too.
 */
char *
ovn_lb_vip6_template_format_internal(const struct ovn_lb_vip *vip)
{
    struct ds s = DS_EMPTY_INITIALIZER;

    if (vip->vip_str && *vip->vip_str == LEX_TEMPLATE_PREFIX) {
        ovn_lb_vip_format__(vip, &s, true);
    } else {
        ovn_lb_vip_format(vip, &s, !!vip->port_str);
    }
    return ds_steal_cstr(&s);
}

static uint32_t
ovn_lb_5tuple_hash(const struct ovn_lb_5tuple *tuple)
{
    uint32_t hash = 0;

    hash = hash_add_in6_addr(hash, &tuple->vip_ip);
    hash = hash_add_in6_addr(hash, &tuple->backend_ip);

    hash = hash_add(hash, tuple->vip_port);
    hash = hash_add(hash, tuple->backend_port);

    hash = hash_add(hash, tuple->proto);

    return hash_finish(hash, 0);

}

void
ovn_lb_5tuple_init(struct ovn_lb_5tuple *tuple, const struct ovn_lb_vip *vip,
                   const struct ovn_lb_backend *backend, uint8_t proto)
{
    tuple->vip_ip = vip->vip;
    tuple->vip_port = vip->vip_port;
    tuple->backend_ip = backend->ip;
    tuple->backend_port = backend->port;
    tuple->proto = vip->vip_port ? proto : 0;
}

void
ovn_lb_5tuple_add(struct hmap *tuples, const struct ovn_lb_vip *vip,
                  const struct ovn_lb_backend *backend, uint8_t proto)
{
    struct ovn_lb_5tuple *tuple = xmalloc(sizeof *tuple);
    ovn_lb_5tuple_init(tuple, vip, backend, proto);
    hmap_insert(tuples, &tuple->hmap_node, ovn_lb_5tuple_hash(tuple));
}

void
ovn_lb_5tuple_find_and_delete(struct hmap *tuples,
                              const struct ovn_lb_5tuple *tuple)
{
    uint32_t hash = ovn_lb_5tuple_hash(tuple);

    struct ovn_lb_5tuple *node;
    HMAP_FOR_EACH_WITH_HASH (node, hmap_node, hash, tuples) {
        if (ipv6_addr_equals(&tuple->vip_ip, &node->vip_ip) &&
            ipv6_addr_equals(&tuple->backend_ip, &node->backend_ip) &&
            tuple->vip_port == node->vip_port &&
            tuple->backend_port == node->backend_port &&
            tuple->proto == node->proto) {
            hmap_remove(tuples, &node->hmap_node);
            free(node);
            return;
        }
    }
}

void
ovn_lb_5tuples_destroy(struct hmap *tuples)
{
    struct ovn_lb_5tuple *tuple;
    HMAP_FOR_EACH_POP (tuple, hmap_node, tuples) {
        free(tuple);
    }

    hmap_destroy(tuples);
}
