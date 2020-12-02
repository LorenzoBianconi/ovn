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
#include "lib/ovn-nb-idl.h"
#include "lib/ovn-sb-idl.h"
#include "lib/ovn-util.h"

/* OpenvSwitch lib includes. */
#include "openvswitch/vlog.h"
#include "lib/smap.h"

VLOG_DEFINE_THIS_MODULE(lb);

static
bool ovn_lb_vip_init(struct ovn_lb_vip *lb_vip, const char *lb_key,
                     const char *lb_value)
{
    int addr_family;

    lb_vip->empty_backend_rej = !!strstr(lb_key, ":R");
    int slen = lb_vip->empty_backend_rej ? strlen(lb_key) - 2 : strlen(lb_key);
    char *key = xmemdup0(lb_key, slen);

    bool err = !ip_address_and_port_from_lb_key(key, &lb_vip->vip_str,
                                                &lb_vip->vip_port, &addr_family);
    free(key);

    if (err) {
        return false;
    }

    if (addr_family == AF_INET) {
        ovs_be32 vip4;
        ip_parse(lb_vip->vip_str, &vip4);
        in6_addr_set_mapped_ipv4(&lb_vip->vip, vip4);
    } else {
        ipv6_parse(lb_vip->vip_str, &lb_vip->vip);
    }

    /* Format for backend ips: "IP1:port1,IP2:port2,...". */
    size_t n_backends = 0;
    size_t n_allocated_backends = 0;
    char *tokstr = xstrdup(lb_value);
    char *save_ptr = NULL;
    for (char *token = strtok_r(tokstr, ",", &save_ptr);
        token != NULL;
        token = strtok_r(NULL, ",", &save_ptr)) {

        if (n_backends == n_allocated_backends) {
            lb_vip->backends = x2nrealloc(lb_vip->backends,
                                          &n_allocated_backends,
                                          sizeof *lb_vip->backends);
        }

        struct ovn_lb_backend *backend = &lb_vip->backends[n_backends];
        int backend_addr_family;
        if (!ip_address_and_port_from_lb_key(token, &backend->ip_str,
                                             &backend->port,
                                             &backend_addr_family)) {
            continue;
        }

        if (addr_family != backend_addr_family) {
            free(backend->ip_str);
            continue;
        }

        if (addr_family == AF_INET) {
            ovs_be32 ip4;
            ip_parse(backend->ip_str, &ip4);
            in6_addr_set_mapped_ipv4(&backend->ip, ip4);
        } else {
            ipv6_parse(backend->ip_str, &backend->ip);
        }
        n_backends++;
    }
    free(tokstr);
    lb_vip->n_backends = n_backends;
    return true;
}

static
void ovn_lb_vip_destroy(struct ovn_lb_vip *vip)
{
    free(vip->vip_str);
    for (size_t i = 0; i < vip->n_backends; i++) {
        free(vip->backends[i].ip_str);
    }
    free(vip->backends);
}

static
void ovn_northd_lb_vip_init(struct ovn_northd_lb_vip *lb_vip_nb,
                            const struct ovn_lb_vip *lb_vip,
                            const struct nbrec_load_balancer *nbrec_lb,
                            const char *vip_port_str, const char *backend_ips,
                            struct hmap *ports,
                            void * (*ovn_port_find)(const struct hmap *ports,
                                                    const char *name))
{
    lb_vip_nb->vip_port_str = xstrdup(vip_port_str);
    lb_vip_nb->backend_ips = xstrdup(backend_ips);
    lb_vip_nb->n_backends = lb_vip->n_backends;
    lb_vip_nb->backends_nb = xcalloc(lb_vip_nb->n_backends,
                                     sizeof *lb_vip_nb->backends_nb);

    struct nbrec_load_balancer_health_check *lb_health_check = NULL;
    if (nbrec_lb->protocol && !strcmp(nbrec_lb->protocol, "sctp")) {
        if (nbrec_lb->n_health_check > 0) {
            static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 1);
            VLOG_WARN_RL(&rl,
                         "SCTP load balancers do not currently support "
                         "health checks. Not creating health checks for "
                         "load balancer " UUID_FMT,
                         UUID_ARGS(&nbrec_lb->header_.uuid));
        }
    } else {
        for (size_t j = 0; j < nbrec_lb->n_health_check; j++) {
            if (!strcmp(nbrec_lb->health_check[j]->vip,
                        lb_vip_nb->vip_port_str)) {
                lb_health_check = nbrec_lb->health_check[j];
                break;
            }
        }
    }

    lb_vip_nb->lb_health_check = lb_health_check;

    for (size_t j = 0; j < lb_vip_nb->n_backends; j++) {
        struct ovn_lb_backend *backend = &lb_vip->backends[j];
        struct ovn_northd_lb_backend *backend_nb = &lb_vip_nb->backends_nb[j];

        struct ovn_port *op = NULL;
        char *svc_mon_src_ip = NULL;
        const char *s = smap_get(&nbrec_lb->ip_port_mappings,
                                 backend->ip_str);
        if (s) {
            char *port_name = xstrdup(s);
            char *p = strstr(port_name, ":");
            if (p) {
                *p = 0;
                p++;
                op = ovn_port_find(ports, port_name);
                svc_mon_src_ip = xstrdup(p);
            }
            free(port_name);
        }

        backend_nb->op = op;
        backend_nb->svc_mon_src_ip = svc_mon_src_ip;
    }
}

