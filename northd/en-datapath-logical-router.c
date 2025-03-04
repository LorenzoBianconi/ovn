/*
 * Copyright (c) 2025, Red Hat, Inc.
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

#include "openvswitch/hmap.h"
#include "openvswitch/util.h"

#include "ovn-nb-idl.h"
#include "ovn-sb-idl.h"
#include "aging.h"
#include "datapath_sync.h"
#include "en-datapath-logical-router.h"
#include "ovn-util.h"

#define LR_SB_KEY_NAME "logical-router"

void *
en_datapath_logical_router_init(struct engine_node *node OVS_UNUSED,
                                struct engine_arg *args OVS_UNUSED)
{
    struct ovn_unsynced_datapath_map *map = xzalloc(sizeof *map);
    ovn_unsynced_datapath_map_init(map, LR_SB_KEY_NAME);
    return map;
}

void
en_datapath_logical_router_run(struct engine_node *node , void *data)
{
    const struct nbrec_logical_router_table *nb_lr_table =
        EN_OVSDB_GET(engine_get_input("NB_logical_router", node));

    struct ovn_unsynced_datapath_map *map = data;

    ovn_unsynced_datapath_map_destroy(map);
    ovn_unsynced_datapath_map_init(map, LR_SB_KEY_NAME);

    const struct nbrec_logical_router *nbr;
    NBREC_LOGICAL_ROUTER_TABLE_FOR_EACH (nbr, nb_lr_table) {
        struct ovn_unsynced_datapath *dp;
        dp = ovn_unsynced_datapath_alloc(nbr->name,
                                         smap_get_int(&nbr->options,
                                                      "requested-tnl-key", 0),
                                         &nbr->header_);

        smap_add_format(&dp->external_ids, LR_SB_KEY_NAME, UUID_FMT,
                        UUID_ARGS(&nbr->header_.uuid));
        smap_add(&dp->external_ids, "name", dp->name);
        const char *neutron_router = smap_get(&nbr->options,
                                               "neutron:router_name");
        if (neutron_router && neutron_router[0]) {
            smap_add(&dp->external_ids, "name2", neutron_router);
        }
        int64_t ct_zone_limit = ovn_smap_get_llong(&nbr->options,
                                                   "ct-zone-limit", -1);
        if (ct_zone_limit > 0) {
            smap_add_format(&dp->external_ids, "ct-zone-limit", "%"PRId64,
                            ct_zone_limit);
        }

        int nat_default_ct = smap_get_int(&nbr->options,
                                          "snat-ct-zone", -1);
        if (nat_default_ct >= 0) {
            smap_add_format(&dp->external_ids, "snat-ct-zone", "%d",
                            nat_default_ct);
        }

        bool learn_from_arp_request =
            smap_get_bool(&nbr->options, "always_learn_from_arp_request",
                          true);
        if (!learn_from_arp_request) {
            smap_add(&dp->external_ids, "always_learn_from_arp_request",
                     "false");
        }

        /* For timestamp refreshing, the smallest threshold of the option is
         * set to SB to make sure all entries are refreshed in time.
         * This approach simplifies processing in ovn-controller, but it
         * may be enhanced, if necessary, to parse the complete CIDR-based
         * threshold configurations to SB to reduce unnecessary refreshes. */
        uint32_t age_threshold = min_mac_binding_age_threshold(
                                       smap_get(&nbr->options,
                                               "mac_binding_age_threshold"));
        if (age_threshold) {
            smap_add_format(&dp->external_ids, "mac_binding_age_threshold",
                            "%u", age_threshold);
        }

        hmap_insert(&map->dps, &dp->hmap_node, uuid_hash(&nbr->header_.uuid));
    }

    engine_set_node_state(node, EN_UPDATED);
}


void
en_datapath_logical_router_cleanup(void *data)
{
    struct ovn_unsynced_datapath_map *map = data;
    ovn_unsynced_datapath_map_destroy(map);
}

static void
synced_logical_router_map_init(
    struct ovn_synced_logical_router_map *router_map)
{
    hmap_init(&router_map->synced_routers);
}

static void
synced_logical_router_map_destroy(
    struct ovn_synced_logical_router_map *router_map)
{
    hmap_destroy(&router_map->synced_routers);
}

void *
en_datapath_synced_logical_router_init(struct engine_node *node OVS_UNUSED,
                                      struct engine_arg *args OVS_UNUSED)
{
    struct ovn_synced_logical_router_map *router_map;
    router_map = xzalloc(sizeof *router_map);
    synced_logical_router_map_init(router_map);

    return router_map;
}

void
en_datapath_synced_logical_router_run(struct engine_node *node , void *data)
{
    const struct ovn_synced_datapaths *dps =
        engine_get_input_data("datapath_sync", node);
    struct ovn_synced_logical_router_map *router_map = data;

    synced_logical_router_map_destroy(router_map);
    synced_logical_router_map_init(router_map);

    struct ovn_synced_datapath *sdp;
    LIST_FOR_EACH (sdp, list_node, &dps->synced_dps) {
        struct uuid key;
        if (!smap_get_uuid(&sdp->sb_dp->external_ids, LR_SB_KEY_NAME, &key)) {
            continue;
        }
        struct ovn_synced_logical_router *lr = xzalloc(sizeof *lr);
        lr->nb = CONTAINER_OF(sdp->nb_row, struct nbrec_logical_router,
                              header_);
        lr->sb = sdp->sb_dp;
        hmap_insert(&router_map->synced_routers, &lr->hmap_node,
                    uuid_hash(&key));
    }

    engine_set_node_state(node, EN_UPDATED);
}

void en_datapath_synced_logical_router_cleanup(void *data)
{
    struct ovn_synced_logical_router_map *router_map = data;
    synced_logical_router_map_destroy(router_map);
}
