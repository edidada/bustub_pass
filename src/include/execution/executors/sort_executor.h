//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// sort_executor.h
//
// Identification: src/include/execution/executors/sort_executor.h
//
// Copyright (c) 2015-2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "binder/bound_order_by.h"
#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/seq_scan_plan.h"
#include "execution/plans/sort_plan.h"
#include "storage/table/tuple.h"
#include "type/value.h"

namespace bustub {

struct SortPair {
  std::vector<Value> sort_keys_;
  std::vector<OrderByType> sort_types_;
  Tuple tuple_;
  RID rid_;

  SortPair() = default;
  SortPair(std::vector<Value> sort_keys, std::vector<OrderByType> sort_types, Tuple tuple, RID rid)
      : sort_keys_(std::move(sort_keys)), sort_types_(std::move(sort_types)), tuple_(std::move(tuple)), rid_(rid) {}
};

struct SortComparator {
  auto operator()(const SortPair &lhs, const SortPair &rhs) const -> bool {
    for (uint32_t i = 0; i < lhs.sort_keys_.size(); i++) {
      switch (lhs.sort_types_[i]) {
        case OrderByType::DEFAULT:
        case OrderByType::ASC:
          if (lhs.sort_keys_[i].CompareLessThan(rhs.sort_keys_[i]) == CmpBool::CmpTrue) {
            return true;
          } 
          if (lhs.sort_keys_[i].CompareGreaterThan(rhs.sort_keys_[i]) == CmpBool::CmpTrue) {
            return false;
          }
          break;
        case OrderByType::DESC:
          if (lhs.sort_keys_[i].CompareGreaterThan(rhs.sort_keys_[i]) == CmpBool::CmpTrue) {
            return true;
          } 
          if (lhs.sort_keys_[i].CompareLessThan(rhs.sort_keys_[i]) == CmpBool::CmpTrue) {
            return false;
          }
          break;
        default:
          break;
      }
    }
    return false;
  }
};

/**
 * The SortExecutor executor executes a sort.
 */


class SortExecutor : public AbstractExecutor {
 public:
  /**
   * Construct a new SortExecutor instance.
   * @param exec_ctx The executor context
   * @param plan The sort plan to be executed
   */
  SortExecutor(ExecutorContext *exec_ctx, const SortPlanNode *plan, std::unique_ptr<AbstractExecutor> &&child_executor);

  /** Initialize the sort */
  void Init() override;

  /**
   * Yield the next tuple from the sort.
   * @param[out] tuple The next tuple produced by the sort
   * @param[out] rid The next tuple RID produced by the sort
   * @return `true` if a tuple was produced, `false` if there are no more tuples
   */
  auto Next(Tuple *tuple, RID *rid) -> bool override;

  /** @return The output schema for the sort */
  auto GetOutputSchema() const -> const Schema & override { return plan_->OutputSchema(); }

 private:
  /** The sort plan node to be executed */
  const SortPlanNode *plan_;

  std::unique_ptr<AbstractExecutor> child_executor_;

  std::vector<SortPair> sort_pairs_;

  uint32_t idx_ = 0;

  bool inited_ = false;
};
}  // namespace bustub