static
void ovn_northd_lb_vip_destroy(struct ovn_northd_lb_vip *vip)
{
    free(vip->vip_port_str);
    free(vip->backend_ips);
    for (size_t i = 0; i < vip->n_backends; i++) {
        free(vip->backends_nb[i].svc_mon_src_ip);
    }
    free(vip->backends_nb);
}

struct ovn_northd_lb *
ovn_northd_lb_create(const struct nbrec_load_balancer *nbrec_lb,
                     struct hmap *ports,
                     void * (*ovn_port_find)(const struct hmap *ports,
                                             const char *name))
{
    struct ovn_northd_lb *lb = xzalloc(sizeof *lb);

    lb->nlb = nbrec_lb;
    lb->n_vips = smap_count(&nbrec_lb->vips);
    lb->vips = xcalloc(lb->n_vips, sizeof *lb->vips);
    lb->vips_nb = xcalloc(lb->n_vips, sizeof *lb->vips_nb);
    struct smap_node *node;
    size_t n_vips = 0;

    SMAP_FOR_EACH (node, &nbrec_lb->vips) {
        struct ovn_lb_vip *lb_vip = &lb->vips[n_vips];
        struct ovn_northd_lb_vip *lb_vip_nb = &lb->vips_nb[n_vips];

        if (!ovn_lb_vip_init(lb_vip, node->key, node->value)) {
            continue;
        }
        ovn_northd_lb_vip_init(lb_vip_nb, lb_vip, nbrec_lb,
                               node->key, node->value, ports, ovn_port_find);
        n_vips++;
    }

    /* It's possible that parsing VIPs fails.  Update the lb->n_vips to the
     * correct value.
     */
    lb->n_vips = n_vips;

    if (nbrec_lb->n_selection_fields) {
        char *proto = NULL;
        if (nbrec_lb->protocol && nbrec_lb->protocol[0]) {
            proto = nbrec_lb->protocol;
        }

        struct ds sel_fields = DS_EMPTY_INITIALIZER;
        for (size_t i = 0; i < lb->nlb->n_selection_fields; i++) {
            char *field = lb->nlb->selection_fields[i];
            if (!strcmp(field, "tp_src") && proto) {
                ds_put_format(&sel_fields, "%s_src,", proto);
            } else if (!strcmp(field, "tp_dst") && proto) {
                ds_put_format(&sel_fields, "%s_dst,", proto);
            } else {
                ds_put_format(&sel_fields, "%s,", field);
            }
        }
        ds_chomp(&sel_fields, ',');
        lb->selection_fields = ds_steal_cstr(&sel_fields);
    }
    return lb;
}

struct ovn_northd_lb *
ovn_northd_lb_find(struct hmap *lbs, const struct uuid *uuid)
{
    struct ovn_northd_lb *lb;
    size_t hash = uuid_hash(uuid);
    HMAP_FOR_EACH_WITH_HASH (lb, hmap_node, hash, lbs) {
        if (uuid_equals(&lb->nlb->header_.uuid, uuid)) {
            return lb;
        }
    }
    return NULL;
}

void
ovn_northd_lb_add_datapath(struct ovn_northd_lb *lb,
                           const struct sbrec_datapath_binding *sb)
{
    if (lb->n_allocated_dps == lb->n_dps) {
        lb->dps = x2nrealloc(lb->dps, &lb->n_allocated_dps, sizeof *lb->dps);
    }
    lb->dps[lb->n_dps++] = sb;
}

void
ovn_northd_lb_destroy(struct ovn_northd_lb *lb)
{
    for (size_t i = 0; i < lb->n_vips; i++) {
        ovn_lb_vip_destroy(&lb->vips[i]);
        ovn_northd_lb_vip_destroy(&lb->vips_nb[i]);
    }
    free(lb->vips);
    free(lb->vips_nb);
    free(lb->selection_fields);
    free(lb->dps);
    free(lb);
}

struct ovn_controller_lb *
ovn_controller_lb_create(const struct sbrec_load_balancer *sbrec_lb)
{
    struct ovn_controller_lb *lb = xzalloc(sizeof *lb);

    lb->slb = sbrec_lb;
    lb->n_vips = smap_count(&sbrec_lb->vips);
    lb->vips = xcalloc(lb->n_vips, sizeof *lb->vips);

    struct smap_node *node;
    size_t n_vips = 0;

    SMAP_FOR_EACH (node, &sbrec_lb->vips) {
        struct ovn_lb_vip *lb_vip = &lb->vips[n_vips];

        if (!ovn_lb_vip_init(lb_vip, node->key, node->value)) {
            continue;
        }
        n_vips++;
    }

    /* It's possible that parsing VIPs fails.  Update the lb->n_vips to the
     * correct value.
     */
    lb->n_vips = n_vips;
    return lb;
}

void
ovn_controller_lb_destroy(struct ovn_controller_lb *lb)
{
    for (size_t i = 0; i < lb->n_vips; i++) {
        ovn_lb_vip_destroy(&lb->vips[i]);
    }
    free(lb->vips);
    free(lb);
}
