//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// hash_join_executor.h
//
// Identification: src/include/execution/executors/hash_join_executor.h
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <string>
#include <vector>
#include <unordered_map>

#include "common/util/hash_util.h"
#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/hash_join_plan.h"
#include "storage/table/table_heap.h"
#include "storage/table/tuple.h"
#include "type/value.h"

namespace bustub {

struct HashJoinKey {
  /** The join-key values*/
  std::vector<Value> join_keys_;

  /**
   * Compares two hash join keys for equality.
   * @param other the other hash join key to be compared with
   * @return `true` if both hash join keys have equivalent hash join expressions, `false` otherwise
   */
  auto operator==(const HashJoinKey &other) const -> bool {
    for (uint32_t i = 0; i < other.join_keys_.size(); i++) {
      if (join_keys_[i].CompareEquals(other.join_keys_[i]) != CmpBool::CmpTrue) {
        return false;
      }
    }
    return true;
  }
};

struct HashJoinValue {
  /** The join-key values*/
  std::vector<Value> tuple_values_;
};
}  // namespace bustub

/** Implements std::hash on HashJoinKey */
namespace std {

template <>
struct hash<bustub::HashJoinKey> {
  auto operator()(const bustub::HashJoinKey &join_key) const -> std::size_t {
    size_t curr_hash = 0;
    for (const auto &key : join_key.join_keys_) {
      if (!key.IsNull()) {
        curr_hash = bustub::HashUtil::CombineHashes(curr_hash, bustub::HashUtil::HashValue(&key));
      }
    }
    return curr_hash;
  }
};

} // namespace std

namespace bustub {
/**
 * A simplified hash table that has all the necessary functionality for aggregations.
 */
class SimpleHashJoinHashTable {
 public:
  SimpleHashJoinHashTable() = default;

  void Insert(const HashJoinKey &key, const HashJoinValue &value) {
    if (ht_.find(key) == ht_.end()) {
      matched_.insert({key, false});
      ht_.insert({key, {value}});
    } else {
      ht_[key].push_back(value);
    }
  }

  auto Get(const HashJoinKey &key, std::vector<HashJoinValue> *values) -> bool {
    if (ht_.find(key) == ht_.end()) {
      return false;
    }
    *values = ht_[key];
    return true;
  }

  void Mark(const HashJoinKey &key) {
    if (matched_.find(key) != matched_.end()) {
      matched_[key] = true;
    }
  }

  auto GetUnmatchedKeys() -> std::vector<HashJoinKey> {
    std::vector<HashJoinKey> unmatched_keys{};
    for (const auto &pair : matched_) {
      if (!pair.second) {
        unmatched_keys.push_back(pair.first);
      }
    }
    return unmatched_keys;
  }


 private:
  /** The hash table is just a map from hash-join keys to hash-join values */
  std::unordered_map<HashJoinKey, std::vector<HashJoinValue> > ht_{};
  std::unordered_map<HashJoinKey, bool> matched_{};
};

/**
 * HashJoinExecutor executes a nested-loop JOIN on two tables.
 */
class HashJoinExecutor : public AbstractExecutor {
 public:
  /**
   * Construct a new HashJoinExecutor instance.
   * @param exec_ctx The executor context
   * @param plan The HashJoin join plan to be executed
   * @param left_child The child executor that produces tuples for the left side of join
   * @param right_child The child executor that produces tuples for the right side of join
   */
  HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                   std::unique_ptr<AbstractExecutor> &&left_child, std::unique_ptr<AbstractExecutor> &&right_child);

  /** Initialize the join */
  void Init() override;

  /**
   * Yield the next tuple from the join.
   * @param[out] tuple The next tuple produced by the join.
   * @param[out] rid The next tuple RID, not used by hash join.
   * @return `true` if a tuple was produced, `false` if there are no more tuples.
   */
  auto Next(Tuple *tuple, RID *rid) -> bool override;

  /** @return The output schema for the join */
  auto GetOutputSchema() const -> const Schema & override { return plan_->OutputSchema(); };

  auto MakeLeftJoinKey(const Tuple* tuple) -> HashJoinKey {
    std::vector<Value> vals{};
    for (const auto &expr : plan_->LeftJoinKeyExpressions()) {
      vals.emplace_back(expr->Evaluate(tuple, left_child_->GetOutputSchema()));
    }
    return {vals};
  }

  auto MakeLeftJoinValue(const Tuple* tuple) -> HashJoinValue {
    std::vector<Value> vals{};
    for (uint32_t idx = 0; idx < left_child_->GetOutputSchema().GetColumnCount(); ++idx) {
      vals.emplace_back(tuple->GetValue(&left_child_->GetOutputSchema(), idx));
    }
    return {vals};
  }

  auto MakeRightJoinKey(const Tuple* tuple) -> HashJoinKey {
    std::vector<Value> vals{};
    for (const auto &expr : plan_->RightJoinKeyExpressions()) {
      vals.emplace_back(expr->Evaluate(tuple, right_child_->GetOutputSchema()));
    }
    return {vals};
  }

  auto MakeRightJoinValue(const Tuple* tuple) -> HashJoinValue {
    std::vector<Value> vals{};
    for (uint32_t idx = 0; idx < right_child_->GetOutputSchema().GetColumnCount(); ++idx) {
      vals.emplace_back(tuple->GetValue(&right_child_->GetOutputSchema(), idx));
    }
    return {vals};
  }

 private:
  /** The NestedLoopJoin plan node to be executed. */
  const HashJoinPlanNode *plan_;

  /** The child executor that produces tuples for the left side of join. */
  std::unique_ptr<AbstractExecutor> left_child_;

  /** The child executor that produces tuples for the right side of join. */
  std::unique_ptr<AbstractExecutor> right_child_;

  /** The result table*/
  std::unique_ptr<TableHeap> result_table_;

  /** The result table iterator*/
  std::unique_ptr<TableIterator> result_table_iter_;

  bool inited_ = false;
};

}  // namespace bustub
