#ifndef EN_NORTHD_H
#define EN_NORTHD_H 1

#include <config.h>

#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>

#include "lib/inc-proc-eng.h"

enum engine_node_state en_northd_run(struct engine_node *node OVS_UNUSED,
                                     void *data OVS_UNUSED);
void *en_northd_init(struct engine_node *node OVS_UNUSED,
                     struct engine_arg *arg);
void en_northd_cleanup(void *data);
void en_northd_clear_tracked_data(void *data);
enum engine_input_handler_result
northd_global_config_handler(struct engine_node *, void *data OVS_UNUSED);
enum engine_input_handler_result
northd_nb_logical_switch_handler(struct engine_node *, void *data);
enum engine_input_handler_result
northd_nb_logical_router_handler(struct engine_node *, void *data);
enum engine_input_handler_result
northd_sb_port_binding_handler(struct engine_node *, void *data);
enum engine_input_handler_result
northd_sb_datapath_binding_handler(struct engine_node *, void *data);
enum engine_input_handler_result northd_lb_data_handler(struct engine_node *,
                                                        void *data);
enum engine_input_handler_result
northd_nb_port_group_handler(struct engine_node *node, void *data);
enum engine_input_handler_result
northd_sb_fdb_change_handler(struct engine_node *node, void *data);
void *en_routes_init(struct engine_node *node OVS_UNUSED,
                            struct engine_arg *arg OVS_UNUSED);
void en_route_policies_cleanup(void *data);
enum engine_input_handler_result
route_policies_northd_change_handler(struct engine_node *node,
                                     void *data OVS_UNUSED);
enum engine_node_state en_route_policies_run(struct engine_node *node,
                                             void *data);
void *en_route_policies_init(struct engine_node *node OVS_UNUSED,
                             struct engine_arg *arg OVS_UNUSED);
void en_routes_cleanup(void *data);
enum engine_input_handler_result
routes_northd_change_handler(struct engine_node *node, void *data OVS_UNUSED);
enum engine_node_state en_routes_run(struct engine_node *node, void *data);
void *en_bfd_init(struct engine_node *node OVS_UNUSED,
                  struct engine_arg *arg OVS_UNUSED);
void en_bfd_cleanup(void *data);
enum engine_node_state en_bfd_run(struct engine_node *node, void *data);
void *en_bfd_sync_init(struct engine_node *node OVS_UNUSED,
                       struct engine_arg *arg OVS_UNUSED);
enum engine_input_handler_result
bfd_sync_northd_change_handler(struct engine_node *node,
                               void *data OVS_UNUSED);
enum engine_node_state en_bfd_sync_run(struct engine_node *node, void *data);
void en_bfd_sync_cleanup(void *data OVS_UNUSED);

#endif /* EN_NORTHD_H */
