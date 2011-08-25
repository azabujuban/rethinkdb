
#include "utils.hpp"
#include <boost/shared_ptr.hpp>

#include "buffer_cache/buf_lock.hpp"
#include "btree/internal_node.hpp"
#include "btree/leaf_node.hpp"
#include "btree/operations.hpp"
#include "btree/slice.hpp"


// Runs a btree_modify_oper_t.
template <class Value>
void run_btree_modify_oper(btree_modify_oper_t<Value> *oper, btree_slice_t *slice, const store_key_t &store_key, castime_t castime, order_token_t token) {
    btree_key_buffer_t kbuffer(store_key);
    btree_key_t *key = kbuffer.key();

    block_size_t block_size = slice->cache()->get_block_size();

    {
        got_superblock_t got_superblock;

        get_btree_superblock(slice, rwi_write, oper->compute_expected_change_count(block_size), castime.timestamp, token, &got_superblock);

        keyvalue_location_t<Value> kv_location;
        find_keyvalue_location_for_write(&got_superblock, key, &kv_location);
        transaction_t *txn = kv_location.txn.get();
        scoped_malloc<Value> the_value;
        the_value.reinterpret_swap(kv_location.value);

        bool expired = the_value && the_value->expired();

        // If the value's expired, delete it.
        if (expired) {
            blob_t b(the_value->value_ref(), blob::btree_maxreflen);
            b.unappend_region(txn, b.valuesize());
            the_value.reset();
        }

        bool update_needed = oper->operate(txn, the_value);
        update_needed = update_needed || expired;

        // Add a CAS to the value if necessary
        if (the_value) {
            if (the_value->has_cas()) {
                rassert(castime.proposed_cas != BTREE_MODIFY_OPER_DUMMY_PROPOSED_CAS);
                the_value->set_cas(block_size, castime.proposed_cas);
            }
        }

        // Actually update the leaf, if needed.
        if (update_needed) {
            kv_location.value.reinterpret_swap(the_value);
            apply_keyvalue_change(&kv_location, key, castime.timestamp, expired);
        }
    }
}
