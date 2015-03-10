// Copyright 2010-2015 RethinkDB, all rights reserved.
#ifndef CLUSTERING_TABLE_CONTRACT_COORDINATOR_HPP_
#define CLUSTERING_TABLE_CONTRACT_COORDINATOR_HPP_

#include "clustering/generic/raft_core.hpp"
#include "clustering/table_contract/contract_metadata.hpp"

/* There is one `contract_coordinator_t` per table, located on whichever server is
currently the Raft leader. It's the only thing which ever initiates Raft transactions.
Its jobs are as follows:

1. Applying config changes: The `table_meta_client_t` sends config change requests to the
    `multi_table_manager_t`, which calls `contract_coordinator_t::change_config()` to
    apply the changes.

2. Issuing `contract_t`s: The coordinator cross-references the `contract_t`s stored in
    the Raft state, the current table configuration stored in the Raft state, and the
    `contract_ack_t`s sent by the `contract_executor_t`s to decide if and when to change
    the `contract_t`s in the Raft state. It's responsible for ensuring correctness during
    complex changes like primary changes, failovers, and replica set changes.

3. Adding and removing replicas: When a new replica appears in the table config, the
    coordinator puts an entry into `table_raft_state_t::member_ids` so the new replica
    will join the Raft cluster. When the new member is ready, the coordinator issues a
    Raft config change to make the new replica a voting member. When a replica leaves, it
    goes through the reverse process.
*/

class contract_coordinator_t : public home_thread_mixin_debug_only_t {
public:
    contract_coordinator_t(
        raft_member_t<table_raft_state_t> *raft,
        watchable_map_t<std::pair<server_id_t, contract_id_t>, contract_ack_t> *acks);

    /* `table_meta_client_t` calls `change_config()` to change the cluster config. */
    boost::optional<raft_log_index_t> change_config(
        const std::function<void(table_config_and_shards_t *)> &changer,
        signal_t *interruptor);

private:
    /* `pump_contracts()` is what actually issues the new contracts. There's an instance
    of it running as long as the `contract_coordinator_t` exists. */
    void pump_contracts(auto_drainer_t::lock_t keepalive);

    /* `pump_configs()` makes changes to the `member_ids` field of the
    `table_raft_state_t` and to the Raft cluster configuration. It's separate from
    `pump_contracts()` because the Raft cluster configuration changes are limited by the
    Raft cluster's readiness for configuration changes, so it's best if they're not
    handled in the same loop. */
    void pump_configs(auto_drainer_t::lock_t keepalive);

    raft_member_t<table_raft_state_t> *const raft;
    watchable_map_t<std::pair<server_id_t, contract_id_t>, contract_ack_t> *const acks;

    /* Whenever something happens that might make it necessary to issue new contracts or
    change the configs, pulse `*wake_pump_contracts` or `*wake_pump_configs`. */
    scoped_ptr_t<cond_t> wake_pump_contracts;
    scoped_ptr_t<cond_t> wake_pump_configs;

    /* `drainer` makes sure that `pump_contracts()` and `pump_configs()` stop before the
    member variables are destroyed. */
    auto_drainer_t drainer;

    watchable_map_t<std::pair<server_id_t, contract_id_t>, contract_ack_t>::all_subs_t
        ack_subs;
};

#endif /* CLUSTERING_TABLE_CONTRACT_COORDINATOR_HPP_ */

